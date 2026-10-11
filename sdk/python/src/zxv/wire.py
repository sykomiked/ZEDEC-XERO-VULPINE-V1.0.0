# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""UBH-168 harmonic wire frames, in pure Python.

A frame is 21 octets: a tag, then five 32-bit words in the fixed byte order
L-B-L-B-L: W0 little-endian (S+), W1 big-endian (S-), W2 LE (S+), W3 BE
(S-), W4 LE (S+). The octets are identical on every host.

Everything here runs without ``libzxv``, so a network service can frame
and parse traffic with no native dependency. The tests cross-check every
function byte for byte against the C library and ``vectors.json``; the
``*_native`` helpers call the C implementation for that purpose.
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass
from typing import Tuple

__all__ = ["FRAME_BYTES", "WireTruth", "Ubh168Frame", "tag_of"]

FRAME_BYTES = 21
"""Octets in one UBH-168 frame."""

_IDLE_PILOT = 0x5A585631  # the idle pilot word (octets "1VXZ" little-endian)
_THRESHOLD_Q16 = 65536 // 16  # zxv_wire_resolve's correlation threshold


class WireTruth(enum.IntEnum):
    """The six-valued truth a frame resolves to (``ZXV_TRUTH_*``)."""

    UNKNOWN = 0
    TRUE = 1
    FALSE = 2
    NEUTRAL = 3
    GLUT = 4
    """A contradiction, held."""
    PARADOX = 5
    """A persistent GLUT (never produced by the stateless resolver)."""


def tag_of(trunk: int, shell: int, sync: int) -> int:
    """The frame tag ``100*sync + 10*shell + trunk`` (trunk, shell 0..9; sync 0..1)."""
    for name, v, hi in (("trunk", trunk, 9), ("shell", shell, 9), ("sync", sync, 1)):
        if isinstance(v, bool) or not isinstance(v, int):
            raise TypeError(f"{name} must be an int")
        if not 0 <= v <= hi:
            raise ValueError(f"{name} must be 0..{hi}")
    return 100 * sync + 10 * shell + trunk


def _u32(v: int, what: str) -> int:
    if isinstance(v, bool) or not isinstance(v, int):
        raise TypeError(f"{what} must be an int")
    if not 0 <= v <= 0xFFFFFFFF:
        raise ValueError(f"{what} must be a uint32")
    return v


def _s32(v: int) -> int:
    return v - (1 << 32) if v & 0x80000000 else v


def _q16_mul(a: int, b: int) -> int:
    # signed Q16.16 product with an arithmetic (floor) shift, as the kernel does
    return (_s32(a) * _s32(b)) >> 16


@dataclass(frozen=True)
class Ubh168Frame:
    """One UBH-168 frame: a tag, three S+ words and two S- words (uint32)."""

    tag: int
    s_plus: Tuple[int, int, int]
    s_minus: Tuple[int, int]

    def __post_init__(self) -> None:
        if isinstance(self.tag, bool) or not isinstance(self.tag, int) or not 0 <= self.tag <= 255:
            raise ValueError("tag must be 0..255")
        sp = tuple(self.s_plus)
        sm = tuple(self.s_minus)
        if len(sp) != 3 or len(sm) != 2:
            raise ValueError("s_plus has 3 words and s_minus has 2")
        for i, w in enumerate(sp):
            _u32(w, f"s_plus[{i}]")
        for i, w in enumerate(sm):
            _u32(w, f"s_minus[{i}]")
        object.__setattr__(self, "s_plus", sp)
        object.__setattr__(self, "s_minus", sm)

    @classmethod
    def build(cls, trunk: int, shell: int, sync: int, s_plus: Tuple[int, int, int],
              s_minus: Tuple[int, int]) -> "Ubh168Frame":
        """A frame with the tag computed from trunk, shell and sync."""
        return cls(tag_of(trunk, shell, sync), tuple(s_plus), tuple(s_minus))  # type: ignore[arg-type]

    @classmethod
    def idle(cls, trunk: int) -> "Ubh168Frame":
        """The idle pilot frame of a trunk (0..9); never all zeros."""
        return cls(tag_of(trunk, 0, 1), (_IDLE_PILOT,) * 3, (_IDLE_PILOT,) * 2)

    def to_bytes(self) -> bytes:
        """The 21 wire octets (L-B-L-B-L)."""
        p, m = self.s_plus, self.s_minus
        return (bytes([self.tag]) + struct.pack("<I", p[0]) + struct.pack(">I", m[0])
                + struct.pack("<I", p[1]) + struct.pack(">I", m[1]) + struct.pack("<I", p[2]))

    @classmethod
    def from_bytes(cls, data: bytes) -> "Ubh168Frame":
        """Parse 21 octets."""
        b = bytes(data)
        if len(b) != FRAME_BYTES:
            raise ValueError(f"a UBH-168 frame is {FRAME_BYTES} bytes, got {len(b)}")
        (w0,) = struct.unpack_from("<I", b, 1)
        (w1,) = struct.unpack_from(">I", b, 5)
        (w2,) = struct.unpack_from("<I", b, 9)
        (w3,) = struct.unpack_from(">I", b, 13)
        (w4,) = struct.unpack_from("<I", b, 17)
        return cls(b[0], (w0, w2, w4), (w1, w3))

    @property
    def trunk(self) -> int:
        """Tag digit 0..9."""
        return self.tag % 10

    @property
    def shell(self) -> int:
        """Tag tens digit."""
        return (self.tag // 10) % 10

    @property
    def sync(self) -> int:
        """Tag hundreds digit."""
        return self.tag // 100

    @property
    def is_idle(self) -> bool:
        """True for an idle pilot frame."""
        return all(w == _IDLE_PILOT for w in self.s_plus + self.s_minus)

    def correlation_q16(self) -> int:
        """The W5 dot product (Q16.16) of the S+ and S- words."""
        p, m = self.s_plus, self.s_minus
        return _q16_mul(p[0], m[0]) + _q16_mul(p[1], m[1])

    def resolve(self) -> WireTruth:
        """The stateless W5 correlation resolver (as ``zxv_wire_resolve``)."""
        p, m = self.s_plus, self.s_minus
        plus = any(p)
        minus = any(m)
        t = _THRESHOLD_Q16
        if not plus and not minus:
            return WireTruth.UNKNOWN
        if self.is_idle or not plus or not minus:
            return WireTruth.NEUTRAL
        dot = self.correlation_q16()
        if dot > t:
            return WireTruth.TRUE
        if dot < -t:
            ep = _q16_mul(p[0], p[0]) + _q16_mul(p[1], p[1])
            em = _q16_mul(m[0], m[0]) + _q16_mul(m[1], m[1])
            lo, hi = min(ep, em), max(ep, em)
            return WireTruth.GLUT if 4 * lo >= hi else WireTruth.FALSE
        return WireTruth.GLUT

    # ------------------------------------------------------------ C cross-check

    def to_bytes_native(self) -> bytes:
        """``zxv_wire_pack`` of this frame (needs libzxv; for cross-checks)."""
        from . import _native
        from ._native import ffi

        sp = ffi.new("uint32_t[3]", list(self.s_plus))
        sm = ffi.new("uint32_t[2]", list(self.s_minus))
        out = ffi.new("uint8_t[21]")
        _native.check(_native.lib().zxv_wire_pack(self.tag, sp, sm, out))
        return bytes(ffi.buffer(out, FRAME_BYTES))

    def resolve_native(self) -> WireTruth:
        """``zxv_wire_resolve`` of this frame (needs libzxv; for cross-checks)."""
        from . import _native
        from ._native import ffi

        out = ffi.new("int32_t *")
        _native.check(_native.lib().zxv_wire_resolve(self.to_bytes(), out))
        return WireTruth(out[0])

