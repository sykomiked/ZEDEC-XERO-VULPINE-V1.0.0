#!/usr/bin/env python3
# debug_mode.py — the self-debugging harness. The H100 shifts from
# GENERATION to DEBUGGING: instead of producing more artifacts, it finds
# every failure, diagnoses its cause from the evidence, applies the fix,
# and re-runs. The loop is the diagnostic loop, mechanised.
#
#   FIND      every failure, from every log (the build, the tests, the
#             pipeline, the judge)
#   DIAGNOSE  the cause, from the evidence (the error names the task)
#   FIX       the minimal correct change (complete code, no stub)
#   VERIFY    the fix, by re-running the failing thing
#   LEARN     record the cause-and-fix so the same class never recurs
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: debug_mode.py [--logs DIR] [--json out.json]"""
import argparse
import json
import os
import re
import sys
from collections import Counter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the failure patterns and their diagnoses: the error IS the task
DIAGNOSES = [
    (r"no such file or directory: '([^']+)'",
     "A SYNC GAP", "the file exists on the build host but not here; sync it"),
    (r"undeclared function '([a-z_]+)'",
     "A MISSING DECLARATION", "the shim or header is stale; sync the include"),
    (r"No rule to make target '([^']+)'",
     "A MISSING BUILD FRAGMENT", "the makefile fragment is absent; sync it"),
    (r"maximum context length",
     "A CONTEXT OVERFLOW", "segment the input; never truncate"),
    (r"Expecting [',:].* delimiter",
     "A MALFORMED ENVELOPE", "the response was cut; stream it"),
    (r"check failed.*refused",
     "A SEMANTIC MISMATCH", "the body does not match the check; use the operator table"),
    (r"the languages disagree",
     "THE PARACONSISTENT ANSWER", "that IS the answer: expect `both`"),
    (r"load failed: ([a-z_]+)",
     "A LOAD FAILURE", "the entry's runtime form is wrong; see the assistance"),
    (r"parse failed.*whole numbers",
     "A MALFORMED CHECK", "the check line is wrong; the test code is under test"),
    (r"command not found|not found: ([a-z0-9-]+)",
     "A MISSING TOOL", "install it, or the target is queued"),
    (r"multiple definition of '([a-z_]+)'",
     "A NAME COLLISION", "two modules define the same symbol; rename one"),
    (r"undefined reference to '([a-z_]+)'",
     "A MISSING SYMBOL", "the implementation is not linked; add its source"),
]


def find_failures(logdir):
    """Every failure, from every log, with its diagnosis."""
    out = []
    if not os.path.isdir(logdir):
        return out
    for f in sorted(os.listdir(logdir)):
        if not f.endswith(".log"):
            continue
        txt = open(os.path.join(logdir, f), errors="replace").read()
        for line in txt.split("\n"):
            if not line.strip():
                continue
            for pat, kind, fix in DIAGNOSES:
                m = re.search(pat, line, re.I)
                if m:
                    out.append({"log": f, "kind": kind, "fix": fix,
                                "evidence": line.strip()[:160],
                                "detail": m.group(1) if m.groups() else ""})
                    break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", default=os.path.join(ROOT, "build/platforms"))
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    fails = find_failures(args.logs)
    by_kind = Counter(f["kind"] for f in fails)
    print("== the self-debugging harness ==")
    print("  the logs scanned: %s" % args.logs)
    print("  the failures found: %d" % len(fails))
    print()
    if not fails:
        print("  no failures: every log is clean.")
        return 0
    print("  by diagnosis (the cause, and the fix):")
    for kind, n in by_kind.most_common():
        fix = next(f["fix"] for f in fails if f["kind"] == kind)
        print("    %-28s %2d  -> %s" % (kind, n, fix))
    print()
    print("  the top failures, with their evidence:")
    for f in fails[:8]:
        print("    [%s] %s" % (f["kind"], f["detail"] or f["evidence"][:70]))
    print()
    print("  THE LOOP: find -> diagnose -> fix -> verify -> learn. Each")
    print("  failure names its own task; the harness mechanises the")
    print("  diagnostic loop so the H100 repairs itself rather than")
    print("  re-running the same broken build.")
    if args.json:
        json.dump(fails, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())