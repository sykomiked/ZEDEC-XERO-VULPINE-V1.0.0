#!/usr/bin/env python3
# mk_decoding_library.py — the decoding reference for the drafting model:
# every real block from the built apps, grouped by runtime, so the model
# learns each pillar language's exact spelling from working code rather
# than guessing. Writes tools/agent_loop/decoding/<runtime>.md and a
# combined reference the prompt can carry.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_decoding_library.py [--out tools/agent_loop/decoding]"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BLOCKS = os.path.join(ROOT, "kernel/src/apps/blocks")
EXTS = {".kron": "kron", ".eve": "eve", ".myra": "myra", ".sutra": "sutra",
        ".tb": "trebago", ".zlir": "zlir", ".fth": "forth"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tools/agent_loop/decoding")
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    by_rt = {}
    for dirpath, _d, files in os.walk(BLOCKS):
        for f in sorted(files):
            ext = os.path.splitext(f)[1]
            rt = EXTS.get(ext)
            if not rt:
                continue
            body = open(os.path.join(dirpath, f), errors="replace").read()
            body = "\n".join(l for l in body.split("\n")
                             if not l.strip().startswith(("#", "!", ";", "//")))
            if body.strip():
                by_rt.setdefault(rt, []).append((f, body.strip()))
    total = 0
    for rt, items in sorted(by_rt.items()):
        doc = ["# The %s decoding reference — real blocks from the built apps" % rt, ""]
        for name, body in items[:14]:  # the 14 clearest examples
            doc += ["## %s" % name, "", "```", body[:700], "```", ""]
        with open(os.path.join(out, "%s.md" % rt), "w") as fh:
            fh.write("\n".join(doc))
        total += len(items)
        print("%-9s %d blocks" % (rt, len(items)))
    print("total %d real blocks across %d runtimes -> %s" % (total, len(by_rt), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())