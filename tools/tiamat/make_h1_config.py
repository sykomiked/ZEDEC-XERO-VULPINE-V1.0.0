#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write the config for harness H1 (kernel/arch/hosted/zxv_tiamat.h) from the
model manifest and a folder of downloaded models.

    python3 tools/tiamat/make_h1_config.py --models-dir DIR [--out tiamat.conf]
        [--log tiamat_log.jsonl] [--verify]

Cells, in order (the first one found leads by default):
    Spinewyrm    Qwen2.5-1.5B-Instruct, the general lead  (else Kindlewyrm, 0.5B)
    Codelindworm Qwen2.5-Coder-1.5B, leads code questions (optional)
    Vouchwyvern  SmolLM2-1.7B-Instruct, the witness of another lineage
A cell whose file is not in the folder is left out, and the script says so.
--verify checks each file's SHA-256 against the manifest (slow on big files).
"""
import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/release/swarm_model_manifest.json"

# (cell, lineage, keywords); lineages: cells of one family count as one witness
H1 = [
    ("Spinewyrm", "qwen", []),
    ("Kindlewyrm", "qwen", []),
    ("Codelindworm", "qwen", ["code", "python", "function", "program", "script", "bug",
                              "compile", "javascript", "rust", "sql", "regex", "c++"]),
    ("Vouchwyvern", "smollm", ["check", "verify"]),
]
FALLBACK = {"Kindlewyrm": "Spinewyrm"}  # used only when the cell it stands in for is missing


def find(models_dir, name):
    want = name.lower()
    for p in sorted(models_dir.iterdir()):
        if p.is_file() and p.name.lower() == want:
            return p
    return None


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def quote(s):
    s = str(s)
    if '"' in s:
        raise SystemExit(f"a path with a double quote is not supported: {s}")
    return f'"{s}"' if any(c in s for c in " \t#") else s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models-dir", required=True)
    ap.add_argument("--out", default="tiamat.conf")
    ap.add_argument("--log", default=None, help="default: tiamat_log.jsonl beside --out")
    ap.add_argument("--manifest", default=str(MANIFEST))
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()
    models_dir = pathlib.Path(a.models_dir).expanduser().resolve()
    by_name = {m["dragon_name"]: m for m in json.load(open(a.manifest))["models"]}

    slots, cells, found = [], [], set()
    for name, lineage, kws in H1:
        if name in FALLBACK and FALLBACK[name] in found:
            continue
        m = by_name[name]
        f = m["shipped_files"][0] if m["shipped_files"] else None
        p = find(models_dir, f["file"]) if f else None
        if not p:
            print(f"  -  {name}: {f['file'] if f else 'no shipped file'} not in {models_dir}",
                  file=sys.stderr)
            continue
        if a.verify:
            got = sha256(p)
            if got != f["sha256"]:
                raise SystemExit(f"{name}: {p.name} has SHA-256 {got}, the manifest pins {f['sha256']}")
        if str(p) not in slots:
            if len(slots) == 4:
                print(f"  -  {name}: no slot left (4 at most)", file=sys.stderr)
                continue
            slots.append(str(p))
        cells.append((name, slots.index(str(p)), lineage, kws, m))
        found.add(name)
        print(f"  ok {name} ({m['model_id']}, {m['developer']}) <- {p.name}", file=sys.stderr)

    if not cells:
        raise SystemExit("no H1 cell's model file is in the folder")
    if len({c[2] for c in cells}) < 2:
        print("  note: only one lineage, so answers are shown unwitnessed", file=sys.stderr)
    out = pathlib.Path(a.out).expanduser().resolve()
    log = pathlib.Path(a.log).expanduser().resolve() if a.log else out.with_name("tiamat_log.jsonl")
    lines = ["# Tiamat harness H1 (kernel/arch/hosted/zxv_tiamat.h); written by",
             "# tools/tiamat/make_h1_config.py from docs/release/swarm_model_manifest.json"]
    lines += [f"slot {i} {quote(s)}" for i, s in enumerate(slots)]
    for name, slot, lineage, kws, m in cells:
        lines.append(f"# {name}: {m['model_id']} by {m['developer']} ({m['license']})")
        lines.append(" ".join([f"cell {name} {slot} {lineage} -"] + kws))
    lines.append(f"log {quote(log)}")
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
