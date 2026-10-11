# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Loading, versions, features and the status-class mapping."""

from __future__ import annotations

import os
import subprocess
import sys
from typing import Any, Dict

import pytest
from conftest import REPO_LIB

import zxv
from zxv import _native


def test_version_and_features(vectors: Dict[str, Any]) -> None:
    assert list(zxv.abi_version()) == vectors["abi"]
    f = zxv.features()
    assert zxv.Feature.RATIONAL in f and zxv.Feature.TENSOR in f
    assert zxv.Feature.LORA not in f, "LoRA is honestly reported as not built"
    assert zxv.has_feature(zxv.Feature.LEDGER | zxv.Feature.HASH)


def test_status_names_and_classes(vectors: Dict[str, Any]) -> None:
    for s in vectors["status"]:
        assert zxv.status_name(s["code"]) == s["name"]
        assert zxv.status_class(s["code"]) == s["class"]
    assert zxv.status_class(-12345) is zxv.StatusClass.SYSTEM  # unknown negative is an error


@pytest.mark.parametrize("code,etype", [
    (-1, zxv.ZxvUsageError), (-24, zxv.FloatRejectedError), (-3, zxv.ZxvNotImplementedError),
    (-6, zxv.InsufficientFundsError), (-17, zxv.CapabilityDeniedError),
    (-19, zxv.BudgetExhaustedError), (-21, zxv.VerificationFailedError), (-15, zxv.ZxvRefusedError),
    (-14, zxv.ZxvSystemError), (-23, zxv.ZxvSystemError), (-18, zxv.ZxvFatal), (-10, zxv.ZxvFatal),
])
def test_every_status_maps_to_its_exception(code: int, etype: type) -> None:
    exc = _native.make_error(code, "detail")
    assert type(exc) is etype
    assert exc.status == code and exc.status_name == zxv.status_name(code)  # type: ignore[attr-defined]


def test_exception_hierarchy() -> None:
    assert issubclass(zxv.ZxvUsageError, ValueError)
    assert issubclass(zxv.FloatRejectedError, TypeError) and issubclass(zxv.FloatRejectedError, zxv.ZxvError)
    assert issubclass(zxv.ZxvNotImplementedError, NotImplementedError)
    assert issubclass(zxv.ZxvFatal, BaseException) and not issubclass(zxv.ZxvFatal, Exception)
    for t in (zxv.CapabilityDeniedError, zxv.BudgetExhaustedError, zxv.InsufficientFundsError,
              zxv.VerificationFailedError, zxv.ZxvNotImplementedError):
        assert issubclass(t, zxv.ZxvRefusedError)


def _run(code: str, **env: str) -> "subprocess.CompletedProcess[str]":
    e = {k: v for k, v in os.environ.items() if k != "ZXV_LIBRARY_PATH"}
    e.update(env)
    return subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, env=e, timeout=60)


def test_search_order_env_var_and_explicit_path() -> None:
    lib_name = {"darwin": "libzxv.dylib", "win32": "zxv.dll"}.get(sys.platform, "libzxv.so")
    lib_file = str(REPO_LIB / lib_name)
    r = _run("import zxv; print(zxv.abi_version(), zxv.library_path())", ZXV_LIBRARY_PATH=lib_file)
    assert r.returncode == 0, r.stderr
    assert lib_file in r.stdout
    r = _run(f"import zxv; zxv.load_library({str(REPO_LIB)!r}); print(zxv.library_path())",
             ZXV_LIBRARY_PATH="/nonexistent/libzxv.so")
    assert r.returncode == 0, r.stderr
    assert str(REPO_LIB) in r.stdout


def test_missing_library_is_a_clear_import_error() -> None:
    r = _run("import zxv\ntry:\n    zxv.load_library('/nonexistent/libzxv.so')\n"
             "except zxv.ZxvLibraryError as e:\n    print('OK', e)")
    assert r.stdout.startswith("OK") and "libzxv not found" in r.stdout


def test_pure_parts_work_without_the_library() -> None:
    code = ("import zxv\n"
            "f = zxv.Ubh168Frame.build(3, 2, 1, (1, 2, 3), (4, 5))\n"
            "assert zxv.Ubh168Frame.from_bytes(f.to_bytes()) == f\n"
            "assert (zxv.Truth.BOTH & zxv.Truth.TRUE) is zxv.Truth.BOTH\n"
            "from zxv import _native\nassert _native.library_path() is None\nprint('pure ok')")
    r = _run(code, ZXV_LIBRARY_PATH="/nonexistent")
    assert r.returncode == 0, r.stderr
    assert "pure ok" in r.stdout
