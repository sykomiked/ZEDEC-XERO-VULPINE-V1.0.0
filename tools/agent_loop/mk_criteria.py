#!/usr/bin/env python3
# mk_criteria.py — author the immaculate per-app criteria documents: every
# deterministic particular (id, matrix, name, purpose, needs, the entry
# table with its languages) pre-resolved from the graph, the wave spec and
# the language matrix, so the A6000 drafts from a complete brief instead of
# guessing.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_criteria.py [--out tools/server-tasks/criteria] [--app ID ...]"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def graph():
    g = "/tmp/appgraph_criteria.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    return json.load(open(g))


def lang_rows():
    rows = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    return rows


def spec_of(task):
    p = os.path.join(ROOT, "tools/server-tasks", task)
    return open(p, errors="replace").read() if task and os.path.exists(p) else ""


def parse_logic(spec):
    """The spec's Logic line names each entry and its languages:
    'Logic: package size and file-count limits in eve; total size exact in
    trebago; ...' -> [(the entry's job, [languages])]."""
    m = re.search(r"^Logic:\s*(.+)$", spec, re.M)
    if not m:
        return []
    out = []
    for clause in m.group(1).split(";"):
        c = clause.strip().rstrip(".")
        if not c:
            continue
        langs = re.findall(r"\b(eve|kron|myra|sutra|trebago|rust|wasm|c|c\+\+|ada|apl|mumps|cobol|fortran|python|prolog|haskell|mojo|logo|tex)\b",
                           c, re.I)
        out.append((c, [l.lower() for l in langs]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/criteria")
    ap.add_argument("--app", action="append", default=[])
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    g = graph()
    nodes, order = g["nodes"], g.get("order", [])
    langs = lang_rows()
    n = 0
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("kind") != "app":
            continue
        if args.app and o not in args.app:
            continue
        spec = spec_of(node.get("task", ""))
        purpose = ""
        m = re.search(r"^What it does:\s*(.+)$", spec, re.M)
        if m:
            purpose = m.group(1).strip()
        needs = []
        m = re.search(r"^needs:\s*(.+)$", spec, re.M)
        if m:
            needs = [c.strip().rstrip(".") for c in m.group(1).split(",")]
        entries = parse_logic(spec)
        rows = langs.get(o, [])
        doc = []
        doc.append("# Criteria — %s" % o)
        doc.append("")
        doc.append("| | |")
        doc.append("|---|---|")
        doc.append("| app | `%s` |" % o)
        doc.append("| matrix layer | %s |" % node.get("slot"))
        doc.append("| icon group | L%s |" % node.get("layer"))
        doc.append("| wave spec | `%s` |" % node.get("task"))
        doc.append("")
        doc.append("## Purpose")
        doc.append("")
        doc.append(purpose or "(from the wave spec)")
        doc.append("")
        doc.append("## Capabilities (needs — the exact registry names)")
        doc.append("")
        for c in needs:
            doc.append("- `%s`" % c)
        doc.append("")
        doc.append("## Entries and languages (from the spec's Logic line)")
        doc.append("")
        if entries:
            doc.append("| the entry's job (from the spec) | languages the spec names |")
            doc.append("|---|---|")
            for job, ls in entries:
                doc.append("| %s | %s |" % (job, ", ".join(ls) if ls else "—"))
        else:
            doc.append("(the spec names no Logic line; assign per the language matrix)")
        doc.append("")
        doc.append("## The language matrix rows for this app")
        doc.append("")
        if rows:
            doc.append("| language | runtime | feature |")
            doc.append("|---|---|---|")
            for r in rows:
                doc.append("| %s | %s | %s |" % (r["language"], r["runtime"], r["feature"]))
        else:
            doc.append("(no rows)")
        doc.append("")
        doc.append("## The manifest contract (the exact grammar the node parses)")
        doc.append("")
        doc.append("```")
        doc.append("zxapp 1")
        doc.append("id %s" % o)
        doc.append("matrix %s        # the graph's slot IS the matrix layer" % node.get("slot"))
        doc.append("name %s" % o.replace("_", " ").title())
        for c in needs:
            doc.append("needs %s" % c)
        doc.append("provides <entry> num|truth <arg>[,<arg>...]")
        doc.append("logic <lang> <entry> <<")
        doc.append("<source>")
        doc.append(".")
        doc.append("check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]")
        doc.append("```")
        doc.append("")
        doc.append("Rules the loader enforces (a violation refuses the manifest):")
        doc.append("")
        doc.append("- Every `provides` entry must have at least one `logic` line.")
        doc.append("- Every `logic` entry must have a `check` line with exact answers.")
        doc.append("- The checks' inputs are integers; answers are integers, `n/d`,")
        doc.append("  `true`, `false`, `neither`, or the word `both` (two languages")
        doc.append("  disagreeing).")
        doc.append("- A truth entry's checks use the four-valued answers, never 0/1.")
        doc.append("- Inline sources end with a line containing only `.`.")
        doc.append("")
        path = os.path.join(out, "%s.md" % o)
        with open(path, "w") as f:
            f.write("\n".join(doc) + "\n")
        n += 1
    print("wrote %d criteria documents to %s" % (n, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())