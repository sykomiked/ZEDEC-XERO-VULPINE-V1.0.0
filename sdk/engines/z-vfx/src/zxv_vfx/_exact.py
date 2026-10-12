# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact helpers shared by the core modules.

``rdiv`` is the only place in the core where the ``/`` operator appears,
and it divides two :class:`zxv.Rational` values in libzxv. Python's ``/`` on
two ints would return a float; the no-float test (``tests/test_nofloat_ast.py``)
refuses ``/`` anywhere else in the core so that cannot happen by accident.
"""

from __future__ import annotations

from typing import Any

from zxv import FloatRejectedError, Rational

__all__ = ["exact", "rdiv", "floor_div", "rounded", "ROUNDINGS"]

ROUNDINGS = ("floor", "ceil", "half_up", "half_even")
"""How a rational becomes a whole frame: toward -inf, toward +inf, nearest with
ties away from zero, or nearest with ties to even."""


def exact(value: Any, what: str = "value") -> Rational:
    """An exact value as a :class:`zxv.Rational`; a float is refused.

    Accepts a Rational, int, :class:`fractions.Fraction`,
    :class:`decimal.Decimal` or exact text (``"12.75"``, ``"3/4"``).
    """
    if isinstance(value, float):
        raise FloatRejectedError(what)
    return Rational.coerce(value, what)


def rdiv(a: Any, b: Any) -> Rational:
    """``a / b`` exactly, in libzxv (``ZXV_E_ARG`` on a zero divisor)."""
    return exact(a, "dividend") / exact(b, "divisor")


def floor_div(r: Rational) -> int:
    """The largest int <= r."""
    return r.numerator // r.denominator


def rounded(r: Rational, rounding: str = "floor") -> int:
    """``r`` as an int under one of :data:`ROUNDINGS`."""
    n, d = r.numerator, r.denominator
    q, rem = divmod(n, d)  # 0 <= rem < d
    if rounding == "floor":
        return q
    if rounding == "ceil":
        return q + (1 if rem else 0)
    if rounding not in ("half_up", "half_even"):
        raise ValueError(f"rounding must be one of {ROUNDINGS}")
    twice = 2 * rem
    if twice > d:
        return q + 1
    if twice < d:
        return q
    # an exact tie: q + 1/2
    if rounding == "half_even":
        return q if q % 2 == 0 else q + 1
    return q + 1 if n >= 0 else q  # half_up: away from zero
