# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The swarm token economy (kernel ``swarm``: Fibonacci budget + emotion).

Each cycle, ``tokens_per_cycle`` tokens are split across the swarm's
Fibonacci levels (the real share) and, by the mood's charge, an imaginary
(emotional) share is split by each model's own feeling. Level ``d`` holds
at most F(d+2) models. Feelings are emoji code points from the kernel
palette with intensity 0..5 (charge 0, 1, 2, 3, 5, 8). All exact integers.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .context import Capability
from .rational import Rational

__all__ = ["Swarm", "Allotment", "EmotionProfile", "Grant"]

Emoji = Union[int, str]
"""An emoji: its code point (``0x1F914``) or a one-character string (``"🤔"``)."""


def _codepoint(e: Emoji) -> int:
    if isinstance(e, str):
        if len(e) != 1:
            raise ValueError("an emoji string must be exactly one code point")
        return ord(e)
    return _native.exact_int(e, "codepoint", *_native.U32)


@dataclass(frozen=True)
class Allotment:
    """A model's tokens for the current cycle."""

    allotted: int
    """Real + imaginary tokens."""
    imaginary: int
    """The emotional (imaginary) part of ``allotted``."""
    used: int

    @property
    def remaining(self) -> int:
        """allotted - used."""
        return self.allotted - self.used


@dataclass(frozen=True)
class Grant:
    """Result of :meth:`Swarm.consume`: what was granted of a request."""

    requested: int
    granted: int

    @property
    def complete(self) -> bool:
        """True when everything requested was granted."""
        return self.granted == self.requested


@dataclass(frozen=True)
class EmotionProfile:
    """The behaviour profile of an emotion in the kernel palette."""

    valence: int
    temperature: Rational
    """Sampling temperature (exact: milli-units / 1000)."""
    explore: Rational
    """Exploration share (exact: permille / 1000)."""
    verify_passes: int
    priority: int


class Swarm(NativeHandle):
    """A swarm of models sharing a per-cycle token budget. Calls need ``Cap.SWARM``.

    >>> with Swarm(cap, levels=3, tokens_per_cycle=1000) as sw:   # doctest: +SKIP
    ...     sw.register(1, level=0)
    ...     sw.begin_cycle()
    ...     sw.allotment(1).allotted
    """

    def __init__(self, cap: Capability, levels: int, tokens_per_cycle: int) -> None:
        """Create a swarm with ``levels`` Fibonacci levels."""
        ctx = cap.context
        out = ffi.new("zxv_swarm **")
        _native.check(_native.lib().zxv_swarm_create_cap(
            cap._handle(), _native.exact_int(levels, "levels", *_native.U32),
            _native.exact_int(tokens_per_cycle, "tokens_per_cycle", *_native.U64), out), ctx)
        self._cap = cap
        super().__init__(out[0], cap, ctx)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_swarm_destroy(handle)

    def _c(self) -> Any:
        return self._cap._handle()

    def register(self, model_id: int, level: int) -> None:
        """Register a model at a level (``ZXV_E_CAPACITY`` when the level is full)."""
        self._check(_native.lib().zxv_swarm_register(
            self._c(), self._handle(), _native.exact_int(model_id, "model_id", *_native.U32),
            _native.exact_int(level, "level", *_native.U32)))

    def set_active(self, model_id: int, active: bool) -> None:
        """Mark a model active or idle."""
        self._check(_native.lib().zxv_swarm_set_active(
            self._c(), self._handle(), _native.exact_int(model_id, "model_id", *_native.U32),
            1 if active else 0))

    def set_rate(self, tokens_per_cycle: int) -> None:
        """Change the tokens per cycle."""
        self._check(_native.lib().zxv_swarm_set_rate(
            self._c(), self._handle(), _native.exact_int(tokens_per_cycle, "tokens_per_cycle", *_native.U64)))

    def set_mood(self, emoji: Emoji, intensity: int) -> None:
        """Set the swarm's mood (an emoji of the palette, intensity 0..5)."""
        self._check(_native.lib().zxv_swarm_set_mood(
            self._c(), self._handle(), _codepoint(emoji), _native.exact_int(intensity, "intensity", 0, 5)))

    def set_feeling(self, model_id: int, emoji: Emoji, intensity: int) -> None:
        """Set one model's feeling (an emoji of the palette, intensity 0..5)."""
        self._check(_native.lib().zxv_swarm_set_feeling(
            self._c(), self._handle(), _native.exact_int(model_id, "model_id", *_native.U32),
            _codepoint(emoji), _native.exact_int(intensity, "intensity", 0, 5)))

    def begin_cycle(self) -> int:
        """Start a cycle (allot tokens); returns the cycle number."""
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_swarm_begin_cycle(self._c(), self._handle(), out))
        return int(out[0])

    def consume(self, model_id: int, requested: int) -> Grant:
        """Take up to ``requested`` tokens from a model's allotment.

        Over-asking is not an error here: the result says how much was
        granted (what was left). Use :meth:`consume_exact` to raise instead.
        """
        req = _native.exact_int(requested, "requested", *_native.U64)
        out = ffi.new("uint64_t *")
        rc = _native.lib().zxv_swarm_consume(self._c(), self._handle(),
                                             _native.exact_int(model_id, "model_id", *_native.U32), req, out)
        if rc == -19:  # ZXV_E_BUDGET: out_granted is written on this status too
            return Grant(req, int(out[0]))
        self._check(rc)
        return Grant(req, int(out[0]))

    def consume_exact(self, model_id: int, requested: int) -> None:
        """Take exactly ``requested`` tokens or raise :class:`BudgetExhaustedError`.

        Note: as in C, a partial grant is still taken from the allotment.
        """
        g = self.consume(model_id, requested)
        if not g.complete:
            from .errors import BudgetExhaustedError

            raise BudgetExhaustedError(-19, "ZXV_E_BUDGET",
                                       f"model {model_id}: granted {g.granted} of {g.requested}")

    def allotment(self, model_id: int) -> Allotment:
        """A model's allotment for the current cycle."""
        v = ffi.new("uint64_t[3]")
        self._check(_native.lib().zxv_swarm_allotment(
            self._handle(), _native.exact_int(model_id, "model_id", *_native.U32), v, v + 1, v + 2))
        return Allotment(int(v[0]), int(v[1]), int(v[2]))

    def end_cycle(self) -> int:
        """Close the cycle; returns the unused tokens (they never carry over)."""
        out = ffi.new("uint64_t *")
        self._check(_native.lib().zxv_swarm_end_cycle(self._c(), self._handle(), out))
        return int(out[0])

    @staticmethod
    def level_capacity(level: int) -> int:
        """How many models level ``level`` holds: F(level + 2)."""
        out = ffi.new("uint32_t *")
        _native.check(_native.lib().zxv_swarm_level_capacity(_native.exact_int(level, "level", *_native.U32), out))
        return int(out[0])

    @staticmethod
    def emotion_profile(emoji: Emoji) -> EmotionProfile:
        """The behaviour profile of an emoji in the kernel palette."""
        val = ffi.new("int32_t *")
        u = ffi.new("uint32_t[4]")
        _native.check(_native.lib().zxv_swarm_emotion_profile(_codepoint(emoji), val, u, u + 1, u + 2, u + 3))
        return EmotionProfile(int(val[0]), Rational(int(u[0]), 1000), Rational(int(u[1]), 1000),
                              int(u[2]), int(u[3]))
