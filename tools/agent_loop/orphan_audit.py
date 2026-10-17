#!/usr/bin/env python3
# orphan_audit.py — the orphan and gap analysis: every kernel module's
# exported symbols against their callers, every capability the apps
# require against the modules that provide it, and every app in the graph
# against its consumers. Nothing may be orphaned; nothing may be needed
# and missing.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: orphan_audit.py [--root .] [--json out.json]"""
import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def sources():
    for d in ("kernel/src", "kernel/arch/x86_64", "kernel/arch/arm64"):
        base = os.path.join(ROOT, d)
        for dp, _dn, fn in os.walk(base):
            for f in fn:
                if f.endswith(".c"):
                    yield os.path.join(dp, f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    # 1. every module's exports (the non-static definitions)
    exported = defaultdict(set)  # module -> symbols
    defined = {}                 # symbol -> module
    for p in sources():
        mod = os.path.relpath(os.path.dirname(p), ROOT)
        txt = open(p, errors="replace").read()
        for m in re.finditer(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*\b([a-z_][a-z0-9_]{2,})\s*\([^;]*\)\s*\{", txt, re.M):
            name = m.group(1)
            exported[mod].add(name)
            defined.setdefault(name, mod)

    # 2. every call site
    called = defaultdict(set)  # symbol -> modules calling it
    for p in sources():
        mod = os.path.relpath(os.path.dirname(p), ROOT)
        txt = open(p, errors="replace").read()
        for name in defined:
            if re.search(r"\b%s\s*\(" % re.escape(name), txt):
                called[name].add(mod)

    # 3. the orphans: exported, never called outside its own module
    orphans = []
    for mod, syms in exported.items():
        for s in syms:
            callers = called.get(s, set())
            external = callers - {mod}
            if not external and mod not in ("kernel/arch/x86_64", "kernel/arch/arm64"):
                orphans.append((mod, s, sorted(callers)))

    # 4. the capability gaps: the apps' needs vs the kernel's registry
    caps = set()
    zxapp = os.path.join(ROOT, "kernel/src/apps/zxapp.c")
    if os.path.exists(zxapp):
        t = open(zxapp, errors="replace").read()
        i, j = t.find("CAPS[] = {"), t.find("};", t.find("CAPS[] = {"))
        for tok in re.findall(r'"([a-z.]+)"', t[i:j]):
            caps.add(tok)
    needs = defaultdict(int)
    import csv
    csvp = os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")
    if os.path.exists(csvp):
        for r in csv.DictReader(open(csvp)):
            pass
    for dp, _dn, fn in os.walk(os.path.join(ROOT, "tools/server-tasks")):
        for f in fn:
            if f.endswith(".md"):
                t = open(os.path.join(dp, f), errors="replace").read()
                m = re.search(r"^needs:\s*(.+)$", t, re.M)
                if m:
                    for c in m.group(1).split(","):
                        needs[c.strip().rstrip(".")] += 1

    unknown_needs = sorted(k for k in needs if k and k not in caps)
    unused_caps = sorted(c for c in caps if c not in needs)

    # 5. the app graph's consumers: which apps nothing depends on
    g = "/tmp/orphan_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    graph = json.load(open(g))
    leaf_apps = []
    for o in graph.get("order", []):
        node = graph["nodes"].get("app:" + o) or graph["nodes"].get(o)
        if node and node.get("kind") == "app" and not node.get("unlock"):
            leaf_apps.append(o)

    report = {
        "orphan_symbols": len(orphans),
        "orphans": orphans[:80],
        "capabilities_known": sorted(caps),
        "needs_unknown": unknown_needs,
        "capabilities_unused": unused_caps,
        "leaf_apps": len(leaf_apps),
    }
    print("== the orphan and gap audit ==")
    print("modules scanned:      %d" % len(exported))
    print("symbols defined:      %d" % len(defined))
    print("orphan symbols:       %d  (exported, no external caller)" % len(orphans))
    print("capabilities known:   %d" % len(caps))
    print("needs not in the registry (MUST be 0): %d %s" % (len(unknown_needs), unknown_needs[:6]))
    print("capabilities no app needs: %d %s" % (len(unused_caps), unused_caps[:6]))
    print("apps nothing depends on (leaf, fine): %d" % len(leaf_apps))
    print("first orphans:")
    for mod, s, callers in orphans[:12]:
        print("   %-46s %s" % (mod, s))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())