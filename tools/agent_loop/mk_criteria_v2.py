#!/usr/bin/env python3
# mk_criteria_v2.py — the immaculate criteria at scale: every app's wave
# spec's Logic clauses are matched against the canonical pattern library,
# and each match is instantiated into a full entry row with its language,
# logic and exact check. Apps the library does not cover are listed with
# their unmatched clauses, so the remaining authoring is explicit and
# countable — never silent.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_criteria_v2.py [--out DIR]"""
import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patterns import match_patterns  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def spec_of(task):
    p = os.path.join(ROOT, "tools/server-tasks", task)
    return open(p, errors="replace").read() if task and os.path.exists(p) else ""


def logic_clauses(spec):
    m = re.search(r"^Logic[^:]*:\s*(.+?)(?=\n[A-Za-z][A-Za-z ]{2,20}:|\Z)", spec, re.M | re.S)
    if not m:
        return []
    body = m.group(1)
    return [c.strip().rstrip(".") for c in re.split(r";|\n", body) if c.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/criteria")
    args = ap.parse_args()
    g = "/tmp/appgraph_v2.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    graph = json.load(open(g))
    nodes, order = graph["nodes"], graph.get("order", [])
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    covered = uncovered = 0
    gaps = []
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("kind") != "app":
            continue
        spec = spec_of(node.get("task", ""))
        clauses = logic_clauses(spec)
        rows = []
        unmatched = []
        for c in clauses:
            ps = match_patterns(c)
            if ps:
                for p in ps[:2]:
                    rows.append((c, p))
            else:
                unmatched.append(c)
        doc = ["# Criteria (instantiated) — %s" % o, ""]
        doc.append("matrix %s · wave spec `%s`" % (node.get("slot"), node.get("task")))
        doc.append("")
        if rows:
            doc.append("| the spec's clause | pattern | kind | args | languages | logic | exact check |")
            doc.append("|---|---|---|---|---|---|---|")
            for clause, p in rows:
                doc.append("| %s | `%s` | %s | `%s` | %s | %s | `%s` |"
                           % (clause[:60], p["name"], p["kind"], p["args"], p["langs"],
                              p["logic"][:70], p["checks"]))
        else:
            doc.append("(no clauses matched — author this app's entries by hand)")
        if unmatched:
            doc.append("")
            doc.append("## Clauses the pattern library does not yet cover (author these)")
            doc.append("")
            for c in unmatched:
                doc.append("- %s" % c)
            uncovered += 1
            gaps.append((o, unmatched))
        else:
            covered += 1
        with open(os.path.join(out, "%s.instantiated.md" % o), "w") as f:
            f.write("\n".join(doc) + "\n")
    with open(os.path.join(out, "_GAPS.md"), "w") as f:
        f.write("# Apps with clauses the library does not cover\n\n")
        for o, us in gaps:
            f.write("## %s\n\n" % o)
            for c in us:
                f.write("- %s\n" % c)
            f.write("\n")
    print("instantiated: %d fully covered, %d with gaps" % (covered, uncovered))
    return 0


if __name__ == "__main__":
    sys.exit(main())