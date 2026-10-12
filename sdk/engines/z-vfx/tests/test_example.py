# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The example runs, and two runs print the same thing."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import zxv

import zxv_vfx

EXAMPLE = Path(__file__).resolve().parents[1] / "examples" / "plan_shot.py"


def _run() -> str:
    path = os.pathsep.join([str(Path(zxv_vfx.__file__).parents[1]), str(Path(zxv.__file__).parents[1])])
    env = {**os.environ, "PYTHONPATH": path, "PYTHONHASHSEED": "random"}
    return subprocess.run([sys.executable, str(EXAMPLE)], capture_output=True, text=True, check=True,
                          env=env).stdout


def test_plan_shot() -> None:
    a, b = _run(), _run()
    assert a == b
    assert "96 frames, 4.004 s" in a
    assert "01:00:00:00 .. 01:00:03:23" in a
    assert "1000.75, 1000.875, 1001, 1001.125, 1001.25" in a
    assert "sh0040_beauty_v2 refused: needs 240, 136 left" in a
    assert "sim finished at 21.75, 2.25 back, 138.25 left" in a
