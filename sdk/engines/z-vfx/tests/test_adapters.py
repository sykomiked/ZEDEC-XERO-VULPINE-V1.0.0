# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The Maya and Houdini adapters, against FAKE ``maya.cmds`` and ``hou``
modules (tests/fakes). This shows what the adapters send; it does not show
that Maya or Houdini accept it. They were not run inside either DCC."""

from __future__ import annotations

import importlib
import sys
from typing import Any

import pytest
from zxv import Rational

from zxv_vfx import FILM, FILM_NTSC, NTSC, PAL, FrameRange, FrameRate, FrameTime, Matrix4, SeedScheme
from zxv_vfx._edge import DccUnavailableError, lossy_float


def test_lazy_imports() -> None:
    for name in ("maya", "maya.cmds", "hou"):
        sys.modules.pop(name, None)
    m = importlib.import_module("zxv_vfx.maya")
    h = importlib.import_module("zxv_vfx.houdini")
    assert "maya.cmds" not in sys.modules and "hou" not in sys.modules
    # pure helpers work without Maya
    assert m.maya_time_unit(FILM) == "film"
    with pytest.raises(DccUnavailableError):
        m.set_scene_rate(FILM)
    with pytest.raises(DccUnavailableError):
        h.set_scene_rate(FILM)
    assert issubclass(DccUnavailableError, ImportError)


def test_lossy_float_is_nearest() -> None:
    assert lossy_float(Rational(1, 3)) == 1 / 3
    assert lossy_float(Rational(24000, 1001)) == 24000 / 1001
    assert lossy_float(7) == 7.0 and isinstance(lossy_float(7), float)
    assert lossy_float("0.1") == 0.1


# ---------------------------------------------------------------- Maya

@pytest.mark.parametrize("rate,unit", [
    (FrameRate(15), "game"), (FILM, "film"), (PAL, "pal"), (FrameRate(30), "ntsc"),
    (FrameRate(48), "show"), (FrameRate(50), "palf"), (FrameRate(60), "ntscf"),
    (FILM_NTSC, "23.976fps"), (NTSC, "29.97fps"), (FrameRate(48000, 1001), "47.952fps"),
    (FrameRate(60000, 1001), "59.94fps"), (FrameRate(120), "120fps"), (FrameRate(12), "12fps"),
])
def test_maya_units(rate: FrameRate, unit: str) -> None:
    from zxv_vfx import maya as m

    assert m.maya_time_unit(rate) == unit
    assert m.rate_from_maya_unit(unit) == rate


def test_maya_unit_refusals() -> None:
    from zxv_vfx import maya as m

    with pytest.raises(ValueError):
        m.maya_time_unit(FrameRate(25, 2))
    with pytest.raises(ValueError):
        m.rate_from_maya_unit("23.976")
    assert m.rate_from_maya_unit("29.97df") == NTSC


def test_maya_writes(fake_maya: Any) -> None:
    from zxv_vfx import maya as m

    assert m.set_scene_rate(FILM_NTSC) == "23.976fps"
    assert fake_maya.state["time_unit"] == "23.976fps"
    assert m.scene_rate() == FILM_NTSC
    assert m.set_playback_range(FrameRange(1001, 1100, FILM_NTSC)) == (1001.0, 1100.0)
    call = fake_maya.calls[-1]
    assert call[0] == "playbackOptions" and call[2]["minTime"] == 1001.0 and call[2]["animationEndTime"] == 1100.0
    assert m.set_current_time(FrameTime(Rational(2003, 2), FILM_NTSC)) == 1001.5
    with pytest.raises(m.RateMismatchError):
        m.set_current_time(FrameTime(1001, FILM))  # 24 is not the scene's 23.976
    with pytest.raises(m.RateMismatchError):
        m.set_playback_range(FrameRange(1, 2, FILM))

    mat = Matrix4.from_quaternion(2, 0, 0, 1) @ Matrix4.translate(1, "1/3", 0)
    vals = m.set_world_matrix("|cam1", mat)
    assert vals[0] == 0.6 and vals[1] == 0.8 and vals[12] == 1.0 and vals[13] == 1 / 3
    assert fake_maya.calls[-1][0] == "xform" and fake_maya.calls[-1][2]["worldSpace"] is True

    keys = [(FrameTime(1001, FILM_NTSC), 0), (FrameTime(1013, FILM_NTSC), "1/3"),
            (FrameTime(Rational(2051, 2), FILM_NTSC), Rational(-5, 4))]
    assert m.set_keyframes("ball", "translateY", keys) == 3
    k = [c for c in fake_maya.calls if c[0] == "setKeyframe"]
    assert [(c[2]["time"], c[2]["value"]) for c in k] == [(1001.0, 0.0), (1013.0, 1 / 3), (1025.5, -1.25)]
    with pytest.raises(m.RateMismatchError):
        m.set_keyframes("ball", "translateY", [(FrameTime(1, PAL), 1)])

    seed = SeedScheme("showA/sh010").seed_int("fx/debris")
    m.set_int_attr("debrisSim", "seed", seed)
    assert fake_maya.calls[-1] == ("setAttr", ("debrisSim.seed", seed), {})
    with pytest.raises(TypeError):
        m.set_int_attr("debrisSim", "seed", 1.0)  # type: ignore[arg-type]


def test_maya_scene_rate_unknown(fake_maya: Any) -> None:
    from zxv_vfx import maya as m

    fake_maya.state["time_unit"] = "mystery"
    with pytest.raises(ValueError):
        m.scene_rate()


# ---------------------------------------------------------------- Houdini

def test_houdini_writes(fake_hou: Any) -> None:
    from zxv_vfx import houdini as h

    assert h.set_scene_rate(FILM_NTSC) == 24000 / 1001
    assert fake_hou.calls[-1] == ("setFps", (24000 / 1001,))
    assert h.set_playback_range(FrameRange(1001, 1240, FILM)) == (1001.0, 1240.0)
    assert ("playbar.setFrameRange", (1001.0, 1240.0)) in fake_hou.calls
    assert ("playbar.setPlaybackRange", (1001.0, 1240.0)) in fake_hou.calls
    assert h.set_frame(FrameTime(Rational(4005, 4), FILM)) == 1001.25

    geo = fake_hou.add_node("/obj/debris", ["seed", "ty"])
    mat = Matrix4.quarter_turns("y", 1) @ Matrix4.translate(5, 0, "-2.5")
    vals = h.set_world_transform("/obj/debris", mat)
    assert geo.world is not None and geo.world.rows[3] == (5.0, 0.0, -2.5, 1.0)
    assert vals == [float(v.numerator) / v.denominator for v in mat.flat()]

    n = h.set_keyframes("/obj/debris", "ty", [(FrameTime(1001, FILM), 0), (FrameTime(1002, FILM), "0.5")])
    assert n == 2 and [(k.frame, k.value) for k in geo.parm("ty").keys] == [(1001.0, 0.0), (1002.0, 0.5)]
    h.set_int_parm("/obj/debris", "seed", 12345)
    assert geo.parm("seed").value == 12345
    with pytest.raises(LookupError):
        h.set_int_parm("/obj/nothing", "seed", 1)
    with pytest.raises(LookupError):
        h.set_keyframes("/obj/debris", "tz", [])
    with pytest.raises(TypeError):
        h.set_int_parm("/obj/debris", "seed", True)


def test_core_never_reads_floats_back(fake_hou: Any, fake_maya: Any) -> None:
    # the floats written cannot come back into the core
    from zxv import FloatRejectedError

    from zxv_vfx import houdini as h

    fps = h.set_scene_rate(NTSC)
    with pytest.raises(FloatRejectedError):
        FrameRate.of(fps)
    with pytest.raises(FloatRejectedError):
        FrameTime(fps, NTSC)
