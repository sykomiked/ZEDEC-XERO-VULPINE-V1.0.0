#!/usr/bin/env python3
# wire.py — the circuit wiring for the whole system. The design is a
# netlist: nodes (apps, modules, capabilities) and edges (use, needs,
# provides, include). This tool extracts the netlist, validates it like a
# circuit (no floating nodes, no dangling nets, no fan-out to nothing,
# no shorted layers), and emits the wiring report and the inter-app test
# edges. Wire it like advanced technology: every pin accounted for.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: wire.py [--netlist out.json] [--check] [--tests]"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def read(p, enc="utf-8"):
    try:
        return open(p, encoding=enc, errors="replace").read()
    except OSError:
        return ""


def netlist():
    """The nodes and edges of the whole system."""
    g = "/tmp/wire_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    graph = json.load(open(g))
    nodes = {}
    edges = []          # (from, to, kind, detail)

    # the app nodes: id -> {slot, task, uses, needs, provides}
    order = graph.get("order", [])
    for o in order:
        n = graph["nodes"].get("app:" + o) or graph["nodes"].get(o)
        if not n or n.get("kind") != "app":
            continue
        nodes["app:" + o] = {"kind": "app", "slot": n.get("slot")}
        # the needs and use edges from the SPEC (the authority): the
        # `needs:` line and the `use <id> <layer>` references
        task = n.get("task", "")
        spec = read(os.path.join(ROOT, "tools/server-tasks", task)) if task else ""
        m = re.search(r"^needs:\s*(.+)$", spec, re.M)
        if m:
            for c in m.group(1).split(","):
                c = c.strip().rstrip(".")
                if c:
                    edges.append(("app:" + o, "cap:" + c, "needs", ""))
        for uid in re.findall(r"`use ([a-z_]+) \d+`", spec):
            edges.append(("app:" + o, "app:" + uid, "use", "spec"))

    # the capability nodes: from the kernel's registry
    z = read(os.path.join(ROOT, "kernel/src/apps/zxapp.c"))
    i = z.find("CAPS[] = {")
    if i >= 0:
        j = z.find("};", i)
        for c in re.findall(r'"([a-z.]+)"', z[i:j]):
            nodes["cap:" + c] = {"kind": "capability"}

    # the module nodes and their include edges (the layer graph)
    strata = read(os.path.join(ROOT, "build_system/strata.txt"))
    for line in strata.split("\n"):
        m = re.match(r"^\s*(S\d)\s+(\S+)", line)
        if m:
            nodes["mod:" + m.group(2)] = {"kind": "module", "stratum": m.group(1)}
    for d in ("kernel/src", "kernel/arch/x86_64", "kernel/arch/arm64"):
        for dp, _dn, fn in os.walk(os.path.join(ROOT, d)):
            for f in fn:
                if not f.endswith((".c", ".h")):
                    continue
                src = os.path.relpath(os.path.join(dp, f), ROOT)
                t = read(os.path.join(dp, f))
                for inc in re.findall(r'#include\s+"([^"]+)"', t):
                    edges.append(("file:" + src, "inc:" + inc, "include", ""))
    return {"nodes": nodes, "edges": edges}


def check(nl):
    """Validate the netlist like a circuit: every node driven, every net
    terminated, no floating pins."""
    nodes, edges = nl["nodes"], nl["edges"]
    problems = []

    # 1. dangling edges: an edge to a node that is not in the netlist
    node_ids = set(nodes)
    for a, b, kind, _d in edges:
        if kind == "use" and b not in node_ids:
            problems.append(("dangling-use", a, b))
        if kind == "needs" and b not in node_ids:
            problems.append(("unregistered-capability", a, b))

    # 2. floating nodes: an app with no outgoing edge of any kind
    has_out = defaultdict(set)
    for a, b, kind, _d in edges:
        has_out[a].add(kind)
    for nid, n in nodes.items():
        if n["kind"] == "app" and not has_out[nid]:
            problems.append(("floating-app", nid, "no needs, no use"))

    # 3. unconnected capabilities: a capability nothing needs
    needed = {b for a, b, k, _ in edges if k == "needs"}
    for nid, n in nodes.items():
        if n["kind"] == "capability" and nid not in needed:
            problems.append(("unconnected-capability", nid, "no app needs it"))

    # 4. undriven modules: a module nothing includes and that includes nothing
    inc_from = defaultdict(set)
    inc_to = defaultdict(set)
    for a, b, k, _ in edges:
        if k == "include":
            inc_from[a].add(b)
            inc_to[b].add(a)
    for nid, n in nodes.items():
        if n["kind"] == "module":
            if nid not in inc_to and nid not in inc_from:
                problems.append(("floating-module", nid, "no include either way"))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--netlist", default="")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--tests", action="store_true")
    args = ap.parse_args()
    nl = netlist()
    kinds = defaultdict(int)
    for n in nl["nodes"].values():
        kinds[n["kind"]] += 1
    ekinds = defaultdict(int)
    for _a, _b, k, _d in nl["edges"]:
        ekinds[k] += 1
    print("== the wiring netlist ==")
    print("nodes:  " + ", ".join("%s %d" % (k, v) for k, v in sorted(kinds.items())))
    print("edges:  " + ", ".join("%s %d" % (k, v) for k, v in sorted(ekinds.items())))
    if args.check:
        probs = check(nl)
        by = defaultdict(int)
        for p in probs:
            by[p[0]] += 1
        print("wiring problems: %d" % len(probs))
        for k, v in sorted(by.items(), key=lambda x: -x[1]):
            print("  %-26s %d" % (k, v))
        for p in probs[:10]:
            print("    %s: %s (%s)" % p)
    if args.netlist:
        json.dump(nl, open(args.netlist, "w"), indent=1)
        print("netlist written: %s" % args.netlist)
    if args.tests:
        # the inter-app test edges: every use edge becomes a T3 contract
        out = os.path.join(ROOT, "tools/server-tasks/tests")
        os.makedirs(out, exist_ok=True)
        n = 0
        bysrc = defaultdict(list)
        for a, b, k, _d in nl["edges"]:
            if k == "use":
                bysrc[a].append(b)
        for src, dsts in bysrc.items():
            aid = src.split(":", 1)[1]
            lines = ["# T3 — %s: the inter-app contracts" % aid, "zxapp 1",
                     "id %s_t3" % aid, "matrix 1",
                     "name %s T3" % aid.replace("_", " ").title()]
            for d in dsts:
                lines.append("# consumes app:%s" % d.split(":", 1)[1])
            open(os.path.join(out, "%s_t3.zxapp" % aid), "w").write("\n".join(lines) + "\n")
            n += 1
        print("T3 inter-app contracts written: %d" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())