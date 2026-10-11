# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The accounting equation, Assets = Liabilities + Equity, computed exactly.

Each stored amount is an int64 :class:`zxv.Rational`; totals are summed as
Python ``Fraction`` (exact and unbounded), so no total can overflow or
round. The result is advisory: it reads, reports, and never blocks or writes.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from fractions import Fraction
from typing import Any, Iterable, Optional

from zxv import Rational, Truth

ASSET = "asset"
LIABILITY = "liability"
EQUITY = "equity"
KINDS = (ASSET, LIABILITY, EQUITY)


def exact_text(f: Fraction) -> str:
    """Exact text of a total: the SDK's form (``"12.75"``, ``"1/3"``) when it fits int64, else ``"n/d"``."""
    try:
        return str(Rational.from_fraction(f))
    except Exception:  # noqa: BLE001 - a total beyond int64 is still exact as n/d
        return f"{f.numerator}/{f.denominator}" if f.denominator != 1 else str(f.numerator)


@dataclass(frozen=True)
class EquationResult:
    """Totals of one currency."""

    currency: str
    assets: Fraction
    liabilities: Fraction
    equity: Fraction
    lines: int
    unclassified: int
    """Lines whose kind is not asset, liability or equity (they make the verdict NEITHER)."""

    @property
    def difference(self) -> Fraction:
        """``assets - (liabilities + equity)``; zero when the equation holds."""
        return self.assets - (self.liabilities + self.equity)

    @property
    def holds(self) -> Truth:
        """TRUE when A = L + E exactly; FALSE when not; NEITHER when unclassified lines leave it unknown."""
        if self.unclassified:
            return Truth.NEITHER
        return Truth.TRUE if self.difference == 0 else Truth.FALSE

    def as_text(self) -> dict[str, str]:
        """Every total as exact text, for templates and JSON."""
        return {
            "currency": self.currency,
            "assets": exact_text(self.assets),
            "liabilities": exact_text(self.liabilities),
            "equity": exact_text(self.equity),
            "liabilities_plus_equity": exact_text(self.liabilities + self.equity),
            "difference": exact_text(self.difference),
            "holds": self.holds.name,
        }


def _fraction(amount: Any) -> Fraction:
    if isinstance(amount, float):
        raise TypeError("a float amount is not exact; store amounts in a RationalField")
    return Rational.coerce(amount, "amount").to_fraction()


def accounting_equation(lines: Iterable[tuple[Any, Any, Optional[str]]]) -> list[EquationResult]:
    """Totals per currency from ``(kind, amount, currency)`` rows, sorted by currency.

    ``kind`` is ``"asset"``, ``"liability"`` or ``"equity"`` (case-insensitive);
    ``amount`` an exact value (Rational, int, Fraction, Decimal, exact text);
    ``currency`` any label, or ``None`` for one unnamed group.
    """
    totals: dict[str, dict[str, Fraction]] = defaultdict(lambda: dict.fromkeys(KINDS, Fraction(0)))
    counts: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for kind, amount, currency in lines:
        key = currency if currency is not None else ""
        counts[key][0] += 1
        k = kind.lower() if isinstance(kind, str) else None
        if k not in KINDS:
            counts[key][1] += 1
            totals[key]  # noqa: B018 - make sure the currency appears
            continue
        totals[key][k] += _fraction(amount)
    return [
        EquationResult(c, t[ASSET], t[LIABILITY], t[EQUITY], counts[c][0], counts[c][1])
        for c, t in sorted(totals.items())
    ]


def overall(results: Iterable[EquationResult]) -> Truth:
    """TRUE only if every currency holds; FALSE if any fails; NEITHER for no data or any unknown."""
    rs = list(results)
    if not rs:
        return Truth.NEITHER
    verdicts = [r.holds for r in rs]
    if Truth.FALSE in verdicts:
        return Truth.FALSE
    if Truth.NEITHER in verdicts:
        return Truth.NEITHER
    return Truth.TRUE
