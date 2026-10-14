# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Exact frame rates, frame times, frame ranges and SMPTE timecode.

A frame rate is a rational number of frames per second (``24000/1001``,
never ``23.976``). A frame time is a rational frame number at a rate
(``1001/2`` is half way through frame 500), and its time in seconds is the
exact rational ``frame / rate``. Nothing here is a float: two machines that
compute a frame time, a retime or a shutter sample get the same value.
"""

from __future__ import annotations

import re
from typing import Any, Iterator, List, Optional

from zxv import Rational

from ._exact import ROUNDINGS, exact, rdiv, rounded

__all__ = ["FrameRate", "FrameTime", "FrameRange", "Timecode", "FILM", "FILM_NTSC", "PAL", "NTSC",
           "NTSC_30", "SHOW", "PAL_FIELD", "NTSC_FIELD", "HFR_60"]

# The NTSC family: these labels are names for N*1000/1001, not decimals.
_NTSC_LABELS = {
    "23.976": 24000,
    "23.98": 24000,
    "29.97": 30000,
    "47.952": 48000,
    "59.94": 60000,
    "119.88": 120000,
}


class FrameRate:
    """An exact, positive frame rate in frames per second."""

    __slots__ = ("_r",)
    _r: Rational

    def __init__(self, numerator: int, denominator: int = 1) -> None:
        r = Rational(numerator, denominator)
        if r <= 0:
            raise ValueError("a frame rate must be positive")
        object.__setattr__(self, "_r", r)

    @classmethod
    def of(cls, value: Any) -> "FrameRate":
        """A rate from a :class:`FrameRate`, an exact value or text (see :meth:`parse`)."""
        if isinstance(value, FrameRate):
            return value
        if isinstance(value, str):
            return cls.parse(value)
        r = exact(value, "frame rate")
        return cls(r.numerator, r.denominator)

    @classmethod
    def parse(cls, text: str) -> "FrameRate":
        """Parse ``"24"``, ``"24000/1001"``, ``"12.5"``, or an NTSC label.

        The labels ``23.976``, ``23.98``, ``29.97``, ``47.952``, ``59.94``
        and ``119.88`` (with an optional ``fps`` suffix) mean N*1000/1001:
        that is what every camera, deck and DCC means by them. Any other
        decimal is read exactly (``"12.5"`` is 25/2).
        """
        t = text.strip().lower()
        if t.endswith("fps"):
            t = t[:-3].strip()
        if t in _NTSC_LABELS:
            return cls(_NTSC_LABELS[t], 1001)
        r = Rational.parse(t)
        return cls(r.numerator, r.denominator)

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("FrameRate is immutable")

    @property
    def rational(self) -> Rational:
        """Frames per second, exactly."""
        return self._r

    @property
    def numerator(self) -> int:
        return self._r.numerator

    @property
    def denominator(self) -> int:
        return self._r.denominator

    @property
    def frame_duration(self) -> Rational:
        """Seconds per frame, exactly (``1001/24000`` at 23.976)."""
        return Rational(self._r.denominator, self._r.numerator)

    @property
    def is_ntsc(self) -> bool:
        """True for an N*1000/1001 rate."""
        return self._r.denominator == 1001 and self._r.numerator % 1000 == 0

    @property
    def nominal(self) -> int:
        """The whole frames per timecode second: the rate rounded up (30 at 29.97)."""
        return rounded(self._r, "ceil")

    @property
    def drop_frames(self) -> int:
        """Frame numbers dropped per minute in drop-frame timecode: 2 at 29.97, 4 at
        59.94, 0 where drop-frame timecode is not defined."""
        if self.is_ntsc and self.nominal % 30 == 0:
            return 2 * (self.nominal // 30)
        return 0

    def label(self) -> str:
        """``"24"``, ``"24000/1001"`` or ``"25/2"``."""
        return str(self._r.numerator) if self._r.denominator == 1 else \
            f"{self._r.numerator}/{self._r.denominator}"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, FrameRate) and self._r == other._r

    def __hash__(self) -> int:
        return hash(("FrameRate", self._r))

    def __repr__(self) -> str:
        return f"FrameRate({self.label()})"

    __str__ = label


FILM = FrameRate(24)
FILM_NTSC = FrameRate(24000, 1001)
PAL = FrameRate(25)
NTSC = FrameRate(30000, 1001)
NTSC_30 = FrameRate(30)
SHOW = FrameRate(48)
PAL_FIELD = FrameRate(50)
NTSC_FIELD = FrameRate(60000, 1001)
HFR_60 = FrameRate(60)


def _timecode_rate(rate: Any) -> FrameRate:
    fr = FrameRate.of(rate)
    if fr.denominator != 1 and not fr.is_ntsc:
        raise ValueError(f"timecode is defined for whole and N*1000/1001 rates, not {fr}")
    return fr


class FrameTime:
    """A frame position at a rate: an exact rational frame number.

    Whole frames are integers; subframes (motion-blur samples, retimes) are
    exact fractions of a frame. :attr:`seconds` is ``frame / rate``.
    """

    __slots__ = ("_frame", "_rate")
    _frame: Rational
    _rate: FrameRate

    def __init__(self, frame: Any, rate: Any) -> None:
        object.__setattr__(self, "_frame", exact(frame, "frame"))
        object.__setattr__(self, "_rate", FrameRate.of(rate))

    @classmethod
    def from_seconds(cls, seconds: Any, rate: Any) -> "FrameTime":
        """The frame time at an exact number of seconds."""
        fr = FrameRate.of(rate)
        return cls(exact(seconds, "seconds") * fr.rational, fr)

    @classmethod
    def from_timecode(cls, timecode: Any, rate: Any) -> "FrameTime":
        """The frame a timecode (a :class:`Timecode` or text) names at ``rate``."""
        fr = FrameRate.of(rate)
        tc = timecode if isinstance(timecode, Timecode) else Timecode.parse(str(timecode), fr)
        return cls(tc.to_frame(fr), fr)

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("FrameTime is immutable")

    @property
    def frame(self) -> Rational:
        return self._frame

    @property
    def rate(self) -> FrameRate:
        return self._rate

    @property
    def seconds(self) -> Rational:
        """``frame / rate``, exactly."""
        return rdiv(self._frame, self._rate.rational)

    @property
    def is_whole(self) -> bool:
        return self._frame.denominator == 1

    def index(self, rounding: str = "floor") -> int:
        """The whole frame this time falls on, under one of ``floor``, ``ceil``,
        ``half_up`` or ``half_even``."""
        return rounded(self._frame, rounding)

    def retime(self, rate: Any, rounding: Optional[str] = None) -> "FrameTime":
        """The same instant at another rate (``frame * new / old``).

        With ``rounding`` the result is snapped to a whole frame; without it
        the exact (possibly fractional) frame is kept.
        """
        fr = FrameRate.of(rate)
        f = self._frame * rdiv(fr.rational, self._rate.rational)
        if rounding is not None:
            if rounding not in ROUNDINGS:
                raise ValueError(f"rounding must be one of {ROUNDINGS}")
            f = Rational(rounded(f, rounding))
        return FrameTime(f, fr)

    def timecode(self, drop_frame: Optional[bool] = None) -> "Timecode":
        """The timecode of this frame (it must be whole)."""
        if not self.is_whole:
            raise ValueError("timecode needs a whole frame; use index() first")
        return Timecode.from_frame(self._frame.numerator, self._rate, drop_frame)

    def shutter_samples(self, open_: Any, close: Any, samples: int) -> List["FrameTime"]:
        """Motion-blur sample times from ``frame + open_`` to ``frame + close``.

        ``open_`` and ``close`` are exact frame offsets (``-1/4``, ``1/4`` for
        a 180-degree shutter centred on the frame). With n samples, sample k
        is at ``open_ + (close - open_) * k / (n - 1)``; one sample sits at
        ``open_``. Every sample is an exact rational frame.
        """
        o, c = exact(open_, "shutter open"), exact(close, "shutter close")
        if isinstance(samples, bool) or not isinstance(samples, int) or samples < 1:
            raise ValueError("samples must be a positive int")
        if c < o:
            raise ValueError("the shutter closes before it opens")
        if samples == 1:
            return [FrameTime(self._frame + o, self._rate)]
        span = c - o
        return [FrameTime(self._frame + o + span * rdiv(k, samples - 1), self._rate)
                for k in range(samples)]

    def __add__(self, frames: Any) -> "FrameTime":
        return FrameTime(self._frame + exact(frames, "frame offset"), self._rate)

    def __sub__(self, other: Any) -> Any:
        if isinstance(other, FrameTime):
            self._same_rate(other)
            return self._frame - other._frame
        return FrameTime(self._frame - exact(other, "frame offset"), self._rate)

    def _same_rate(self, other: "FrameTime") -> None:
        if other._rate != self._rate:
            raise ValueError(f"rates differ ({self._rate} vs {other._rate}); retime() first")

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, FrameTime):
            return NotImplemented
        return self._rate == other._rate and self._frame == other._frame

    def __hash__(self) -> int:
        return hash(("FrameTime", self._frame, self._rate))

    def __lt__(self, other: "FrameTime") -> bool:
        return self.seconds < other.seconds

    def __le__(self, other: "FrameTime") -> bool:
        return self.seconds <= other.seconds

    def __gt__(self, other: "FrameTime") -> bool:
        return self.seconds > other.seconds

    def __ge__(self, other: "FrameTime") -> bool:
        return self.seconds >= other.seconds

    def __repr__(self) -> str:
        return f"FrameTime({self._frame}, {self._rate.label()})"


class FrameRange:
    """Whole frames ``start..end`` inclusive, every ``step`` frames, at a rate."""

    __slots__ = ("start", "end", "step", "rate")
    start: int
    end: int
    step: int
    rate: FrameRate

    def __init__(self, start: int, end: int, rate: Any, step: int = 1) -> None:
        for name, v in (("start", start), ("end", end), ("step", step)):
            if isinstance(v, bool) or not isinstance(v, int):
                raise TypeError(f"{name} must be an int frame number")
        if end < start:
            raise ValueError("end is before start")
        if step < 1:
            raise ValueError("step must be >= 1")
        object.__setattr__(self, "start", start)
        object.__setattr__(self, "end", end)
        object.__setattr__(self, "step", step)
        object.__setattr__(self, "rate", FrameRate.of(rate))

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("FrameRange is immutable")

    def __len__(self) -> int:
        return (self.end - self.start) // self.step + 1

    def __iter__(self) -> Iterator[FrameTime]:
        for f in range(self.start, self.end + 1, self.step):
            yield FrameTime(f, self.rate)

    @property
    def count(self) -> int:
        return len(self)

    @property
    def duration(self) -> Rational:
        """Seconds from the start of the first frame to the end of the last."""
        return rdiv(self.end - self.start + 1, self.rate.rational)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, FrameRange) and (self.start, self.end, self.step, self.rate) == \
            (other.start, other.end, other.step, other.rate)

    def __hash__(self) -> int:
        return hash((self.start, self.end, self.step, self.rate))

    def __repr__(self) -> str:
        s = f", step={self.step}" if self.step != 1 else ""
        return f"FrameRange({self.start}, {self.end}, {self.rate.label()}{s})"


_TC = re.compile(r"^(\d{2}):(\d{2}):(\d{2})([:;.,])(\d{2,3})$")


class Timecode:
    """SMPTE timecode ``HH:MM:SS:FF`` (non-drop) or ``HH:MM:SS;FF`` (drop-frame).

    Timecode counts frames with labels; at 29.97 and 59.94 drop-frame
    timecode skips frame *labels* (never frames) so that the label stays
    close to the wall clock. Conversions are exact integer arithmetic.
    Timecode covers one day (00:00:00:00 to 23:59:59:FF); frame numbers
    outside it are refused rather than wrapped.
    """

    __slots__ = ("hours", "minutes", "seconds", "frames", "drop_frame")
    hours: int
    minutes: int
    seconds: int
    frames: int
    drop_frame: bool

    def __init__(self, hours: int, minutes: int, seconds: int, frames: int, drop_frame: bool = False) -> None:
        for name, v in (("hours", hours), ("minutes", minutes), ("seconds", seconds), ("frames", frames)):
            if isinstance(v, bool) or not isinstance(v, int) or v < 0:
                raise ValueError(f"{name} must be a non-negative int")
        if hours > 23 or minutes > 59 or seconds > 59:
            raise ValueError("timecode out of range")
        object.__setattr__(self, "hours", hours)
        object.__setattr__(self, "minutes", minutes)
        object.__setattr__(self, "seconds", seconds)
        object.__setattr__(self, "frames", frames)
        object.__setattr__(self, "drop_frame", bool(drop_frame))

    def __setattr__(self, name: str, value: Any) -> None:
        raise AttributeError("Timecode is immutable")

    @classmethod
    def parse(cls, text: str, rate: Any) -> "Timecode":
        """Parse ``HH:MM:SS:FF``; a ``;`` (or ``.`` / ``,``) before the frames
        means drop-frame. The frame field is checked against ``rate``."""
        m = _TC.match(text.strip())
        if not m:
            raise ValueError(f"not a timecode: {text!r}")
        hh, mm, ss, sep, ff = m.groups()
        tc = cls(int(hh), int(mm), int(ss), int(ff), sep != ":")
        tc.to_frame(rate)  # validates the frames field and drop-frame labels
        return tc

    @classmethod
    def from_frame(cls, frame: int, rate: Any, drop_frame: Optional[bool] = None) -> "Timecode":
        """The timecode of whole frame ``frame`` (0 = 00:00:00:00).

        ``drop_frame`` defaults to True at 29.97 and 59.94 and must be False
        (or None) at any other rate.
        """
        fr = _timecode_rate(rate)
        if isinstance(frame, bool) or not isinstance(frame, int):
            raise TypeError("frame must be an int")
        if frame < 0:
            raise ValueError("timecode starts at frame 0")
        nom, drop = fr.nominal, fr.drop_frames
        if drop_frame is None:
            drop_frame = drop > 0
        if drop_frame and drop == 0:
            raise ValueError(f"drop-frame timecode is not defined at {fr}")
        f = frame
        if drop_frame:
            per_10min = nom * 600 - drop * 9
            per_min = nom * 60 - drop
            tens, rem = divmod(f, per_10min)
            f += drop * 9 * tens
            if rem > drop:
                f += drop * ((rem - drop) // per_min)
        ff = f % nom
        total_s = f // nom
        hh = total_s // 3600
        if hh > 23:
            raise ValueError("frame is past 24 hours of timecode")
        return cls(hh, (total_s // 60) % 60, total_s % 60, ff, drop_frame)

    def to_frame(self, rate: Any) -> int:
        """The whole frame number this timecode names at ``rate``."""
        fr = _timecode_rate(rate)
        nom, drop = fr.nominal, fr.drop_frames
        if self.frames >= nom:
            raise ValueError(f"frames field {self.frames} is not below {nom}")
        total_min = self.hours * 60 + self.minutes
        f = (total_min * 60 + self.seconds) * nom + self.frames
        if self.drop_frame:
            if drop == 0:
                raise ValueError(f"drop-frame timecode is not defined at {fr}")
            if self.seconds == 0 and self.frames < drop and self.minutes % 10 != 0:
                raise ValueError(f"{self} does not exist in drop-frame timecode")
            f -= drop * (total_min - total_min // 10)
        return f

    def __str__(self) -> str:
        sep = ";" if self.drop_frame else ":"
        return f"{self.hours:02d}:{self.minutes:02d}:{self.seconds:02d}{sep}{self.frames:02d}"

    def __repr__(self) -> str:
        return f"Timecode({str(self)!r})"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Timecode) and str(self) == str(other)

    def __hash__(self) -> int:
        return hash(str(self))
