# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The cffi declarations must match zxv_api.h exactly (catches drift)."""

from __future__ import annotations

import re
from typing import Dict

from conftest import HEADER

from zxv._cdef import CDEF
from zxv._native import ffi, lib


def _decls(text: str, with_macro: bool) -> Dict[str, str]:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    pat = r"ZXV_API\s+([^;]*?\);)" if with_macro else r"^\s*((?:const\s+)?[a-z_0-9]+\s*\**\s*zxv_[a-z0-9_]+\s*\([^;]*?\);)"
    out = {}
    for d in re.findall(pat, text, flags=re.S | re.M):
        d = re.sub(r"\s+", " ", d).replace("( ", "(").strip()
        name = re.search(r"(zxv_[a-z0-9_]+)\s*\(", d)
        assert name
        out[name.group(1)] = d
    return out


def test_every_header_function_is_declared_identically() -> None:
    header = _decls(HEADER.read_text(encoding="utf-8"), True)
    cdef = _decls(CDEF, False)
    assert len(header) >= 158
    missing = sorted(set(header) - set(cdef))
    extra = sorted(set(cdef) - set(header))
    assert not missing, f"functions in zxv_api.h but not in the cdef: {missing}"
    assert not extra, f"functions in the cdef but not in zxv_api.h: {extra}"
    changed = {n: (header[n], cdef[n]) for n in header if header[n] != cdef[n]}
    assert not changed, f"signatures differ: {changed}"


def test_every_declared_function_resolves_in_the_library() -> None:
    L = lib()
    for name in _decls(CDEF, False):
        assert getattr(L, name) is not None, name


def test_abi_constants_match_header() -> None:
    import zxv

    text = HEADER.read_text(encoding="utf-8")
    defines = dict(re.findall(r"#define\s+(ZXV_[A-Z0-9_]+)\s+\(?(-?(?:0x)?[0-9A-Fa-f]+)u?\)?", text))
    for flag in zxv.Feature:
        if flag.name not in ("NONE",):
            assert int(defines[f"ZXV_FEAT_{flag.name}"], 0) == flag.value, flag
    for c in zxv.Cap:
        if c.name not in ("NONE", "ALL"):
            m = re.search(rf"#define ZXV_CAP_{c.name}\s+\(UINT64_C\(1\) << (\d+)\)", text)
            assert m and (1 << int(m.group(1))) == c.value, c
    assert int(defines["ZXV_PHASE_CYCLE"]) == zxv.PHASE_CYCLE
    assert ffi.sizeof("int64_t") == 8
