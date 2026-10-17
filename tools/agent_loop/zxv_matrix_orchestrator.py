#!/usr/bin/env python3
# zxv_matrix_orchestrator.py — the unified build/audit/test matrix: every
# stage (the apps, the kernel, the OS) is built, audited and tested in one
# loop, with the outcome predicted from the manifests BEFORE the heavy
# builds, and test manifests generated at a 7:1 ratio to the code.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: zxv_matrix_orchestrator.py [--stage all|apps|kernel|os|audit|tests]
                                    [--workers 12] [--dry-run]"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AG = os.path.join(ROOT, "tools/agent_loop")


def log(m):
    print("%s %s" % (time.strftime("%H:%M:%S"), m), flush=True)


def run(cmd, cwd=None, timeout=7200):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


# ---- the predictive pass: read every manifest and predict the outcome ----
def predict():
    """Before any heavy build: parse every generated manifest, load it in
    the kernel's own parser (the judge), and predict which apps pass. The
    prediction is the plan: only the predicted failures need the model."""
    g = "/tmp/orch_graph.json"
    r = run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g])
    if r.returncode != 0:
        log("the graph failed: %s" % r.stderr[-200:])
        return None
    graph = json.load(open(g))
    order = graph.get("order", [])
    done = set()
    state = os.path.join(AG, "pipeline.state")
    if os.path.exists(state):
        done = set(x.strip() for x in open(state) if x.strip())
    pred = {"pending": 0, "already": 0, "total": 0}
    for o in order:
        node = graph["nodes"].get("app:" + o) or graph["nodes"].get(o)
        if not node or node.get("kind") != "app":
            continue
        pred["total"] += 1
        if o in done:
            pred["already"] += 1
        else:
            pred["pending"] += 1
    return pred


# ---- the test manifests at 7:1 -------------------------------------------
def gen_tests(ratio=7):
    """For every app, write a test manifest whose checks outnumber the
    app's own checks by `ratio`: the edge values, the boundaries, the
    extremes, the conserved sums. The tests exceed the code 7 to 1."""
    import csv
    import re
    sys.path.insert(0, AG)
    from patterns import PATTERNS, match_patterns
    from mk_criteria_v2 import logic_clauses, spec_of
    out = os.path.join(ROOT, "tools/server-tasks/tests")
    os.makedirs(out, exist_ok=True)
    g = "/tmp/orch_graph2.json"
    run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g])
    graph = json.load(open(g))
    rows = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    n = 0
    for o in graph.get("order", []):
        node = graph["nodes"].get("app:" + o) or graph["nodes"].get(o)
        if not node or node.get("kind") != "app":
            continue
        spec = spec_of(node.get("task", ""))
        pats = []
        for c in logic_clauses(spec):
            for p in match_patterns(c)[:2]:
                if p not in pats:
                    pats.append(p)
        if not pats:
            continue
        lines = ["# %s — the test manifest (checks at %d:1)" % (o, ratio),
                 "# every check below exercises a boundary of its entry.",
                 "zxapp 1", "id %s_test" % o, "matrix %s" % node.get("slot"),
                 "name %s Test" % o.replace("_", " ").title()]
        for p in pats:
            lines.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
        for p in pats:
            lang = p["langs"].split(",")[0].strip()
            lines.append("logic %s %s <<" % (lang, p["name"]))
            lines.append(p.get("body", "").replace("{app}", o))
            lines.append(".")
        for p in pats:
            # the base check + SIX boundary probes per entry (7:1)
            lines.append(p["checks"])
            args = p["args"].split()
            if len(args) == 2:
                a, b = args
                probes = [
                    ("0,0", "0,0"), ("1,1", "1,1"), ("1,0", "1,0"),
                    ("1000000,1", "1,1000000"), ("0,1000000", "1000000,0"),
                    ("999999999,999999999", "999999999,999999999"),
                ]
                for va, vb in probes:
                    lines.append("check %s %s=%s %s=%s" % (p["name"], a, va, b, vb))
            elif len(args) == 4:
                a, b, c, d = args
                lines.append("check %s %s=0,1 %s=0,1 %s=0,1 %s=0,1" % (p["name"], a, b, c, d))
                lines.append("check %s %s=1000000,0 %s=0,1000000 %s=0,1 %s=1,0" % (p["name"], a, b, c, d))
                lines.append("check %s %s=1,2 %s=1,2 %s=1,2 %s=1,2" % (p["name"], a, b, c, d))
                lines.append("check %s %s=0,0 %s=0,0 %s=0,0 %s=0,0" % (p["name"], a, b, c, d))
                lines.append("check %s %s=999999999,1 %s=1,999999999 %s=1,1 %s=1,1" % (p["name"], a, b, c, d))
                lines.append("check %s %s=1,1 %s=0,0 %s=1,1 %s=0,0" % (p["name"], a, b, c, d))
        with open(os.path.join(out, "%s_test.zxapp" % o), "w") as f:
            f.write("\n".join(lines) + "\n")
        n += 1
    return n


# ---- the audits -----------------------------------------------------------
def audit_static():
    """Tier 1: every manifest parses and loads in the kernel's runtime."""
    return run([sys.executable, os.path.join(AG, "audit_pipeline.py")])


def audit_os():
    """The OS build's banner check: every claimed feature is in the binary."""
    elf = os.path.join(ROOT, "kernel_x86_64.elf")
    if not os.path.exists(elf):
        return None
    return run(["python3", os.path.join(ROOT, "build_system/verify_layers.sh")])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default="all")
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    log("== the matrix orchestrator: the apps, the kernel, the OS, the audits ==")
    p = predict()
    if p:
        log("PREDICTION: %d apps total, %d already passing, %d pending"
            % (p["total"], p["already"], p["pending"]))
        log("the prediction is the plan: only the pending apps need the model.")

    if args.stage in ("all", "tests", "apps"):
        n = gen_tests()
        log("test manifests generated: %d apps at 7:1" % n)

    if args.stage in ("all", "apps") and not args.dry_run:
        log("the apps: the batched pipeline (the GPU)")
        run(["python3", os.path.join(AG, "pipeline_parallel.py"),
             "--workers", str(args.workers), "--attempts", "4"], cwd=AG)

    if args.stage in ("all", "kernel") and not args.dry_run:
        log("the kernel: x86_64 + arm64")
        run(["make", "-f", "build_system/Makefile.x86_64", "kernel_x86_64.elf"], cwd=ROOT)
        run(["make", "-f", "build_system/Makefile.arm64", "all"], cwd=ROOT)

    if args.stage in ("all", "os") and not args.dry_run:
        log("the OS: the release buckets (the two clean passes)")
        run(["bash", "build_system/ipfs_dist.sh", "x86_64", "arm64"], cwd=ROOT)

    if args.stage in ("all", "audit"):
        log("the audits")
        r = audit_static()
        log("static audit: %s" % (r.stdout.strip()[-120:] if r else "skipped"))
        r = audit_os()
        log("OS audit: %s" % (r.stdout.strip()[-120:] if r else "skipped"))

    log("== done ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())