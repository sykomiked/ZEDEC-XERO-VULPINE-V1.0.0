# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Frame rates, frame times, ranges and timecode: exact, never float."""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction

import pytest
import zxv
from zxv import Rational

from zxv_vfx import (FILM, FILM_NTSC, NTSC, NTSC_FIELD, PAL, FrameRange, FrameRate, FrameTime,
                     Timecode)


@pytest.mark.parametrize("text,num,den", [
    ("24", 24, 1), ("25", 25, 1), ("24000/1001", 24000, 1001), ("23.976", 24000, 1001),
    ("23.98", 24000, 1001), ("29.97", 30000, 1001), ("29.97fps", 30000, 1001),
    ("59.94", 60000, 1001), ("47.952", 48000, 1001), ("119.88", 120000, 1001),
    ("12.5", 25, 2), ("30000/1001", 30000, 1001), ("48 fps", 48, 1), ("120", 120, 1),
])
def test_rate_parse(text: str, num: int, den: int) -> None:
    r = FrameRate.parse(text)
    assert (r.numerator, r.denominator) == (num, den)
    assert r == FrameRate(num, den) and hash(r) == hash(FrameRate(num, den))


@pytest.mark.parametrize("bad", ["0", "-24", "abc", "1e3", "", "24/0"])
def test_rate_parse_refuses(bad: str) -> None:
    with pytest.raises((ValueError, zxv.ZxvUsageError)):
        FrameRate.parse(bad)


def test_rate_refuses_float() -> None:
    with pytest.raises(zxv.FloatRejectedError):
        FrameRate.of(23.976)
    with pytest.raises(TypeError):
        FrameRate(24.0, 1)  # type: ignore[arg-type]
    with pytest.raises(zxv.FloatRejectedError):
        FrameTime(1.5, FILM)


def test_rate_properties() -> None:
    assert FILM_NTSC.frame_duration == Rational(1001, 24000)
    assert FILM_NTSC.is_ntsc and not FILM.is_ntsc and not FrameRate(25, 2).is_ntsc
    assert FILM_NTSC.nominal == 24 and NTSC.nominal == 30 and NTSC_FIELD.nominal == 60
    assert FILM.nominal == 24 and FrameRate(25, 2).nominal == 13
    assert NTSC.drop_frames == 2 and NTSC_FIELD.drop_frames == 4
    assert FILM_NTSC.drop_frames == 0 and FILM.drop_frames == 0
    assert FrameRate(120000, 1001).drop_frames == 8
    assert str(FILM_NTSC) == "24000/1001" and str(FILM) == "24" and repr(PAL) == "FrameRate(25)"
    assert FrameRate.of(Fraction(24000, 1001)) == FILM_NTSC
    assert FrameRate.of(Decimal("25")) == PAL and FrameRate.of(24) == FILM and FrameRate.of(FILM) is FILM
    with pytest.raises(AttributeError):
        FILM._r = Rational(1)  # type: ignore[misc]


def test_seconds_are_exact() -> None:
    # 86400 frames at 23.976 last 3603.6 seconds, exactly
    t = FrameTime(86400, FILM_NTSC)
    assert t.seconds == Rational(86400 * 1001, 24000) == Rational(36036, 10)
    assert FrameTime(24, FILM).seconds == 1
    assert FrameTime(1001, FILM_NTSC).seconds == Rational(1001 * 1001, 24000)
    # no drift over a million frames: frame -> seconds -> frame is the identity
    for f in (0, 1, 1001, 999_999, 1_000_000, -7):
        for rate in (FILM, FILM_NTSC, NTSC, NTSC_FIELD, PAL, FrameRate(25, 2)):
            t = FrameTime(f, rate)
            assert FrameTime.from_seconds(t.seconds, rate) == t


def test_subframes_and_index() -> None:
    t = FrameTime(Rational(2003, 2), FILM)  # 1001.5
    assert not t.is_whole
    assert t.index("floor") == 1001 and t.index("ceil") == 1002
    assert t.index("half_up") == 1002 and t.index("half_even") == 1002
    t2 = FrameTime(Rational(2001, 2), FILM)  # 1000.5
    assert t2.index("half_even") == 1000 and t2.index("half_up") == 1001
    neg = FrameTime(Rational(-5, 2), FILM)
    assert neg.index("floor") == -3 and neg.index("ceil") == -2
    assert neg.index("half_up") == -3 and neg.index("half_even") == -2
    assert FrameTime(Rational(7, 3), FILM).index("half_up") == 2
    assert FrameTime(Rational(8, 3), FILM).index("half_even") == 3
    with pytest.raises(ValueError):
        t.index("nearest")


def test_retime() -> None:
    # 24 -> 23.976 keeps the instant: frame * (24000/1001) / 24
    t = FrameTime(1001, FILM).retime(FILM_NTSC)
    assert t.frame == 1000 and t.seconds == FrameTime(1001, FILM).seconds
    t = FrameTime(100, FILM).retime(PAL)
    assert t.frame == Rational(625, 6)
    assert FrameTime(100, FILM).retime(PAL, "floor").frame == 104
    assert FrameTime(100, FILM).retime(PAL, "ceil").frame == 105
    assert FrameTime(100, FILM).retime(PAL, "half_even").frame == 104
    # retime there and back is exact
    for f in range(0, 500, 7):
        a = FrameTime(f, NTSC)
        assert a.retime(FILM).retime(NTSC) == a
    with pytest.raises(ValueError):
        FrameTime(1, FILM).retime(PAL, "sideways")


def test_shutter_samples() -> None:
    t = FrameTime(1001, FILM)
    s = t.shutter_samples(Rational(-1, 4), Rational(1, 4), 5)
    assert [x.frame for x in s] == [Rational(4003, 4), Rational(8007, 8), 1001, Rational(8009, 8),
                                    Rational(4005, 4)]
    assert t.shutter_samples(0, "1/2", 1)[0].frame == 1001
    third = t.shutter_samples(0, 1, 4)
    assert [x.frame - 1001 for x in third] == [0, Rational(1, 3), Rational(2, 3), 1]
    with pytest.raises(ValueError):
        t.shutter_samples(1, 0, 3)
    with pytest.raises(ValueError):
        t.shutter_samples(0, 1, 0)
    with pytest.raises(zxv.FloatRejectedError):
        t.shutter_samples(-0.25, 0.25, 3)


def test_arithmetic_and_order() -> None:
    a, b = FrameTime(10, FILM), FrameTime(Rational(21, 2), FILM)
    assert (a + Rational(1, 2)) == b and (b - Rational(1, 2)) == a
    assert b - a == Rational(1, 2)
    assert a < b and b > a and a <= a and b >= a
    assert FrameTime(24, FILM) == FrameTime(24, FILM) and FrameTime(24, FILM) != FrameTime(24, PAL)
    # order across rates is by time
    assert FrameTime(24, FILM) < FrameTime(26, PAL)
    with pytest.raises(ValueError):
        _ = FrameTime(1, FILM) - FrameTime(1, PAL)
    assert len({a, FrameTime(10, FILM), b}) == 2


def test_range() -> None:
    r = FrameRange(1001, 1100, FILM)
    assert len(r) == 100 and r.count == 100
    assert r.duration == Rational(100, 24)
    fr = list(r)
    assert fr[0] == FrameTime(1001, FILM) and fr[-1] == FrameTime(1100, FILM)
    s = FrameRange(1, 10, FILM_NTSC, step=3)
    assert [t.frame for t in s] == [1, 4, 7, 10] and len(s) == 4
    assert FrameRange(1, 1, FILM).count == 1
    assert repr(s) == "FrameRange(1, 10, 24000/1001, step=3)"
    with pytest.raises(ValueError):
        FrameRange(5, 4, FILM)
    with pytest.raises(ValueError):
        FrameRange(1, 4, FILM, step=0)
    with pytest.raises(TypeError):
        FrameRange(1.0, 4, FILM)  # type: ignore[arg-type]


# ---------------------------------------------------------------- timecode

@pytest.mark.parametrize("frame,rate,text", [
    (0, FILM, "00:00:00:00"),
    (23, FILM, "00:00:00:23"),
    (24, FILM, "00:00:01:00"),
    (86399 * 24 + 23, FILM, "23:59:59:23"),
    (90000, PAL, "01:00:00:00"),
    (1001, FILM_NTSC, "00:00:41:17"),  # NDF at 23.976: labels count 24 per second
    (1799, NTSC, "00:00:59;29"),
    (1800, NTSC, "00:01:00;02"),
    (3597, NTSC, "00:01:59;29"),
    (17982, NTSC, "00:10:00;00"),
    (17983, NTSC, "00:10:00;01"),
    (107892, NTSC, "01:00:00;00"),
    (3600, NTSC_FIELD, "00:01:00;04"),
    (35964, NTSC_FIELD, "00:10:00;00"),
    (215784, NTSC_FIELD, "01:00:00;00"),
])
def test_timecode_vectors(frame: int, rate: FrameRate, text: str) -> None:
    tc = Timecode.from_frame(frame, rate)
    assert str(tc) == text
    assert tc.to_frame(rate) == frame
    assert Timecode.parse(text, rate) == tc
    assert FrameTime.from_timecode(text, rate) == FrameTime(frame, rate)
    assert FrameTime(frame, rate).timecode() == tc


@pytest.mark.parametrize("rate", [NTSC, NTSC_FIELD])
def test_drop_frame_round_trip(rate: FrameRate) -> None:
    # every frame of the first 12 minutes, and samples across a day
    frames = list(range(0, rate.nominal * 60 * 12)) + list(range(0, 24 * 3600 * 29, 10007))
    prev = None
    for f in frames:
        tc = Timecode.from_frame(f, rate)
        assert tc.drop_frame
        assert tc.to_frame(rate) == f, (f, str(tc))
        if prev is not None and f == prev + 1:
            # labels strictly increase
            assert (tc.hours, tc.minutes, tc.seconds, tc.frames) > prev_tc
        prev, prev_tc = f, (tc.hours, tc.minutes, tc.seconds, tc.frames)
        # dropped labels never appear
        if tc.seconds == 0 and tc.minutes % 10 != 0:
            assert tc.frames >= rate.drop_frames


def test_non_drop_at_ntsc() -> None:
    tc = Timecode.from_frame(1800, NTSC, drop_frame=False)
    assert str(tc) == "00:01:00:00" and tc.to_frame(NTSC) == 1800
    # NDF at 29.97 runs slow against the wall clock: an hour of labels is
    # 108000 frames, which is 3603.6 seconds.
    assert FrameTime.from_timecode("01:00:00:00", NTSC).seconds == Rational(36036, 10)
    assert FrameTime.from_timecode("01:00:00;00", NTSC).seconds == Rational(107892 * 1001, 30000)


def test_timecode_refusals() -> None:
    with pytest.raises(ValueError):
        Timecode.parse("00:01:00;00", NTSC)  # a dropped label
    with pytest.raises(ValueError):
        Timecode.parse("00:01:00;01", NTSC)
    assert Timecode.parse("00:10:00;00", NTSC).to_frame(NTSC) == 17982  # every 10th minute keeps it
    with pytest.raises(ValueError):
        Timecode.parse("00:00:00:24", FILM)
    with pytest.raises(ValueError):
        Timecode.parse("00:00:00;00", FILM)  # no drop-frame at 24
    with pytest.raises(ValueError):
        Timecode.from_frame(10, FILM, drop_frame=True)
    with pytest.raises(ValueError):
        Timecode.from_frame(24 * 3600 * 24, FILM)  # a day is the end
    with pytest.raises(ValueError):
        Timecode.from_frame(-1, FILM)
    with pytest.raises(TypeError):
        Timecode.from_frame(1.0, FILM)  # type: ignore[arg-type]
    with pytest.raises(ValueError):
        Timecode.parse("1:00:00:00", FILM)
    with pytest.raises(ValueError):
        Timecode.from_frame(1, FrameRate(25, 2))  # no timecode at 12.5
    with pytest.raises(ValueError):
        FrameTime(Rational(1, 2), FILM).timecode()
    with pytest.raises(ValueError):
        Timecode(24, 0, 0, 0)
    assert Timecode.parse("00:00:01.00", NTSC).drop_frame
    assert repr(Timecode(1, 2, 3, 4)) == "Timecode('01:02:03:04')"
