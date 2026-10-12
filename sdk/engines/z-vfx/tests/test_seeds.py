# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Deterministic seeds: libzxv's SHA3-256, checked against hashlib and
pinned vectors, so a re-run on another machine gets the same parameters."""

from __future__ import annotations

import hashlib
import subprocess
import sys
from collections import Counter
from decimal import Decimal
from fractions import Fraction

import pytest
import zxv
from zxv import Rational

from zxv_vfx import DOMAIN, FILM, FILM_NTSC, FrameTime, SeedScheme, SeedStream, encode_part

S = SeedScheme("showA/sq010/sh0040")


def _blob(tag: bytes, data: bytes) -> bytes:
    return tag + len(data).to_bytes(4, "big") + data


def test_seed_matches_hashlib() -> None:
    # the documented encoding, recomputed with Python's own SHA3-256
    want = hashlib.sha3_256(
        _blob(b"D", b"zxv-vfx/seed/v1") + _blob(b"n", b"showA/sq010/sh0040")
        + _blob(b"s", b"fx/debris") + _blob(b"i", b"1001") + _blob(b"r", b"1/3")
        + _blob(b"f", b"24000/1001|2003/2") + _blob(b"b", b"\x00\xff")).digest()
    got = S.seed("fx/debris", 1001, Rational(1, 3), FrameTime(Rational(2003, 2), FILM_NTSC), b"\x00\xff")
    assert got == want
    assert DOMAIN == b"zxv-vfx/seed/v1"


def test_pinned_vectors() -> None:
    # Pinned once; a change here means old renders would no longer re-run.
    assert S.seed().hex() == hashlib.sha3_256(
        _blob(b"D", DOMAIN) + _blob(b"n", b"showA/sq010/sh0040")).digest().hex()
    assert S.seed("fx/rbd/debris", 1001).hex() == \
        "e397682a9de27bdb797321201a33ead0cf9d06aa3c3837d752daaea5499bba7a"
    assert S.seed_int("fx/rbd/debris", 1001) == 1909175317
    st = S.stream("fx/rbd/debris")
    assert [st.below(1000) for _ in range(8)] == [326, 724, 631, 574, 572, 478, 743, 525]


def test_equal_values_same_part() -> None:
    assert encode_part(Rational(1, 2)) == encode_part(Fraction(1, 2)) == encode_part(Decimal("0.5"))
    assert encode_part(FILM) == encode_part(Rational(24))
    assert encode_part("1") != encode_part(1)  # text and ints are different parts
    assert S.seed("a", "b") != S.seed("ab") != S.seed("b", "a")
    assert S.seed(True) == S.seed(1)
    assert SeedScheme("x").seed("a") != SeedScheme("y").seed("a")


def test_refusals() -> None:
    with pytest.raises(zxv.FloatRejectedError):
        S.seed(0.5)
    with pytest.raises(TypeError):
        S.seed(None)
    with pytest.raises(TypeError):
        S.seed([1, 2])
    with pytest.raises(ValueError):
        SeedScheme("")
    with pytest.raises(ValueError):
        S.seed_int(bits=0)
    with pytest.raises(ValueError):
        SeedStream(b"short")
    with pytest.raises(AttributeError):
        S.namespace = "z"  # type: ignore[misc]


def test_seed_int_bits() -> None:
    for bits in (1, 8, 31, 32, 64, 256):
        v = S.seed_int("x", bits=bits)
        assert 0 <= v < 2 ** bits
    assert S.seed_int("x", bits=256) == int.from_bytes(S.seed("x"), "big")
    assert S.seed_int("x", bits=31) == int.from_bytes(S.seed("x"), "big") >> 225


def test_stream_blocks_match_hashlib() -> None:
    seed = S.seed("blocks")
    st = SeedStream(seed)
    want = b"".join(hashlib.sha3_256(seed + k.to_bytes(8, "big")).digest() for k in range(3))
    got = st.bits(768)
    assert got == int.from_bytes(want, "big")


def test_stream_reproducible_and_unbiased() -> None:
    a, b = S.stream("p", FrameTime(1001, FILM)), S.stream("p", FrameTime(1001, FILM))
    da = [a.below(6) for _ in range(6000)]
    db = [b.below(6) for _ in range(6000)]
    assert da == db
    c = Counter(da)
    assert set(c) == set(range(6))
    assert all(850 < c[k] < 1150 for k in range(6))  # loose: 6000 rolls of a fair die
    assert S.stream("p", FrameTime(1002, FILM)).below(10 ** 9) != S.stream("p", FrameTime(1001, FILM)).below(10 ** 9)


def test_stream_draws() -> None:
    st = S.stream("draws")
    for _ in range(200):
        v = st.randint(-3, 3)
        assert -3 <= v <= 3
        u = st.uniform("-1/2", "1/2", 1000)
        assert isinstance(u, Rational) and -Rational(1, 2) <= u <= Rational(1, 2)
        assert (u * 1000).denominator == 1  # on the grid
    assert st.chance(0) is False and st.chance(1) is True
    hits = sum(st.chance("1/4") for _ in range(4000))
    assert 850 < hits < 1150
    items = list(range(20))
    st.shuffle(items)
    assert sorted(items) == list(range(20)) and items != list(range(20))
    assert st.choice("abc") in "abc"
    smp = st.sample(10, 10)
    assert sorted(smp) == list(range(10))
    assert st.below(1) == 0 and st.bits(0) == 0
    with pytest.raises(ValueError):
        st.below(0)
    with pytest.raises(ValueError):
        st.uniform(1, 0, 3)
    with pytest.raises(ValueError):
        st.chance("3/2")
    with pytest.raises(IndexError):
        st.choice([])
    with pytest.raises(ValueError):
        st.sample(3, 4)
    with pytest.raises(zxv.FloatRejectedError):
        st.uniform(0.0, 1, 3)


def test_same_in_a_fresh_interpreter() -> None:
    # another process (its own hash() salt, its own library load) agrees
    code = ("import sys; sys.path[:0] = sys.argv[1:3]; from zxv_vfx import SeedScheme; "
            "s = SeedScheme('showA/sq010/sh0040'); st = s.stream('fx/rbd/debris'); "
            "print(s.seed('fx/rbd/debris', 1001).hex(), [st.below(1000) for _ in range(8)])")
    import zxv_vfx
    from pathlib import Path
    kit_src = str(Path(zxv_vfx.__file__).parents[1])
    sdk_src = str(Path(zxv.__file__).parents[1])
    out = subprocess.run([sys.executable, "-c", code, kit_src, sdk_src], capture_output=True, text=True,
                         check=True, env={**__import__("os").environ, "PYTHONHASHSEED": "random"})
    st = S.stream("fx/rbd/debris")
    assert out.stdout.strip() == f"{S.seed('fx/rbd/debris', 1001).hex()} {[st.below(1000) for _ in range(8)]}"
