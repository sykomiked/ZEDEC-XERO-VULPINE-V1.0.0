# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact rational resource budgets (kernel ``rmag``, K2)."""

from __future__ import annotations

import asyncio
import enum
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional, Tuple

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .errors import BudgetExhaustedError
from .rational import Rational, RationalLike

__all__ = ["Resource", "BudgetEvent", "BudgetStatus", "Budget", "Budgets"]


class Resource(enum.IntEnum):
    """What a budget measures (``rmag_resource_t``)."""

    COMPUTE_CYCLES = 0
    MEMORY = 1
    BANDWIDTH = 2
    ENERGY = 3
    STORAGE = 4
    TOKENS = 12
    """One model token = one event (used by :meth:`zxv.Session.bind_budget`)."""


class BudgetEvent(enum.IntEnum):
    """Listener events delivered by ``libzxv``."""

    DENIED = 1
    """A consume was refused (exhausted)."""
    INVALID = 2
    """A consume or release was malformed."""
    REPLENISHED = 3
    """A release or a larger limit freed budget."""


@dataclass(frozen=True)
class BudgetStatus:
    """Exact state of one budget."""

    remaining: Rational
    """limit - consumed."""
    used: Rational
    """consumed so far."""
    epoch: int
    """Counts REPLENISHED events; changes whenever budget is freed."""


class Budgets(NativeHandle):
    """A set of rational resource budgets (one ``zxv_rmag``), used through a capability.

    Creating, consuming and releasing need ``Cap.BUDGET``. The SDK installs
    the single C listener of the set and fans events out to
    :meth:`on_exhausted` / :meth:`on_replenished` callbacks and to waiters of
    :meth:`Budget.consume_async` and :meth:`Budget.consume_wait`.

    Callbacks run synchronously on the thread whose call caused the event
    (possibly a generation worker thread). Keep them short.
    """

    def __init__(self, cap: Capability) -> None:
        """Create an empty budget set in ``cap.context``."""
        ctx = cap.context
        out = ffi.new("zxv_rmag **")
        L = _native.lib()
        _native.check(L.zxv_rmag_create_cap(cap._handle(), out), ctx)
        self._cap = cap
        self._cond = threading.Condition()
        self._async_waiters: Dict[int, List[Tuple[asyncio.AbstractEventLoop, "asyncio.Future[None]"]]] = {}
        self._on_exhausted: List[Callable[["Budget"], None]] = []
        self._on_replenished: List[Callable[["Budget"], None]] = []
        self._budgets: Dict[int, Budget] = {}
        super().__init__(out[0], cap, ctx)

        def listener(user: Any, budget: int, event: int) -> None:
            self._dispatch(int(budget), int(event))

        self._listener = ffi.callback("zxv_rmag_listener", listener, onerror=_print_and_continue)
        _native.check(L.zxv_rmag_listen(self._h, self._listener, ffi.NULL), ctx)

    def _destroy(self, handle: Any) -> None:
        L = _native.lib()
        L.zxv_rmag_listen(handle, ffi.NULL, ffi.NULL)
        L.zxv_rmag_destroy(handle)
        self._wake_all()

    @property
    def capability(self) -> Capability:
        """The capability every call uses."""
        return self._cap

    def create(self, name: str, resource: Resource, limit: RationalLike, *,
               enforce: bool = True) -> "Budget":
        """Create a budget with an exact limit (>= 0).

        Args:
            name: a label.
            resource: what it measures.
            limit: the exact limit (int, Fraction, Decimal, Rational or text).
            enforce: ``False`` only counts and never denies.
        """
        lim = Rational.coerce(limit, "limit")
        out = ffi.new("uint32_t *")
        self._check(_native.lib().zxv_rmag_budget(
            self._cap._handle(), self._handle(), _native.to_cstr(name, "name"),
            int(Resource(resource)), lim.numerator, lim.denominator, 1 if enforce else 0, out))
        b = Budget(self, int(out[0]), name)
        self._budgets[b.id] = b
        return b

    def on_exhausted(self, callback: Callable[["Budget"], None]) -> None:
        """Call ``callback(budget)`` whenever a consume on any budget is denied."""
        self._on_exhausted.append(callback)

    def on_replenished(self, callback: Callable[["Budget"], None]) -> None:
        """Call ``callback(budget)`` whenever budget is freed (release / larger limit)."""
        self._on_replenished.append(callback)

    # ------------------------------------------------------------ events

    def _dispatch(self, budget: int, event: int) -> None:
        b = self._budgets.get(budget)
        if event == BudgetEvent.REPLENISHED:
            self._wake(budget)
            for cb in list(self._on_replenished):
                if b is not None:
                    cb(b)
        elif event == BudgetEvent.DENIED:
            for cb in list(self._on_exhausted):
                if b is not None:
                    cb(b)

    def _wake(self, budget: int) -> None:
        with self._cond:
            self._cond.notify_all()
            waiters = self._async_waiters.pop(budget, [])
        for loop, fut in waiters:
            try:
                loop.call_soon_threadsafe(_set_done, fut)
            except RuntimeError:  # loop closed
                pass

    def _wake_all(self) -> None:
        for b in list(self._async_waiters):
            self._wake(b)

    def _add_async_waiter(self, budget: int, loop: asyncio.AbstractEventLoop,
                          fut: "asyncio.Future[None]") -> None:
        with self._cond:
            self._async_waiters.setdefault(budget, []).append((loop, fut))

    def _remove_async_waiter(self, budget: int, fut: "asyncio.Future[None]") -> None:
        with self._cond:
            lst = self._async_waiters.get(budget)
            if lst:
                self._async_waiters[budget] = [w for w in lst if w[1] is not fut]


def _set_done(fut: "asyncio.Future[None]") -> None:
    if not fut.done():
        fut.set_result(None)


def _print_and_continue(exc_type: Any, exc: Any, tb: Any) -> None:
    import traceback

    traceback.print_exception(exc_type, exc, tb)


class Budget:
    """One budget in a :class:`Budgets` set. Amounts are exact rationals."""

    def __init__(self, owner: Budgets, budget_id: int, name: str) -> None:
        self._owner = owner
        self._id = budget_id
        self._name = name

    @property
    def id(self) -> int:
        """The budget's id within its set."""
        return self._id

    @property
    def name(self) -> str:
        """The label given at creation."""
        return self._name

    @property
    def budgets(self) -> Budgets:
        """The set this budget belongs to."""
        return self._owner

    def _call(self, fn: str, amount: RationalLike, what: str) -> None:
        a = Rational.coerce(amount, what)
        o = self._owner
        o._check(getattr(_native.lib(), fn)(o._cap._handle(), o._handle(), self._id,
                                            a.numerator, a.denominator))

    def consume(self, amount: RationalLike) -> None:
        """Consume ``amount`` (> 0) now.

        Raises:
            BudgetExhaustedError: it would pass the limit (nothing consumed).
            ZxvUsageError: ``ZXV_E_RMAG_INVALID`` (negative/overflowing); never retry.
        """
        self._call("zxv_rmag_consume", amount, "amount")

    def try_consume(self, amount: RationalLike) -> bool:
        """Like :meth:`consume` but returns ``False`` instead of raising on exhaustion."""
        try:
            self.consume(amount)
            return True
        except BudgetExhaustedError:
            return False

    def release(self, amount: RationalLike) -> None:
        """Give ``amount`` back (clamped at zero consumed); wakes waiters."""
        self._call("zxv_rmag_release", amount, "amount")

    def set_limit(self, limit: RationalLike) -> None:
        """Replace the limit; a larger limit wakes waiters."""
        self._call("zxv_rmag_set_limit", limit, "limit")

    @property
    def status(self) -> BudgetStatus:
        """Remaining, used and epoch, exactly."""
        o = self._owner
        v = ffi.new("int64_t[4]")
        ep = ffi.new("uint64_t *")
        o._check(_native.lib().zxv_rmag_status(o._handle(), self._id, v, v + 1, v + 2, v + 3, ep))
        return BudgetStatus(Rational._raw(v[0], v[1]), Rational._raw(v[2], v[3]), int(ep[0]))

    @property
    def remaining(self) -> Rational:
        """limit - consumed."""
        return self.status.remaining

    async def consume_async(self, amount: RationalLike, *, timeout: Optional[float] = None) -> None:
        """Consume ``amount``, waiting until the budget is replenished if needed.

        Completes when the consume succeeds. On exhaustion it waits for a
        ``release`` or a larger limit (the epoch changes) and retries. A
        malformed amount fails immediately. Cancel the task to stop waiting.

        Args:
            amount: exact amount.
            timeout: seconds to wait in total (an int or Decimal; ``None`` waits
                forever). This is wall-clock host time, so a float is
                accepted here.

        Raises:
            asyncio.TimeoutError: the timeout passed first.
        """
        a = Rational.coerce(amount, "amount")
        loop = asyncio.get_running_loop()
        deadline = None if timeout is None else loop.time() + float(timeout)
        o = self._owner
        while True:
            epoch = self.status.epoch
            try:
                self.consume(a)
                return
            except BudgetExhaustedError:
                pass
            fut: asyncio.Future[None] = loop.create_future()
            o._add_async_waiter(self._id, loop, fut)
            try:
                if o.closed:
                    raise BudgetExhaustedError(-19, "ZXV_E_BUDGET", "budget set closed")
                if self.status.epoch != epoch:
                    continue  # replenished between the denial and registering
                remaining = None if deadline is None else max(0.0, deadline - loop.time())
                await asyncio.wait_for(fut, remaining)
            finally:
                o._remove_async_waiter(self._id, fut)

    def consume_wait(self, amount: RationalLike, *, timeout: Optional[float] = None) -> bool:
        """Blocking form of :meth:`consume_async` for threads.

        Returns ``True`` once consumed, ``False`` if ``timeout`` seconds
        passed first.
        """
        a = Rational.coerce(amount, "amount")
        o = self._owner
        deadline = None if timeout is None else time.monotonic() + float(timeout)
        while True:
            with o._cond:
                epoch = self.status.epoch
                try:
                    self.consume(a)
                    return True
                except BudgetExhaustedError:
                    pass
                while self.status.epoch == epoch:
                    if o.closed:
                        return False
                    left = None if deadline is None else deadline - time.monotonic()
                    if left is not None and left <= 0:
                        return False
                    o._cond.wait(left)

    def __repr__(self) -> str:
        return f"<zxv.Budget {self._name!r} id={self._id}>"
