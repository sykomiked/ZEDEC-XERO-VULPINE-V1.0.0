# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""A fake ``hou`` module that records calls. NOT SideFX Houdini.

It models only what the Houdini adapter calls: setFps, setFrame, the
playbar, nodes with parameters, Matrix4 and Keyframe, and checks argument
types the way Houdini would take them (frames, matrices and values are
doubles).
"""

from typing import Any, Dict, List, Optional, Tuple

calls: List[Tuple[str, Tuple[Any, ...]]] = []
_nodes: Dict[str, "_Node"] = {}


def reset() -> None:
    calls.clear()
    _nodes.clear()


def setFps(fps: Any) -> None:
    assert isinstance(fps, float)
    calls.append(("setFps", (fps,)))


def setFrame(frame: Any) -> None:
    assert isinstance(frame, float)
    calls.append(("setFrame", (frame,)))


class _Playbar:
    def setFrameRange(self, lo: Any, hi: Any) -> None:
        assert isinstance(lo, float) and isinstance(hi, float)
        calls.append(("playbar.setFrameRange", (lo, hi)))

    def setPlaybackRange(self, lo: Any, hi: Any) -> None:
        assert isinstance(lo, float) and isinstance(hi, float)
        calls.append(("playbar.setPlaybackRange", (lo, hi)))


playbar = _Playbar()


class Matrix4:
    def __init__(self, rows: Any) -> None:
        assert len(rows) == 4 and all(len(r) == 4 for r in rows)
        assert all(isinstance(v, float) for r in rows for v in r)
        self.rows = rows


class Keyframe:
    def __init__(self) -> None:
        self.frame: Optional[float] = None
        self.value: Optional[float] = None

    def setFrame(self, f: Any) -> None:
        assert isinstance(f, float)
        self.frame = f

    def setValue(self, v: Any) -> None:
        assert isinstance(v, float)
        self.value = v


class _Parm:
    def __init__(self, node: "_Node", name: str) -> None:
        self.node, self.name = node, name
        self.keys: List[Keyframe] = []
        self.value: Any = None

    def setKeyframe(self, k: Keyframe) -> None:
        self.keys.append(k)
        calls.append(("parm.setKeyframe", (self.node._path, self.name, k.frame, k.value)))

    def set(self, v: Any) -> None:
        self.value = v
        calls.append(("parm.set", (self.node._path, self.name, v)))


class _Node:
    def __init__(self, path: str, parms: List[str]) -> None:
        self._path = path
        self._parms = {p: _Parm(self, p) for p in parms}
        self.world: Optional[Matrix4] = None

    def path(self) -> str:
        return self._path

    def parm(self, name: str) -> Optional[_Parm]:
        return self._parms.get(name)

    def setWorldTransform(self, m: Matrix4) -> None:
        assert isinstance(m, Matrix4)
        self.world = m
        calls.append(("setWorldTransform", (self._path,)))


def add_node(path: str, parms: List[str]) -> _Node:
    n = _Node(path, parms)
    _nodes[path] = n
    return n


def node(path: str) -> Optional[_Node]:
    return _nodes.get(path)
