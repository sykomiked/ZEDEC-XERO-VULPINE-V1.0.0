// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.budget;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * A registry of exact rational resource budgets (the kernel's RMAG, K2),
 * used through one capability holding {@code BUDGET}.
 *
 * <pre>{@code
 * try (Budgets budgets = Budgets.create(cap)) {
 *     Budget tokens = budgets.budget("tokens", Resource.TOKENS, Rational.of(10), true);
 *     tokens.consume(Rational.parse("3.5"));
 *     tokens.consumeAsync(Rational.of(7)).join();  // waits for a release
 * }
 * }</pre>
 *
 * <p>libzxv reports replenishment (a release, or a larger limit) to a
 * listener the SDK installs; that is what wakes {@link Budget#consumeAsync}.
 */
public final class Budgets implements AutoCloseable {
    private static final int EV_DENIED = 1;
    private static final int EV_REPLENISHED = 3;
    private static final MethodHandle ON_EVENT;

    static {
        try {
            ON_EVENT = MethodHandles.lookup().findVirtual(Budgets.class, "onEvent",
                    MethodType.methodType(void.class, MemorySegment.class, int.class, int.class));
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    final Capability cap;
    final ZxvContext ctx;
    final NativeResource res;
    private final Arena upcallArena = Arena.ofShared();
    final Map<Integer, Set<Budget.Waiter>> waiters = new ConcurrentHashMap<>();
    final Map<Integer, List<Consumer<Budget>>> exhaustedHandlers = new ConcurrentHashMap<>();
    final Map<Integer, Budget> byIndex = new ConcurrentHashMap<>();

    private Budgets(Capability cap, ZxvContext ctx, MemorySegment handle) {
        this.cap = cap;
        this.ctx = ctx;
        MemorySegment stub = Native.linker().upcallStub(ON_EVENT.bindTo(this),
                FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT, JAVA_INT), upcallArena);
        Ffi.check(Ffi.call(Native.rmag_listen, handle, stub, MemorySegment.NULL), ctx);
        this.res = new NativeResource("budget registry", handle, h -> {
            Ffi.call(Native.rmag_listen, h, MemorySegment.NULL, MemorySegment.NULL);
            Ffi.callVoid(Native.rmag_destroy, h);
            upcallArena.close();
        }, ctx, Caps.res(ctx));
        res.onClose(this::failWaiters);
    }

    /** A new registry in the capability's context. */
    public static Budgets create(Capability cap) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.rmag_create_cap, l.h(0), out), ctx);
            return new Budgets(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /**
     * Creates a budget.
     *
     * @param name     a label (shown in error details)
     * @param resource what it measures
     * @param limit    the exact limit, at least zero
     * @param enforce  true to refuse consumes past the limit; false only counts
     */
    public Budget budget(String name, Resource resource, Rational limit, boolean enforce) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.rmag_budget, l.h(0), l.h(1), Ffi.cstr(a, name), resource.code(),
                    limit.numerator(), limit.denominator(), enforce ? 1 : 0, out), ctx);
            Budget b = new Budget(this, out.get(JAVA_INT, 0), name, resource);
            byIndex.put(b.index(), b);
            return b;
        }
    }

    /** The capability this registry uses. */
    public Capability capability() {
        return cap;
    }

    // Called by libzxv on the thread whose call caused the event, after its lock is released.
    // Must never throw: an exception escaping an upcall would crash the JVM.
    @SuppressWarnings("unused")
    private void onEvent(MemorySegment user, int budget, int event) {
        try {
            if (event == EV_REPLENISHED) {
                Set<Budget.Waiter> ws = waiters.get(budget);
                if (ws != null) {
                    for (Budget.Waiter w : List.copyOf(ws)) {
                        ws.remove(w);
                        Thread.ofVirtual().name("zxv-budget-retry").start(w::retry);
                    }
                }
            } else if (event == EV_DENIED) {
                Budget b = byIndex.get(budget);
                List<Consumer<Budget>> hs = exhaustedHandlers.get(budget);
                if (b != null && hs != null) {
                    for (Consumer<Budget> h : hs) {
                        try {
                            h.accept(b);
                        } catch (Throwable ignored) {
                            // a handler's failure must not cross into C
                        }
                    }
                }
            }
        } catch (Throwable ignored) {
            // never let anything cross into C
        }
    }

    void addExhaustedHandler(int index, Consumer<Budget> h) {
        exhaustedHandlers.computeIfAbsent(index, k -> new CopyOnWriteArrayList<>()).add(h);
    }

    void removeExhaustedHandler(int index, Consumer<Budget> h) {
        List<Consumer<Budget>> hs = exhaustedHandlers.get(index);
        if (hs != null) hs.remove(h);
    }

    private void failWaiters() {
        for (Set<Budget.Waiter> ws : waiters.values()) {
            for (Budget.Waiter w : List.copyOf(ws)) w.future.completeExceptionally(
                    new IllegalStateException("the budget registry was closed"));
            ws.clear();
        }
    }

    /** Frees the registry; pending {@code consumeAsync} futures fail. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
