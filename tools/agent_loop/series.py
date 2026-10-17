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
        if not node.get("done") and node.get("kind") == "app":
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
            f"is the authority on what the app does and its exact logic. Produce:\n"
            f"1. The manifest skeleton (id, slot, layer, needs, provides, budget) "
            f"per kernel/src/apps/zxapp.h.\n"
            f"2. The logic entries in the languages assigned below — use each "
            f"language where it excels (eve for safety/limits, kron for exact "
            f"arrays, myra for records/indexes, C/Sutra/wasm for the rest). "
            f"Every entry complete, no TODO, no stubs.\n"
            f"3. A test plan naming the exact checks the node would run.\n"
            f"4. The reuse lines: what the spec says to 'use' and what to "
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