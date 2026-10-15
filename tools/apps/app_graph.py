#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The 407 apps as one graph: what each needs, what each unlocks, how far along it is.

Apps are not built in a line. One app expands into the apps that contain it, and those
need components of their own, so finishing one shared piece moves every app that
reaches it. This tool reads only what the repository already records:

  - the parts an L2 or L3 app is built from ("Depends on:" in its task file,
    tools/server-tasks/wave*/NNN-<id>.md), and the blocks and apps a built
    app's manifest uses (kernel/src/apps/*.zxapp, "use <id>");
  - the layers an L1 app's code leans on (MATRIX_ORDER.tsv's leans_on, only
    those built before it, since an app uses only what is nearer the core);
  - the matrix's Zeckendorf ancestors: an L1 layer N that is not a Fibonacci
    number is F + M, the largest Fibonacci number F <= N and the rest M, and
    it draws on layers F and M (the anchors are the seeds the rest grow from);
  - the daemon methods a task names (`a.b` in backquotes): built when
    api/zxvd.openrpc.json lists them, missing when it does not.

A leans_on edge that would close a cycle with the edges above is left out
and listed; the others are the app's own words or the matrix's rule.

Every app's closure is itself and everything it reaches. A node's unlock count
is how many apps' closures hold it. Completion is weighted by unlocks:

    done% = sum(unlock(n) for built n) / sum(unlock(n) for all n)

over the apps and shared blocks (the daemon methods they call are counted in a
second figure, and the missing ones listed), in hundredths of a percent
(integers only), so one shared component that lands
moves the figure by as much as it moves every app that needs it; the flat
count of built apps is printed beside it. An app's own figure is the share of
its closure's apps and blocks that is built. The build order takes, each step, the unbuilt app
whose parts are all built (or ordered before it) and that unlocks the most,
ties by matrix position, then slot.

Writes docs/APP_GRAPH.md and tools/server-tasks/BUILD_ORDER.tsv; --check
fails when either is stale (test_zxapp.sh runs it, so every new app or method
updates them in the commit that adds it); --json writes the whole graph.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TASKS = os.path.join(ROOT, "tools", "server-tasks")
APPS = os.path.join(ROOT, "kernel", "src", "apps")
DOC = os.path.join(ROOT, "docs", "APP_GRAPH.md")
ORDER = os.path.join(TASKS, "BUILD_ORDER.tsv")
HEAD = "# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC\n" \
       "# SPDX-License-Identifier: Apache-2.0\n"

TASK_RE = re.compile(r"(\d{3})-([a-z0-9_]+)\.md")
PART_RE = re.compile(r"\b(\d{3}) ([a-z0-9_]+)\b")
METHOD_RE = re.compile(r"`([a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+)`")


def fibs(top):
    f = [1, 2]
    while f[-1] + f[-2] <= top:
        f.append(f[-1] + f[-2])
    return f


def zeckendorf_parents(n, fib):
    """N = F + M with F the largest Fibonacci number <= N: (F, M); () for an anchor."""
    if n in fib:
        return ()
    f = max(x for x in fib if x <= n)
    return (f, n - f)


def layer_of(slot):
    return 1 if slot <= 144 else 2 if slot <= 271 else 3


def read_tasks():
    """slot -> (id, task path); the wave 0 apps have manifests but no task."""
    out = {}
    for d in sorted(os.listdir(TASKS)):
        if not re.fullmatch(r"wave\d+", d):
            continue
        for f in sorted(os.listdir(os.path.join(TASKS, d))):
            m = TASK_RE.fullmatch(f)
            if m:
                out[int(m.group(1), 10)] = (m.group(2), os.path.join(d, f))
    return out


def read_manifests():
    """id -> {slot, uses}, for every app and block in the tree."""
    out = {}
    for f in sorted(os.listdir(APPS)) + [os.path.join("blocks", x) for x in
                                         sorted(os.listdir(os.path.join(APPS, "blocks")))]:
        if not (f.endswith(".zxapp") or f.endswith(".zxblock")):
            continue
        ent = {"slot": 0, "uses": [], "block": f.endswith(".zxblock"), "id": None}
        for line in open(os.path.join(APPS, f), encoding="utf-8"):
            w = line.split("#", 1)[0].split()
            if len(w) >= 2 and w[0] == "id":
                ent["id"] = w[1]
            elif len(w) >= 2 and w[0] == "slot":
                ent["slot"] = int(w[1], 10)
            elif len(w) >= 2 and w[0] == "use":
                ent["uses"].append(w[1])
        if ent["id"]:
            out[ent["id"]] = ent
    return out


def read_order():
    """id -> (matrix position, leans_on slots) from MATRIX_ORDER.tsv."""
    out = {}
    for line in open(os.path.join(TASKS, "MATRIX_ORDER.tsv"), encoding="utf-8"):
        c = line.rstrip("\n").split("\t")
        if line.startswith("#") or len(c) < 7 or not c[0].isdigit():
            continue
        out[c[3]] = (int(c[0], 10), [] if c[6] == "-" else [int(x, 10) for x in c[6].split(",")])
    return out


def read_methods():
    with open(os.path.join(ROOT, "api", "zxvd.openrpc.json"), encoding="utf-8") as f:
        return {m["name"] for m in json.load(f)["methods"]}


def has_tests(app_id):
    t = os.path.join(APPS, "tests")
    return os.path.exists(os.path.join(t, f"{app_id}_zxapp.inc")) and \
        os.path.exists(os.path.join(t, f"{app_id}_apps.inc"))


def build_graph():
    tasks, mans, order, known = read_tasks(), read_manifests(), read_order(), read_methods()
    groups = {m.split(".")[0] for m in known}
    slot_id = {s: i for s, (i, _) in tasks.items()}
    for i, m in mans.items():
        if not m["block"] and m["slot"]:
            slot_id.setdefault(m["slot"], i)
    nodes, deps, cut = {}, {}, []

    def node(key, kind, done, **kw):
        if key not in nodes:
            nodes[key] = dict(kind=kind, done=done, **kw)
            deps[key] = set()
        return key

    for slot in sorted(slot_id):
        i = slot_id[slot]
        m = mans.get(i)
        wave0 = m is not None and slot not in tasks
        built = m is not None and not m["block"] and (wave0 or has_tests(i))
        node(f"app:{i}", "app", built, slot=slot, layer=layer_of(slot),
             task=tasks[slot][1] if slot in tasks else "-")
    for i, m in mans.items():
        if m["block"]:
            node(f"block:{i}", "block", True, slot=0, layer=0, task="-")
    fib = fibs(144)
    for slot in sorted(slot_id):
        a = f"app:{slot_id[slot]}"
        text = open(os.path.join(TASKS, tasks[slot][1]), encoding="utf-8").read() \
            if slot in tasks else ""
        for line in text.splitlines():
            if line.startswith("Depends on:"):
                for s, pid in PART_RE.findall(line):
                    if f"app:{pid}" in nodes:
                        deps[a].add(f"app:{pid}")
        for meth in sorted(set(METHOD_RE.findall(text))):
            if meth.split(".")[0] in groups:
                deps[a].add(node(f"method:{meth}", "method", meth in known))
        m = mans.get(slot_id[slot])
        for u in (m["uses"] if m else []):
            key = f"block:{u}" if f"block:{u}" in nodes else f"app:{u}"
            if key in nodes:
                deps[a].add(key)
        if slot <= 144:
            for p in zeckendorf_parents(slot, fib):
                if p in slot_id:
                    deps[a].add(f"app:{slot_id[p]}")

    def reaches(src, dst):
        seen, stack = set(), [src]
        while stack:
            x = stack.pop()
            if x == dst:
                return True
            if x not in seen:
                seen.add(x)
                stack.extend(deps[x])
        return False

    for slot in sorted(slot_id):  # soft: leans_on, kept only when it closes no cycle
        i = slot_id[slot]
        pos, lean = order.get(i, (0, []))
        for s in lean:
            pid = slot_id.get(s)
            if not s or pid is None or pid == i:
                continue
            before = pid not in order or order[pid][0] < pos
            if not before or f"app:{pid}" in deps[f"app:{i}"]:
                continue
            if reaches(f"app:{pid}", f"app:{i}"):
                cut.append((i, pid))
            else:
                deps[f"app:{i}"].add(f"app:{pid}")
    return nodes, deps, order, cut


def closures(nodes, deps):
    memo = {}

    def close(x, path):
        if x in memo:
            return memo[x]
        if x in path:
            raise SystemExit(f"app_graph: a cycle through {x}")
        path.add(x)
        out = {x}
        for d in deps[x]:
            out |= close(d, path)
        path.discard(x)
        memo[x] = frozenset(out)
        return memo[x]

    return {k: close(k, set()) for k in nodes if nodes[k]["kind"] == "app"}


def analyse():
    nodes, deps, order, cut = build_graph()
    clo = closures(nodes, deps)
    unlock = {k: 0 for k in nodes}
    for c in clo.values():
        for x in c:
            unlock[x] += 1
    def weighted(keep):
        total = sum(unlock[k] for k in nodes if keep(k))
        done = sum(unlock[k] for k in nodes if keep(k) and nodes[k]["done"])
        return done * 10000 // total if total else 0

    def built(k):  # what the app builders make: apps and shared blocks
        return nodes[k]["kind"] != "method"

    pct = (weighted(built), weighted(lambda k: True))
    apps = [k for k in nodes if nodes[k]["kind"] == "app"]
    per = {}
    for k in apps:
        own = [x for x in clo[k] if built(x)]
        per[k] = sum(1 for x in own if nodes[x]["done"]) * 10000 // len(own)
    # the build order: unbuilt apps, each once its app parts are built or ordered
    placed = {k for k in apps if nodes[k]["done"]}
    rank, todo = [], [k for k in apps if not nodes[k]["done"]]

    def key(k):
        i = k[4:]
        return (-unlock[k], order.get(i, (10 ** 6, []))[0], nodes[k]["slot"])

    while todo:
        ready = sorted((k for k in todo if all(d in placed for d in deps[k] if d.startswith("app:"))),
                       key=key)
        if not ready:
            raise SystemExit("app_graph: no app can be built next (a part is never built)")
        rank.append(ready[0])
        placed.add(ready[0])
        todo.remove(ready[0])
    return nodes, deps, clo, unlock, pct, per, rank, cut


def pct_text(x):
    return f"{x // 100}.{x % 100:02d}%"


def render():
    nodes, deps, clo, unlock, pct, per, rank, cut = analyse()
    apps = [k for k in nodes if nodes[k]["kind"] == "app"]
    built = [k for k in apps if nodes[k]["done"]]
    flat = len(built) * 10000 // len(apps)
    tsv = [HEAD.rstrip("\n"),
           "# The order to build the unbuilt apps in (tools/apps/app_graph.py): each one's parts",
           "# are built or come earlier; among those ready, the one that unlocks the most first.",
           "# unlock: apps whose closure holds it; open: its closure's unbuilt nodes; own: its",
           "# closure built, in hundredths of a percent.",
           "rank\tslot\tid\tlayer\tunlock\topen\town\ttask"]
    for n, k in enumerate(rank, 1):
        o = sum(1 for x in clo[k] if not nodes[x]["done"])
        tsv.append(f"{n}\t{nodes[k]['slot']}\t{k[4:]}\tL{nodes[k]['layer']}\t{unlock[k]}\t{o}\t"
                   f"{per[k]}\t{nodes[k]['task']}")
    open_nodes = sorted((k for k in nodes if not nodes[k]["done"]),
                        key=lambda k: (-unlock[k], k))
    missing = [k for k in open_nodes if nodes[k]["kind"] == "method"]
    md = ["<!--", "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
          "SPDX-License-Identifier: Apache-2.0", "-->", "",
          "# The app graph", "",
          "Generated by `tools/apps/app_graph.py` (do not edit; `--check` fails when stale). "
          "Apps grow as a web: an app expands into the apps that contain it and back into the "
          "components they need, so completion is weighted by how many apps each piece unlocks. "
          "The build order is `tools/server-tasks/BUILD_ORDER.tsv`.", "",
          f"- **Completion, weighted by unlocks:** {pct_text(pct[0])} of the apps and shared "
          "blocks (each counted once per app that needs it)",
          f"- **With the daemon methods they call:** {pct_text(pct[1])}",
          f"- **Apps built (flat count):** {len(built)} of {len(apps)} ({pct_text(flat)})",
          f"- **Nodes:** {len(apps)} apps, "
          f"{sum(1 for k in nodes if nodes[k]['kind'] == 'block')} shared blocks, "
          f"{sum(1 for k in nodes if nodes[k]['kind'] == 'method')} daemon methods named "
          f"({len(missing)} missing); "
          f"{sum(len(v) for v in deps.values())} edges", "",
          "## What unlocks the most (unbuilt)", "",
          "| node | kind | unlocks | ready now |", "|---|---|---|---|"]
    placed = {k for k in nodes if nodes[k]["done"]}
    for k in open_nodes[:40]:
        ready = all(d in placed for d in deps[k])
        md.append(f"| {k.split(':', 1)[1]} | {nodes[k]['kind']} | {unlock[k]} | "
                  f"{'yes' if ready else 'no'} |")
    md += ["", "## Daemon methods tasks name that zxvd does not have", ""]
    md += [f"- `{k[7:]}`: {unlock[k]} apps" for k in missing] or ["- none"]
    md += ["", "## By layer", "", "| layer | apps | built | weighted |", "|---|---|---|---|"]
    for L in (1, 2, 3):
        ks = [k for k in apps if nodes[k]["layer"] == L]
        b = sum(1 for k in ks if nodes[k]["done"])
        w = sum(per[k] for k in ks) // len(ks) if ks else 0
        md.append(f"| L{L} | {len(ks)} | {b} | {pct_text(w)} |")
    md += ["", "## The anchors (Fibonacci layers; the rest grow from them)", "",
           "| layer | app | built | unlocks |", "|---|---|---|---|"]
    by_slot = {nodes[k]["slot"]: k for k in apps}
    for f in fibs(144):
        k = by_slot.get(f)
        if k:
            md.append(f"| {f} | {k[4:]} | {'yes' if nodes[k]['done'] else 'no'} | {unlock[k]} |")
    md += ["", "## The next twenty", "", "| rank | app | layer | unlocks | own |",
           "|---|---|---|---|---|"]
    for n, k in enumerate(rank[:20], 1):
        md.append(f"| {n} | {k[4:]} ({nodes[k]['slot']}) | L{nodes[k]['layer']} | {unlock[k]} | "
                  f"{pct_text(per[k])} |")
    md += ["", "## Leans left out (each would close a cycle)", ""]
    md += [f"- {a} leans on {b}" for a, b in cut] or ["- none"]
    graph = {"completion_x100": pct[0], "with_methods_x100": pct[1], "nodes": {k: dict(v, unlock=unlock[k]) for k, v in nodes.items()},
             "deps": {k: sorted(v) for k, v in deps.items()}, "order": [k[4:] for k in rank]}
    return "\n".join(md) + "\n", "\n".join(tsv) + "\n", graph


def headline(md):
    return next(x for x in md.splitlines() if x.startswith("- **Completion")).replace("**", "")[2:]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when a written file is stale")
    ap.add_argument("--json", help="also write the whole graph here")
    a = ap.parse_args()
    md, tsv, graph = render()
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(graph, f, indent=1, sort_keys=True)
    if a.check:
        bad = [p for p, t in ((DOC, md), (ORDER, tsv))
               if not os.path.exists(p) or open(p, encoding="utf-8").read() != t]
        for p in bad:
            print(f"[FAIL] {os.path.relpath(p, ROOT)} is stale: python3 tools/apps/app_graph.py")
        if bad:
            return 1
        print(f"[PASS] app graph: {headline(md)}")
        return 0
    for p, t in ((DOC, md), (ORDER, tsv)):
        with open(p, "w", encoding="utf-8") as f:
            f.write(t)
    print(headline(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
