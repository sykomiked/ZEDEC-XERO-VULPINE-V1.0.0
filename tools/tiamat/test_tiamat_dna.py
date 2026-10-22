#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Tests for the TDNA v1 record codec. Run: python3 tools/tiamat/test_tiamat_dna.py"""
import copy
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import tiamat_dna as t  # noqa: E402

SIG = lambda body: bytes([0xA5]) * t.SIG_LEN  # stand-in; real records use the owner's ML-DSA-87 key


def rejects(rec, why):
    try:
        t.decode(rec)
    except t.DnaError:
        return
    raise AssertionError(f"accepted {why}")


def bad_encode(d, why):
    try:
        t.encode(d, SIG)
    except (t.DnaError, ValueError):
        return
    raise AssertionError(f"encoded {why}")


def main():
    d = t.founder_example()
    rec = t.encode(d, SIG)
    got, body, sig = t.decode(rec)
    assert sig == SIG(body) and body == rec[:-t.SIG_LEN]
    for k in d:
        assert got[k] == d[k], k
    assert got["isf_tokens_per_cycle"] == 2584
    assert t.dna_id(rec) == t.dna_id(t.encode(d, SIG)), "deterministic"
    # every truncation and one extra byte are refused
    for n in range(len(rec)):
        rejects(rec[:n], f"truncation at {n}")
    rejects(rec + b"\0", "trailing byte")
    # each single-field corruption that breaks a rule is refused
    body = bytearray(rec)
    body[0] ^= 1
    rejects(bytes(body), "bad magic")
    for field, val in [("hobbies", [40, 3, 99]), ("hobbies", [3, 3, 99]), ("hobbies", [1, 2]), ("hobbies", [1, 2, 144]),
                       ("isf_class", 1), ("cells", [])]:
        e = copy.deepcopy(d)
        e[field] = val
        bad_encode(e, f"{field}={val}")
    for tissue in [{"trunk": 0, "split": 1, "members": [0, 1]}, {"trunk": 0, "split": 1, "members": [2, 1]},
                   {"trunk": 3, "split": 1, "members": [1]}, {"trunk": 0, "split": 1, "members": []}]:
        e = copy.deepcopy(d)
        e["tissues"] = [tissue]
        bad_encode(e, f"tissue {tissue}")
    e = copy.deepcopy(d)
    e["tissues"] = [{"trunk": 0, "split": 1, "members": [1]}, {"trunk": 0, "split": 2, "members": [1, 2]}]
    bad_encode(e, "a cell in two tissues")
    # decoder catches what an encoder could have been tricked into writing
    off = 4 + 1 + 32 + 1 + 32 + 1 + 8 + 96
    raw = bytearray(rec)
    raw[off + 1:off + 6] = bytes([99, 40, 3, 0xFF, 0xFF])
    rejects(bytes(raw), "unsorted hobbies")
    print(f"TDNA v1 OK: {len(rec)} bytes ({len(rec) - t.SIG_LEN} body), id {t.dna_id(rec).hex()[:16]}")


if __name__ == "__main__":
    main()
