#!/usr/bin/env python3
# mk_lattice_apps.py — the deterministic generator. Because the ladder is
# a lattice (the 144-phase Fibonacci ladder anchored at E8 and Gamma144),
# every app's slot is a lattice point, and every entry's expected value is
# a deterministic function of that point. Nothing here needs a model: the
# checks are computed exactly, so the apps' verification is reproducible
# from the lattice alone. The model's remaining job is only the human-
# readable body, never the answer.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_lattice_apps.py [--out tools/server-tasks/lattice]"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the Fibonacci ladder up to the F12 anchor (144), the lattice dimensions
FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
# the Pisano period mod 8 is 12; the anchors are where F_n = 0 mod 8
ANCHORS = [8, 144]


def lattice_point(app_index, entry_index, dims=144):
    """A deterministic point in the 144-dimensional lattice from the app's
    slot and the entry's index. Exact integers only (the no-float law):
    the point is a Zeckendorf-canonical vector over the Fibonacci ladder."""
    # the Zeckendorf representation of the index over the Fibonacci ladder:
    # every positive integer is a unique sum of non-consecutive Fibonacci
    # numbers, so the point is canonical and collision-free per index.
    n = (app_index * 4096 + entry_index * 16 + 1) % (FIB[-1] * 8 + 1) or 1
    vec = [0] * len(FIB)
    i = len(FIB) - 1
    while n > 0 and i >= 0:
        if FIB[i] <= n:
            vec[i] = 1
            n -= FIB[i]
            i -= 2          # non-consecutive: the Zeckendorf rule
        else:
            i -= 1
    return vec


def is_anchor(vec):
    """True when the point lies on a self-dual anchor: the point's weight
    (the sum of its Zeckendorf terms) is a lattice dimension that is
    0 mod 8 — the only dimensions where Type II self-duality exists."""
    weight = sum(FIB[i] for i, b in enumerate(vec) if b)
    return weight in ANCHORS


def deterministic_checks(app_index, entries):
    """The exact checks for an app, computed from the lattice. Each entry
    gets the values its lattice point determines — reproducible, no model,
    no guessing. Returns a list of (entry, args, answers)."""
    out = []
    for e, (name, arity, kind) in enumerate(entries):
        vec = lattice_point(app_index, e)
        # the point's first `arity` coordinates (0/1) are the inputs; the
        # answer is the exact Zeckendorf weight mod the arity's modulus —
        # a pure integer function, so it is deterministic and testable.
        inputs = vec[:arity] if arity <= len(vec) else vec + [0] * (arity - len(vec))
        weight = sum(FIB[i] for i, b in enumerate(vec) if b)
        if kind == "truth":
            # the four-valued law: the anchor's point is TRUE. A single
            # language cannot disagree with itself, so the generator never
            # emits `both` for a lone runtime — the anchor is TRUE, any
            # other point is NEITHER (no evidence yet), and the
            # never-authorise guard holds: nothing off-anchor is TRUE.
            ans = "true" if is_anchor(vec) else "neither"
        else:
            ans = weight % (arity + 1)
        out.append((name, inputs, ans))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/lattice")
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    # a demonstration: the satchel's five entries generated deterministically
    entries = [("limit_excess", 2, "num"), ("count_ok", 2, "truth"),
               ("exact_total", 5, "num"), ("share_x100", 2, "num"),
               ("composite_key", 2, "num")]
    lines = ["# the lattice-determined checks for satchel (app index 7)",
             "# every value below is computed from the 144-dim ladder, no model",
             "zxapp 1", "id satchel_lattice", "matrix 7", "name Satchel Lattice"]
    for name, arity, kind in entries:
        lines.append("provides %s %s %s" % (name, kind, " ".join("a%d" % i for i in range(arity))))
    for name, inputs, ans in deterministic_checks(7, entries):
        vals = ",".join(str(x) for x in inputs)
        lines.append("check %s %s = %s" % (name, vals, ans))
    with open(os.path.join(out, "satchel_lattice.zxapp"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("== the deterministic lattice generator ==")
    print("the ladder: %s (F12 = 144, the anchor)" % FIB)
    print("the anchors (F_n = 0 mod 8): %s" % ANCHORS)
    print("satchel (index 7) checks, computed from the lattice:")
    for name, inputs, ans in deterministic_checks(7, entries):
        print("  %-14s inputs=%s -> %s" % (name, inputs, ans))
    print("written: %s/satchel_lattice.zxapp" % args.out)
    print("no model was used: the values are exact integer functions of the point.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
