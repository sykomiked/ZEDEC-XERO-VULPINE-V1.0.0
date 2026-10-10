#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""coverage_table.py: turn `llvm-cov export -summary-only` JSON into the
per-module line / branch / MC/DC table of docs/COVERAGE.md.

    coverage_table.py SUMMARY.json REPO_ROOT [--header FILE] > docs/COVERAGE.md

A module is the directory under kernel/src/ (kernel/src/pay/pay_ledger.c ->
pay); other trees are named by their first two path parts. Test drivers
(test_*.c), the fuzz harnesses and system headers are left out: the table
measures the code under test, not the tests.
"""
import json
import os
import sys
from collections import defaultdict


def module_of(rel):
    parts = rel.split("/")
    if parts[0] == "kernel" and len(parts) > 2 and parts[1] == "src":
        return parts[2] if len(parts) > 3 else "(kernel/src)"
    if parts[0] == "kernel" and len(parts) > 2:
        return "kernel/" + parts[1]
    return "/".join(parts[:2]) if len(parts) > 1 else parts[0]


def keep(rel):
    base = os.path.basename(rel)
    if rel.startswith("..") or rel.startswith("/"):
        return False
    if base.startswith("test_") or rel.startswith("fuzz/") or "/tests/" in "/" + rel:
        return False
    return rel.endswith(".c") or rel.endswith(".h")


def pct(c, n):
    return "-" if n == 0 else "%.1f%%" % (100.0 * c / n)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = json.load(open(sys.argv[1]))
    root = os.path.realpath(sys.argv[2])
    header = ""
    if "--header" in sys.argv:
        header = open(sys.argv[sys.argv.index("--header") + 1]).read()
    files = {}
    for d in data["data"]:
        for f in d["files"]:
            rel = os.path.relpath(os.path.realpath(f["filename"]), root)
            if keep(rel):
                files[rel] = f["summary"]
    agg = defaultdict(lambda: defaultdict(int))
    tot = defaultdict(int)
    for rel, s in files.items():
        m = agg[module_of(rel)]
        m["files"] += 1
        for k in ("lines", "branches", "functions", "mcdc"):
            if k in s:
                m[k + "_n"] += s[k]["count"]
                m[k + "_c"] += s[k]["covered"]
                tot[k + "_n"] += s[k]["count"]
                tot[k + "_c"] += s[k]["covered"]
        tot["files"] += 1
    has_mcdc = tot["mcdc_n"] > 0
    out = [header.rstrip() + "\n"] if header else []
    cols = ["Module", "Files", "Lines", "Line cov", "Branches", "Branch cov", "Functions",
            "Func cov"]
    if has_mcdc:
        cols += ["MC/DC conds", "MC/DC cov"]
    out.append("| " + " | ".join(cols) + " |")
    out.append("|" + "|".join(["---"] + ["---:"] * (len(cols) - 1)) + "|")

    def row(name, m):
        r = [name, str(m["files"]), str(m["lines_n"]), pct(m["lines_c"], m["lines_n"]),
             str(m["branches_n"]), pct(m["branches_c"], m["branches_n"]),
             str(m["functions_n"]), pct(m["functions_c"], m["functions_n"])]
        if has_mcdc:
            r += [str(m["mcdc_n"]), pct(m["mcdc_c"], m["mcdc_n"])]
        return "| " + " | ".join(r) + " |"

    for name in sorted(agg):
        out.append(row(name, agg[name]))
    out.append(row("**total**", tot))
    # modules under kernel/src with C sources that no test program links
    src = os.path.join(root, "kernel", "src")
    missing = []
    if os.path.isdir(src):
        for d in sorted(os.listdir(src)):
            p = os.path.join(src, d)
            if not os.path.isdir(p) or d in agg:
                continue
            if any(f.endswith(".c") and not f.startswith("test_") for f in os.listdir(p)):
                missing.append(d)
    out.append("")
    out.append("Modules under kernel/src that no host test or fuzz harness links (not in the "
               "totals above, so effectively 0%%): %d" % len(missing))
    out.append("")
    out.append(", ".join(missing) if missing else "(none)")
    print("\n".join(out))


if __name__ == "__main__":
    main()
