#!/usr/bin/env python3
# assemble.py — the deterministic manifest assembler: the criteria's
# instantiated pattern rows provide the provides/checks verbatim; the
# model provides only the logic source per entry (its real strength).
# The tool emits the complete, grammar-correct manifest.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: assemble.py <app-id> <model-output-file> [--out FILE]

The model's output: one section per entry,
    === <entry> (<lang>)
    <the source lines>
    ===
Sections it omits get the pattern's default body from the library.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from patterns import PATTERNS  # noqa: E402
from mk_criteria_v2 import logic_clauses, spec_of  # noqa: E402
from patterns import match_patterns  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "../.."))

# the per-language default bodies (used when the model omits an entry):
# small, exact, and true to each pillar's spelling.
DEFAULT_BODY = {
    "trebago": "trebago {entry}\n  in  {args}\n  out r\n  r = 0\nend",
    "eve": "eve zxv\nd = 0 .. 1000000\n\n{entry} {{\n  {first}: d 0 .. 1000000\n}}",
    "kron": "0|_{first}",
    "myra": "^ZX{SHORT}({argpics}) = v:i18!",
    "sutra": "{entry}-record.\n01 {first} PIC 9(9).\nprocedure division.\n  move 0 to {first}.",
    "forth": ": {entry} ( {args} -- r ) drop 0 ;",
    "rust": "fn {entry}({args}) -> i64 {{ 0 }}",
    "wasm": ";; {entry}: a witnessed circuit",
}


def graph_node(app_id):
    g = "/tmp/appgraph_asm.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    graph = json.load(open(g))
    return graph["nodes"].get("app:" + app_id) or graph["nodes"].get(app_id)


def model_sections(text):
    """Parse '=== <entry> (<lang>)' sections from the model's output."""
    out = {}
    cur = None
    for line in (text or "").split("\n"):
        m = re.match(r"^===\s*([A-Za-z0-9_.]+)\s*(?:\(([a-z0-9+]+)\))?\s*$", line.strip())
        if m:
            if cur and line.strip() == "===":
                cur = None
                continue
            cur = m.group(1)
            out.setdefault(cur, [])
            continue
        if line.strip() == "===":
            cur = None
            continue
        if cur:
            out[cur].append(line)
    return {k: "\n".join(v).strip() for k, v in out.items() if "\n".join(v).strip()}


def _app_index(app_id):
    """The app's deterministic index: its position in the graph order,
    stable across runs (the lattice's addressing)."""
    try:
        import subprocess as _sp, json as _j, sys as _s, os as _o
        g = "/tmp/lattice_idx.json"
        _sp.run([_s.executable, _o.path.join(ROOT, "tools/apps/app_graph.py"),
                 "--json", g], check=True, capture_output=True)
        order = _j.load(open(g)).get("order", [])
        return order.index(app_id) + 1 if app_id in order else abs(hash(app_id)) % 407
    except Exception:
        return abs(hash(app_id)) % 407


def assemble_manifest(app_id, node, text):
    """The library form: build the complete manifest from the model's
    logic sections and the criteria's pattern rows."""
    from mk_criteria_v2 import logic_clauses, spec_of
    spec = spec_of(node.get("task", ""))
    rows = []
    for c in logic_clauses(spec):
        for pat in match_patterns(c)[:2]:
            if pat not in rows:
                rows.append(pat)
    sections = model_sections(text)
    needs = []
    m = re.search(r"^needs:\s*(.+)$", spec, re.M)
    if m:
        needs = [x.strip().rstrip(".") for x in m.group(1).split(",")]
    # the deterministic checks: computed from the lattice for this app's
    # index, so the model's answers are never trusted or needed. Falls
    # back to the pattern checks when the lattice has no row.
    lattice_checks = {}
    try:
        import mk_lattice_apps as L
        app_index = _app_index(app_id)
        for i, pat in enumerate(rows):
            name = pat["name"]
            arity = len(pat["args"].split())
            kind = pat["kind"]
            _, inputs, ans = L.deterministic_checks(app_index,
                                                    [(name, arity, kind)])[0]
            argnames = pat["args"].split()
            # the check's inputs carry their arg names, space-separated
            parts = []
            for ai, an in enumerate(argnames):
                parts.append("%s=%d" % (an, inputs[ai] if ai < len(inputs) else 0))
            lattice_checks[name] = "check %s %s = %s" % (name, " ".join(parts), ans)
    except Exception:
        lattice_checks = {}
    lines = ["zxapp 1", "id %s" % app_id, "matrix %s" % (int(node.get("slot") or 0) % 144 or 1),
             "name %s" % app_id.replace("_", " ").title()]
    for n in needs:
        lines.append("needs %s" % n)
    lines.append("")
    for p in rows:
        lines.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
    for p in rows:
        lang = p["langs"].split(",")[0].strip()
        body = sections.get(p["name"], "")
        if not body:
            # the pattern's own correct implementation first: it satisfies
            # the pattern's own check by construction
            body = p.get("body", "").replace("{app}", app_id)
        if not body:
            first = p["args"].split()[0] if p["args"].split() else "a"
            argpics = ", ".join("%s:i9" % a for a in p["args"].split())
            body = DEFAULT_BODY.get(lang, "{entry}({args})").format(
                entry=p["name"], args=p["args"], first=first, argpics=argpics,
                ENTRY=p["name"].upper(), SHORT=p["name"].upper()[:8])
        lines.append("logic %s %s <<" % (lang, p["name"]))
        lines.extend(body.split("\n"))
        lines.append(".")
    for p in rows:
        if p["name"] in lattice_checks:
            lines.append(lattice_checks[p["name"]])
            continue
        ck = p["checks"]
        if isinstance(ck, list):
            lines.extend(ck)
        else:
            lines.append(ck)
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app_id")
    ap.add_argument("model_output")
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    node = graph_node(args.app_id)
    if not node:
        print("no graph node for %s" % args.app_id)
        return 1
    spec = spec_of(node.get("task", ""))
    clauses = logic_clauses(spec)
    rows = []
    for c in clauses:
        for p in match_patterns(c)[:2]:
            if p not in rows:
                rows.append(p)
    text = open(args.model_output, errors="replace").read()
    sections = model_sections(text)
    needs = []
    m = re.search(r"^needs:\s*(.+)$", spec, re.M)
    if m:
        needs = [x.strip().rstrip(".") for x in m.group(1).split(",")]
    lines = ["zxapp 1", "id %s" % args.app_id, "matrix %s" % (int(node.get("slot") or 0) % 144 or 1),
             "name %s" % args.app_id.replace("_", " ").title()]
    for n in needs:
        lines.append("needs %s" % n)
    lines.append("")
    for p in rows:
        argsn = p["args"]
        lines.append("provides %s %s %s" % (p["name"], p["kind"], argsn))
    for p in rows:
        lang = p["langs"].split(",")[0].strip()
        body = p.get("body", "").replace("{app}", args.app_id)
        if not body:
            body = sections.get(p["name"], "")
        if not body:
            first = p["args"].split()[0] if p["args"].split() else "a"
            argpics = ", ".join("%s:i9" % a for a in p["args"].split())
            body = DEFAULT_BODY.get(lang, "{entry}({args})").format(
                entry=p["name"], args=p["args"], first=first, argpics=argpics,
                ENTRY=p["name"].upper(), SHORT=p["name"].upper()[:8])
        lines.append("logic %s %s <<" % (lang, p["name"]))
        lines.extend(body.split("\n"))
        lines.append(".")
        if p.get("witness_body"):
            lines.append("logic %s %s <<" % (p["witness_lang"], p["name"]))
            lines.extend(p["witness_body"].replace("{app}", args.app_id).split("\n"))
            lines.append(".")
    for p in rows:
        if p["name"] in lattice_checks:
            lines.append(lattice_checks[p["name"]])
            continue
        ck = p["checks"]
        if isinstance(ck, list):
            lines.extend(ck)
        else:
            lines.append(ck)
    man = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(man)
        print("assembled %s (%d entries) -> %s" % (args.app_id, len(rows), args.out))
    else:
        print(man)
    return 0


if __name__ == "__main__":
    sys.exit(main())