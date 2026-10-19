# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact rationals against vectors.json, and the float boundary."""

from __future__ import annotations

import pickle
import struct
from decimal import Decimal
from fractions import Fraction
from typing import Any, Dict

import pytest

import zxv
from zxv import Rational


def R(p: Any) -> Rational:
    return Rational(p[0], p[1])


def test_operations_match_vectors(vectors: Dict[str, Any]) -> None:
    for v in vectors["rational_ops"]:
        a, b = R(v["a"]), R(v["b"])
        assert a + b == R(v["add"]) and (a + b).numerator == v["add"][0] and (a + b).denominator == v["add"][1]
        assert a - b == R(v["sub"])
        assert a * b == R(v["mul"])
        assert a / b == R(v["div"])
        assert a.compare(b) == v["cmp"]
        assert (a > b) == (v["cmp"] > 0) and (a < b) == (v["cmp"] < 0)


def test_parse_and_format_match_vectors(vectors: Dict[str, Any]) -> None:
    for v in vectors["rational_parse"]:
        if v["status"] == 0:
            r = Rational.parse(v["text"])
            assert (r.numerator, r.denominator) == tuple(v["value"])
            assert str(r) == v["format"]
        else:
            with pytest.raises(zxv.ZxvError) as e:
                Rational.parse(v["text"])
            assert e.value.status == v["status"]


def test_ieee754_boundary_matches_vectors(vectors: Dict[str, Any]) -> None:
    for v in vectors["ieee754"]:
        bits = int(v["bits"], 16)
        (x,) = struct.unpack("<d", bits.to_bytes(8, "little"))
        if v["status"] == 0:
            assert (lambda r: (r.numerator, r.denominator))(Rational.from_ieee754_bits(bits)) == tuple(v["value"])
            r = Rational.from_float_exact(x)
            assert (r.numerator, r.denominator) == tuple(v["value"])
        elif v["status"] == -24:
            with pytest.raises(zxv.FloatRejectedError):
                Rational.from_float_exact(x)
        else:
            with pytest.raises(zxv.ZxvUsageError) as e:
                Rational.from_ieee754_bits(bits)
            assert e.value.status == v["status"]


def test_q16_matches_vectors(vectors: Dict[str, Any]) -> None:
    for v in vectors["q16"]:
        if v["status"] == 0:
            assert R(v["value"]).to_q16() == v["q16"]
        else:
            with pytest.raises(zxv.ZxvUsageError):
                R(v["value"]).to_q16()


def test_tenth_is_not_one_tenth() -> None:
    assert Rational.from_float_exact(0.1) != Rational.parse("0.1")
    assert Rational.parse("0.1") + Rational.parse("0.2") == Rational.parse("0.3")


@pytest.mark.parametrize("bad", [
    lambda: Rational(0.5),
    lambda: Rational(1, 2.0),
    lambda: Rational(1) + 0.5,
    lambda: 0.5 + Rational(1),
    lambda: Rational(1) * 2.0,
    lambda: Rational(1) == 1.0,
    lambda: Rational(1) < 2.0,
    lambda: Rational.coerce(0.25),
    lambda: zxv.GenerateOptions(temperature=0.7),
])
def test_floats_are_rejected_everywhere(bad: Any) -> None:
    with pytest.raises(zxv.FloatRejectedError) as e:
        bad()
    assert "from_float_exact" in str(e.value)
    assert isinstance(e.value, TypeError)


def test_float_conversion_out_is_refused() -> None:
    with pytest.raises(TypeError):
        float(Rational(1, 2))


def test_interop_with_fraction_and_decimal() -> None:
    half = Rational(1, 2)
    assert half == Fraction(1, 2) and Fraction(1, 2) == half
    assert half == Decimal("0.5") and Decimal("0.5") == half
    assert hash(half) == hash(Fraction(1, 2)) == hash(Decimal("0.5"))
    assert hash(Rational(3)) == hash(3) and Rational(3) == 3
    assert {half: "x"}[Fraction(1, 2)] == "x"
    assert half + Fraction(1, 3) == Rational(5, 6)
    assert Fraction(1, 3) + half == Rational(5, 6)
    assert isinstance(Fraction(1, 3) + half, Rational)
    assert half + Decimal("0.25") == Rational(3, 4)
    assert Rational.from_decimal(Decimal("12.75")) == Rational(51, 4)
    assert Rational(51, 4).to_decimal() == Decimal("12.75")
    assert Rational(1, 3).to_fraction() == Fraction(1, 3)
    with pytest.raises(ValueError):
        Rational(1, 3).to_decimal()
    assert 1 - half == half and 2 / Rational(4) == half
    assert Fraction(1, 3) < half < Decimal("0.6")
    with pytest.raises(zxv.FloatRejectedError):
        Rational.from_decimal(Decimal("NaN"))


def test_immutable_hashable_picklable() -> None:
    r = Rational(6, -4)
    assert (r.numerator, r.denominator) == (-3, 2)
    with pytest.raises(AttributeError):
        r.x = 1  # type: ignore[attr-defined]
    assert pickle.loads(pickle.dumps(r)) == r
    assert len({Rational(1, 2), Rational(2, 4), Fraction(1, 2)}) == 1
    assert repr(r) == "Rational(-3, 2)" and str(r) == "-1.5"


def test_overflow_is_detected_never_wrapped() -> None:
    big = Rational(2**63 - 1)
    with pytest.raises(zxv.ZxvUsageError) as e:
        big * 2
    assert e.value.status_name == "ZXV_E_RANGE"
    with pytest.raises(zxv.ZxvUsageError):
        Rational(-(2**63))  # INT64_MIN refused
    with pytest.raises(zxv.ZxvUsageError) as e:
        Rational(1, 2) / 0
    assert e.value.status_name == "ZXV_E_ARG"
    with pytest.raises(zxv.ZxvUsageError):
        Rational(1, 0)
    with pytest.raises(TypeError):
        Rational(True)
