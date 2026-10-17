#!/usr/bin/env python3
# lattice_targets.py — the completeness measure defined BY the lattice,
# not by an arbitrary ratio. The 144-dimensional matrix sets the
# parameters: the cells (the dimensions), the layers (the vertical
# chain), and the cross-references (cell x layer). Completeness is the
# fill of that matrix with full vertical integration; the test-to-code
# ratio is an OUTCOME, never a target.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: lattice_targets.py [--json out.json]"""
import argparse
import csv
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the lattice's parameters — derived from the 144-dimensional matrix, not chosen
FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
ANCHORS = [8, 144]                      # the self-dual dimensions (F6, F12)
MILESTONES = [5, 8, 13, 21, 34, 55, 89, 144]

# the vertical chain: every cell must reach all of these to be integrated
LAYERS = ["data", "logic", "security", "network", "interface", "ai"]

# the lattice's own targets (each is a parameter of the matrix)
def targets():
    cells = 144                          # the matrix's dimensions
    return {
        "cells": cells,                  # one application per dimension
        "layers": len(LAYERS),           # the vertical chain
        "cross_references": cells * len(LAYERS),   # cell x layer
        "anchors": ANCHORS,              # the self-dual cells (must be filled)
        "milestones": MILESTONES,        # the Fibonacci gates that must pass
        # the ratio is NOT a target: it is reported, never set
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    t = targets()

    # the current fill
    g = "/tmp/lt_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    order = json.load(open(g)).get("order", [])
    built = 0
    ad = os.path.join(ROOT, "kernel/src/apps")
    if os.path.isdir(ad):
        built = len([f for f in os.listdir(ad) if f.endswith(".zxapp")])
    passed = 0
    st = os.path.join(ROOT, "tools/agent_loop/pipeline.state")
    if os.path.exists(st):
        passed = len([x for x in open(st) if x.strip()])
    cells_named = len(order) + built

    # the layer fill (from the language matrix)
    by_layer = {L: 0 for L in LAYERS}
    rows = {}
    csvp = os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")
    if os.path.exists(csvp):
        for r in csv.DictReader(open(csvp)):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    integrated = 0
    for c in order + [""] * 0:
        rs = rows.get(c.lower(), [])
        feats = {r.get("owns", "").strip().lower() for r in rs}
        has = {
            "data": bool(feats & {"records", "view", "model"}),
            "logic": bool(feats & {"numeric", "arrays", "limits"}),
            "security": bool(feats & {"machine", "crypto", "security"}),
            "network": bool(feats & {"net", "transport", "mesh"}),
            "interface": bool(feats & {"view", "limits"}),
            "ai": bool(feats & {"machine", "logic"}),
        }
        for L in LAYERS:
            if has[L]:
                by_layer[L] += 1
        if all(has.values()):
            integrated += 1

    # the ratio: an OUTCOME (reported, never targeted)
    code = 0
    for p in ("kernel/src/apps", "kernel/src/lpres", "kernel/src/tribute"):
        d = os.path.join(ROOT, p)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.endswith((".c", ".h", ".zxapp")):
                    code += sum(1 for _ in open(os.path.join(d, f), errors="replace"))
    tests = 0
    for p in ("tools/server-tasks/tests", "tools/server-tasks/lang-tests",
              "tools/server-tasks/truth-tables", "tools/server-tasks/lattice"):
        d = os.path.join(ROOT, p)
        if os.path.isdir(d):
            for f in os.listdir(d):
                tests += sum(1 for _ in open(os.path.join(d, f), errors="replace"))

    print("== the lattice's parameters (the completeness measure) ==")
    print("  the matrix:            %d cells (the dimensions)" % t["cells"])
    print("  the vertical chain:    %d layers" % t["layers"])
    print("  the cross-references:  %d  (cell x layer)" % t["cross_references"])
    print("  the self-dual anchors: %s  (must be filled)" % t["anchors"])
    print("  the Fibonacci gates:   %s" % t["milestones"])
    print()
    print("== the current fill ==")
    print("  cells named:           %d / %d" % (cells_named, t["cells"]))
    print("  cells built:           %d" % built)
    print("  cells passed:          %d" % passed)
    print("  cells integrated:      %d / %d  (all %d layers)"
          % (integrated, t["cells"], t["layers"]))
    print("  the layer fill:")
    for L in LAYERS:
        print("    %-10s %d / %d" % (L, by_layer[L], cells_named))
    print()
    print("  the test-to-code ratio: %.1f : 1   (an OUTCOME, not a target)"
          % (tests / max(code, 1)))
    print("  completeness = the matrix's fill x the vertical integration,")
    print("  cross-referenced with the lattice. The ratio follows; it is not set.")
    if args.json:
        json.dump({"targets": t, "cells_named": cells_named, "built": built,
                   "passed": passed, "integrated": integrated,
                   "by_layer": by_layer, "ratio": tests / max(code, 1)},
                  open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())