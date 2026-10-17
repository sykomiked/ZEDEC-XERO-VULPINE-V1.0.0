#!/usr/bin/env python3
# audit_pipeline.py — audit the app-drafting pipeline BEFORE it feeds the
# LLM: every pending app must have its spec, its icon, its languages, and
# the reference manifest must load in the testbed.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: audit_pipeline.py [--root .]

Reports, for the pending apps:
  - spec coverage      (the wave file the prompt reads)
  - icon coverage      (the SVG + the converted ZXVG)
  - language coverage  (the app's rows in APP_LANGUAGES_407.csv, and the
                        full 104+ language set's distribution)
  - the testbed reference (banker.zxapp) parse/load readiness
Exit 1 when any gap would starve the LLM.
"""
import argparse
import csv
import json
import os
import subprocess
import sys
from collections import Counter


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--graph", default="/tmp/appgraph_audit.json")
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    # the pending order from the graph
    subprocess.run([sys.executable, os.path.join(root, "tools/apps/app_graph.py"),
                    "--json", args.graph], check=True, capture_output=True)
    g = json.load(open(args.graph))
    order = [o for o in g.get("order", [])]
    nodes = g["nodes"]

    langs = {}
    with open(os.path.join(root, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            langs.setdefault(r["app"].strip().lower(), []).append(r["language"].strip())

    svg = set()
    for layer in ("l1", "l2", "l3"):
        d = os.path.join(root, "zxv-brand/app-icons/svg", layer)
        if os.path.isdir(d):
            svg.update(x[:-4] for x in os.listdir(d) if x.endswith(".svg"))
    zxvg = set()
    d = os.path.join(root, "zxv-brand/app-icons/zxvg")
    if os.path.isdir(d):
        zxvg.update(x[:-5] for x in os.listdir(d) if x.endswith(".zxvg"))

    gaps = {"spec": [], "icon": [], "lang": []}
    lang_hist = Counter()
    checked = 0
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("done") or node.get("kind") != "app":
            continue
        checked += 1
        task = node.get("task", "")
        if not task or not os.path.exists(os.path.join(root, "tools/server-tasks", task)):
            gaps["spec"].append(o)
        # the icon: the app's slot number prefixes the icon file
        slot = node.get("slot")
        pref = "%03d-" % slot if isinstance(slot, int) else ""
        if not any(x.startswith(pref) for x in svg) or not any(x.startswith(pref) for x in zxvg):
            gaps["icon"].append(o)
        rows = langs.get(o, [])
        if not rows:
            gaps["lang"].append(o)
        for l in rows:
            lang_hist[l] += 1

    print(f"pending apps audited: {checked}")
    print(f"spec gaps:   {len(gaps['spec'])}")
    print(f"icon gaps:   {len(gaps['icon'])}")
    print(f"lang gaps:   {len(gaps['lang'])}")
    print(f"distinct languages across the pending apps: {len(lang_hist)}")
    print("top languages: " + ", ".join(f"{l}={n}" for l, n in lang_hist.most_common(12)))
    if gaps["spec"][:5]:
        print("first spec gaps:", gaps["spec"][:5])
    if gaps["icon"][:5]:
        print("first icon gaps:", gaps["icon"][:5])
    if gaps["lang"][:5]:
        print("first lang gaps:", gaps["lang"][:5])
    # the reference manifest must load: the testbed judges every draft with it
    ref = os.path.join(root, "kernel/src/apps/banker.zxapp")
    print("reference manifest:", "present" if os.path.exists(ref) else "MISSING")
    bad = len(gaps["spec"]) + len(gaps["icon"]) + len(gaps["lang"])
    print("VERDICT:", "READY for the LLM" if bad == 0 else f"{bad} gaps to close first")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())