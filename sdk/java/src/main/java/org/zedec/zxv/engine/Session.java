// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.io.ByteArrayOutputStream;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Spliterator;
import java.util.Spliterators;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Flow;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.ReentrantLock;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;
import org.zedec.zxv.budget.Budget;
import org.zedec.zxv.internal.BudgetBridge;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * One conversation with a {@link Model}: its KV cache. Generation appends to
 * the session, so a chat continues turn after turn; {@link #reset()} forgets
 * it. A session runs one generation at a time (concurrent calls wait their
 * turn); use several sessions for parallel work.
 *
 * <p>Four ways to generate:
 * <ul>
 *   <li>{@link #generate(String, GenerateOptions)}: blocking, returns the whole result;</li>
 *   <li>{@link #generate(String, GenerateOptions, TokenListener)}: blocking, a callback per token
 *       (an FFM upcall from the engine);</li>
 *   <li>{@link #stream(String, GenerateOptions)}: a {@link Stream} of tokens, or
 *       {@link #publisher(String, GenerateOptions)}: a {@link Flow.Publisher} with back-pressure;</li>
 *   <li>{@link #generateAsync(String, GenerateOptions)}: a {@link CompletableFuture}, on a virtual thread.</li>
 * </ul>
 */
public final class Session implements AutoCloseable {
    private static final MethodHandle ON_TOKEN;

    static {
        try {
            ON_TOKEN = MethodHandles.lookup().findVirtual(Sink.class, "onToken",
                    MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, long.class));
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private final Model model;
    private final NativeResource res;
    private final ReentrantLock busy = new ReentrantLock();
    private volatile Budget bound;

    Session(Model model, MemorySegment h) {
        this.model = model;
        this.res = new NativeResource("session", h, x -> Ffi.callVoid(Native.session_destroy, x), model.ctx, model.res);
    }

    /** The model this session runs. */
    public Model model() {
        return model;
    }

    /** Forgets the conversation (empties the KV cache). */
    public void reset() {
        busy.lock();
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.session_reset, l.h(0)), model.ctx);
        } finally {
            busy.unlock();
        }
    }

    /** How many tokens the session holds. */
    public int position() {
        busy.lock();
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment p = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.session_position, l.h(0), p), model.ctx);
            return p.get(JAVA_INT, 0);
        } finally {
            busy.unlock();
        }
    }

    /**
     * Charges {@code budget} one unit per prompt and generated token.
     * Prompt tokens are charged up front: if they do not fit, generate raises
     * {@link org.zedec.zxv.BudgetExhaustedException} and runs nothing; if the
     * budget runs out mid-way, generation stops with {@link StopReason#BUDGET}.
     * Closing the budget registry unbinds it.
     */
    public void bindBudget(Budget budget) {
        NativeResource reg = BudgetBridge.get().registry(budget);
        int index = BudgetBridge.get().index(budget);
        if (reg.owner() != model.ctx) {
            // libzxv does not check that the budget belongs to the session's context; the SDK does.
            throw Errors.usage(-1, "the budget belongs to a different ZxvContext than this session");
        }
        busy.lock();
        try (Lease l = Lease.of(res, reg)) {
            Ffi.check(Ffi.call(Native.session_bind_budget, l.h(0), l.h(1), index), model.ctx);
            bound = budget;
        } finally {
            busy.unlock();
        }
        reg.onClose(() -> {
            busy.lock();
            try {
                if (bound == budget && !res.isClosed()) {
                    try (Lease l = Lease.of(res)) {
                        Ffi.call(Native.session_bind_budget, l.h(0), MemorySegment.NULL, 0);
                    }
                    bound = null;
                }
            } catch (RuntimeException | Error ignored) {
                // the session is going away too
            } finally {
                busy.unlock();
            }
        });
    }

    /** Removes the budget binding. */
    public void unbindBudget() {
        busy.lock();
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.session_bind_budget, l.h(0), MemorySegment.NULL, 0), model.ctx);
            bound = null;
        } finally {
            busy.unlock();
        }
    }

    /** Generates from a prompt and returns the whole result. */
    public GenerateResult generate(String prompt, GenerateOptions options) {
        return generate(prompt.getBytes(StandardCharsets.UTF_8), options, null, null);
    }

    /** Generates, calling {@code listener} for each token as it is produced. */
    public GenerateResult generate(String prompt, GenerateOptions options, TokenListener listener) {
        return generate(prompt.getBytes(StandardCharsets.UTF_8), options, listener, null);
    }

    /** Generates from raw UTF-8 prompt bytes. */
    public GenerateResult generate(byte[] prompt, GenerateOptions options, TokenListener listener) {
        return generate(prompt, options, listener, null);
    }

    /**
     * Generates on a new virtual thread. Cancelling the future stops
     * generation at the next token.
     */
    public CompletableFuture<GenerateResult> generateAsync(String prompt, GenerateOptions options) {
        return generateAsync(prompt, options, null);
    }

    /** {@link #generateAsync(String, GenerateOptions)} with a per-token listener (runs on the virtual thread). */
    public CompletableFuture<GenerateResult> generateAsync(String prompt, GenerateOptions options, TokenListener listener) {
        CompletableFuture<GenerateResult> f = new CompletableFuture<>();
        AtomicBoolean stop = new AtomicBoolean();
        f.whenComplete((r, e) -> stop.set(true));
        byte[] p = prompt.getBytes(StandardCharsets.UTF_8);
        Thread.ofVirtual().name("zxv-generate").start(() -> {
            try {
                f.complete(generate(p, options, listener, stop));
            } catch (Throwable t) {
                f.completeExceptionally(t);
            }
        });
        return f;
    }

    /**
     * Generates on a virtual thread and returns the tokens as a sequential
     * {@link Stream}. Close the stream (try-with-resources) to stop early; an
     * error during generation is rethrown from the stream's terminal operation.
     */
    public Stream<Token> stream(String prompt, GenerateOptions options) {
        BlockingQueue<Object> q = new ArrayBlockingQueue<>(64);
        AtomicBoolean stop = new AtomicBoolean();
        Object end = new Object();
        byte[] p = prompt.getBytes(StandardCharsets.UTF_8);
        Thread producer = Thread.ofVirtual().name("zxv-generate-stream").start(() -> {
            Object last = end;
            try {
                generate(p, options, t -> {
                    try {
                        q.put(t);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        return false;
                    }
                    return !stop.get();
                }, stop);
            } catch (Throwable t) {
                last = t;
            }
            try {
                q.put(last);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        });
        Iterator<Token> it = new Iterator<>() {
            private Object next;

            @Override
            public boolean hasNext() {
                if (next == null) {
                    try {
                        next = q.take();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("interrupted while waiting for a token", e);
                    }
                }
                if (next instanceof Throwable t) {
                    next = end;
                    if (t instanceof RuntimeException re) throw re;
                    if (t instanceof Error er) throw er;
                    throw new IllegalStateException(t);
                }
                return next != end;
            }

            @Override
            public Token next() {
                if (!hasNext()) throw new NoSuchElementException();
                Token t = (Token) next;
                next = null;
                return t;
            }
        };
        return StreamSupport.stream(Spliterators.spliteratorUnknownSize(it, Spliterator.ORDERED | Spliterator.NONNULL), false)
                .onClose(() -> {
                    stop.set(true);
                    producer.interrupt();
                    q.clear();
                });
    }

    /**
     * Generation as a {@link Flow.Publisher} with back-pressure: tokens are
     * produced only as the subscriber requests them, on a virtual thread
     * started at subscription. Cancelling the subscription stops generation.
     * Single use: one subscriber.
     */
    public Flow.Publisher<Token> publisher(String prompt, GenerateOptions options) {
        AtomicBoolean used = new AtomicBoolean();
        byte[] p = prompt.getBytes(StandardCharsets.UTF_8);
        return subscriber -> {
            if (!used.compareAndSet(false, true)) {
                subscriber.onSubscribe(new Flow.Subscription() {
                    @Override
                    public void request(long n) {}

                    @Override
                    public void cancel() {}
                });
                subscriber.onError(new IllegalStateException("this publisher already has a subscriber"));
                return;
            }
            Semaphore demand = new Semaphore(0);
            AtomicLong outstanding = new AtomicLong();
            AtomicBoolean stop = new AtomicBoolean();
            subscriber.onSubscribe(new Flow.Subscription() {
                @Override
                public void request(long n) {
                    if (n <= 0) {
                        stop.set(true);
                        demand.release();
                        return;
                    }
                    long before = outstanding.getAndAccumulate(n, (a, b) -> a + b < 0 ? Long.MAX_VALUE : a + b);
                    if (before == 0) demand.release();
                }

                @Override
                public void cancel() {
                    stop.set(true);
                    demand.release();
                }
            });
            Thread.ofVirtual().name("zxv-generate-publisher").start(() -> {
                try {
                    generate(p, options, t -> {
                        try {
                            while (outstanding.get() == 0 && !stop.get()) demand.acquire();
                        } catch (InterruptedException e) {
                            return false;
                        }
                        if (stop.get()) return false;
                        outstanding.decrementAndGet();
                        subscriber.onNext(t);
                        return !stop.get();
                    }, stop);
                    if (!stop.get()) subscriber.onComplete();
                } catch (Throwable t) {
                    if (!stop.get()) subscriber.onError(t);
                }
            });
        };
    }

    /** Receives tokens from the engine through an FFM upcall stub. */
    private static final class Sink {
        private final TokenListener listener;
        private final AtomicBoolean stop;
        final ByteArrayOutputStream text = new ByteArrayOutputStream();
        int[] ids = new int[64];
        int n;
        Throwable failure;

        Sink(TokenListener listener, AtomicBoolean stop) {
            this.listener = listener;
            this.stop = stop;
        }

        // Runs on the generating thread, inside zxv_generate. Never throws into C.
        @SuppressWarnings("unused")
        int onToken(MemorySegment user, int token, MemorySegment piece, long len) {
            try {
                byte[] bytes = len == 0 ? new byte[0] : piece.reinterpret(len).toArray(JAVA_BYTE);
                if (n == ids.length) ids = java.util.Arrays.copyOf(ids, n * 2);
                ids[n++] = token;
                text.writeBytes(bytes);
                if (listener != null && !listener.onToken(new Token(token, bytes))) return 0;
                return stop != null && stop.get() ? 0 : 1;
            } catch (Throwable t) {
                failure = t;
                return 0;
            }
        }
    }

    private GenerateResult generate(byte[] prompt, GenerateOptions o, TokenListener listener, AtomicBoolean stop) {
        int temp = o.temperature().toQ16(), topP = o.topP().toQ16(), rep = o.repeatPenalty().toQ16();
        Budget b = bound;
        NativeResource reg = b == null ? null : BudgetBridge.get().registry(b);
        Sink sink = new Sink(listener, stop);
        try (Lease l = reg == null ? Lease.of(Caps.res(model.cap), model.res, res)
                : Lease.of(Caps.res(model.cap), model.res, res, reg);
             Arena a = Arena.ofConfined()) {
            busy.lock();
            try {
                MemorySegment stub = Native.linker().upcallStub(ON_TOKEN.bindTo(sink),
                        FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_LONG), a);
                MemorySegment np = a.allocate(JAVA_INT), ng = a.allocate(JAVA_INT), st = a.allocate(JAVA_INT);
                int rc = Ffi.call(Native.generate, l.h(0), l.h(2), Ffi.bytes(a, prompt), (long) prompt.length,
                        o.maxTokens(), temp, o.topK(), topP, rep, o.repeatLastN(), o.seed(), stub, MemorySegment.NULL,
                        np, ng, st);
                Ffi.check(rc, model.ctx);
                if (sink.failure instanceof RuntimeException re) throw re;
                if (sink.failure instanceof Error er) throw er;
                if (sink.failure != null) throw new IllegalStateException(sink.failure);
                return new GenerateResult(sink.text.toString(StandardCharsets.UTF_8), java.util.Arrays.copyOf(sink.ids, sink.n),
                        np.get(JAVA_INT, 0), ng.get(JAVA_INT, 0), StopReason.fromCode(st.get(JAVA_INT, 0)));
            } finally {
                busy.unlock();
            }
        }
    }

    /** Frees the session. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
