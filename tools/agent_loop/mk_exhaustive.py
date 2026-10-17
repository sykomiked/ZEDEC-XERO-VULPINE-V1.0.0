#!/usr/bin/env python3
# mk_exhaustive.py — the exhaustive parameter corpus: for every app, the
# complete, verified manifest (the pattern library's proven bodies and
# exact checks) plus the full per-entry specification, the capability
# scopes, the language bindings, the human/AI interface binding, the
# dependencies and the test plan. Nothing is left to inference.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_exhaustive.py [--out tools/server-tasks/exhaustive]"""
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

# the entry semantics: what each pattern computes, its edge cases, its
# error law. The model (or a reviewer) never has to infer these.
SEMANTICS = {
    "limit_excess": "The excess over a ceiling. 0 when value <= limit, else value - limit. "
                    "Integer only; the limit is inclusive; no negative excess ever.",
    "within_range": "The range law. true when lo <= value <= hi, false outside. "
                    "Both bounds inclusive. A truth entry: the four values apply (a "
                    "contradicted claim is both, no evidence is neither).",
    "conserved_sum": "The conservation law. true when a+b+c+d equals whole exactly. "
                     "Nothing is created or destroyed through the stages.",
    "exact_total": "The exact sum of the first n of the four sizes. Integer only, no "
                   "rounding, no overflow silently: the caller checks the bound.",
    "share_x100": "A part's share of a whole in hundredths of a percent, EXACT rational "
                  "(the K's division is exact: 10000/3, not 3333). The caller floors.",
    "largest_remainder": "The first recipient's exact share of the pool by weight; the "
                         "largest-remainder apportionment's integer part is the caller's law.",
    "modular_draw": "The sortition's modular walk. Integer only, no hashing in the block. "
                    "The residue operator is '|' in K ('%' is division).",
    "difference": "a - b exactly, never negative in the app's domain.",
    "product": "a * b exactly in minor units. Integer only.",
    "count_ok": "The registry law. true when 0 < count <= cap. A truth entry: the four "
                "values apply.",
    "composite_key": "The record's composite index: high*1000 + low. Explicit parens: K "
                     "evaluates right to left.",
    "grade_band": "The band index 0..4 of the value across the four ascending bounds. "
                  "The comparisons sum (each true adds 1).",
    "ratio_x1000": "The ratio in thousandths, exact rational. The caller floors.",
    "running_state": "The transition's legality: the (state,event) pairs that are allowed. "
                     "A truth entry: both/neither are reachable.",
    "flags_none": "No forbidden bit set: (bits & forbidden) == 0. A truth entry.",
    "clamp": "The value clamped into [lo,hi]. The result is always within the bounds.",
}


def graph():
    g = "/tmp/appgraph_ex.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    return json.load(open(g))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/server-tasks/exhaustive")
    args = ap.parse_args()
    g = graph()
    nodes, order = g["nodes"], g.get("order", [])
    rows = {}
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["app"].strip().lower(), []).append(r)
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
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
        uses = re.findall(r"`use ([a-z_]+) (\d+)`", spec)
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

        d = []
        d += ["# %s — the exhaustive specification" % o, ""]
        d += ["| field | value |", "|---|---|",
              "| app id | `%s` |" % o,
              "| matrix layer | %s |" % node.get("slot"),
              "| icon group | L%s |" % node.get("layer"),
              "| wave spec | `%s` |" % node.get("task"),
              "| status | %s |" % ("built" if node.get("done") else "pending"),
              ""]
        d += ["## 1. Purpose", "", purpose or "(see the wave spec)", ""]
        d += ["## 2. Capabilities (the exact registry names and scopes)", ""]
        for c in needs:
            d.append("- `needs %s`" % c)
        d += ["", "These are the ONLY capability names the loader accepts. A name not in "
                  "the kernel's table refuses the whole manifest.", ""]
        d += ["## 3. Dependencies (the built apps this one calls)", ""]
        if uses:
            for uid, layer in uses:
                d.append("- `use %s %s` — call `<id>.<entry>` after the use line" % (uid, layer))
        else:
            d.append("- none beyond the core (layer 0)")
        d += [""]
        d += ["## 4. The language bindings (which language owns which component)", ""]
        d += ["| language | runtime | owns | feature | role | status |", "|---|---|---|---|---|---|"]
        for r in app_rows:
            d.append("| %s | `%s` | %s | %s | %s | %s |"
                     % (r["language"], r["runtime"], r["owns"], r["feature"],
                        r["role"], r["status"]))
        d += [""]
        d += ["## 5. The complete manifest — VERIFIED (the library's proven bodies)", ""]
        d += ["Every line is final: these bodies and checks were proven against a running "
              "node (satchel: loaded, checks passed). Emit exactly.", "", "```zxapp",
              "zxapp 1", "id %s" % o, "matrix %s" % node.get("slot"),
              "name %s" % o.replace("_", " ").title()]
        for c in needs:
            d.append("needs %s" % c)
        d.append("")
        for p in pats:
            d.append("provides %s %s %s" % (p["name"], p["kind"], p["args"]))
        for p in pats:
            lang = p["langs"].split(",")[0].strip()
            d.append("logic %s %s <<" % (lang, p["name"]))
            d.append(p.get("body", "").replace("{app}", o))
            d.append(".")
        for p in pats:
            d.append(p["checks"])
        d += ["```", ""]
        d += ["## 6. The entry specifications (what each entry computes)", ""]
        for p in pats:
            d += ["### `%s` (%s) — %s" % (p["name"], p["kind"], p["langs"].split(",")[0].strip()),
                  "", "**Args.** `%s`" % p["args"], "",
                  "**Semantics.** %s" % SEMANTICS.get(p["name"], p["logic"]), "",
                  "**The exact check.** `%s`" % p["checks"], ""]
        if unmatched:
            d += ["## 7. The app-specific entries (spelled out to the letter)", "",
                  "The library does not cover these clauses. Each below gives the clause, "
                  "the runtime, and the required shapes.", ""]
            for i, c in enumerate(unmatched, 1):
                lang = app_rows[(i - 1) % len(app_rows)]["runtime"] if app_rows else "trebago"
                d += ["### 7.%d — %s" % (i, c), "",
                      "**Runtime.** `%s` — %s" % (lang, RUNTIME_RULES.get(lang, "")), "",
                      "**provides line:** `provides <entry> num|truth <arg> <arg> ...`", "",
                      "**body:** `logic %s <entry> <<` then the source then `.`" % lang, "",
                      "**check:** `check <entry> <arg>=<int>,... = <answer>,...`", ""]
        else:
            d += ["## 7. App-specific entries", "",
                  "None — this app is fully covered by the verified library. Emit the "
                  "manifest in section 5 exactly.", ""]
        d += ["## 8. The dual interface (human and AI)", "",
              "- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; "
              "its entries' views render on the framebuffer.", 
              "- **AI.** Every provided entry is callable through the app runtime "
              "(`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.",
              ""]
        d += ["## 9. The test plan", "",
              "- The node loads the manifest (`zxapp_load`): every logic entry compiles.",
              "- Every check line runs and passes (`zxapp_check`): the exact answers.",
              "- The browser engine (`zxv_app.wasm`) loads the same manifest and passes "
              "the same checks.", ""]
        with open(os.path.join(out, "%s.md" % o), "w") as f:
            f.write("\n".join(d) + "\n")
        n += 1
    print("wrote %d exhaustive specifications to %s" % (n, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())