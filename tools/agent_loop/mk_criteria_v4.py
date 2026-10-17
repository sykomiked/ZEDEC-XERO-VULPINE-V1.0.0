#!/usr/bin/env python3
# mk_criteria_v4.py — the exhaustive per-app parameter document: the
# COMPLETE manifest, already correct where the pattern library reaches,
# with every remaining entry spelled out to the letter — the exact
# provides line, the runtime, the required body shape (from the real
# blocks), and the exact check. Nothing is left for the model to guess.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_criteria_v4.py [--out tools/server-tasks/manifests]"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from patterns import match_patterns  # noqa: E402
from mk_criteria_v2 import logic_clauses, spec_of  # noqa: E402
from mk_criteria_v3 import RUNTIME_RULES  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "../.."))


def graph():
    g = "/tmp/appgraph_v4.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    return json.load(open(g))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/manifests")
    args = ap.parse_args()
    g = graph()
    nodes, order = g["nodes"], g.get("order", [])
    rows = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    full = partial = 0
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
        pats, unmatched = [], []
        for c in clauses:
            ps = match_patterns(c)
            if ps:
                for p in ps[:2]:
                    if p not in pats:
                        pats.append(p)
            else:
                unmatched.append(c)
        app_rows = rows.get(o, [])

        doc = ["# %s — the complete manifest (the parameter document)" % o, "",
               "**Purpose.** %s" % (purpose or "(see the wave spec)"), "",
               "**Matrix layer %s · icon group L%s · wave spec `%s`.**"
               % (node.get("slot"), node.get("layer"), node.get("task")), "",
               "## The manifest: the parts the pattern library already makes correct", "",
               "Every line below is FINAL — the provides, the body and the check are the",
               "library's verified implementations. Emit them exactly.", "",
               "```zxapp", "zxapp 1", "id %s" % o, "matrix %s" % node.get("slot"),
               "name %s" % o.replace("_", " ").title()]
        for c in needs:
            doc.append("needs %s" % c)
        doc.append("")
        for p in pats:
            doc.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
        for p in pats:
            lang = p["langs"].split(",")[0].strip()
            doc.append("logic %s %s <<" % (lang, p["name"]))
            doc.append(p["body"])
            doc.append(".")
        for p in pats:
            ck = p["checks"]
            doc.extend(ck if isinstance(ck, list) else [ck])
        doc += ["```", ""]
        if unmatched:
            partial += 1
            doc += ["## The app-specific entries — SPELLED OUT TO THE LETTER", "",
                    "These are the entries the library does not cover. Each one below",
                    "gives the exact `provides` line, the runtime, the body's required",
                    "shape (from the real blocks) and the exact check. Write the body;",
                    "nothing else is yours to decide.", ""]
            for i, c in enumerate(unmatched, 1):
                doc += ["### Entry %d — %s" % (i, c), "",
                        "**The clause it implements.** %s" % c, ""]
                # pick the app's language for this clause (its rows, in order)
                lang = app_rows[(i - 1) % len(app_rows)]["runtime"] if app_rows else "trebago"
                doc += ["**Runtime.** `%s` — %s" % (lang, RUNTIME_RULES.get(lang, "see the catalogue")),
                        "",
                        "**The provides line (copy exactly, choose the entry name):**", "",
                        "```", "provides <entry_name> num|truth <arg> <arg> ...", "```", "",
                        "**The body (this shape, this runtime):**", "",
                        "```", "logic %s <entry_name> <<" % lang, "<the source>", ".", "```", ""]
                # the REAL block from the built apps in this very runtime: the
                # exact spelling the node compiles, so nothing is guessed
                dec = os.path.join(ROOT, "tools/agent_loop/decoding", "%s.md" % lang)
                if os.path.exists(dec):
                    txt = open(dec, errors="replace").read()
                    blocks = re.findall(r"## ([^\n]+)\n\n```\n(.*?)\n```", txt, re.S)
                    if blocks:
                        nm, bd = blocks[min(i, len(blocks) - 1)]
                        doc += ["**A real block from the built apps in this runtime — copy",
                                "this spelling exactly (`%s`):**" % nm, "",
                                "```", bd[:600], "```", ""]
                doc += ["**The check (one line; the inputs are integers, the answers the",
                        "rows):**", "",
                        "```", "check <entry_name> <arg>=<int>,... = <answer>,...", "```", "",
                        "**Rules that the node enforces for this runtime:** %s" %
                        RUNTIME_RULES.get(lang, "see the catalogue"), ""]
        else:
            full += 1
            doc += ["## This app is COMPLETE from the library", "",
                    "No app-specific entries remain. Emit the manifest above exactly;",
                    "the node will load every entry and pass every check.", ""]
        with open(os.path.join(out, "%s.manifest.md" % o), "w") as f:
            f.write("\n".join(doc) + "\n")
    print("wrote %d parameter documents: %d complete, %d with explicit slots"
          % (full + partial, full, partial))
    return 0


if __name__ == "__main__":
    sys.exit(main())