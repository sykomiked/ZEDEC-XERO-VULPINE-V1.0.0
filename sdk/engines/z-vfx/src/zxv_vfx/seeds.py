# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Deterministic procedural seeds and random streams, from SHA3-256.

A simulation re-run on another machine, another OS or another Python must
get the same random parameters. So nothing here touches the host's
``random`` module, the clock, ``hash()`` (randomised per process) or a
float. A seed is SHA3-256 (computed by libzxv) of a canonical encoding of
*what the randomness is for*: the show/shot namespace and any parts you
name (an asset path, a frame time, an instance index). A stream draws
32-byte blocks ``SHA3-256(seed || counter)`` and turns them into ints and
exact rationals with rejection sampling, so every value is unbiased and
reproducible.

The encoding is versioned (``zxv-vfx/seed/v1``); a change to it gets a new
version string, so old seeds stay reproducible.
"""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction
from typing import Any, List, MutableSequence, Sequence, TypeVar

from zxv import FloatRejectedError, Hashes, Rational

from ._exact import exact, rdiv
from .frametime import FrameRate, FrameTime

__all__ = ["SeedScheme", "SeedStream", "encode_part", "DOMAIN"]

DOMAIN = b"zxv-vfx/seed/v1"
"""The domain-separation prefix of every seed."""

T = TypeVar("T")


def _blob(tag: bytes, data: bytes) -> bytes:
    return tag + len(data).to_bytes(4, "big") + data


def encode_part(part: Any) -> bytes:
    """The canonical bytes of one seed part.

    Each part is a type tag, a 4-byte big-endian length and the data:
    ``s`` UTF-8 text, ``i`` an int in decimal, ``r`` an exact rational as
    ``n/d`` (Fraction, Decimal and Rational all reduce to it, so ``1/2``,
    ``Decimal("0.5")`` and ``Rational(1, 2)`` are the same part), ``f`` a
    frame time as ``rate|frame``, ``b`` raw bytes. Floats and anything else
    are refused.
    """
    if isinstance(part, float):
        raise FloatRejectedError("seed part")
    if isinstance(part, bool):
        return _blob(b"i", b"1" if part else b"0")
    if isinstance(part, int):
        return _blob(b"i", str(part).encode("ascii"))
    if isinstance(part, str):
        return _blob(b"s", part.encode("utf-8"))
    if isinstance(part, (bytes, bytearray)):
        return _blob(b"b", bytes(part))
    if isinstance(part, FrameTime):
        f = part.frame
        text = f"{part.rate.numerator}/{part.rate.denominator}|{f.numerator}/{f.denominator}"
        return _blob(b"f", text.encode("ascii"))
    if isinstance(part, FrameRate):
        return _blob(b"r", f"{part.numerator}/{part.denominator}".encode("ascii"))
    if isinstance(part, (Rational, Fraction, Decimal)):
        r = exact(part, "seed part")
        return _blob(b"r", f"{r.numerator}/{r.denominator}".encode("ascii"))
    raise TypeError(f"a seed part must be str, int, bytes, an exact rational or a FrameTime, "
                    f"not {type(part).__name__}")


class SeedScheme:
    """Seeds for one namespace, e.g. ``"show/seq010/sh0040"``.

    >>> s = SeedScheme("showA/sq01/sh010")
    >>> s.seed("fx/rbd/debris", 1001) == s.seed("fx/rbd/debris", 1001)
    True
    """

    __slots__ = ("namespace",)
    namespace: str

    def __init__(self, namespace: str) -> None:
        if not isinstance(namespace, str) or not namespace:
            raise ValueError("the namespace must be non-empty text")
        object.__setattr__(self, "namespace", namespace)

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("SeedScheme is immutable")

    def seed(self, *parts: Any) -> bytes:
        """The 32-byte seed for these parts (order matters)."""
        data = _blob(b"D", DOMAIN) + _blob(b"n", self.namespace.encode("utf-8"))
        for p in parts:
            data += encode_part(p)
        return Hashes.sha3_256(data)

    def seed_int(self, *parts: Any, bits: int = 31) -> int:
        """The seed as a non-negative int of ``bits`` bits (1..256), from its
        leading bytes. 31 bits fits a signed 32-bit int parameter in any DCC."""
        if isinstance(bits, bool) or not isinstance(bits, int) or not 1 <= bits <= 256:
            raise ValueError("bits must be 1..256")
        return int.from_bytes(self.seed(*parts), "big") >> (256 - bits)

    def stream(self, *parts: Any) -> "SeedStream":
        """A deterministic random stream for these parts."""
        return SeedStream(self.seed(*parts))


class SeedStream:
    """A deterministic stream of unbiased ints and exact rationals.

    Block k is ``SHA3-256(seed || k as 8 bytes big-endian)``; bits are taken
    from the blocks in order. Every draw uses rejection sampling, so
    ``below(n)`` is exactly uniform on ``0..n-1``.
    """

    __slots__ = ("_seed", "_counter", "_pool", "_pool_bits")

    def __init__(self, seed: bytes) -> None:
        if not isinstance(seed, (bytes, bytearray)) or len(seed) != 32:
            raise ValueError("a seed is 32 bytes")
        self._seed = bytes(seed)
        self._counter = 0
        self._pool = 0
        self._pool_bits = 0

    def _refill(self) -> None:
        block = Hashes.sha3_256(self._seed + self._counter.to_bytes(8, "big"))
        self._counter += 1
        self._pool = (self._pool << 256) | int.from_bytes(block, "big")
        self._pool_bits += 256

    def bits(self, n: int) -> int:
        """``n`` fresh random bits as an int in ``0 .. 2**n - 1``."""
        if isinstance(n, bool) or not isinstance(n, int) or n < 0:
            raise ValueError("n must be a non-negative int")
        while self._pool_bits < n:
            self._refill()
        self._pool_bits -= n
        out = self._pool >> self._pool_bits
        self._pool &= (1 << self._pool_bits) - 1
        return out

    def below(self, n: int) -> int:
        """A uniform int in ``0 .. n-1`` (rejection sampling)."""
        if isinstance(n, bool) or not isinstance(n, int) or n < 1:
            raise ValueError("n must be a positive int")
        k = (n - 1).bit_length()
        while True:
            v = self.bits(k)
            if v < n:
                return v

    def randint(self, lo: int, hi: int) -> int:
        """A uniform int in ``lo .. hi`` inclusive."""
        if hi < lo:
            raise ValueError("hi < lo")
        return lo + self.below(hi - lo + 1)

    def uniform(self, lo: Any, hi: Any, steps: int) -> Rational:
        """A uniform exact rational on the grid ``lo + (hi - lo) * k / steps``,
        ``k`` in ``0 .. steps`` (both ends included)."""
        a, b = exact(lo, "lo"), exact(hi, "hi")
        if b < a:
            raise ValueError("hi < lo")
        if isinstance(steps, bool) or not isinstance(steps, int) or steps < 1:
            raise ValueError("steps must be a positive int")
        return a + (b - a) * rdiv(self.below(steps + 1), steps)

    def chance(self, probability: Any) -> bool:
        """True with exactly ``probability`` (an exact rational in 0..1)."""
        p = exact(probability, "probability")
        if p < 0 or p > 1:
            raise ValueError("a probability is 0..1")
        return self.below(p.denominator) < p.numerator

    def choice(self, items: Sequence[T]) -> T:
        if not items:
            raise IndexError("choice from an empty sequence")
        return items[self.below(len(items))]

    def shuffle(self, items: MutableSequence[Any]) -> None:
        """Fisher-Yates, in place."""
        for i in range(len(items) - 1, 0, -1):
            j = self.below(i + 1)
            items[i], items[j] = items[j], items[i]

    def sample(self, n: int, k: int) -> List[int]:
        """``k`` distinct ints from ``0 .. n-1``, in draw order."""
        if not 0 <= k <= n:
            raise ValueError("need 0 <= k <= n")
        pool = list(range(n))
        out = []
        for i in range(k):
            j = i + self.below(n - i)
            pool[i], pool[j] = pool[j], pool[i]
            out.append(pool[i])
        return out
