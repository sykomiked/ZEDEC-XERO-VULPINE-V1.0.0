// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.budget;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import org.zedec.zxv.BudgetExhaustedException;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ZxvUsageException;
import org.zedec.zxv.internal.BudgetBridge;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * One exact rational budget in a {@link Budgets} registry. Thread-safe.
 *
 * <ul>
 *   <li>{@link #consume} raises {@link BudgetExhaustedException} when an
 *       enforced limit would be passed (nothing is consumed) and
 *       {@link ZxvUsageException} ({@code ZXV_E_RMAG_INVALID}) for a
 *       negative or overflowing amount: never retry that.</li>
 *   <li>{@link #consumeAsync} waits instead: it completes when the consume
 *       succeeds, retrying each time the budget is replenished.</li>
 * </ul>
 */
public final class Budget {
    static {
        BudgetBridge.install(new BudgetBridge.Bridge() {
            @Override
            public NativeResource registry(Object budget) {
                return ((Budget) budget).owner.res;
            }

            @Override
            public int index(Object budget) {
                return ((Budget) budget).index;
            }
        });
    }

    /**
     * A snapshot of a budget.
     *
     * @param remaining limit minus consumed (never below zero)
     * @param used      consumed so far
     * @param epoch     how many replenishments happened (bumps on release or a larger limit)
     */
    public record Status(Rational remaining, Rational used, long epoch) {}

    private final Budgets owner;
    private final int index;
    private final String name;
    private final Resource resource;

    Budget(Budgets owner, int index, String name, Resource resource) {
        this.owner = owner;
        this.index = index;
        this.name = name;
        this.resource = resource;
    }

    /** The budget's index in its registry. */
    public int index() {
        return index;
    }

    /** Its name. */
    public String name() {
        return name;
    }

    /** What it measures. */
    public Resource resource() {
        return resource;
    }

    /** The registry it lives in. */
    public Budgets registry() {
        return owner;
    }

    /**
     * Consumes an exact amount (&gt; 0).
     *
     * @throws BudgetExhaustedException if an enforced limit would be passed; nothing is consumed
     * @throws ZxvUsageException        {@code ZXV_E_RMAG_INVALID} for a malformed or overflowing amount
     */
    public void consume(Rational amount) {
        Outcome o = rawConsume(amount);
        if (o.status != 0) throw Errors.raise(o.status, o.detail, owner.ctx);
    }

    /** {@link #consume(Rational)} of a whole number of units. */
    public void consume(long units) {
        consume(Rational.of(units));
    }

    /** Consumes if it fits; returns false (consuming nothing) on exhaustion. Other errors still throw. */
    public boolean tryConsume(Rational amount) {
        Outcome o = rawConsume(amount);
        if (o.status == -19) return false;
        if (o.status != 0) throw Errors.raise(o.status, o.detail, owner.ctx);
        return true;
    }

    private record Outcome(int status, String detail) {}

    private Outcome rawConsume(Rational amount) {
        try (Lease l = Lease.of(Caps.res(owner.cap), owner.res)) {
            int st = Ffi.call(Native.rmag_consume, l.h(0), l.h(1), index, amount.numerator(), amount.denominator());
            // zxv_last_error is thread-local: read it on this thread, right away.
            return new Outcome(st, st == 0 ? "" : Ffi.lastError());
        }
    }

    /**
     * Consumes asynchronously. The future completes when the consume
     * succeeds: at once if it fits, otherwise after a replenishment (a
     * {@link #release} or a larger {@link #setLimit limit} through this SDK)
     * makes it fit. It fails at once on {@code ZXV_E_RMAG_INVALID} or a
     * capability denial. Cancel the future to stop waiting.
     */
    public CompletableFuture<Void> consumeAsync(Rational amount) {
        Waiter w = new Waiter(amount);
        w.future.whenComplete((v, e) -> w.unregister());
        w.attempt();
        return w.future;
    }

    /**
     * {@link #consumeAsync(Rational)} with a timeout: the future fails with
     * {@link java.util.concurrent.TimeoutException} if the consume has not
     * succeeded in time (nothing is consumed then).
     */
    public CompletableFuture<Void> consumeAsync(Rational amount, Duration timeout) {
        return consumeAsync(amount).orTimeout(timeout.toNanos(), TimeUnit.NANOSECONDS);
    }

    /** Gives back an amount (consumption is clamped at zero); wakes async waiters. */
    public void release(Rational amount) {
        try (Lease l = Lease.of(Caps.res(owner.cap), owner.res)) {
            Ffi.check(Ffi.call(Native.rmag_release, l.h(0), l.h(1), index, amount.numerator(), amount.denominator()),
                    owner.ctx);
        }
    }

    /** Replaces the limit; a larger limit wakes async waiters. */
    public void setLimit(Rational limit) {
        try (Lease l = Lease.of(Caps.res(owner.cap), owner.res)) {
            Ffi.check(Ffi.call(Native.rmag_set_limit, l.h(0), l.h(1), index, limit.numerator(), limit.denominator()),
                    owner.ctx);
        }
    }

    /** Remaining, used and the replenishment epoch, read atomically. */
    public Status status() {
        try (Lease l = Lease.of(owner.res); Arena a = Arena.ofConfined()) {
            MemorySegment rn = a.allocate(JAVA_LONG), rd = a.allocate(JAVA_LONG), un = a.allocate(JAVA_LONG),
                    ud = a.allocate(JAVA_LONG), ep = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.rmag_status, l.h(0), index, rn, rd, un, ud, ep), owner.ctx);
            return new Status(Rational.of(rn.get(JAVA_LONG, 0), rd.get(JAVA_LONG, 0)),
                    Rational.of(un.get(JAVA_LONG, 0), ud.get(JAVA_LONG, 0)), ep.get(JAVA_LONG, 0));
        }
    }

    /** {@code limit - used}, exact. */
    public Rational remaining() {
        return status().remaining();
    }

    /** Consumed so far, exact. */
    public Rational used() {
        return status().used();
    }

    /**
     * Calls {@code handler} whenever a consume on this budget is denied. It
     * runs synchronously on the thread whose consume was refused; keep it
     * short and do not close the registry from it. Close the returned handle
     * to unregister.
     */
    public AutoCloseable onExhausted(Consumer<Budget> handler) {
        owner.addExhaustedHandler(index, handler);
        return () -> owner.removeExhaustedHandler(index, handler);
    }

    @Override
    public String toString() {
        return "Budget[" + name + ", " + resource + "]";
    }

    final class Waiter {
        final Rational amount;
        final CompletableFuture<Void> future = new CompletableFuture<>();

        Waiter(Rational amount) {
            this.amount = amount;
        }

        void attempt() {
            if (future.isDone()) return;
            try {
                long before = status().epoch();
                Outcome o = rawConsume(amount);
                int st = o.status;
                if (st == 0) {
                    if (!future.complete(null)) release(amount); // cancelled meanwhile: give it back
                    return;
                }
                if (st != -19) {
                    future.completeExceptionally(Errors.raise(st, o.detail, owner.ctx));
                    return;
                }
                Set<Waiter> ws = owner.waiters.computeIfAbsent(index, k -> ConcurrentHashMap.newKeySet());
                ws.add(this);
                // A replenishment between the denial and the registration must not be lost.
                if (status().epoch() != before && ws.remove(this)) {
                    Thread.ofVirtual().name("zxv-budget-retry").start(this::retry);
                }
            } catch (Throwable t) {
                future.completeExceptionally(t);
            }
        }

        void retry() {
            attempt();
        }

        void unregister() {
            Set<Waiter> ws = owner.waiters.get(index);
            if (ws != null) ws.remove(this);
        }
    }
}
