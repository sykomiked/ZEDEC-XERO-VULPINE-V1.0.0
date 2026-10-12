# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Fixtures: the real libzxv and Python SDK, and the fake DCC modules."""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any, Iterator

import pytest

HERE = Path(__file__).resolve().parent
KIT = HERE.parent
SDK = KIT.parents[1]  # .../sdk
FAKES = HERE / "fakes"

# Test against the in-tree libzxv build unless the developer points elsewhere.
os.environ.setdefault("ZXV_LIBRARY_PATH", str(SDK / "libzxv" / "build"))
# Use the in-tree package and Python SDK when they are not installed.
for p in (KIT / "src", SDK / "python" / "src"):
    if str(p) not in sys.path:
        sys.path.append(str(p))

import zxv  # noqa: E402


@pytest.fixture
def cap() -> Iterator[zxv.Capability]:
    with zxv.Context() as ctx:
        yield ctx.root_capability()


def _drop(prefixes: tuple) -> None:  # type: ignore[type-arg]
    for name in list(sys.modules):
        if name in prefixes or any(name.startswith(p + ".") for p in prefixes):
            del sys.modules[name]


@pytest.fixture
def fake_maya(monkeypatch: pytest.MonkeyPatch) -> Iterator[Any]:
    """The fake ``maya.cmds`` on sys.path for one test."""
    monkeypatch.syspath_prepend(str(FAKES))
    _drop(("maya",))
    import maya.cmds as cmds  # type: ignore[import-not-found]

    cmds.reset()
    yield cmds
    _drop(("maya",))


@pytest.fixture
def fake_hou(monkeypatch: pytest.MonkeyPatch) -> Iterator[Any]:
    """The fake ``hou`` on sys.path for one test."""
    monkeypatch.syspath_prepend(str(FAKES))
    _drop(("hou",))
    import hou  # type: ignore[import-not-found]

    hou.reset()
    yield hou
    _drop(("hou",))
