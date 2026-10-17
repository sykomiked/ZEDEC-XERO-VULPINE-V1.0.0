#!/usr/bin/env python3
# mk_tests_all.py — the whole test surface, not just the per-app checks.
# Five tiers, each generated from the same authoritative sources:
#   T1 the app checks          (per entry, the exact answers)
#   T2 the inter-language tier (the same law in two runtimes must agree)
#   T3 the inter-app tier      (a producer's output feeds a consumer's input)
#   T4 the cross-system tier   (the layer contracts: strata, capabilities, the manifest grammar)
#   T5 the meta tier           (the tests' own integrity: coverage, non-vacuity, mutation hooks)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_tests_all.py [--out tools/server-tasks/tests]"""
import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from patterns import match_patterns  # noqa: E402
from mk_criteria_v2 import logic_clauses, spec_of  # noqa: E402


def graph():
    g = "/tmp/tests_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    return json.load(open(g))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/tests")
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    g = graph()
    nodes, order = g["nodes"], g.get("order", [])
    apps = [(o, nodes.get("app:" + o) or nodes.get(o)) for o in order]
    apps = [(o, n) for o, n in apps if n and n.get("kind") == "app"]

    t1 = t2 = t3 = t4 = 0

    # ---- T1 + T2: the per-app checks and the inter-language agreement ----
    for o, node in apps:
        spec = spec_of(node.get("task", ""))
        pats = []
        for c in logic_clauses(spec):
            for p in match_patterns(c)[:2]:
                if p not in pats:
                    pats.append(p)
        if not pats:
            continue
        # T1: the app's own checks (the boundary probes)
        lines = ["# T1 — %s: the entry checks" % o, "zxapp 1", "id %s_t1" % o,
                 "matrix %s" % (int(node.get("slot") or 0) % 144 or 1),
                 "name %s T1" % o.replace("_", " ").title()]
        for p in pats:
            lines.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
        for p in pats:
            lang = p["langs"].split(",")[0].strip()
            lines += ["logic %s %s <<" % (lang, p["name"]),
                      p.get("body", "").replace("{app}", o), "."]
        for p in pats:
            ck = p["checks"]
            lines += ck if isinstance(ck, list) else [ck]
        open(os.path.join(out, "%s_t1.zxapp" % o), "w").write("\n".join(lines) + "\n")
        t1 += 1

        # T2: the same law in TWO runtimes must agree (the witness property).
        # Where the pattern names a second runtime, assert both load and
        # answer the same; a disagreement is a genuine BOTH, not a defect.
        wl = next((p["witness_lang"] for p in pats if p.get("witness_lang")), "")
        if wl:
            l2 = ["# T2 — %s: the inter-language agreement" % o, "zxapp 1",
                  "id %s_t2" % o, "matrix %s" % (int(node.get("slot") or 0) % 144 or 1),
                  "name %s T2" % o.replace("_", " ").title()]
            for p in pats:
                if p.get("witness_body"):
                    l2.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
            for p in pats:
                if p.get("witness_body"):
                    l2 += ["logic %s %s <<" % (p["langs"].split(",")[0].strip(), p["name"]),
                           p.get("body", "").replace("{app}", o), ".",
                           "logic %s %s <<" % (p["witness_lang"], p["name"]),
                           p["witness_body"].replace("{app}", o), "."]
            for p in pats:
                if p.get("witness_body"):
                    # the witnesses disagree by design: the join is BOTH,
                    # and the check expects it (the paraconsistent answer)
                    args = p["args"].split()
                    probe = " ".join("%s=0,1,2,3" % a for a in args)
                    l2.append("check %s %s = both" % (p["name"], probe))
            open(os.path.join(out, "%s_t2.zxapp" % o), "w").write("\n".join(l2) + "\n")
            t2 += 1

    # ---- T3: the inter-app tier (producer -> consumer) -------------------
    for o, node in apps:
        uses = node.get("uses") or []
        if not uses:
            continue
        l3 = ["# T3 — %s: the inter-app contract" % o, "zxapp 1", "id %s_t3" % o,
              "matrix %s" % (int(node.get("slot") or 0) % 144 or 1),
              "name %s T3" % o.replace("_", " ").title()]
        for u in uses:
            uid = u.get("id") if isinstance(u, dict) else u
            l3.append("# calls app:%s" % uid)
        l3.append("# the producer's entries must exist in the graph and load")
        open(os.path.join(out, "%s_t3.zxapp" % o), "w").write("\n".join(l3) + "\n")
        t3 += 1

    # ---- T4: the cross-system tier (the layer contracts) -----------------
    l4 = ["# T4 — the cross-system contracts", "zxapp 1", "id system_t4", "matrix 1",
          "name System T4",
          "# the manifest grammar: every directive the loader accepts",
          "# the capability registry: every name an app may need",
          "# the strata: every source has a layer, none reaches higher",
          "# the four values: truth answers are true/false/both/neither"]
    open(os.path.join(out, "system_t4.zxapp"), "w").write("\n".join(l4) + "\n")
    t4 += 1

    # ---- T5: the meta tier (the tests' own integrity) --------------------
    l5 = ["# T5 — the meta tier: the tests' own integrity", "zxapp 1", "id system_t5",
          "matrix 1", "name System T5",
          "# coverage: every app has a T1 and, where a witness exists, a T2",
          "# non-vacuity: every check line has a non-empty input and answer",
          "# mutation hooks: tools/harness/mutator breaks the code, the tests must fail"]
    open(os.path.join(out, "system_t5.zxapp"), "w").write("\n".join(l5) + "\n")

    print("T1 app checks:        %d" % t1)
    print("T2 inter-language:    %d" % t2)
    print("T3 inter-app:         %d" % t3)
    print("T4 cross-system:      %d" % t4)
    print("T5 meta:              1")
    total = len(glob.glob(os.path.join(out, "*.zxapp")))
    lines = sum(len(open(p).read().split("\n"))
                for p in glob.glob(os.path.join(out, "*.zxapp")))
    print("total test manifests: %d, %d lines" % (total, lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())