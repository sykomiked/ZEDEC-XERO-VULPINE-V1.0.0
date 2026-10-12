# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""A render-farm budget on libzxv's exact rational budgets (``rmag``).

The farm has a limit in a unit you choose (core-hours is the usual one),
held as an exact rational. Submitting a job reserves its whole estimated
cost (frames x cost per frame) or refuses the job; nothing is reserved in
part. Completing a job settles it against the actual cost: an
under-run is given back, an over-run is charged (and refused if the farm
cannot cover it). Cancelling gives back the frames not rendered. The
arithmetic and the limit check run in libzxv, so totals never drift.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Sequence

from zxv import BudgetExhaustedError, Budgets, Capability, Rational, Resource

from ._exact import exact
from .frametime import FrameRange

__all__ = ["RenderJob", "FarmBudget", "JobRefused", "Settlement"]


class JobRefused(Exception):
    """The farm cannot cover a job (or an over-run); nothing was charged."""

    def __init__(self, job: str, needed: Rational, remaining: Rational) -> None:
        super().__init__(f"job {job!r} needs {needed}, the farm has {remaining} left")
        self.job = job
        self.needed = needed
        self.remaining = remaining


@dataclass(frozen=True)
class RenderJob:
    """A job: a name, its frames and an exact cost per frame."""

    name: str
    frames: FrameRange
    cost_per_frame: Rational

    def __init__(self, name: str, frames: FrameRange, cost_per_frame: Any) -> None:
        if not isinstance(name, str) or not name:
            raise ValueError("a job needs a name")
        if not isinstance(frames, FrameRange):
            raise TypeError("frames must be a FrameRange")
        c = exact(cost_per_frame, "cost per frame")
        if c <= 0:
            raise ValueError("cost per frame must be positive")
        object.__setattr__(self, "name", name)
        object.__setattr__(self, "frames", frames)
        object.__setattr__(self, "cost_per_frame", c)

    @property
    def cost(self) -> Rational:
        """The whole job: frames x cost per frame, exactly."""
        return self.cost_per_frame * len(self.frames)


@dataclass(frozen=True)
class Settlement:
    """What :meth:`FarmBudget.complete` or :meth:`FarmBudget.cancel` did."""

    job: str
    reserved: Rational
    charged: Rational
    """The final cost of the job."""
    returned: Rational
    """Given back to the farm (0 on an over-run)."""
    extra: Rational
    """Charged beyond the reservation (0 on an under-run)."""


class FarmBudget:
    """One farm budget. Every call needs ``Cap.BUDGET`` on the capability."""

    def __init__(self, cap: Capability, limit: Any, *, name: str = "render-farm") -> None:
        lim = exact(limit, "limit")
        if lim < 0:
            raise ValueError("the limit must be >= 0")
        self._budgets = Budgets(cap)
        self._budget = self._budgets.create(name, Resource.COMPUTE_CYCLES, lim)
        self._reserved: Dict[str, RenderJob] = {}

    def close(self) -> None:
        self._budgets.close()

    def __enter__(self) -> "FarmBudget":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    @property
    def remaining(self) -> Rational:
        return self._budget.status.remaining

    @property
    def used(self) -> Rational:
        return self._budget.status.used

    @property
    def jobs(self) -> List[str]:
        """Names of jobs reserved and not yet settled, in submission order."""
        return list(self._reserved)

    def plan(self, jobs: Sequence[RenderJob]) -> List[RenderJob]:
        """The jobs, in order, that would be accepted if submitted now (each
        one that fits is counted against the rest). Reserves nothing."""
        left = self.remaining
        out = []
        for j in jobs:
            if j.cost <= left:
                out.append(j)
                left = left - j.cost
        return out

    def submit(self, job: RenderJob) -> Rational:
        """Reserve the job's whole cost; returns it. :class:`JobRefused` if the
        farm cannot cover it (nothing reserved). ValueError for a name in use."""
        if job.name in self._reserved:
            raise ValueError(f"job {job.name!r} is already reserved")
        cost = job.cost
        try:
            self._budget.consume(cost)
        except BudgetExhaustedError:
            raise JobRefused(job.name, cost, self.remaining) from None
        self._reserved[job.name] = job
        return cost

    def complete(self, name: str, actual: Any) -> Settlement:
        """Settle a job at its actual exact cost (>= 0).

        Under-run: the difference goes back to the farm. Over-run: the extra
        is charged; if the farm cannot cover it, :class:`JobRefused` is raised,
        nothing changes and the job stays reserved (settle it again, e.g.
        after raising the limit).
        """
        job = self._job(name)
        cost = exact(actual, "actual cost")
        if cost < 0:
            raise ValueError("an actual cost is >= 0")
        reserved = job.cost
        zero = Rational(0)
        if cost < reserved:
            back = reserved - cost
            self._budget.release(back)
            del self._reserved[name]
            return Settlement(name, reserved, cost, back, zero)
        extra = cost - reserved
        if extra > 0:
            try:
                self._budget.consume(extra)
            except BudgetExhaustedError:
                raise JobRefused(name, extra, self.remaining) from None
        del self._reserved[name]
        return Settlement(name, reserved, cost, zero, extra)

    def cancel(self, name: str, frames_done: int = 0) -> Settlement:
        """Cancel a job after ``frames_done`` frames; the rest is given back."""
        job = self._job(name)
        n = len(job.frames)
        if isinstance(frames_done, bool) or not isinstance(frames_done, int) or not 0 <= frames_done <= n:
            raise ValueError(f"frames_done must be 0..{n}")
        return self.complete(name, job.cost_per_frame * frames_done)

    def raise_limit(self, limit: Any) -> None:
        """Replace the farm's limit (e.g. a new allocation for the week)."""
        lim = exact(limit, "limit")
        if lim < 0:
            raise ValueError("the limit must be >= 0")
        self._budget.set_limit(lim)

    def _job(self, name: str) -> RenderJob:
        try:
            return self._reserved[name]
        except KeyError:
            raise KeyError(f"no reserved job {name!r}") from None
