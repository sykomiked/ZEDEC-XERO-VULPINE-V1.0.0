# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact 3D transforms: rational vectors and 4x4 matrices.

Convention: row vectors, as in Maya and Houdini. A point ``p`` is
transformed as ``p @ M``; the translation sits in the last row; ``A @ B``
applies A first, then B. Every entry is a :class:`zxv.Rational` and every
product and sum runs in libzxv, so a transform composed on one machine is
bit-for-bit the transform composed on another. Overflow raises
``ZXV_E_RANGE``; nothing is rounded.

Rotations are exact when they are rational: quarter turns, and any
rotation given by a quaternion with rational components (its matrix is
rational after dividing by the squared norm). An arbitrary angle in degrees
has no exact rational rotation; it belongs to the DCC, not to the core.
"""

from __future__ import annotations

from typing import Any, Iterable, Iterator, List, Sequence, Tuple

from zxv import Rational

from ._exact import exact, rdiv

__all__ = ["Vec3", "Matrix4"]

_ZERO = Rational(0)
_ONE = Rational(1)


class Vec3:
    """An exact 3-vector."""

    __slots__ = ("x", "y", "z")
    x: Rational
    y: Rational
    z: Rational

    def __init__(self, x: Any, y: Any, z: Any) -> None:
        object.__setattr__(self, "x", exact(x, "x"))
        object.__setattr__(self, "y", exact(y, "y"))
        object.__setattr__(self, "z", exact(z, "z"))

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Vec3 is immutable")

    def __iter__(self) -> Iterator[Rational]:
        return iter((self.x, self.y, self.z))

    def __add__(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def scaled(self, k: Any) -> "Vec3":
        r = exact(k, "scale")
        return Vec3(self.x * r, self.y * r, self.z * r)

    def dot(self, o: "Vec3") -> Rational:
        return self.x * o.x + self.y * o.y + self.z * o.z

    def cross(self, o: "Vec3") -> "Vec3":
        return Vec3(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Vec3) and (self.x, self.y, self.z) == (other.x, other.y, other.z)

    def __hash__(self) -> int:
        return hash((self.x, self.y, self.z))

    def __repr__(self) -> str:
        return f"Vec3({self.x}, {self.y}, {self.z})"


def _rows(rows: Iterable[Iterable[Any]]) -> Tuple[Tuple[Rational, ...], ...]:
    out = tuple(tuple(exact(v, "matrix entry") for v in row) for row in rows)
    if len(out) != 4 or any(len(r) != 4 for r in out):
        raise ValueError("a Matrix4 has 4 rows of 4 entries")
    return out


class Matrix4:
    """An exact 4x4 matrix (row-vector convention; see the module docstring)."""

    __slots__ = ("_m",)
    _m: Tuple[Tuple[Rational, ...], ...]

    def __init__(self, rows: Iterable[Iterable[Any]]) -> None:
        object.__setattr__(self, "_m", _rows(rows))

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Matrix4 is immutable")

    # ------------------------------------------------------------ builders

    @classmethod
    def identity(cls) -> "Matrix4":
        return cls([[1 if i == j else 0 for j in range(4)] for i in range(4)])

    @classmethod
    def translate(cls, t: Any, ty: Any = None, tz: Any = None) -> "Matrix4":
        """Translation by a :class:`Vec3` or by three exact values."""
        v = t if isinstance(t, Vec3) else Vec3(t, ty, tz)
        return cls([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [v.x, v.y, v.z, 1]])

    @classmethod
    def scale(cls, sx: Any, sy: Any = None, sz: Any = None) -> "Matrix4":
        """Scale by one exact factor, or by three."""
        x = exact(sx, "scale")
        y = x if sy is None else exact(sy, "scale")
        z = x if sz is None else exact(sz, "scale")
        return cls([[x, 0, 0, 0], [0, y, 0, 0], [0, 0, z, 0], [0, 0, 0, 1]])

    @classmethod
    def quarter_turns(cls, axis: str, turns: int) -> "Matrix4":
        """A rotation by ``turns`` x 90 degrees about ``"x"``, ``"y"`` or ``"z"``
        (right-handed, counter-clockwise looking down the axis)."""
        if isinstance(turns, bool) or not isinstance(turns, int):
            raise TypeError("turns must be an int")
        c, s = ((1, 0), (0, 1), (-1, 0), (0, -1))[turns % 4]
        if axis == "x":
            rows = [[1, 0, 0, 0], [0, c, s, 0], [0, -s, c, 0], [0, 0, 0, 1]]
        elif axis == "y":
            rows = [[c, 0, -s, 0], [0, 1, 0, 0], [s, 0, c, 0], [0, 0, 0, 1]]
        elif axis == "z":
            rows = [[c, s, 0, 0], [-s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        else:
            raise ValueError("axis must be 'x', 'y' or 'z'")
        return cls(rows)

    @classmethod
    def from_quaternion(cls, w: Any, x: Any, y: Any, z: Any) -> "Matrix4":
        """The rotation of the quaternion ``w + xi + yj + zk`` (any non-zero
        rational quaternion; it is normalised exactly by its squared norm).

        ``from_quaternion(1, 0, 0, 1)`` is 90 degrees about z;
        ``from_quaternion(2, 0, 0, 1)`` is the rotation whose cosine is 3/5.
        """
        W, X, Y, Z = (exact(v, "quaternion") for v in (w, x, y, z))
        n = W * W + X * X + Y * Y + Z * Z
        if n == 0:
            raise ValueError("the zero quaternion is not a rotation")
        k = rdiv(2, n)
        # The rotation matrix for column vectors, transposed for row vectors.
        r = [
            [1 - k * (Y * Y + Z * Z), k * (X * Y - Z * W), k * (X * Z + Y * W)],
            [k * (X * Y + Z * W), 1 - k * (X * X + Z * Z), k * (Y * Z - X * W)],
            [k * (X * Z - Y * W), k * (Y * Z + X * W), 1 - k * (X * X + Y * Y)],
        ]
        return cls([[r[j][i] for j in range(3)] + [0] for i in range(3)] + [[0, 0, 0, 1]])

    @classmethod
    def from_flat(cls, values: Sequence[Any]) -> "Matrix4":
        """From 16 exact values, row by row."""
        if len(values) != 16:
            raise ValueError("a flat Matrix4 has 16 values")
        return cls([values[4 * i:4 * i + 4] for i in range(4)])

    # ------------------------------------------------------------ access

    @property
    def rows(self) -> Tuple[Tuple[Rational, ...], ...]:
        return self._m

    def flat(self) -> List[Rational]:
        """The 16 entries, row by row (the order Maya's ``xform -matrix`` uses)."""
        return [v for row in self._m for v in row]

    def __getitem__(self, ij: Tuple[int, int]) -> Rational:
        return self._m[ij[0]][ij[1]]

    @property
    def translation(self) -> Vec3:
        return Vec3(*self._m[3][:3])

    # ------------------------------------------------------------ algebra

    def __matmul__(self, o: "Matrix4") -> "Matrix4":
        a, b = self._m, o._m
        return Matrix4([[sum((a[i][k] * b[k][j] for k in range(4)), _ZERO) for j in range(4)]
                        for i in range(4)])

    def transpose(self) -> "Matrix4":
        return Matrix4([[self._m[j][i] for j in range(4)] for i in range(4)])

    def apply_point(self, p: Vec3) -> Vec3:
        """``p @ M`` for a point (w = 1); a projective result is divided out exactly."""
        v = [p.x, p.y, p.z, _ONE]
        out = [sum((v[k] * self._m[k][j] for k in range(4)), _ZERO) for j in range(4)]
        if out[3] == 0:
            raise ValueError("the point maps to infinity")
        if out[3] == 1:
            return Vec3(out[0], out[1], out[2])
        return Vec3(rdiv(out[0], out[3]), rdiv(out[1], out[3]), rdiv(out[2], out[3]))

    def apply_vector(self, d: Vec3) -> Vec3:
        """``d @ M`` for a direction (w = 0): translation does not apply."""
        v = [d.x, d.y, d.z]
        return Vec3(*(sum((v[k] * self._m[k][j] for k in range(3)), _ZERO) for j in range(3)))

    def determinant(self) -> Rational:
        m = [list(r) for r in self._m]
        det = _ONE
        for c in range(4):
            p = next((r for r in range(c, 4) if m[r][c] != 0), None)
            if p is None:
                return _ZERO
            if p != c:
                m[c], m[p] = m[p], m[c]
                det = -det
            det = det * m[c][c]
            for r in range(c + 1, 4):
                if m[r][c] != 0:
                    f = rdiv(m[r][c], m[c][c])
                    m[r] = [m[r][j] - f * m[c][j] for j in range(4)]
        return det

    def inverse(self) -> "Matrix4":
        """The exact inverse (Gauss-Jordan in rationals); ValueError if singular."""
        m = [list(r) + [_ONE if i == j else _ZERO for j in range(4)] for i, r in enumerate(self._m)]
        for c in range(4):
            p = next((r for r in range(c, 4) if m[r][c] != 0), None)
            if p is None:
                raise ValueError("the matrix is singular")
            m[c], m[p] = m[p], m[c]
            piv = m[c][c]
            m[c] = [rdiv(v, piv) for v in m[c]]
            for r in range(4):
                if r != c and m[r][c] != 0:
                    f = m[r][c]
                    m[r] = [m[r][j] - f * m[c][j] for j in range(8)]
        return Matrix4([row[4:] for row in m])

    # ------------------------------------------------------------ text

    def to_text(self) -> List[List[str]]:
        """Exact text for every entry (``"1/3"``, ``"12.75"``), for JSON or USD
        metadata that must round-trip without loss."""
        return [[str(v) for v in row] for row in self._m]

    @classmethod
    def from_text(cls, rows: Iterable[Iterable[str]]) -> "Matrix4":
        return cls([[Rational.parse(v) for v in row] for row in rows])

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Matrix4) and self._m == other._m

    def __hash__(self) -> int:
        return hash(self._m)

    def __repr__(self) -> str:
        return "Matrix4(" + repr(self.to_text()) + ")"
