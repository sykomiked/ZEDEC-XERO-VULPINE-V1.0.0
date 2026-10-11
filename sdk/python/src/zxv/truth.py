# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Four-valued (paraconsistent) logic: the kernel's ``lpres`` states."""

from __future__ import annotations

import enum
from typing import Optional, TypeVar

__all__ = ["Truth", "UncertainTruthError"]

T = TypeVar("T")

# The C tables (vectors.json key "lpres"), indexed [a][b] by kernel value.
_NOT = (0, 2, 1, 3)
_AND = ((0, 0, 2, 0), (0, 1, 2, 3), (2, 2, 2, 2), (0, 3, 2, 3))
_OR = ((0, 1, 0, 3), (1, 1, 1, 1), (0, 1, 2, 3), (3, 1, 3, 3))


class UncertainTruthError(ValueError):
    """Raised by :meth:`Truth.require_certain` and friends for BOTH or NEITHER."""


class Truth(enum.Enum):
    """A four-valued truth: ``NEITHER``, ``TRUE``, ``FALSE`` or ``BOTH``.

    Values are numbered as the kernel numbers them (``int(truth.value)``).
    ``BOTH`` is a *held contradiction* (evidence for and against), not an
    error and not ``None``; ``NEITHER`` means unknown / unattested.

    A ``Truth`` never turns into a Python ``bool`` silently: ``if truth:``
    raises :class:`TypeError`. Convert with an explicit policy:
    :meth:`is_certainly_true`, :meth:`to_bool` or :meth:`require_true`.

    The operators ``&``, ``|`` and ``~`` follow the C tables (Belnap's FOUR);
    the module-level tables are cross-checked against ``zxv_lp_*`` in tests.
    """

    NEITHER = 0
    """S0: unknown, unattested, pending."""
    TRUE = 1
    """S+: attested present and valid."""
    FALSE = 2
    """S-: attested absent or invalid."""
    BOTH = 3
    """Contradictory evidence, held."""

    def __bool__(self) -> bool:
        raise TypeError(
            f"Truth.{self.name} has no implicit bool value; use is_certainly_true(), "
            "to_bool(on_both=..., on_neither=...) or require_true()"
        )

    # ------------------------------------------------------------ logic

    def __invert__(self) -> "Truth":
        return Truth(_NOT[self.value])

    def __and__(self, other: "Truth") -> "Truth":
        if not isinstance(other, Truth):
            return NotImplemented
        return Truth(_AND[self.value][other.value])

    def __or__(self, other: "Truth") -> "Truth":
        if not isinstance(other, Truth):
            return NotImplemented
        return Truth(_OR[self.value][other.value])

    def not_(self) -> "Truth":
        """Four-valued negation (TRUE <-> FALSE; BOTH and NEITHER fixed)."""
        return ~self

    def and_(self, other: "Truth") -> "Truth":
        """Four-valued conjunction (same as ``self & other``)."""
        return self & other

    def or_(self, other: "Truth") -> "Truth":
        """Four-valued disjunction (same as ``self | other``)."""
        return self | other

    @classmethod
    def from_evidence(cls, positive: int, negative: int) -> "Truth":
        """BOTH if both counts > 0, TRUE / FALSE if only one is, else NEITHER."""
        if positive > 0 and negative > 0:
            return cls.BOTH
        if positive > 0:
            return cls.TRUE
        if negative > 0:
            return cls.FALSE
        return cls.NEITHER

    @classmethod
    def of(cls, value: bool) -> "Truth":
        """Lift a Python bool: ``True`` -> TRUE, ``False`` -> FALSE."""
        if not isinstance(value, bool):
            raise TypeError("Truth.of takes a bool")
        return cls.TRUE if value else cls.FALSE

    # ------------------------------------------------------------ explicit conversion

    @property
    def is_certain(self) -> bool:
        """True for TRUE and FALSE (no contradiction, nothing unknown)."""
        return self in (Truth.TRUE, Truth.FALSE)

    @property
    def is_contradiction(self) -> bool:
        """True for BOTH."""
        return self is Truth.BOTH

    @property
    def is_unknown(self) -> bool:
        """True for NEITHER."""
        return self is Truth.NEITHER

    def is_certainly_true(self) -> bool:
        """True only for TRUE (BOTH and NEITHER are not certainly true)."""
        return self is Truth.TRUE

    def is_certainly_false(self) -> bool:
        """True only for FALSE."""
        return self is Truth.FALSE

    def to_bool(self, *, on_both: bool, on_neither: bool) -> bool:
        """Collapse to bool with an explicit policy for BOTH and NEITHER."""
        if self is Truth.TRUE:
            return True
        if self is Truth.FALSE:
            return False
        return on_both if self is Truth.BOTH else on_neither

    def require_certain(self, what: Optional[str] = None) -> bool:
        """Return the bool for TRUE/FALSE; raise :class:`UncertainTruthError` otherwise."""
        if self.is_certain:
            return self is Truth.TRUE
        raise UncertainTruthError(f"{what or 'truth'} is {self.name}, not certain")

    def require_true(self, what: Optional[str] = None) -> None:
        """Raise :class:`UncertainTruthError` unless this is TRUE."""
        if self is not Truth.TRUE:
            raise UncertainTruthError(f"{what or 'truth'} is {self.name}, not TRUE")

    @property
    def mode(self) -> int:
        """Reserved: the kernel's orthogonal 2-bit mode. Always 0 in ABI 1.1."""
        return 0
