# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Shared fixtures: the library under test, vectors.json and the engine fixtures."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Dict, Iterator

import pytest

SDK_ROOT = Path(__file__).resolve().parents[2]  # .../sdk
REPO_LIB = SDK_ROOT / "libzxv" / "build"
VECTORS = SDK_ROOT / "conformance" / "vectors.json"
FIXTURES = SDK_ROOT / "conformance" / "fixtures"
HEADER = SDK_ROOT / "libzxv" / "include" / "zxv_api.h"

# Test against the in-tree build unless the developer points elsewhere.
os.environ.setdefault("ZXV_LIBRARY_PATH", str(REPO_LIB))

import zxv  # noqa: E402


@pytest.fixture(scope="session")
def vectors() -> Dict[str, Any]:
    """The conformance answer key."""
    with open(VECTORS, encoding="utf-8") as f:
        return json.load(f)  # type: ignore[no-any-return]


@pytest.fixture(scope="session")
def fixtures_dir() -> Path:
    """Directory holding tiny-qwen2.gguf, tiny-tokenizer.gguf, prompt.txt."""
    return FIXTURES


@pytest.fixture
def ctx() -> Iterator[zxv.Context]:
    """A fresh context, closed after the test."""
    with zxv.Context() as c:
        yield c


@pytest.fixture
def cap(ctx: zxv.Context) -> zxv.Capability:
    """A root capability with every bit."""
    return ctx.root_capability()


def needs(feature: "zxv.Feature") -> Any:
    """Skip a test when the loaded library lacks a feature group."""
    return pytest.mark.skipif(not zxv.has_feature(feature), reason=f"libzxv built without {feature!r}")
