#!/usr/bin/env python3
# lattice_completion.py — the completion measure. The system is not "N of
# 407 apps": it is the 144-dimensional matrix, and it is complete when
# every cell of the matrix is filled by a vertically integrated
# application. This reports the lattice's completion by layer, by feature
# and by the vertical chain, so the honest question is always "how much
# of the matrix is filled", never "how many apps exist".
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: lattice_completion.py [--json out.json]"""
import argparse
import csv
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
ANCHORS = [8, 144]

# the vertical chain every cell must satisfy to count as integrated
LAYERS = ["data", "logic", "security", "network", "interface", "ai"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    # the matrix: every app's slot is a lattice cell
    g = "/tmp/lc_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    graph = json.load(open(g))
    order = graph.get("order", [])

    # the passed apps (the H100 state), if available locally
    passed = set()
    st = os.path.join(ROOT, "tools/agent_loop/pipeline.state")
    if os.path.exists(st):
        passed = set(x.strip() for x in open(st) if x.strip())

    # the built manifests (the originals)
    built = set()
    ap_dir = os.path.join(ROOT, "kernel/src/apps")
    if os.path.isdir(ap_dir):
        for f in os.listdir(ap_dir):
            if f.endswith(".zxapp"):
                built.add(f[:-6])

    # the cells: the graph's apps plus the built originals
    cells = list(dict.fromkeys(order + sorted(built)))
    total_cells = 144          # the matrix's dimensions

    # the layer coverage: which of the six chain layers each cell reaches
    # (from the app's language matrix rows, the proxy for its integration)
    by_layer = {L: 0 for L in LAYERS}
    rows = {}
    csvp = os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")
    if os.path.exists(csvp):
        for r in csv.DictReader(open(csvp)):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    integrated = 0
    for c in cells:
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

    # the anchors: how many cells sit on a self-dual dimension
    on_anchor = sum(1 for i, c in enumerate(cells) if (i + 1) in ANCHORS)

    report = {
        "the matrix": total_cells,
        "cells named": len(cells),
        "cells passed": len(passed),
        "cells built": len(built),
        "cells vertically integrated": integrated,
        "by layer": by_layer,
        "cells on a self-dual anchor": on_anchor,
        "the anchors": ANCHORS,
    }
    print("== the lattice completion (the 144-dimensional matrix) ==")
    print("  the matrix:                 %d dimensions" % total_cells)
    print("  cells named (apps):         %d" % len(cells))
    print("  cells built:                %d" % len(built))
    print("  cells passed:               %d" % len(passed))
    print("  cells vertically integrated: %d  (all six chain layers)" % integrated)
    print("  cells on a self-dual anchor: %d  (the dims %s)" % (on_anchor, ANCHORS))
    print("  by chain layer:")
    for L in LAYERS:
        print("    %-10s %d" % (L, by_layer[L]))
    pct = (len(passed) / max(len(cells), 1)) * 100
    print("  the lattice's completion:   %.1f%% of the named cells pass" % pct)
    print("  (the honest measure is the matrix's fill, not the app count)")
    if args.json:
        json.dump(report, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())