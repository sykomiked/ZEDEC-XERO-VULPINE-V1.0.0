# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The Houdini adapter: write exact ``zxv_vfx`` values into a Houdini session.

``hou`` is imported lazily, inside each call, so this module imports
anywhere and only the calls need Houdini (hython or the Houdini UI).

THE EDGE IS LOSSY AND ONE-WAY. Houdini's frame rate, frames, matrices and
key values are doubles, so every value written here goes through
:func:`zxv_vfx._edge.lossy_float` and may be rounded. This adapter reads
nothing back: ``hou.fps()`` is a float, so the exact rate lives in the
pipeline (your shot record), and :func:`set_scene_rate` writes it to
Houdini, not the other way round.

Not tested inside Houdini: the tests run against a fake ``hou`` that
records calls. See the README.
"""

from __future__ import annotations

from typing import Any, Iterable, List, Tuple

from ._edge import DccUnavailableError, lossy_float, lossy_floats
from .frametime import FrameRange, FrameRate, FrameTime
from .transform import Matrix4

__all__ = ["set_scene_rate", "set_playback_range", "set_frame", "set_world_transform",
           "set_keyframes", "set_int_parm"]


def _hou() -> Any:
    try:
        import hou  # type: ignore[import-not-found]
    except ImportError as e:
        raise DccUnavailableError("hou is not importable: run this inside Houdini "
                                  "(hython or the Houdini UI)") from e
    return hou


def _node(hou: Any, path: str) -> Any:
    node = hou.node(path)
    if node is None:
        raise LookupError(f"no Houdini node at {path!r}")
    return node


def _parm(node: Any, name: str) -> Any:
    parm = node.parm(name)
    if parm is None:
        raise LookupError(f"node {node.path()} has no parameter {name!r}")
    return parm


def set_scene_rate(rate: Any) -> float:
    """``hou.setFps`` (LOSSY: 24000/1001 becomes the double nearest to it).
    Returns the float written."""
    fps = lossy_float(FrameRate.of(rate).rational)
    _hou().setFps(fps)
    return fps


def set_playback_range(frames: FrameRange) -> Tuple[float, float]:
    """The global frame range and the playback range (LOSSY). Returns the floats written."""
    hou = _hou()
    lo, hi = lossy_float(frames.start), lossy_float(frames.end)
    hou.playbar.setFrameRange(lo, hi)
    hou.playbar.setPlaybackRange(lo, hi)
    return lo, hi


def set_frame(t: FrameTime) -> float:
    """``hou.setFrame`` (LOSSY for subframes). Returns the float written."""
    f = lossy_float(t.frame)
    _hou().setFrame(f)
    return f


def set_world_transform(node_path: str, m: Matrix4) -> List[float]:
    """``setWorldTransform(hou.Matrix4(...))`` on an object node (LOSSY).
    Houdini, like this kit, uses row vectors with translation in the last row."""
    hou = _hou()
    node = _node(hou, node_path)
    values = lossy_floats(m.flat())
    node.setWorldTransform(hou.Matrix4(tuple(tuple(values[4 * i:4 * i + 4]) for i in range(4))))
    return values


def set_keyframes(node_path: str, parm_name: str, keys: Iterable[Tuple[FrameTime, Any]]) -> int:
    """A ``hou.Keyframe`` per (frame time, exact value) on one parameter
    (LOSSY). Returns the number of keys written."""
    hou = _hou()
    parm = _parm(_node(hou, node_path), parm_name)
    n = 0
    for t, value in keys:
        key = hou.Keyframe()
        key.setFrame(lossy_float(t.frame))
        key.setValue(lossy_float(value))
        parm.setKeyframe(key)
        n += 1
    return n


def set_int_parm(node_path: str, parm_name: str, value: int) -> None:
    """Set an int parameter (exact: e.g. a seed from :meth:`SeedScheme.seed_int`)."""
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("value must be an int")
    hou = _hou()
    _parm(_node(hou, node_path), parm_name).set(value)
