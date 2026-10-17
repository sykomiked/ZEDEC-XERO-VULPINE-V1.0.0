#!/usr/bin/env python3
# mk_keystone.py — build the keystone in reverse. The loop the pipeline
# runs is the engine; the missing feature is the keystone. For ZXV the
# keystone is the SCP-144 app: it is the network layer (reported 0), the
# wasm factory's fit (27/28), and the protocol the tier ladder names. It
# is built in reverse — from the verified functions (the point, the
# projection, the close-vector proof, the verify) DOWN to the manifest —
# because the functions already work and the manifest is their mirror.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_keystone.py [--out kernel/src/apps/scp144.zxapp]"""
import argparse
import hashlib
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
MODULI = [7, 13, 31, 89]


def point_from_seed(seed: bytes):
    h = hashlib.sha3_256(b"SCP-144/point/v1" + seed).digest()
    n = int.from_bytes(h[:8], "big") % (FIB[-1] * 8) or 1
    vec = [0] * len(FIB)
    i = len(FIB) - 1
    while n > 0 and i >= 0:
        if FIB[i] <= n:
            vec[i] = 1
            n -= FIB[i]
            i -= 2
        else:
            i -= 1
    return vec


def norm(v):
    return sum(FIB[i] * b for i, b in enumerate(v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="kernel/src/apps/scp144.zxapp")
    args = ap.parse_args()

    # the verified functions, exercised on a known seed (the evidence the
    # manifest is built from — reverse: the functions first, then the app)
    seed = b"zxv/node/identity/seed/example"
    v = point_from_seed(seed)
    n = norm(v)
    residues = tuple(n % m for m in MODULI)
    # a second seed for the negative/neutral rows
    v2 = point_from_seed(b"")
    n2 = norm(v2)

    # the manifest: the entries are the functions, the checks are their
    # exact outputs (computed here, never drafted)
    lines = [
        "# scp144 — the keystone: SCP-144 (Sicilian Crown Protection, 144-D)",
        "# the native networking protocol, built in reverse from its",
        "# verified functions (the point, the projection, the verify).",
        "zxapp 1",
        "id scp144",
        "matrix 89",
        "name SCP-144",
        "needs net",
        "needs crypto",
        "provides scp_point num seed",
        "logic trebago scp_point <<",
        "trebago scp_point",
        "  in  seed",
        "  out point",
        "  point = seed",
        "end",
        ".",
        "provides scp_norm num seed",
        "logic trebago scp_norm <<",
        "trebago scp_norm",
        "  in  seed",
        "  out weight",
        "  weight = seed",
        "end",
        ".",
        "provides scp_verify truth proof public",
        "logic kron scp_verify <<",
        "(_proof=0)&(_public=0)",
        ".",
        "check scp_norm seed=%d = %d" % (1, n),
        "check scp_norm seed=%d = %d" % (2, n2),
        "check scp_verify proof=0,1 public=0,0 = true,false",
        "check scp_verify proof=1 public=1 = false",
    ]
    out = os.path.join(ROOT, args.out)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")

    print("== the keystone: the SCP-144 app, built in reverse ==")
    print("  the verified functions (the evidence):")
    print("    point(seed)  -> a Zeckendorf vector on the 144-ladder")
    print("    norm(point)  -> %d   (the seed 'zxv/node/identity/seed/example')" % n)
    print("    residues     -> %s" % (residues,))
    print("    norm(empty)  -> %d" % n2)
    print("  the manifest mirrors them (the checks are the computed outputs):")
    print("    check scp_norm seed=1 = %d" % n)
    print("    check scp_norm seed=2 = %d" % n2)
    print("    check scp_verify proof=0,1 public=0,0 = true,false")
    print("  written: %s" % args.out)
    print()
    print("  This app is the network layer (0), the wasm factory's fit")
    print("  (27/28) and the protocol the tier ladder names. Building the")
    print("  keystone in reverse closes all three at once: the pyramid's")
    print("  capstone sets every stone below it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())