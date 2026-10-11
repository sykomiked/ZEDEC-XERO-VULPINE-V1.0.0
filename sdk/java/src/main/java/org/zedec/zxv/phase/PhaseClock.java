// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.phase;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.math.BigInteger;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.Set;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * The phase coordinator's logical clock (the kernel's K6). ZXV time is a
 * tick count, not wall-clock time; one harmonic fundamental is
 * {@link #CYCLE} = 27,720 ticks (every harmonic 1..11 lands on a whole tick).
 *
 * <p>Two ways to drive it:
 * <ul>
 *   <li>manually with {@link #advance(long)}: deterministic, for tests and simulations;</li>
 *   <li>in real time with {@link #startRealtime(long)}: a scheduled task advances the
 *       clock at a chosen tick rate and completes waiters.</li>
 * </ul>
 * Advancing, admitting and deferring need {@code PHASE}.
 *
 * <pre>{@code
 * try (PhaseClock clock = PhaseClock.create(cap)) {
 *     DeferredEvent ev = clock.defer(PhaseClock.CYCLE);
 *     CompletableFuture<DeferredEvent> done = ev.awaitCleared();
 *     clock.advance(PhaseClock.CYCLE);        // done completes here
 * }
 * }</pre>
 */
public final class PhaseClock implements AutoCloseable {
    /** Ticks in one harmonic fundamental: lcm(1..11) = 27,720. */
    public static final long CYCLE = 27720L;

    /**
     * The result of an admission request.
     *
     * @param decision the coordinator's decision
     * @param token    non-zero only on {@link Decision#ADMIT}
     */
    public record Admission(Decision decision, long token) {}

    /**
     * The result of an advance.
     *
     * @param now     the new tick
     * @param cleared how many deferred events cleared in this advance
     */
    public record Advance(long now, int cleared) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;
    private final Map<Long, DeferredEvent> waiting = new ConcurrentHashMap<>();

    private PhaseClock(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("phase clock", h, x -> Ffi.callVoid(Native.phase_destroy, x), ctx, Caps.res(ctx));
        res.onClose(() -> {
            for (DeferredEvent e : List.copyOf(waiting.values())) {
                e.cleared.completeExceptionally(new CancellationException("the phase clock was closed"));
            }
            waiting.clear();
        });
    }

    /** A new clock at tick 0 in the capability's context. */
    public static PhaseClock create(Capability cap) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.phase_create_cap, l.h(0), out), ctx);
            return new PhaseClock(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /** The current tick. */
    public long now() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment t = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.phase_now, l.h(0), t), ctx);
            return t.get(JAVA_LONG, 0);
        }
    }

    /**
     * Advances by {@code delta} ticks. Deferred events due at or before the
     * new tick clear, and their {@link DeferredEvent#awaitCleared()} futures
     * complete on this thread before this method returns.
     */
    public Advance advance(long delta) {
        if (delta < 0) throw Errors.usage(-1, "delta must be >= 0");
        Advance r;
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment now = a.allocate(JAVA_LONG), cl = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.phase_advance, l.h(0), l.h(1), delta, now, cl), ctx);
            r = new Advance(now.get(JAVA_LONG, 0), cl.get(JAVA_INT, 0));
        }
        settle();
        return r;
    }

    private void settle() {
        for (DeferredEvent e : List.copyOf(waiting.values())) {
            DeferredEvent.State s = eventState(e.id());
            if (s == DeferredEvent.State.CLEARED) {
                waiting.remove(e.id());
                e.cleared.complete(e);
            } else if (s == DeferredEvent.State.CANCELLED) {
                waiting.remove(e.id());
                e.cleared.completeExceptionally(new CancellationException("deferred event " + e.id() + " was cancelled"));
            }
        }
    }

    /**
     * Asks the coordinator to admit a step.
     *
     * @param phaseId 1..13
     * @param stepId  the caller's step id
     * @param flags   the step's properties
     */
    public Admission admit(int phaseId, int stepId, Set<StepFlag> flags) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(JAVA_INT), t = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.phase_admit, l.h(0), l.h(1), phaseId, stepId, StepFlag.mask(flags), d, t), ctx);
            return new Admission(Decision.fromCode(d.get(JAVA_INT, 0)), t.get(JAVA_LONG, 0));
        }
    }

    /** {@link #admit(int, int, Set)} with varargs flags. */
    public Admission admit(int phaseId, int stepId, StepFlag... flags) {
        Set<StepFlag> s = EnumSet.noneOf(StepFlag.class);
        for (StepFlag f : flags) s.add(f);
        return admit(phaseId, stepId, s);
    }

    /** Reports a phase's health. */
    public void setHealth(int phaseId, Health health) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native.phase_set_health, l.h(0), l.h(1), phaseId, health.ordinal()), ctx);
        }
    }

    /**
     * Parks an S0 event until absolute tick {@code dueTick} (which must be
     * after {@link #now()}).
     */
    public DeferredEvent defer(long dueTick) {
        DeferredEvent ev;
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.phase_defer, l.h(0), l.h(1), dueTick, id), ctx);
            ev = new DeferredEvent(this, id.get(JAVA_LONG, 0), dueTick);
        }
        waiting.put(ev.id(), ev);
        settle(); // in case a concurrent advance passed the tick already
        return ev;
    }

    /**
     * A future that completes with {@code tick} once the clock reaches it
     * (at once if it already has).
     */
    public CompletableFuture<Long> untilTick(long tick) {
        if (tick <= now()) return CompletableFuture.completedFuture(tick);
        return defer(tick).awaitCleared().thenApply(e -> tick);
    }

    /** The earliest due tick of a pending event, if any. */
    public OptionalLong nextDue() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment t = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native.phase_next_due, l.h(0), t);
            if (st == -4) return OptionalLong.empty();
            Ffi.check(st, ctx);
            return OptionalLong.of(t.get(JAVA_LONG, 0));
        }
    }

    DeferredEvent.State eventState(long id) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.phase_event, l.h(0), id, s, MemorySegment.NULL), ctx);
            int v = s.get(JAVA_INT, 0);
            return v == 1 ? DeferredEvent.State.CLEARED : v == 2 ? DeferredEvent.State.CANCELLED : DeferredEvent.State.PENDING;
        }
    }

    void cancel(DeferredEvent ev) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native.phase_cancel, l.h(0), l.h(1), ev.id()), ctx);
        }
        settle();
    }

    /**
     * Starts a real-time driver on its own daemon thread: the clock advances
     * by {@code ticksPerSecond} ticks per second of wall time (exactly, in
     * aggregate) and waiters complete on that thread. Close the driver to stop.
     */
    public RealtimeDriver startRealtime(long ticksPerSecond) {
        ScheduledExecutorService ex = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "zxv-phase-clock");
            t.setDaemon(true);
            return t;
        });
        return new RealtimeDriver(ticksPerSecond, ex, true);
    }

    /** {@link #startRealtime(long)} on an executor you own (it is not shut down on close). */
    public RealtimeDriver startRealtime(long ticksPerSecond, ScheduledExecutorService executor) {
        return new RealtimeDriver(ticksPerSecond, executor, false);
    }

    /** A running real-time driver. Close it to stop advancing. */
    public final class RealtimeDriver implements AutoCloseable {
        private final long ticksPerSecond;
        private final ScheduledExecutorService executor;
        private final boolean owned;
        private final ScheduledFuture<?> task;
        private final long startNanos = System.nanoTime();
        private long advanced;
        private volatile Throwable failure;

        private RealtimeDriver(long ticksPerSecond, ScheduledExecutorService executor, boolean owned) {
            if (ticksPerSecond <= 0) throw Errors.usage(-1, "ticksPerSecond must be > 0");
            this.ticksPerSecond = ticksPerSecond;
            this.executor = executor;
            this.owned = owned;
            long period = Math.max(1_000_000L, 1_000_000_000L / ticksPerSecond);
            this.task = executor.scheduleAtFixedRate(this::step, period, period, TimeUnit.NANOSECONDS);
        }

        private void step() {
            try {
                long elapsed = System.nanoTime() - startNanos;
                long target = BigInteger.valueOf(elapsed).multiply(BigInteger.valueOf(ticksPerSecond))
                        .divide(BigInteger.valueOf(1_000_000_000L)).longValueExact();
                long delta = target - advanced;
                if (delta > 0) {
                    advance(delta);
                    advanced = target;
                }
            } catch (Throwable t) {
                failure = t;
                task.cancel(false);
            }
        }

        /** The tick rate. */
        public long ticksPerSecond() {
            return ticksPerSecond;
        }

        /** Why the driver stopped on its own (for example a revoked capability), if it did. */
        public Optional<Throwable> failure() {
            return Optional.ofNullable(failure);
        }

        /** Stops advancing. */
        @Override
        public void close() {
            task.cancel(false);
            if (owned) executor.shutdown();
        }
    }

    /** Frees the clock; pending {@code awaitCleared} futures fail with a cancellation. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
