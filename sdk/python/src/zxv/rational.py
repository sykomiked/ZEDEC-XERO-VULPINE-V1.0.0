# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact rationals: the kernel's ``rat_t`` (the orbital_compat numeric form)."""

from __future__ import annotations

import struct
from decimal import Decimal
from fractions import Fraction
from math import gcd
from typing import Any, Optional, Union

from . import _native
from ._native import ffi
from .errors import FloatRejectedError, ZxvUsageError

__all__ = ["Rational", "RationalLike"]

_I64_MAX = 2**63 - 1
_I64_MIN_OK = -(2**63) + 1  # INT64_MIN is refused: its negation is not an int64

RationalLike = Union["Rational", int, Fraction, Decimal]
"""Anything accepted where an exact value is expected (never ``float``)."""


def _range_error(what: str) -> ZxvUsageError:
    return ZxvUsageError(-8, "ZXV_E_RANGE", f"{what} does not fit an int64 rational")


def _check_i64(v: int, what: str) -> int:
    if not _I64_MIN_OK <= v <= _I64_MAX:
        raise _range_error(what)
    return v


class Rational:
    """An immutable, exact rational number ``numerator / denominator``.

    Both parts are int64 (the C ABI's representation); the value is always
    reduced with a positive denominator. Arithmetic runs in ``libzxv`` so it
    is bit-for-bit the kernel's: overflow raises :class:`ZxvUsageError`
    (``ZXV_E_RANGE``) instead of wrapping or rounding.

    Rationals interoperate exactly with :class:`int`,
    :class:`fractions.Fraction` and :class:`decimal.Decimal` (equality,
    ordering, hashing, arithmetic). A Python ``float`` is refused everywhere
    with :class:`FloatRejectedError`; convert one on purpose with
    :meth:`from_float_exact`.

    >>> Rational.parse("0.1") + Rational.parse("0.2") == Rational(3, 10)
    True
    """

    __slots__ = ("_n", "_d", "_hash")
    _n: int
    _d: int
    _hash: Optional[int]

    def __init__(self, numerator: int, denominator: int = 1) -> None:
        """Create ``numerator/denominator`` from two ints (reduced automatically).

        Raises:
            FloatRejectedError: either part is a float.
            TypeError: either part is not an int.
            ZxvUsageError: zero denominator (``ZXV_E_ARG``) or a part outside
                the int64 range (``ZXV_E_RANGE``).
        """
        for name, v in (("numerator", numerator), ("denominator", denominator)):
            if isinstance(v, float):
                raise FloatRejectedError(f"Rational {name}")
            if isinstance(v, bool) or not isinstance(v, int):
                raise TypeError(f"Rational {name} must be an int, not {type(v).__name__}")
        _check_i64(numerator, "numerator")
        _check_i64(denominator, "denominator")
        if denominator == 0:
            raise ZxvUsageError(-1, "ZXV_E_ARG", "zero denominator")
        g = gcd(numerator, denominator) or 1
        n, d = numerator // g, denominator // g
        if d < 0:
            n, d = -n, -d
        object.__setattr__(self, "_n", int(n))
        object.__setattr__(self, "_d", int(d))
        object.__setattr__(self, "_hash", None)

    # -------------------------------------------------------- constructors

    @classmethod
    def of(cls, numerator: int, denominator: int = 1) -> "Rational":
        """Same as ``Rational(numerator, denominator)``."""
        return cls(numerator, denominator)

    @classmethod
    def parse(cls, text: str) -> "Rational":
        """Parse exact text: ``"12.75"``, ``"-0.1"``, ``"3/4"`` or ``"42"``.

        Exponents, spaces, ``"NaN"`` and ``".5"`` are refused
        (``ZXV_E_FORMAT``); values that do not fit raise ``ZXV_E_RANGE``.
        """
        b = _native.to_cstr(text, "rational text")
        out = ffi.new("int64_t[2]")
        _native.check(_native.lib().zxv_rat_parse(b, out, out + 1))
        return cls._raw(out[0], out[1])

    @classmethod
    def from_float_exact(cls, x: float) -> "Rational":
        """The exact value of a binary64 float: the ONLY float entry point.

        Every finite double is exactly ``m * 2**e``; this returns that value,
        so ``0.1`` becomes ``3602879701896397/36028797018963968``, not
        ``1/10``. The drift is made visible, never hidden.

        Raises:
            FloatRejectedError: NaN or infinity (``ZXV_E_FLOAT``).
            ZxvUsageError: the exact form needs more than 63 bits (``ZXV_E_RANGE``).
        """
        if not isinstance(x, (float, int)) or isinstance(x, bool):
            raise TypeError("from_float_exact takes a float")
        (bits,) = struct.unpack("<Q", struct.pack("<d", float(x)))
        return cls.from_ieee754_bits(bits)

    @classmethod
    def from_ieee754_bits(cls, bits: int) -> "Rational":
        """The exact value of raw IEEE-754 binary64 bits (a uint64)."""
        bits = _native.exact_int(bits, "bits", *_native.U64)
        out = ffi.new("int64_t[2]")
        _native.check(_native.lib().zxv_rat_from_ieee754(bits, out, out + 1))
        return cls._raw(out[0], out[1])

    @classmethod
    def from_fraction(cls, f: Fraction) -> "Rational":
        """Exact conversion from :class:`fractions.Fraction` (range-checked)."""
        return cls(f.numerator, f.denominator) if _fits(f) else _raise_range("Fraction")

    @classmethod
    def from_decimal(cls, d: Decimal) -> "Rational":
        """Exact conversion from a finite :class:`decimal.Decimal`."""
        if not d.is_finite():
            raise FloatRejectedError(detail="NaN or infinite Decimal refused (ZXV_E_FLOAT)")
        return cls.from_fraction(Fraction(d))

    @classmethod
    def coerce(cls, value: Any, what: str = "value") -> "Rational":
        """Convert an exact value (Rational, int, Fraction, Decimal, str) to Rational.

        Raises:
            FloatRejectedError: ``value`` is a float.
            TypeError: any other type.
        """
        if isinstance(value, Rational):
            return value
        if isinstance(value, float):
            raise FloatRejectedError(what)
        if isinstance(value, bool):
            raise TypeError(f"{what} must be exact (int, Fraction, Decimal, Rational), not bool")
        if isinstance(value, int):
            return cls(value)
        if isinstance(value, Fraction):
            return cls.from_fraction(value)
        if isinstance(value, Decimal):
            return cls.from_decimal(value)
        if isinstance(value, str):
            return cls.parse(value)
        raise TypeError(f"{what} must be exact (int, Fraction, Decimal, Rational), "
                        f"not {type(value).__name__}")

    @classmethod
    def _raw(cls, n: int, d: int) -> "Rational":
        r = object.__new__(cls)
        object.__setattr__(r, "_n", int(n))
        object.__setattr__(r, "_d", int(d))
        object.__setattr__(r, "_hash", None)
        return r

    # -------------------------------------------------------- accessors

    @property
    def numerator(self) -> int:
        """The reduced numerator (carries the sign)."""
        return self._n

    @property
    def denominator(self) -> int:
        """The reduced denominator (always > 0)."""
        return self._d

    def to_fraction(self) -> Fraction:
        """The same value as a :class:`fractions.Fraction`."""
        return Fraction(self._n, self._d)

    def to_decimal(self) -> Decimal:
        """The same value as a :class:`decimal.Decimal`, when it terminates.

        Raises:
            ValueError: the value has no finite decimal form (e.g. 1/3).
        """
        text = str(self)
        if "/" in text:
            raise ValueError(f"{text} has no exact decimal form")
        return Decimal(text)

    def to_q16(self) -> int:
        """Round to Q16.16 fixed point (half away from zero), as the engine takes."""
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_rat_to_q16(self._n, self._d, out))
        return int(out[0])

    def is_integer(self) -> bool:
        """True if the denominator is 1."""
        return self._d == 1

    def __str__(self) -> str:
        """Exact text: a terminating decimal (``"12.75"``) or ``"n/d"`` (``"1/3"``)."""
        L = _native.lib()
        return _native.read_text(lambda b, c, n: L.zxv_rat_format(self._n, self._d, b, c, n))

    def __repr__(self) -> str:
        return f"Rational({self._n}, {self._d})"

    def __float__(self) -> float:
        raise TypeError("Rational never converts to float implicitly; use to_fraction() "
                        "or str() and convert on purpose")

    def __bool__(self) -> bool:
        return self._n != 0

    def __hash__(self) -> int:
        h = self._hash
        if h is None:
            h = hash(Fraction(self._n, self._d))  # equal to int/Fraction/Decimal hashes
            object.__setattr__(self, "_hash", h)
        return h

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Rational is immutable")

    def __reduce__(self) -> Any:
        return (Rational, (self._n, self._d))

    # -------------------------------------------------------- arithmetic

    def _binop(self, other: Any, fn: str, swap: bool = False) -> "Rational":
        o = _other(other)
        if o is NotImplemented:
            return NotImplemented  # type: ignore[no-any-return]
        a, b = (o, self) if swap else (self, o)
        out = ffi.new("int64_t[2]")
        _native.check(getattr(_native.lib(), fn)(a._n, a._d, b._n, b._d, out, out + 1))
        return Rational._raw(out[0], out[1])

    def __add__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_add")

    def __radd__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_add", True)

    def __sub__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_sub")

    def __rsub__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_sub", True)

    def __mul__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_mul")

    def __rmul__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_mul", True)

    def __truediv__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_div")

    def __rtruediv__(self, o: Any) -> "Rational":
        return self._binop(o, "zxv_rat_div", True)

    def __neg__(self) -> "Rational":
        return Rational._raw(-self._n, self._d)

    def __pos__(self) -> "Rational":
        return self

    def __abs__(self) -> "Rational":
        return Rational._raw(abs(self._n), self._d)

    def compare(self, other: RationalLike) -> int:
        """-1, 0 or +1, computed exactly by ``zxv_rat_cmp``."""
        o = Rational.coerce(other, "comparison operand")
        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_rat_cmp(self._n, self._d, o._n, o._d, out))
        return int(out[0])

    # Comparisons are exact in pure Python (cross-multiplication on unbounded
    # ints); they agree with zxv_rat_cmp, which tests check.
    def _cmp_key(self, other: Any) -> Any:
        if isinstance(other, float):
            raise FloatRejectedError("comparison operand")
        if isinstance(other, Rational):
            return Fraction(other._n, other._d)
        if isinstance(other, bool):
            return NotImplemented
        if isinstance(other, (int, Fraction)):
            return Fraction(other)
        if isinstance(other, Decimal):
            if not other.is_finite():
                return NotImplemented
            return Fraction(other)
        return NotImplemented

    def __eq__(self, other: object) -> bool:
        k = self._cmp_key(other)
        if k is NotImplemented:
            return NotImplemented
        return bool(Fraction(self._n, self._d) == k)

    def __lt__(self, other: Any) -> bool:
        k = self._cmp_key(other)
        return NotImplemented if k is NotImplemented else Fraction(self._n, self._d) < k

    def __le__(self, other: Any) -> bool:
        k = self._cmp_key(other)
        return NotImplemented if k is NotImplemented else Fraction(self._n, self._d) <= k

    def __gt__(self, other: Any) -> bool:
        k = self._cmp_key(other)
        return NotImplemented if k is NotImplemented else Fraction(self._n, self._d) > k

    def __ge__(self, other: Any) -> bool:
        k = self._cmp_key(other)
        return NotImplemented if k is NotImplemented else Fraction(self._n, self._d) >= k


def _fits(f: Fraction) -> bool:
    return _I64_MIN_OK <= f.numerator <= _I64_MAX and f.denominator <= _I64_MAX


def _raise_range(what: str) -> Rational:
    raise _range_error(what)


def _other(o: Any) -> Any:
    if isinstance(o, Rational):
        return o
    if isinstance(o, float):
        raise FloatRejectedError("arithmetic operand")
    if isinstance(o, bool):
        return NotImplemented
    if isinstance(o, (int, Fraction, Decimal)):
        return Rational.coerce(o)
    return NotImplemented
