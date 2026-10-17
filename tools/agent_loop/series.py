#!/usr/bin/env python3
# series.py — generate the next app-drafting queue from the build graph.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Turn the next pending apps (by dependency order) into agent_loop tasks.

Each task gives the model the app's wave spec and its language rows from
APP_LANGUAGES_407.csv, and asks for a complete draft of the .zxapp manifest
skeleton: the manifest header (id, slot, layer, needs, provides), the logic
entries per assigned language (each language where it excels), and a test
plan — everything reviewable before any code lands in the OS. The point is
develop-then-integrate: apps draft and debug outside the kernel image, and
the nine nodes load them at runtime via zxapp_load.

Usage: series.py N [--out QUEUE]
"""
import argparse
import csv
from pathlib import Path
import json
import sys

import yaml

ROOT = "tools/server-tasks"
GRAPH = "/tmp/appgraph.json"
LANGS = f"{ROOT}/APP_LANGUAGES_407.csv"
SPEC = "tools/apps/app_graph.py"

APP_SKELETON = """\
# Draft only: review before anything lands. The .zxapp manifest format is
# kernel/src/apps/zxapp.h; logic entries live per language runtime (eve for
# safety/limits, kron for exact arrays, myra for records, Sutra/C/wasm for
# the rest). Each entry: 'logic <lang> <name>' then indented lines. Keep
# every entry within ZXAPP_LOGIC_MAX lines and ZXAPP_USES uses.
"""


def already_drafted(app_id):
    """A draft exists when a result file names it."""
    from pathlib import Path
    res = Path("tools/agent_loop/results")
    if not res.is_dir():
        return False
    for f in res.glob("*.txt"):
        if f.name.endswith("_draft_%s.txt" % app_id) or \
           ("draft_%s" % app_id) in f.name:
            return True
    return False


def next_pending(n):
    import subprocess
    subprocess.run([sys.executable, SPEC, "--json", GRAPH], check=True,
                   capture_output=True)
    g = json.load(open(GRAPH))
    nodes = g["nodes"]
    order = g.get("order", [])
    out = []
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if node is None:
            continue
        if not node.get("done") and node.get("kind") == "app" and not already_drafted(o):
            out.append((o, node))
        if len(out) >= n:
            break
    return out


def lang_rows(app_id):
    """The app's rows from APP_LANGUAGES_407.csv (app names are unique)."""
    rows = []
    with open(LANGS) as f:
        for r in csv.DictReader(f):
            if r["app"].strip().lower() == app_id.lower():
                rows.append(f"{r['language']}/{r['runtime']}: {r['feature']} ({r['role']}, {r['status']})")
    return rows


REF_MANIFEST = "\n".join(Path(__file__).parent.joinpath(
    "../../kernel/src/apps/banker.zxapp").resolve().read_text().split("\n")[:34])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("n", type=int, nargs="?", default=5)
    ap.add_argument("--out", default="tools/agent_loop/queue.yaml")
    args = ap.parse_args()
    tasks = []
    for i, (app_id, node) in enumerate(next_pending(args.n), 1):
        task = node.get("task", "")
        spec_path = f"{ROOT}/{task}" if task else ""
        lang = lang_rows(app_id)
        task_text = (
            f"Draft the .zxapp application '{app_id}' (layer {node.get('layer')}, "
            f"slot {node.get('slot')}) from its wave spec. Read the spec first: it "
            f"is the authority on what the app does and its exact logic.\n"
            f"OUTPUT FORMAT (non-negotiable): the COMPLETE, LOADABLE .zxapp "
            f"manifest text, starting with the line 'zxapp 1' and ending at the "
            f"last check line. NO C code, NO explanations, NO markdown fences. "
            f"The manifest is parsed and TESTED by a running node — a single "
            f"syntax error fails it. Follow the reference manifest exactly:\n"
            f"--- reference (banker.zxapp) ---\n{REF_MANIFEST}\n--- end reference ---\n"
            f"Requirements:\n"
            f"1. The header: zxapp 1 / id / name / version / slot / layer.\n"
            f"2. needs lines for every capability the spec names.\n"
            f"3. provides + logic lines in the languages assigned below — use "
            f"each where it excels (eve for safety/limits, kron for exact arrays, "
            f"myra for records/indexes, sutra/cobol for records and procedures, "
            f"wasm/C for the rest). Self-contained entries use INLINE sources "
            f"(logic <lang> <entry> << then the source, then a line with only '.').\n"
            f"4. check lines (K2 proofs) for EVERY provided entry, with exact "
            f"inputs and expected outputs from the spec's logic.\n"
            f"5. The reuse lines: what the spec says to 'use' and what to "
            f"register in CONCEPTS.tsv.\n"
            f"Assigned languages: {', '.join(lang) if lang else 'see the spec'}."
        )
        files = [spec_path] if spec_path else []
        tasks.append({"id": i, "title": f"draft {app_id}",
                      "task": task_text, "files": files, "tokens": 1800})
    with open(args.out, "w") as f:
        yaml.safe_dump(tasks, f, sort_keys=False)
    print(f"queued {len(tasks)}: {', '.join(t['title'] for t in tasks)}")


if __name__ == "__main__":
    sys.exit(main())