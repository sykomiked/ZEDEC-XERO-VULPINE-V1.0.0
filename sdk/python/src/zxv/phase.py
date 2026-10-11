# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The phase coordinator clock (kernel ``phase_coord``, K6).

ZXV time is a logical tick count, not wall-clock time. One harmonic
fundamental is :data:`PHASE_CYCLE` = 27,720 ticks (lcm(1..11)), so every
harmonic 1..11 lands on a whole tick. The host drives the clock:
manually with :meth:`PhaseClock.advance` (deterministic tests and
simulations) or in real time with :meth:`PhaseClock.start_realtime`.
"""

from __future__ import annotations

import asyncio
import enum
import threading
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability

__all__ = [
    "PHASE_CYCLE", "Decision", "StepFlag", "Health", "EventState", "Admission",
    "DeferredEvent", "PhaseClock", "RealtimeDriver",
]

PHASE_CYCLE = 27720
"""Ticks in one harmonic fundamental."""


class Decision(enum.IntEnum):
    """The K6 coordinator's answer to :meth:`PhaseClock.admit`."""

    ADMIT = 0
    VETO = 1
    DEFER = 2
    RETRY = 3


class StepFlag(enum.IntFlag):
    """Flags describing a step submitted to :meth:`PhaseClock.admit`."""

    NONE = 0
    REQUIRES_HEALTH = 0x1
    HARDWARE = 0x2
    S0_PENDING = 0x4


class Health(enum.IntEnum):
    """Health of a phase."""

    UNKNOWN = 0
    HEALTHY = 1
    DEGRADED = 2
    UNHEALTHY = 3


class EventState(enum.IntEnum):
    """State of a deferred event."""

    PENDING = 0
    """S0: waiting for its tick."""
    CLEARED = 1
    CANCELLED = 2


@dataclass(frozen=True)
class Admission:
    """Result of :meth:`PhaseClock.admit`."""

    decision: Decision
    token: int
    """Non-zero only when ``decision`` is ADMIT."""

    @property
    def admitted(self) -> bool:
        """True when the step was admitted."""
        return self.decision is Decision.ADMIT


class DeferredEvent:
    """An S0 event parked until a tick; await it with :meth:`wait_cleared`."""

    def __init__(self, clock: "PhaseClock", event_id: int, due_tick: int) -> None:
        self._clock = clock
        self._id = event_id
        self._due = due_tick

    @property
    def id(self) -> int:
        """The event id."""
        return self._id

    @property
    def due_tick(self) -> int:
        """The absolute tick at which it clears."""
        return self._due

    @property
    def state(self) -> EventState:
        """The current :class:`EventState`."""
        return self._clock._event_state(self._id)

    def cancel(self) -> None:
        """Cancel the event; waiters raise :class:`asyncio.CancelledError`."""
        self._clock._cancel(self)

    async def wait_cleared(self) -> int:
        """Wait until the clock passes ``due_tick``; returns the clock's tick then.

        Raises:
            asyncio.CancelledError: the event was cancelled.
        """
        return await self._clock._await_event(self)

    def wait_cleared_blocking(self, timeout: Optional[float] = None) -> bool:
        """Block the calling thread until cleared; ``False`` on timeout or cancel."""
        return self._clock._block_event(self, timeout)

    def __repr__(self) -> str:
        return f"<zxv.DeferredEvent id={self._id} due={self._due}>"


class PhaseClock(NativeHandle):
    """A phase coordinator clock. Advancing, deferring and admitting need ``Cap.PHASE``.

    The SDK tracks waiters of :meth:`DeferredEvent.wait_cleared`; every
    :meth:`advance` (from any thread, or from the real-time driver) wakes
    the ones whose tick has come.
    """

    def __init__(self, cap: Capability) -> None:
        """Create a clock at tick 0 in ``cap.context``."""
        ctx = cap.context
        out = ffi.new("zxv_phase **")
        _native.check(_native.lib().zxv_phase_create_cap(cap._handle(), out), ctx)
        self._cap = cap
        self._wlock = threading.Condition()
        self._waiters: Dict[int, List[Tuple[asyncio.AbstractEventLoop, "asyncio.Future[int]"]]] = {}
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_phase_destroy(handle)
        with self._wlock:
            waiters, self._waiters = self._waiters, {}
            self._wlock.notify_all()
        for lst in waiters.values():
            for loop, fut in lst:
                _call_threadsafe(loop, _cancel_future, fut)

    @property
    def now(self) -> int:
        """The current tick."""
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_phase_now(self._handle(), out))
        return int(out[0])

    def advance(self, delta: int) -> int:
        """Advance by ``delta`` ticks; due events clear. Returns how many cleared."""
        d = _native.exact_int(delta, "delta", *_native.U64)
        now = ffi.new("uint64_t *")
        cleared = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_phase_advance(self._cap._handle(), self._handle(), d, now, cleared))
        self._resolve_waiters(int(now[0]))
        return int(cleared[0])

    def admit(self, phase_id: int, step_id: int, flags: StepFlag = StepFlag.NONE) -> Admission:
        """Ask the coordinator to admit step ``step_id`` of phase ``phase_id`` (1..13)."""
        dec = ffi.new("int32_t *")
        tok = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_phase_admit(
            self._cap._handle(), self._handle(), _native.exact_int(phase_id, "phase_id", *_native.U32),
            _native.exact_int(step_id, "step_id", *_native.U32), int(flags), dec, tok))
        return Admission(Decision(dec[0]), int(tok[0]))

    def set_health(self, phase_id: int, health: Health) -> None:
        """Report a phase's health."""
        self._check(_native.lib().zxv_phase_set_health(
            self._cap._handle(), self._handle(), _native.exact_int(phase_id, "phase_id", *_native.U32),
            int(Health(health))))

    def defer(self, due_tick: int) -> DeferredEvent:
        """Park an S0 event until absolute tick ``due_tick`` (> now)."""
        due = _native.exact_int(due_tick, "due_tick", *_native.U64)
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_phase_defer(self._cap._handle(), self._handle(), due, out))
        return DeferredEvent(self, int(out[0]), due)

    async def until_tick(self, tick: int) -> int:
        """Wait until the clock reaches ``tick`` (returns at once if it has)."""
        if self.now >= tick:
            return self.now
        return await self.defer(tick).wait_cleared()

    def next_due(self) -> Optional[int]:
        """Earliest due tick of a pending event, or ``None``."""
        out = ffi.new("uint64_t *")
        rc = _native.lib().zxv_phase_next_due(self._handle(), out)
        if rc == -4:
            return None
        self._check(rc)
        return int(out[0])

    def start_realtime(self, ticks_per_second: int) -> "RealtimeDriver":
        """Start an asyncio task that advances the clock in real time.

        Must be called from a running event loop. The task advances by the
        whole number of ticks elapsed (integer arithmetic on
        ``time.monotonic_ns``), wakes waiters, and yields back to the loop.
        Stop it with :meth:`RealtimeDriver.stop`.
        """
        tps = _native.exact_int(ticks_per_second, "ticks_per_second", 1, 10**9)
        return RealtimeDriver(self, tps)

    # ------------------------------------------------------------ internals

    def _event_state(self, event_id: int) -> EventState:
        st = ffi.new("int32_t *")
        due = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_phase_event(self._handle(), event_id, st, due))
        return EventState(st[0])

    def _cancel(self, ev: DeferredEvent) -> None:
        self._check(_native.lib().zxv_phase_cancel(self._cap._handle(), self._handle(), ev.id))
        with self._wlock:
            waiters = self._waiters.pop(ev.id, [])
            self._wlock.notify_all()
        for loop, fut in waiters:
            _call_threadsafe(loop, _cancel_future, fut)

    def _resolve_waiters(self, now: int) -> None:
        ready: List[Tuple[asyncio.AbstractEventLoop, "asyncio.Future[int]"]] = []
        with self._wlock:
            for eid in list(self._waiters):
                if self._event_state(eid) is not EventState.PENDING:
                    ready.extend(self._waiters.pop(eid))
            self._wlock.notify_all()
        for loop, fut in ready:
            _call_threadsafe(loop, _set_future, fut, now)

    async def _await_event(self, ev: DeferredEvent) -> int:
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[int] = loop.create_future()
        with self._wlock:
            st = self._event_state(ev.id)
            if st is EventState.CLEARED:
                return self.now
            if st is EventState.CANCELLED:
                raise asyncio.CancelledError(f"deferred event {ev.id} was cancelled")
            self._waiters.setdefault(ev.id, []).append((loop, fut))
        try:
            return await fut
        finally:
            with self._wlock:
                lst = self._waiters.get(ev.id)
                if lst:
                    self._waiters[ev.id] = [w for w in lst if w[1] is not fut]

    def _block_event(self, ev: DeferredEvent, timeout: Optional[float]) -> bool:
        deadline = None if timeout is None else time.monotonic() + float(timeout)
        with self._wlock:
            while True:
                if self._closed:
                    return False
                st = self._event_state(ev.id)
                if st is EventState.CLEARED:
                    return True
                if st is EventState.CANCELLED:
                    return False
                left = None if deadline is None else deadline - time.monotonic()
                if left is not None and left <= 0:
                    return False
                self._wlock.wait(left)


def _set_future(fut: "asyncio.Future[int]", value: int) -> None:
    if not fut.done():
        fut.set_result(value)


def _cancel_future(fut: "asyncio.Future[Any]") -> None:
    if not fut.done():
        fut.cancel()


def _call_threadsafe(loop: asyncio.AbstractEventLoop, fn: Any, *args: Any) -> None:
    try:
        loop.call_soon_threadsafe(fn, *args)
    except RuntimeError:  # loop already closed
        pass


class RealtimeDriver:
    """The asyncio task that advances a :class:`PhaseClock` in real time."""

    def __init__(self, clock: PhaseClock, ticks_per_second: int) -> None:
        self._clock = clock
        self._tps = ticks_per_second
        self._stop = asyncio.Event()
        self._task = asyncio.get_running_loop().create_task(self._run(), name="zxv-phase-realtime")

    @property
    def ticks_per_second(self) -> int:
        """The configured rate."""
        return self._tps

    @property
    def task(self) -> "asyncio.Task[None]":
        """The underlying asyncio task."""
        return self._task

    async def _run(self) -> None:
        start = time.monotonic_ns()
        advanced = 0
        # Sleep about one tick, but at least 1 ms and at most 50 ms, so the
        # loop stays responsive at any rate.
        period_ns = max(1_000_000, min(50_000_000, 1_000_000_000 // self._tps))
        while not self._stop.is_set():
            target = (time.monotonic_ns() - start) * self._tps // 1_000_000_000
            if target > advanced and not self._clock.closed:
                self._clock.advance(target - advanced)
                advanced = target
            try:
                await asyncio.wait_for(self._stop.wait(), period_ns / 1e9)
            except asyncio.TimeoutError:
                pass

    async def stop(self) -> None:
        """Stop advancing and wait for the task to finish."""
        self._stop.set()
        await self._task
