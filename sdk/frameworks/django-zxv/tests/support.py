# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Helpers: a throwaway runtime, so a test can halt a context without poisoning the others."""

from __future__ import annotations

from contextlib import contextmanager
from typing import Any, Iterator

from django.conf import settings

from django_zxv import runtime
from django_zxv.conf import parse_settings


@contextmanager
def isolated_runtime(zxv: Any = None) -> Iterator[runtime.ZxvRuntime]:
    """Swaps in a fresh runtime (from ``zxv`` or ``settings.ZXV``) and restores the shared one after."""
    fresh = runtime.ZxvRuntime(parse_settings(settings.ZXV if zxv is None else zxv))
    saved = runtime._runtime
    runtime._runtime = fresh
    try:
        yield fresh
    finally:
        runtime._runtime = saved
        fresh.close()
