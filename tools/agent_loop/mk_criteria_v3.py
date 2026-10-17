#!/usr/bin/env python3
# mk_criteria_v3.py — the exhaustive app architecture map: every one of the
# 407 apps' complete brief, fusing the language matrix (which language owns
# which component), the runtime rules (the pillar/Lingua/host tiers), the
# wave spec, and the manifest contract with its exact checks.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_criteria_v3.py [--out DIR]

Writes one <app>.brief.md per app (the full architecture of that app) and
an index _ARCHITECTURE.md (the whole map: every app, every language, every
component).
"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from patterns import match_patterns, PATTERNS  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "../.."))

# the runtime rules: what each runtime IS and how its source must look.
RUNTIME_RULES = {
    "c": "the host tier: C11, zero-malloc, fixed buffers, exact integers. The "
         "view and model layer; reaches the system through the C ABI.",
    "eve": "the pillar (Ada): ranges and limits. A package line 'eve <app>', "
           "domain declarations '<name> = <lo> .. <hi>', and blocks "
           "'<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is "
           "allowed; it cannot compute.",
    "kron": "the pillar (APL/K): exact array arithmetic as ONE expression line "
            "'<first-row-answer>|<expr with _arg names>'. Multiply, modulus, "
            "comparisons, min/max only. No floats.",
    "myra": "the pillar (MUMPS): records and indexes. One global line "
            "'^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are "
            "the field widths; the index is the first key.",
    "sutra": "the pillar (COBOL): records and procedures. A record with PIC "
             "pictures, then 'procedure division.' with COMPUTE and IF. A "
             "value that does not fit its picture is refused, never cut.",
    "trebago": "the pillar (FORTRAN): exact numerics. 'trebago <entry>' then "
               "'in  <args>' / 'out <result>' / assignments / 'end'. Integer "
               "only; sums via sum(i, expr).",
    "forth": "the run tier: stack words. '<entry> ( args -- result )' with "
             "integer words. Range laws are natural here.",
    "rust": "the host tier: safe systems code, exact integers, no floats; "
            "witnesses alongside the pillar implementation.",
    "wasm": "the host tier: the witnessed circuit; the same computation "
            "compiled to a WebAssembly module the checks can witness.",
    "c++": "the host tier: the same as C, with the stronger type surface.",
    "python": "the host tier (via the SDK): tooling and glue, exact integers.",
    "prolog": "the run tier (Lingua): relations and the reasoning laws.",
    "haskell": "the run tier (Lingua): pure functions over exact integers.",
    "mojo": "the host tier: accelerated numerics for the model paths.",
    "logo": "the run tier (Lingua): turtle/geometry steps for the visual apps.",
    "tex": "the docs tier: the app's typeset specification.",
    "apl": "the pillar (KRON): see kron.",
    "mumps": "the pillar (MYRA): see myra.",
    "ada": "the pillar (EVE): see eve.",
    "cobol": "the pillar (SUTRA): see sutra.",
    "fortran": "the pillar (TREBAGO): see trebago.",
    "javascript": "the host tier: the browser surface.",
    "typescript": "the host tier: the browser surface, typed.",
}


def graph():
    g = "/tmp/appgraph_v3.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    return json.load(open(g))


def spec_of(task):
    p = os.path.join(ROOT, "tools/server-tasks", task)
    return open(p, errors="replace").read() if task and os.path.exists(p) else ""


def logic_clauses(spec):
    m = re.search(r"^Logic[^:]*:\s*(.+?)(?=\n[A-Za-z][A-Za-z ]{2,20}:|\Z)", spec, re.M | re.S)
    if not m:
        return []
    return [c.strip().rstrip(".") for c in re.split(r";|\n", m.group(1)) if c.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/criteria")
    args = ap.parse_args()
    g = graph()
    nodes, order = g["nodes"], g.get("order", [])
    rows = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    index = ["# The 407 app architecture map", "",
             "Every app: its matrix layer, purpose, capabilities, the language that owns "
             "each component, and the manifest contract with exact checks.", ""]
    n = 0
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("kind") != "app":
            continue
        spec = spec_of(node.get("task", ""))
        purpose = ""
        m = re.search(r"^What it does:\s*(.+)$", spec, re.M)
        if m:
            purpose = m.group(1).strip()
        needs = []
        m = re.search(r"^needs:\s*(.+)$", spec, re.M)
        if m:
            needs = [x.strip().rstrip(".") for x in m.group(1).split(",")]
        clauses = logic_clauses(spec)
        pats = []
        for c in clauses:
            for p in match_patterns(c)[:2]:
                if p not in pats:
                    pats.append(p)
        app_rows = rows.get(o, [])
        doc = ["# %s — the complete brief" % o, "",
               "| | |", "|---|---|",
               "| matrix layer | %s |" % node.get("slot"),
               "| icon group | L%s |" % node.get("layer"),
               "| wave spec | `%s` |" % node.get("task"), "",
               "## Purpose", "", purpose or "(from the wave spec)", "",
               "## Capabilities (the exact registry names for `needs`)", ""]
        for c in needs:
            doc.append("- `%s`" % c)
        doc += ["", "## The language map: which language owns which component", ""]
        if app_rows:
            doc += ["| language | runtime | owns | feature | role | status |",
                    "|---|---|---|---|---|---|"]
            for r in app_rows:
                doc.append("| %s | `%s` | %s | %s | %s | %s |"
                           % (r["language"], r["runtime"], r["owns"], r["feature"],
                              r["role"], r["status"]))
        else:
            doc.append("(no matrix rows; assign per the catalogue)")
        doc += ["", "## The runtime rules (how each named runtime's source must look)", ""]
        seen = set()
        for r in app_rows:
            rt = r["runtime"].strip().lower()
            if rt in seen:
                continue
            seen.add(rt)
            rule = RUNTIME_RULES.get(rt, "see docs/LANGUAGE_CATALOGUE.md for this tier")
            doc.append("- **%s** (%s): %s" % (r["runtime"], r["language"], rule))
        doc += ["", "## The spec's Logic clauses (what each component computes)", ""]
        for c in clauses:
            doc.append("- %s" % c)
        doc += ["", "## The entries and their exact checks", ""]
        if pats:
            doc += ["| entry (pattern) | kind | args | language | exact check |",
                    "|---|---|---|---|---|"]
            for p in pats:
                doc.append("| `%s` | %s | `%s` | %s | `%s` |"
                           % (p["name"], p["kind"], p["args"],
                              p["langs"].split(",")[0].strip(), p["checks"]))
        else:
            doc.append("(no library pattern matched; author the entries from the clauses)")
        doc += ["", "## The manifest contract", "", "```",
                "zxapp 1", "id %s" % o,
                "matrix %s" % node.get("slot"),
                "name %s" % o.replace("_", " ").title()]
        for c in needs:
            doc.append("needs %s" % c)
        doc += ["provides <entry> num|truth <arg> <arg> ...",
                "logic <runtime> <entry> <<", "<source>", ".",
                "check <entry> <arg>=<int>,... = <answer>,...", "```", ""]
        with open(os.path.join(out, "%s.brief.md" % o), "w") as f:
            f.write("\n".join(doc) + "\n")
        index.append("- **%s** (matrix %s, L%s) — %s" % (o, node.get("slot"),
                                                         node.get("layer"),
                                                         purpose[:90]))
        n += 1
    with open(os.path.join(out, "_ARCHITECTURE.md"), "w") as f:
        f.write("\n".join(index) + "\n")
    print("wrote %d briefs + _ARCHITECTURE.md to %s" % (n, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())