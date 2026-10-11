# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Attestations, rational budgets (sync, async, threads) and the phase clock."""

from __future__ import annotations

import asyncio
import threading
import time
from typing import List

import pytest

import zxv
from zxv import Cap, Rational, Truth


# ------------------------------------------------------------------ attestations


def test_attestations_hold_contradictions(cap: zxv.Capability) -> None:
    with zxv.Attestations(cap) as reg:
        i = reg.attest("model.gguf", zxv.Subject.MODEL_PACK, Truth.TRUE, "loader", digest=b"\x01" * 32)
        assert reg.find("model.gguf") == i and reg.find("nope") is None
        assert reg.gate() is True
        reg.evidence(i, True)
        st = reg.evidence(i, False)
        assert st.truth is Truth.BOTH and (st.positive, st.negative) == (2, 1)
        assert reg.gate() is False, "BOTH blocks the actuator gate"


def test_attest_needs_capability(cap: zxv.Capability) -> None:
    ro = cap.attenuate(Cap.LEDGER_READ)
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.Attestations(ro)  # ABI 1.2: creating the registry needs ATTEST
    rw = cap.attenuate(Cap.ATTEST)
    with zxv.Attestations(rw) as reg:
        rw.set_attestation(Truth.NEITHER)
        with pytest.raises(zxv.CapabilityDeniedError):
            reg.attest("x", zxv.Subject.DATA, Truth.TRUE, "me")


# ------------------------------------------------------------------ budgets


def test_budget_exact_consume_exhaustion_invalid_release(cap: zxv.Capability) -> None:
    with zxv.Budgets(cap) as budgets:
        denied: List[str] = []
        budgets.on_exhausted(lambda b: denied.append(b.name))
        b = budgets.create("tokens", zxv.Resource.TOKENS, 10)
        b.consume(Rational(7, 2))
        b.consume("6.5")
        with pytest.raises(zxv.BudgetExhaustedError):
            b.consume(Rational(1, 3))
        assert denied == ["tokens"]
        assert b.try_consume(Rational(1, 3)) is False
        with pytest.raises(zxv.ZxvUsageError) as e:
            b.consume(-1)
        assert e.value.status_name == "ZXV_E_RMAG_INVALID"
        with pytest.raises(zxv.FloatRejectedError):
            b.consume(0.5)
        st = b.status
        assert st.remaining == 0 and st.used == 10 and st.epoch == 0
        b.release(Rational(1, 2))
        st = b.status
        assert st.remaining == Rational(1, 2) and st.epoch == 1, "release replenishes and bumps the epoch"


def test_budget_needs_capability(cap: zxv.Capability) -> None:
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.Budgets(cap.attenuate(Cap.LEDGER_READ))  # ABI 1.2: creating needs BUDGET
    b = cap.attenuate(Cap.BUDGET)
    with zxv.Budgets(b) as budgets:
        b.revoke()
        with pytest.raises(zxv.CapabilityDeniedError):
            budgets.create("x", zxv.Resource.ENERGY, 1)


async def test_consume_async_waits_for_replenish(cap: zxv.Capability) -> None:
    with zxv.Budgets(cap) as budgets:
        b = budgets.create("gpu-seconds", zxv.Resource.COMPUTE_CYCLES, 2)
        b.consume(2)
        waiter = asyncio.create_task(b.consume_async(1))
        await asyncio.sleep(0.05)
        assert not waiter.done(), "exhausted: still waiting"
        b.release(1)  # replenish -> wakes the waiter, which retries and succeeds
        await asyncio.wait_for(waiter, 2)
        assert b.status.remaining == 0
        # a larger limit also replenishes
        waiter = asyncio.create_task(b.consume_async(3))
        await asyncio.sleep(0.02)
        b.set_limit(5)
        await asyncio.wait_for(waiter, 2)
        assert b.status.used == 5


async def test_consume_async_timeout_cancel_and_invalid(cap: zxv.Capability) -> None:
    with zxv.Budgets(cap) as budgets:
        b = budgets.create("t", zxv.Resource.TOKENS, 1)
        b.consume(1)
        with pytest.raises(asyncio.TimeoutError):
            await b.consume_async(1, timeout=0.05)
        t = asyncio.create_task(b.consume_async(1))
        await asyncio.sleep(0.01)
        t.cancel()
        with pytest.raises(asyncio.CancelledError):
            await t
        assert not budgets._async_waiters.get(b.id), "cancelled waiter is removed"
        with pytest.raises(zxv.ZxvUsageError):
            await b.consume_async(-1)  # INVALID fails immediately, never waits


async def test_replenish_from_another_thread(cap: zxv.Capability) -> None:
    with zxv.Budgets(cap) as budgets:
        b = budgets.create("t", zxv.Resource.TOKENS, 1)
        b.consume(1)
        threading.Timer(0.05, b.release, args=(1,)).start()
        await asyncio.wait_for(b.consume_async(1), 2)


def test_consume_wait_blocking_across_threads(cap: zxv.Capability) -> None:
    with zxv.Budgets(cap) as budgets:
        b = budgets.create("t", zxv.Resource.TOKENS, 1)
        b.consume(1)
        assert b.consume_wait(1, timeout=0.05) is False
        threading.Timer(0.05, b.release, args=(1,)).start()
        assert b.consume_wait(1, timeout=2) is True


# ------------------------------------------------------------------ phase clock


def test_phase_defer_clears_on_its_tick(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        ev = clock.defer(zxv.PHASE_CYCLE)
        assert clock.next_due() == 27720
        assert clock.advance(zxv.PHASE_CYCLE - 1) == 0
        assert ev.state is zxv.EventState.PENDING
        assert clock.advance(1) == 1 and clock.now == 27720
        assert ev.state is zxv.EventState.CLEARED
        assert clock.next_due() is None


def test_phase_admission(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        a = clock.admit(6, 1, zxv.StepFlag.S0_PENDING)
        assert a.decision is zxv.Decision.DEFER and a.token == 0 and not a.admitted
        clock.set_health(6, zxv.Health.HEALTHY)
        a = clock.admit(6, 2, zxv.StepFlag.REQUIRES_HEALTH)
        assert a.admitted and a.token != 0


def test_phase_needs_capability(cap: zxv.Capability) -> None:
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.PhaseClock(cap.attenuate(Cap.LEDGER_READ))  # ABI 1.2: creating needs PHASE
    p = cap.attenuate(Cap.PHASE)
    with zxv.PhaseClock(p) as clock:
        p.revoke()
        with pytest.raises(zxv.CapabilityDeniedError):
            clock.advance(1)


async def test_deferred_event_is_awaitable_with_manual_driver(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        ev = clock.defer(100)
        task = asyncio.create_task(ev.wait_cleared())
        await asyncio.sleep(0)
        clock.advance(99)
        await asyncio.sleep(0.01)
        assert not task.done()
        clock.advance(1)
        assert await asyncio.wait_for(task, 1) == 100
        assert await clock.until_tick(50) == 100  # already passed: immediate


async def test_cancelled_event_cancels_waiters(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        ev = clock.defer(10)
        task = asyncio.create_task(ev.wait_cleared())
        await asyncio.sleep(0)
        ev.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert ev.state is zxv.EventState.CANCELLED


async def test_realtime_driver(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        driver = clock.start_realtime(ticks_per_second=1000)
        t0 = time.monotonic()
        tick = await asyncio.wait_for(clock.until_tick(50), 5)
        assert tick >= 50 and time.monotonic() - t0 >= 0.03
        await driver.stop()
        assert driver.task.done()


def test_blocking_wait_from_a_thread(cap: zxv.Capability) -> None:
    with zxv.PhaseClock(cap) as clock:
        ev = clock.defer(5)
        threading.Timer(0.05, clock.advance, args=(5,)).start()
        assert ev.wait_cleared_blocking(timeout=2) is True
