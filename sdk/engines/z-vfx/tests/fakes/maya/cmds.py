# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""A fake ``maya.cmds`` that records calls. NOT Autodesk Maya.

It accepts the calls and flags the Maya adapter uses, keeps the little
state the adapter queries (the time unit), and checks argument types the
way Maya would take them (times, matrices and values are doubles).
"""

from typing import Any, Dict, List, Tuple

calls: List[Tuple[str, Tuple[Any, ...], Dict[str, Any]]] = []
state: Dict[str, Any] = {"time_unit": "film"}


def reset() -> None:
    calls.clear()
    state.clear()
    state["time_unit"] = "film"


def currentUnit(**kw: Any) -> Any:
    calls.append(("currentUnit", (), kw))
    if kw.get("query") or kw.get("q"):
        return state["time_unit"]
    if "time" in kw:
        assert isinstance(kw["time"], str)
        state["time_unit"] = kw["time"]
    return None


def playbackOptions(**kw: Any) -> None:
    for k in ("minTime", "maxTime", "animationStartTime", "animationEndTime"):
        if k in kw:
            assert isinstance(kw[k], float), k
    calls.append(("playbackOptions", (), kw))


def currentTime(t: Any, **kw: Any) -> None:
    assert isinstance(t, float)
    calls.append(("currentTime", (t,), kw))


def xform(node: str, **kw: Any) -> None:
    m = kw.get("matrix")
    assert m is None or (len(m) == 16 and all(isinstance(v, float) for v in m))
    calls.append(("xform", (node,), kw))


def setKeyframe(node: str, **kw: Any) -> None:
    assert isinstance(kw["time"], float) and isinstance(kw["value"], float)
    calls.append(("setKeyframe", (node,), kw))


def setAttr(plug: str, value: Any) -> None:
    calls.append(("setAttr", (plug, value), {}))
