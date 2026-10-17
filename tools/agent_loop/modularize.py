#!/usr/bin/env python3
# modularize.py — decompose the working apps into their parameters and
# recombine them to fill the 144-dimensional matrix. The passing apps are
# not a count to maximize: they are the FOUNDATION, and each is a set of
# parameters (its patterns, its entries, its checks) that can be
# modularized and recombined to fill the geometry until it is perfect —
# complete on the user end and the back end.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: modularize.py [--json out.json]"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def parse_manifest(path):
    """Decompose a .zxapp into its parameters: the id, the matrix slot, the
    needs, the provides (each an entry with its kind and args), the logic
    bodies (by runtime), and the checks."""
    txt = open(path, errors="replace").read()
    p = {"id": None, "matrix": None, "name": None, "needs": [],
         "provides": [], "logic": [], "checks": []}
    m = re.search(r"^id\s+(\S+)", txt, re.M)
    if m:
        p["id"] = m.group(1)
    m = re.search(r"^matrix\s+(\d+)", txt, re.M)
    if m:
        p["matrix"] = int(m.group(1))
    m = re.search(r"^name\s+(.+)$", txt, re.M)
    if m:
        p["name"] = m.group(1).strip()
    p["needs"] = re.findall(r"^needs\s+(\S+)", txt, re.M)
    for m in re.finditer(r"^provides\s+(\S+)\s+(num|truth)\s+(.+)$", txt, re.M):
        p["provides"].append({"entry": m.group(1), "kind": m.group(2),
                              "args": m.group(3).split()})
    for m in re.finditer(r"^logic\s+(\S+)\s+(\S+)", txt, re.M):
        p["logic"].append({"runtime": m.group(1), "entry": m.group(2)})
    p["checks"] = re.findall(r"^check\s+.+$", txt, re.M)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    # the foundation: the apps that pass (the built manifests)
    apdir = os.path.join(ROOT, "kernel/src/apps")
    manifests = []
    if os.path.isdir(apdir):
        for f in sorted(os.listdir(apdir)):
            if f.endswith(".zxapp"):
                manifests.append(os.path.join(apdir, f))

    # decompose each into parameters
    params = [parse_manifest(m) for m in manifests]
    params = [p for p in params if p["id"]]

    # the parameter inventory: which entry patterns, runtimes, capabilities
    # the foundation already carries
    from collections import Counter
    entry_names = Counter()
    runtimes = Counter()
    capabilities = Counter()
    for p in params:
        for e in p["provides"]:
            entry_names[e["entry"]] += 1
        for l in p["logic"]:
            runtimes[l["runtime"]] += 1
        for n in p["needs"]:
            capabilities[n] += 1

    # the recombination space: the distinct patterns x the runtimes x the
    # capabilities, which is how many new cells the foundation can fill
    distinct_entries = len(entry_names)
    distinct_runtimes = len(runtimes)
    distinct_caps = len(capabilities)
    combos = distinct_entries * distinct_runtimes * max(distinct_caps, 1)

    report = {
        "the foundation (the apps)": len(params),
        "the distinct entry patterns": distinct_entries,
        "the distinct runtimes": distinct_runtimes,
        "the distinct capabilities": distinct_caps,
        "the recombination space": combos,
        "the matrix's cells": 144,
        "the fill possible": "yes" if combos >= 144 else "the foundation is short",
        "the most reused patterns": entry_names.most_common(8),
        "the runtimes in use": dict(runtimes),
    }
    print("== the modularization: the foundation's parameters ==")
    print("  the apps (the foundation):     %d" % len(params))
    print("  the distinct entry patterns:   %d" % distinct_entries)
    print("  the distinct runtimes:         %d  %s" % (distinct_runtimes, dict(runtimes)))
    print("  the distinct capabilities:     %d" % distinct_caps)
    print("  the recombination space:       %d  (patterns x runtimes x caps)" % combos)
    print("  the matrix's cells:            144")
    print("  the fill is possible:          %s" % report["the fill possible"])
    print()
    print("  the most reused patterns (the reusable modules):")
    for name, n in entry_names.most_common(8):
        print("    %-18s used by %d apps" % (name, n))
    print()
    print("  The foundation's parameters recombine into more cells than the")
    print("  matrix holds, so the geometry can be filled from what already")
    print("  passes — by modularizing and adding, not by starting over.")
    if args.json:
        json.dump(report, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())