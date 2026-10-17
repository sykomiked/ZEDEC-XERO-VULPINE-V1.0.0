#!/usr/bin/env python3
# reconcile_names.py — the naming convention, made consistent. Three
# systems name the same 407 apps: the graph (the code's ids, lowercase),
# the icon set (numbered, hyphenated) and the matrix (Capitalized). This
# tool designates the graph's id as canonical, maps every alias to it,
# and writes the one table every consumer reads: web/apps/names.tsv.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: reconcile_names.py [--write] [--json report.json]"""
import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def norm(s):
    """The comparison form: lowercase, alphanumerics only. 'Dragon-Courier',
    'dragon_courier' and 'dragoncourier' all compare equal."""
    return re.sub(r"[^a-z0-9]", "", s.lower())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    # the canonical ids: the graph's app ids
    g = "/tmp/reconcile_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    graph = json.load(open(g))
    ids = [o for o in graph.get("order", [])
           if (graph["nodes"].get("app:" + o) or graph["nodes"].get(o) or {}).get("kind") == "app"]

    # the icon set
    icons = {}
    for p in sorted(glob.glob(os.path.join(ROOT, "zxv-brand/app-icons/zxvg/*.zxvg"))):
        b = os.path.basename(p).rsplit(".", 1)[0]
        m = re.match(r"^(\d+)-(.+)$", b)
        num = int(m.group(1)) if m else 0
        name = m.group(2) if m else b
        icons[norm(name)] = {"file": os.path.basename(p), "num": num, "name": name}

    # the matrix
    matrix = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            matrix.setdefault(norm(r["app"]), r["app"].strip())

    # the mapping: canonical id -> {icon, matrix}
    rows = []
    unmatched_icons = set(icons)
    unmatched_matrix = set(matrix)
    for i in ids:
        key = norm(i)
        ic = icons.get(key)
        mx = matrix.get(key)
        if ic:
            unmatched_icons.discard(key)
        if mx:
            unmatched_matrix.discard(key)
        rows.append({
            "id": i,
            "icon": ic["file"] if ic else "",
            "icon_num": ic["num"] if ic else 0,
            "matrix": mx or "",
        })

    matched = sum(1 for r in rows if r["icon"])
    report = {
        "canonical": "the graph's app id (lowercase)",
        "apps": len(ids),
        "icons_total": len(icons),
        "icons_matched": matched,
        "icons_unmatched": len(unmatched_icons),
        "matrix_matched": len(ids) - len(unmatched_matrix),
        "matrix_unmatched": len(unmatched_matrix),
        "unmatched_icons_sample": sorted(icons[k]["file"] for k in list(unmatched_icons)[:10]),
        "unmatched_matrix_sample": sorted(matrix[k] for k in list(unmatched_matrix)[:10]),
        "rows": rows,
    }
    print("== the naming reconciliation ==")
    print("canonical:            the graph's app id")
    print("apps:                 %d" % len(ids))
    print("icons:                %d total, %d matched, %d unmatched"
          % (len(icons), matched, len(unmatched_icons)))
    print("matrix:               %d matched, %d unmatched"
          % (report["matrix_matched"], report["matrix_unmatched"]))
    if report["unmatched_icons_sample"]:
        print("unmatched icons:      %s" % report["unmatched_icons_sample"])
    if report["unmatched_matrix_sample"]:
        print("unmatched matrix:     %s" % report["unmatched_matrix_sample"])

    if args.write:
        out = os.path.join(ROOT, "web/apps")
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "names.tsv"), "w") as f:
            f.write("id\ticon\tmatrix\n")
            for r in rows:
                f.write("%s\t%s\t%s\n" % (r["id"], r["icon"], r["matrix"]))
        print("written: web/apps/names.tsv (%d rows)" % len(rows))
    if args.json:
        json.dump(report, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())