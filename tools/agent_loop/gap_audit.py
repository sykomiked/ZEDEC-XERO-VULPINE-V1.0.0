#!/usr/bin/env python3
# gap_audit.py — the vertical-integration gap detector: for every layer of
# the ZXV stack, find what is missing, unimplemented, orphaned, or
# disconnected, and emit the exact work item for each gap. The H100 reads
# the output and fills the gaps; nothing is left implicit.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: gap_audit.py [--json gaps.json] [--criteria]"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def read(path, enc="utf-8"):
    try:
        return open(path, encoding=enc, errors="replace").read()
    except OSError:
        return ""


def app_graph():
    g = "/tmp/gap_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    return json.load(open(g))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    ap.add_argument("--criteria", action="store_true")
    args = ap.parse_args()
    gaps = []

    # ---- layer 1: the app graph's coverage --------------------------------
    graph = app_graph()
    nodes, order = graph["nodes"], graph.get("order", [])
    built = pending = 0
    for o in order:
        n = nodes.get("app:" + o) or nodes.get(o)
        if not n or n.get("kind") != "app":
            continue
        if n.get("done"):
            built += 1
        else:
            pending += 1
    if pending:
        gaps.append({
            "layer": "apps", "kind": "unbuilt",
            "count": pending, "built": built,
            "work": "each pending app needs a .zxapp manifest that loads and passes its checks",
            "how": "tools/agent_loop/pipeline_parallel.py --workers N; the criteria in "
                   "tools/server-tasks/exhaustive/<app>.md carry the complete spec",
        })

    # ---- layer 2: the app graph's dependencies ----------------------------
    missing_uses = []
    for o in order:
        n = nodes.get("app:" + o) or nodes.get(o)
        if not n or n.get("kind") != "app":
            continue
        for dep in (n.get("uses") or []):
            did = dep.get("id") if isinstance(dep, dict) else dep
            if did and did not in nodes and ("app:" + str(did)) not in nodes:
                missing_uses.append((o, did))
    if missing_uses:
        gaps.append({"layer": "graph", "kind": "dangling-use", "count": len(missing_uses),
                     "samples": missing_uses[:20],
                     "work": "a `use <id>` names an app the graph does not contain",
                     "how": "add the named app to the wave spec, or correct the use line"})

    # ---- layer 3: the capability registry ---------------------------------
    caps = set()
    z = read(os.path.join(ROOT, "kernel/src/apps/zxapp.c"))
    i = z.find("CAPS[] = {")
    if i >= 0:
        j = z.find("};", i)
        caps = set(re.findall(r'"([a-z.]+)"', z[i:j]))
    needs = defaultdict(list)
    ex = os.path.join(ROOT, "tools/server-tasks/exhaustive")
    for f in sorted(os.listdir(ex)) if os.path.isdir(ex) else []:
        if f.endswith(".md"):
            t = read(os.path.join(ex, f))
            m = re.search(r"^needs:\s*(.+)$", t, re.M)
            if m:
                for c in m.group(1).split(","):
                    needs[c.strip().rstrip(".")].append(f[:-3])
    unknown = {c: v for c, v in needs.items() if c and c not in caps}
    if unknown:
        gaps.append({"layer": "capabilities", "kind": "unregistered-need",
                     "count": len(unknown), "samples": list(unknown)[:12],
                     "work": "an app requires a capability the kernel's registry lacks",
                     "how": "add the name to CAPS[] in kernel/src/apps/zxapp.c and give it a "
                            "provider, or correct the app's needs line"})
    unused = sorted(c for c in caps if c not in needs)
    if unused:
        gaps.append({"layer": "capabilities", "kind": "unused-capability",
                     "count": len(unused), "samples": unused[:12],
                     "work": "a registered capability no app requires",
                     "how": "assign it to an app that should use it, or retire it from CAPS[]"})

    # ---- layer 4: the stratum DAG ----------------------------------------
    strata = read(os.path.join(ROOT, "build_system/strata.txt"))
    srcs = 0
    for d in ("kernel/src", "kernel/arch/x86_64", "kernel/arch/arm64"):
        for dp, _dn, fn in os.walk(os.path.join(ROOT, d)):
            srcs += sum(1 for f in fn if f.endswith((".c", ".h")))
    mapped = len([l for l in strata.split("\n") if l.strip() and not l.startswith("#")])
    if srcs and mapped and mapped < srcs * 0.6:
        gaps.append({"layer": "strata", "kind": "unmapped-sources",
                     "count": srcs - mapped, "sources": srcs, "map_lines": mapped,
                     "work": "sources without a stratum",
                     "how": "add each to build_system/strata.txt; build_system/verify_layers.sh "
                            "names them"})

    # ---- layer 5: the module orphan check --------------------------------
    exported = defaultdict(set)
    for d in ("kernel/src",):
        for dp, _dn, fn in os.walk(os.path.join(ROOT, d)):
            for f in fn:
                if f.endswith(".c"):
                    mod = os.path.relpath(os.path.dirname(os.path.join(dp, f)), ROOT)
                    t = read(os.path.join(dp, f))
                    for m in re.finditer(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*\b([a-z_][a-z0-9_]{3,})\s*\([^;{]*\)\s*\{",
                                         t, re.M):
                        exported[mod].add(m.group(1))
    orphan_mods = [m for m, s in exported.items() if not s]
    if orphan_mods:
        gaps.append({"layer": "modules", "kind": "empty-module",
                     "count": len(orphan_mods), "samples": orphan_mods[:12],
                     "work": "a module directory defines no function",
                     "how": "implement it, or remove it from the tree and the strata map"})

    # ---- the report -------------------------------------------------------
    print("== the vertical-integration gap audit ==")
    print("apps: %d built, %d pending" % (built, pending))
    print("gaps found: %d" % len(gaps))
    for g in gaps:
        print("  [%s/%s] %d — %s" % (g["layer"], g["kind"], g["count"], g["work"]))
    if args.json:
        json.dump(gaps, open(args.json, "w"), indent=1)
        print("written: %s" % args.json)
    if args.criteria:
        print("\nThe gap-filling criteria: tools/agent_loop/GAP_FILLING.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())