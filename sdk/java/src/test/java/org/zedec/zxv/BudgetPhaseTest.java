// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.budget.Budget;
import org.zedec.zxv.budget.Budgets;
import org.zedec.zxv.budget.Resource;
import org.zedec.zxv.phase.Decision;
import org.zedec.zxv.phase.DeferredEvent;
import org.zedec.zxv.phase.Health;
import org.zedec.zxv.phase.PhaseClock;
import org.zedec.zxv.phase.StepFlag;

/** Budgets (exact, async waiting) and the phase clock (manual and real time). */
class BudgetPhaseTest {
    @Test
    void exactBudgetExhaustion() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.BUDGET);
             Budgets budgets = Budgets.create(cap)) {
            Budget b = budgets.budget("tokens", Resource.TOKENS, Rational.of(10), true);
            AtomicInteger denied = new AtomicInteger();
            b.onExhausted(x -> denied.incrementAndGet());
            b.consume(Rational.of(7, 2));
            b.consume(Rational.of(13, 2));
            BudgetExhaustedException e = assertThrows(BudgetExhaustedException.class, () -> b.consume(Rational.of(1, 3)));
            assertEquals(-19, e.status());
            assertEquals(1, denied.get(), "onExhausted saw the denial");
            assertFalse(b.tryConsume(Rational.ONE));
            ZxvUsageException inv = assertThrows(ZxvUsageException.class, () -> b.consume(Rational.of(-1)));
            assertEquals("ZXV_E_RMAG_INVALID", inv.statusName());
            Budget.Status s = b.status();
            assertEquals(Rational.ZERO, s.remaining());
            assertEquals(Rational.of(10), s.used());
            assertEquals(0, s.epoch());
            b.release(Rational.of(1, 2));
            assertEquals(new Budget.Status(Rational.of(1, 2), Rational.parse("9.5"), 1), b.status());
            try (Capability noBudget = cap.attenuate()) {
                assertThrows(CapabilityDeniedException.class,
                        () -> Budgets.create(noBudget).budget("x", Resource.MEMORY, Rational.ONE, true));
            }
        }
    }

    @Test
    void consumeAsyncWaitsForReplenishment() throws Exception {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.BUDGET);
             Budgets budgets = Budgets.create(cap)) {
            Budget b = budgets.budget("energy", Resource.ENERGY, Rational.of(5), true);
            b.consume(Rational.of(5));
            CompletableFuture<Void> waiting = b.consumeAsync(Rational.of(2));
            assertFalse(waiting.isDone(), "nothing left: the consume waits");
            b.release(Rational.ONE);
            Thread.sleep(50);
            assertFalse(waiting.isDone(), "one unit is not enough for two");
            b.setLimit(Rational.of(6)); // a larger limit also replenishes
            waiting.get(10, TimeUnit.SECONDS);
            assertEquals(Rational.of(6), b.used());

            CompletableFuture<Void> immediate = b.consumeAsync(Rational.ZERO.add(Rational.of(-1)));
            ExecutionException inv = assertThrows(ExecutionException.class, () -> immediate.get(5, TimeUnit.SECONDS));
            assertTrue(inv.getCause() instanceof ZxvUsageException, "INVALID fails at once");

            CompletableFuture<Void> timed = b.consumeAsync(Rational.of(100), Duration.ofMillis(100));
            ExecutionException te = assertThrows(ExecutionException.class, () -> timed.get(10, TimeUnit.SECONDS));
            assertTrue(te.getCause() instanceof TimeoutException);

            CompletableFuture<Void> cancelled = b.consumeAsync(Rational.of(100));
            cancelled.cancel(true);
            b.release(Rational.of(6));
            Thread.sleep(50);
            assertEquals(Rational.ZERO, b.used(), "a cancelled waiter consumed nothing");

            CompletableFuture<Void> pending = b.consumeAsync(Rational.of(100));
            budgets.close();
            assertThrows(ExecutionException.class, () -> pending.get(5, TimeUnit.SECONDS), "closing fails waiters");
        }
    }

    @Test
    void manualPhaseClock() throws Exception {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.PHASE);
             PhaseClock clock = PhaseClock.create(cap)) {
            assertEquals(27720, PhaseClock.CYCLE);
            DeferredEvent ev = clock.defer(PhaseClock.CYCLE);
            CompletableFuture<DeferredEvent> cleared = ev.awaitCleared();
            assertEquals(PhaseClock.CYCLE, clock.nextDue().orElseThrow());
            PhaseClock.Advance r = clock.advance(PhaseClock.CYCLE - 1);
            assertEquals(0, r.cleared());
            assertFalse(cleared.isDone(), "still S0 one tick early");
            assertEquals(DeferredEvent.State.PENDING, ev.state());
            r = clock.advance(1);
            assertEquals(new PhaseClock.Advance(27720, 1), r);
            assertTrue(cleared.isDone(), "cleared on tick 27720, inside advance");
            assertEquals(ev, cleared.get());
            assertTrue(clock.nextDue().isEmpty());

            DeferredEvent later = clock.defer(30000);
            CompletableFuture<DeferredEvent> c2 = later.awaitCleared();
            later.cancel();
            assertThrows(CancellationException.class, c2::join);
            assertEquals(DeferredEvent.State.CANCELLED, later.state());

            assertEquals(Decision.DEFER, clock.admit(6, 1, StepFlag.S0_PENDING).decision());
            clock.setHealth(6, Health.HEALTHY);
            PhaseClock.Admission ok = clock.admit(6, 2, StepFlag.REQUIRES_HEALTH);
            assertEquals(Decision.ADMIT, ok.decision());
            assertTrue(ok.token() != 0);
            assertTrue(clock.untilTick(5).isDone(), "a past tick is already reached");
        }
    }

    @Test
    void realtimePhaseClockAndPhaseDeferral() throws Exception {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.PHASE);
             PhaseClock clock = PhaseClock.create(cap)) {
            CompletableFuture<Long> reached = clock.untilTick(2000);
            try (PhaseClock.RealtimeDriver driver = clock.startRealtime(20_000)) {
                assertEquals(2000L, reached.get(10, TimeUnit.SECONDS));
                assertTrue(driver.failure().isEmpty());
            }
            assertTrue(clock.now() >= 2000);
            try (Capability noPhase = cap.attenuate()) {
                assertThrows(CapabilityDeniedException.class, () -> PhaseClock.create(noPhase).defer(1_000_000));
            }
        }
    }
}
