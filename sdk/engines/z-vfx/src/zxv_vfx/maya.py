# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The Maya adapter: write exact ``zxv_vfx`` values into a Maya scene.

``maya.cmds`` is imported lazily, inside each call, so this module imports
anywhere (a render node, CI, a pipeline tool) and only the calls need Maya.

THE EDGE IS LOSSY AND ONE-WAY. Maya stores times, matrices and key values
as doubles, so every value written here goes through
:func:`zxv_vfx._edge.lossy_float` and may be rounded. Nothing is read back
from Maya into the core as a float. The one value this adapter reads,
the scene's time unit, is a string (``"film"``, ``"23.976fps"``) and maps
to an exact :class:`FrameRate`.

Not tested inside Maya: the tests run against a fake ``maya.cmds`` that
records calls. See the README.
"""

from __future__ import annotations

import re
from typing import Any, Dict, Iterable, List, Tuple

from ._edge import DccUnavailableError, lossy_float, lossy_floats
from .frametime import FrameRange, FrameRate, FrameTime
from .transform import Matrix4

__all__ = ["maya_time_unit", "rate_from_maya_unit", "scene_rate", "set_scene_rate",
           "set_playback_range", "set_current_time", "set_world_matrix", "set_keyframes",
           "set_int_attr", "RateMismatchError"]

# Maya's named time units.
_NAMED: Dict[str, FrameRate] = {
    "game": FrameRate(15),
    "film": FrameRate(24),
    "pal": FrameRate(25),
    "ntsc": FrameRate(30),
    "show": FrameRate(48),
    "palf": FrameRate(50),
    "ntscf": FrameRate(60),
}
_BY_RATE = {r: n for n, r in _NAMED.items()}
_NTSC_UNITS = {
    FrameRate(24000, 1001): "23.976fps",
    FrameRate(30000, 1001): "29.97fps",
    FrameRate(48000, 1001): "47.952fps",
    FrameRate(60000, 1001): "59.94fps",
}
_UNIT_RE = re.compile(r"^(\d+)fps$")


class RateMismatchError(ValueError):
    """A frame time's rate is not the scene's rate."""


def _cmds() -> Any:
    try:
        import maya.cmds as cmds  # type: ignore[import-not-found]
    except ImportError as e:
        raise DccUnavailableError("maya.cmds is not importable: run this inside Maya "
                                  "(mayapy or the Maya UI)") from e
    return cmds


def maya_time_unit(rate: Any) -> str:
    """Maya's ``currentUnit -time`` name for an exact rate (``"film"``,
    ``"23.976fps"``, ``"120fps"``). ValueError if Maya has no unit for it."""
    fr = FrameRate.of(rate)
    if fr in _BY_RATE:
        return _BY_RATE[fr]
    if fr in _NTSC_UNITS:
        return _NTSC_UNITS[fr]
    if fr.denominator == 1:
        return f"{fr.numerator}fps"
    raise ValueError(f"Maya has no time unit for {fr}")


def rate_from_maya_unit(unit: str) -> FrameRate:
    """The exact rate of a Maya time unit name (a string, never a float)."""
    u = unit.strip()
    if u in _NAMED:
        return _NAMED[u]
    for fr, name in _NTSC_UNITS.items():
        if u == name:
            return fr
    if u == "29.97df":  # drop-frame display of 29.97
        return FrameRate(30000, 1001)
    m = _UNIT_RE.match(u)
    if m:
        return FrameRate(int(m.group(1)))
    raise ValueError(f"unknown Maya time unit {unit!r}")


def scene_rate() -> FrameRate:
    """The scene's rate, read as Maya's time-unit string."""
    return rate_from_maya_unit(str(_cmds().currentUnit(query=True, time=True)))


def set_scene_rate(rate: Any) -> str:
    """Set the scene's time unit; returns the unit name written (exact: a name)."""
    unit = maya_time_unit(rate)
    _cmds().currentUnit(time=unit)
    return unit


def _check_rate(fr: FrameRate) -> None:
    sr = scene_rate()
    if sr != fr:
        raise RateMismatchError(f"the scene runs at {sr}, the value is at {fr}; retime() first")


def set_playback_range(frames: FrameRange) -> Tuple[float, float]:
    """Set the playback and animation range (LOSSY: Maya times are doubles).
    The range's rate must be the scene's. Returns the floats written."""
    _check_rate(frames.rate)
    lo, hi = lossy_float(frames.start), lossy_float(frames.end)
    _cmds().playbackOptions(minTime=lo, maxTime=hi, animationStartTime=lo, animationEndTime=hi)
    return lo, hi


def set_current_time(t: FrameTime) -> float:
    """Go to a frame time (LOSSY for subframes). Its rate must be the scene's."""
    _check_rate(t.rate)
    f = lossy_float(t.frame)
    _cmds().currentTime(f, edit=True)
    return f


def set_world_matrix(node: str, m: Matrix4) -> List[float]:
    """``xform -worldSpace -matrix`` with the 16 entries row by row (LOSSY)."""
    values = lossy_floats(m.flat())
    _cmds().xform(node, worldSpace=True, matrix=values)
    return values


def set_keyframes(node: str, attribute: str, keys: Iterable[Tuple[FrameTime, Any]]) -> int:
    """``setKeyframe`` for each (frame time, exact value) (LOSSY). Every key's
    rate must be the scene's. Returns the number of keys written."""
    cmds = _cmds()
    sr = scene_rate()
    n = 0
    for t, value in keys:
        if t.rate != sr:
            raise RateMismatchError(f"the scene runs at {sr}, key {t} is at {t.rate}")
        cmds.setKeyframe(node, attribute=attribute, time=lossy_float(t.frame), value=lossy_float(value))
        n += 1
    return n


def set_int_attr(node: str, attribute: str, value: int) -> None:
    """``setAttr`` of an int (exact: e.g. a seed from :meth:`SeedScheme.seed_int`)."""
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("value must be an int")
    _cmds().setAttr(f"{node}.{attribute}", value)
