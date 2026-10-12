# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact transforms: rational matrices composed in libzxv."""

from __future__ import annotations

import itertools

import pytest
import zxv
from zxv import Rational

from zxv_vfx import Matrix4, Vec3

I = Matrix4.identity()


def test_vec3() -> None:
    a, b = Vec3(1, 2, 3), Vec3("1/2", "0.25", -1)
    assert a + b == Vec3(Rational(3, 2), Rational(9, 4), 2)
    assert a - b == Vec3(Rational(1, 2), Rational(7, 4), 4)
    assert a.dot(b) == Rational(1, 2) + Rational(1, 2) - 3
    assert Vec3(1, 0, 0).cross(Vec3(0, 1, 0)) == Vec3(0, 0, 1)
    assert a.scaled("1/3") == Vec3(Rational(1, 3), Rational(2, 3), 1)
    assert list(a) == [1, 2, 3] and hash(a) == hash(Vec3(1, 2, 3))
    with pytest.raises(zxv.FloatRejectedError):
        Vec3(0.1, 0, 0)
    with pytest.raises(AttributeError):
        a.x = Rational(5)  # type: ignore[misc]


def test_translate_scale_compose() -> None:
    t = Matrix4.translate(10, Rational(1, 3), -2)
    s = Matrix4.scale(2)
    p = Vec3(1, 1, 1)
    assert t.apply_point(p) == Vec3(11, Rational(4, 3), -1)
    assert (s @ t).apply_point(p) == Vec3(12, Rational(7, 3), 0)  # scale first, then move
    assert (t @ s).apply_point(p) == Vec3(22, Rational(8, 3), -2)  # move first, then scale
    assert t.translation == Vec3(10, Rational(1, 3), -2)
    assert t.apply_vector(Vec3(1, 0, 0)) == Vec3(1, 0, 0)  # directions ignore translation
    assert Matrix4.scale(1, 2, 3).apply_vector(Vec3(1, 1, 1)) == Vec3(1, 2, 3)
    assert I @ t == t == t @ I
    assert Matrix4.translate(Vec3(1, 2, 3)) == Matrix4.translate(1, 2, 3)


@pytest.mark.parametrize("axis", ["x", "y", "z"])
def test_quarter_turns(axis: str) -> None:
    q = Matrix4.quarter_turns(axis, 1)
    assert q @ q @ q @ q == I
    assert Matrix4.quarter_turns(axis, 4) == I and Matrix4.quarter_turns(axis, -1) == q.inverse()
    assert q.determinant() == 1
    assert q.inverse() == q.transpose()  # orthogonal
    with pytest.raises(ValueError):
        Matrix4.quarter_turns("w", 1)


def test_right_handed() -> None:
    x, y, z = Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)
    assert Matrix4.quarter_turns("z", 1).apply_vector(x) == y
    assert Matrix4.quarter_turns("x", 1).apply_vector(y) == z
    assert Matrix4.quarter_turns("y", 1).apply_vector(z) == x
    assert Matrix4.from_quaternion(1, 0, 0, 1) == Matrix4.quarter_turns("z", 1)
    assert Matrix4.from_quaternion(1, 1, 0, 0) == Matrix4.quarter_turns("x", 1)
    assert Matrix4.from_quaternion(1, 0, 1, 0) == Matrix4.quarter_turns("y", 1)


def test_rational_quaternion() -> None:
    # (2, 0, 0, 1): the rotation about z with cos = 3/5, sin = 4/5, exactly
    r = Matrix4.from_quaternion(2, 0, 0, 1)
    assert r.apply_vector(Vec3(1, 0, 0)) == Vec3(Rational(3, 5), Rational(4, 5), 0)
    assert r.determinant() == 1
    assert r @ r.transpose() == I
    # the same rotation from a scaled quaternion
    assert Matrix4.from_quaternion(4, 0, 0, 2) == r
    # q and -q are the same rotation
    assert Matrix4.from_quaternion(-2, 0, 0, -1) == r
    # composition of quaternion rotations stays orthonormal, exactly
    m = I
    for q in [(1, 2, 3, 4), (5, -1, 2, 0), (3, 3, -1, 2), (7, 1, 1, 1)]:
        m = m @ Matrix4.from_quaternion(*q)
        assert m @ m.transpose() == I and m.determinant() == 1
    with pytest.raises(ValueError):
        Matrix4.from_quaternion(0, 0, 0, 0)


def test_inverse_and_determinant() -> None:
    m = Matrix4.scale(2, 3, 5) @ Matrix4.from_quaternion(1, 2, 3, 4) @ Matrix4.translate(7, "1/3", -2)
    inv = m.inverse()
    assert m @ inv == I and inv @ m == I
    assert m.determinant() == 30
    p = Vec3("1.5", "-2/7", 9)
    assert inv.apply_point(m.apply_point(p)) == p
    singular = Matrix4.scale(1, 0, 1)
    assert singular.determinant() == 0
    with pytest.raises(ValueError):
        singular.inverse()
    # a projective matrix divides out exactly
    proj = Matrix4([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 1], [0, 0, 0, 1]])
    assert proj.apply_point(Vec3(2, 4, 1)) == Vec3(1, 2, Rational(1, 2))
    with pytest.raises(ValueError):
        proj.apply_point(Vec3(0, 0, -1))


def test_text_round_trip() -> None:
    m = Matrix4.from_quaternion(2, 1, 0, 1) @ Matrix4.translate("0.1", "1/3", 5)
    text = m.to_text()
    assert all(isinstance(v, str) for row in text for v in row)
    assert Matrix4.from_text(text) == m
    assert Matrix4.from_flat(m.flat()) == m
    assert m[3, 0] == m.rows[3][0]
    assert "Matrix4(" in repr(m)
    with pytest.raises(ValueError):
        Matrix4([[1, 2, 3]])
    with pytest.raises(ValueError):
        Matrix4.from_flat([1] * 15)
    with pytest.raises(zxv.FloatRejectedError):
        Matrix4.translate(0.5, 0, 0)


def test_associative_exactly() -> None:
    # float matrices are not associative; rational ones are, bit for bit
    ms = [Matrix4.from_quaternion(1, 2, 0, 1), Matrix4.translate("1/7", 3, "-0.5"),
          Matrix4.scale("3/2", 1, "2/3"), Matrix4.quarter_turns("y", 1)]
    for a, b, c in itertools.permutations(ms, 3):
        assert (a @ b) @ c == a @ (b @ c)


def test_overflow_is_reported() -> None:
    big = Matrix4.scale(2 ** 40)
    with pytest.raises(zxv.ZxvUsageError):
        _ = big @ big  # 2**80 does not fit an int64 rational
