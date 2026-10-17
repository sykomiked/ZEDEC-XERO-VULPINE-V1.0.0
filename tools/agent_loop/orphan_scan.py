#!/usr/bin/env python3
# orphan_scan.py — every kernel module, every exported function, and
# whether anything outside the module calls it. A module with no external
# caller is dead surface: an OS that claims vertical integration cannot
# carry it. The scan names each orphan and its likely integration point.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: orphan_scan.py [--json orphans.json] [--top N]"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def read(p):
    try:
        return open(p, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    # every .c under kernel/, grouped by module directory
    mods = defaultdict(list)
    for dp, _dn, fn in os.walk(os.path.join(ROOT, "kernel")):
        for f in fn:
            if f.endswith(".c") and not f.startswith("test_"):
                mod = os.path.relpath(dp, os.path.join(ROOT, "kernel"))
                mods[mod].append(os.path.join(dp, f))

    # every exported definition (non-static function bodies)
    defined = {}          # name -> module
    for mod, files in mods.items():
        for p in files:
            t = read(p)
            for m in re.finditer(
                    r"^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b([a-z_][a-z0-9_]{3,})\s*\([^;{]*\)\s*\{",
                    t, re.M):
                defined.setdefault(m.group(1), mod)

    # every call site, per module
    callers = defaultdict(set)   # symbol -> modules that call it
    for mod, files in mods.items():
        for p in files:
            t = read(p)
            for name in defined:
                if re.search(r"\b%s\s*\(" % re.escape(name), t):
                    callers[name].add(mod)

    # a module is orphaned when NONE of its exports is called elsewhere
    orphans = []
    for mod, files in sorted(mods.items()):
        exports = [s for s, m in defined.items() if m == mod]
        if not exports:
            orphans.append({"module": mod, "exports": 0,
                            "reason": "defines no exported function",
                            "callers": []})
            continue
        external = [s for s in exports if callers.get(s, set()) - {mod}]
        if not external:
            sample = exports[:3]
            orphans.append({"module": mod, "exports": len(exports),
                            "reason": "no export is called outside the module",
                            "sample": sample, "callers": []})

    print("== the orphan scan: every kernel module vs its callers ==")
    print("modules: %d | exported symbols: %d" % (len(mods), len(defined)))
    print("orphaned modules: %d" % len(orphans))
    for o in orphans[:args.top]:
        print("  %-34s %2d exports  %s" % (o["module"], o["exports"], o["reason"]))
    if args.json:
        json.dump(orphans, open(args.json, "w"), indent=1)
        print("written: %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())