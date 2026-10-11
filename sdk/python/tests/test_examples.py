# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The example programs run to completion."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


@pytest.mark.parametrize("name", ["quickstart.py", "orchestrate_sessions.py", "async_pipeline.py"])
def test_example_runs(name: str) -> None:
    r = subprocess.run([sys.executable, str(EXAMPLES / name)], capture_output=True, text=True,
                       env=dict(os.environ), timeout=120)
    assert r.returncode == 0, r.stderr
    if name == "orchestrate_sessions.py":
        assert "bit-identical: True" in r.stdout and "INVALID" not in r.stdout
