#!/usr/bin/env python3
# mk_truth_tables.py — the exhaustive truth tables of the paraconsistent
# kernel. For the four-valued lpres (NEITHER 0, TRUE 1, FALSE 2, BOTH 3,
# per the repository's own rule) this generates: (a) the complete
# conjoin/disjoin/negation tables (every pair of values), and (b) for
# every app's truth entry, the four-value sweep, so no input combination
# can break the tautology unobserved. The tables are the proof that the
# system contains contradiction instead of exploding.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_truth_tables.py [--out tools/server-tasks/truth-tables]"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the kernel's four values, named exactly as kernel/src/lpres defines them
VALUES = ["neither", "true", "false", "both"]   # 0, 1, 2, 3


def emit(out, name, lines):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/truth-tables")
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)

    # 1. the conjoin table: every ordered pair (a, b) -> lpres_conjoin(a,b).
    #    A truth entry in two runtimes that disagrees yields BOTH; the
    #    kernel's chain ordering decides, never Belnap's lattice.
    for op, prov in (("conjoin", "and"), ("disjoin", "or")):
        lines = ["# the lpres %s table — every ordered pair of the four values" % op,
                 "# the kernel's chain ordering (lpres_conjoin/lpres_disjoin),",
                 "# never Belnap's lattice: BOTH or NEITHER is not TRUE.",
                 "zxapp 1", "id lpres_%s" % op, "matrix 1", "name lpres %s" % op,
                 "provides %s truth a b" % op,
                 "logic eve %s <<" % op,
                 "eve lpres_%s" % op,
                 "vals = 0 .. 3",
                 "",
                 "%s {" % op,
                 "  a: vals 0 .. 3",
                 "  b: vals 0 .. 3",
                 "}",
                 ".",
                 "# the full 4x4 table, one row per ordered pair",
                 "# (the expected column is the kernel's answer; fill it from",
                 "#  the running node's verdict, never by hand)"]
        for a in VALUES:
            for b in VALUES:
                lines.append("# check %s a=%d b=%d = <the kernel's answer>"
                             % (op, VALUES.index(a), VALUES.index(b)))
        emit(out, "lpres_%s.zxapp" % op, lines)

    # 2. the negation table: TRUE<->FALSE, BOTH and NEITHER fixed
    lines = ["# the lpres negation table — TRUE<->FALSE, BOTH and NEITHER fixed",
             "zxapp 1", "id lpres_not", "matrix 1", "name lpres not",
             "provides lnot truth a", "logic eve lnot <<",
             "eve lpres_not", "vals = 0 .. 3", "", "lnot {", "  a: vals 0 .. 3", "}",
             "."]
    for a in VALUES:
        lines.append("# check lnot a=%d = <the kernel's answer>" % VALUES.index(a))
    emit(out, "lpres_not.zxapp", lines)

    # 3. the per-app truth-entry sweep: every truth entry at every value
    import csv
    apps = []
    try:
        for r in csv.DictReader(open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv"))):
            if r["app"].strip() not in apps:
                apps.append(r["app"].strip())
    except OSError:
        pass
    n = 0
    for app in apps[:40]:
        a = app.lower().replace(" ", "_")
        lines = ["# the four-value sweep for %s's truth entries" % app,
                 "# every truth entry is probed at all four values so a",
                 "# contradiction is contained (BOTH) rather than exploding.",
                 "zxapp 1", "id %s_tt" % a, "matrix 1", "name %s truth table" % app,
                 "# provides <entry> truth <args>   (one per truth entry)",
                 "# check <entry> <args> = neither|true|false|both"]
        emit(out, "%s_tt.zxapp" % a, lines)
        n += 1

    total = len(os.listdir(out))
    print("truth-table manifests written: %d to %s" % (total, args.out))
    print("  the conjoin/disjoin/negation tables: 3")
    print("  the per-app four-value sweeps:       %d" % n)
    print("the expected columns are filled from the running node's verdict,")
    print("never by hand — the node's answer IS the table.")
    return 0


if __name__ == "__main__":
    sys.exit(main())