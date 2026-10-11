# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Four-valued logic: tables against vectors.json and the C library."""

from __future__ import annotations

from typing import Any, Dict

import pytest

from zxv import Truth, UncertainTruthError
from zxv._native import ffi, lib


def test_numbering_and_names(vectors: Dict[str, Any]) -> None:
    assert [t.name for t in sorted(Truth, key=lambda t: t.value)] == vectors["lpres"]["names"]
    for t in Truth:
        assert ffi.string(lib().zxv_lp_name(t.value)).decode() == t.name


def test_tables_match_vectors_and_c(vectors: Dict[str, Any]) -> None:
    lp = vectors["lpres"]
    out = ffi.new("int32_t *")
    for a in Truth:
        assert (~a).value == lp["not"][a.value]
        assert lib().zxv_lp_not(a.value, out) == 0 and out[0] == (~a).value
        for b in Truth:
            assert (a & b).value == lp["and"][a.value][b.value]
            assert (a | b).value == lp["or"][a.value][b.value]
            assert lib().zxv_lp_and(a.value, b.value, out) == 0 and out[0] == (a & b).value
            assert lib().zxv_lp_or(a.value, b.value, out) == 0 and out[0] == (a | b).value
            assert a.and_(b) is (a & b) and a.or_(b) is (a | b)


def test_from_evidence_matches_c() -> None:
    out = ffi.new("int32_t *")
    for p in range(3):
        for n in range(3):
            assert lib().zxv_lp_from_evidence(p, n, out) == 0
            assert Truth.from_evidence(p, n).value == out[0]


def test_no_silent_bool() -> None:
    for t in Truth:
        with pytest.raises(TypeError):
            bool(t)
        with pytest.raises(TypeError):
            if t:  # noqa: SIM108
                pass
    assert Truth.TRUE.is_certainly_true() and not Truth.BOTH.is_certainly_true()
    assert Truth.BOTH.to_bool(on_both=False, on_neither=False) is False
    assert Truth.NEITHER.to_bool(on_both=True, on_neither=True) is True
    assert Truth.FALSE.require_certain() is False
    with pytest.raises(UncertainTruthError):
        Truth.BOTH.require_certain()
    with pytest.raises(UncertainTruthError):
        Truth.NEITHER.require_true()
    assert Truth.of(True) is Truth.TRUE and Truth.BOTH.is_contradiction and Truth.NEITHER.is_unknown
    assert Truth.TRUE.mode == 0
