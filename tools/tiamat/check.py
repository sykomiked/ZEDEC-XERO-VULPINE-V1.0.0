#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Consistency checks for the Tiamat roster, manifest and swarm config.

    python3 tools/tiamat/check.py
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import assign_dna_hobbies  # noqa: E402
import roster  # noqa: E402

ROOT = HERE.parents[1]
bad = []
names = {c["name"]: c for c in roster.ADMITTED}
if len(names) != len(roster.ADMITTED):
    bad.append("duplicate dragon names")
ids = {c["id"] for c in roster.ADMITTED}
for i, *_ in roster.EXCLUDED:
    if i in ids:
        bad.append(f"{i} is both admitted and excluded")
for c in roster.ADMITTED:
    if c["trunk"] and c["trunk"] not in ids:
        bad.append(f"{c['name']}: trunk {c['trunk']} not admitted")
    if c["trunk"] and not c["ship"].startswith("adapter"):
        bad.append(f"{c['name']}: has a trunk but does not ship as an adapter")

cfg = json.loads((HERE / "tiamat_swarm.json").read_text())
placed = set()
for organ, o in cfg["organs"].items():
    for n in o["cells"]:
        if n not in names:
            bad.append(f"organ {organ}: unknown cell {n}")
        placed.add(n)
for t in cfg["tissues"].values():
    placed.add(t["trunk"])
    for n in t["cells"]:
        if names.get(n, {}).get("trunk") is None:
            bad.append(f"tissue cell {n} has no trunk")
for n in names:
    if n not in placed:
        bad.append(f"cell {n} is in no organ or tissue")
fib = {1, 2}
a, b = 1, 2
while b < 10**7:
    a, b = b, a + b
    fib.add(b)
for p, v in cfg["isf"]["tokens_per_cycle"].items():
    if v not in fib:
        bad.append(f"profile {p}: budget {v} is not a Fibonacci number")
for p in cfg["isf"]["tokens_per_cycle"]:
    if p not in cfg["profiles"]:
        bad.append(f"budget for unknown profile {p}")

man = json.loads((ROOT / "docs/release/swarm_model_manifest.json").read_text())
mids = {m["model_id"] for m in man["models"]}
if mids != ids:
    bad.append(f"manifest out of date: run build_manifest.py ({sorted(mids ^ ids)[:5]})")
for m in man["models"]:
    if m["sha256_status"] == "pinned upstream" and not all(s["sha256"] for s in m["shipped_files"]):
        bad.append(f"{m['model_id']}: shipped file without a hash")
b = man["disc_bytes"]
if sum(v for k, v in b.items() if k != "limit") > b["limit"]:
    bad.append("disc set over its limit")

notice = (ROOT / "NOTICE").read_text()
for c in roster.ADMITTED:
    if c["id"] not in notice:
        bad.append(f"NOTICE does not credit {c['id']}")

s1 = assign_dna_hobbies.assign("00" * 32)
s2 = assign_dna_hobbies.assign("00" * 32, [s1])
if s1 == s2 or not 3 <= len(s1) <= 5:
    bad.append("hobby assignment is not unique or out of range")

if bad:
    print("\n".join(bad))
    sys.exit(1)
print(f"tiamat OK: {len(names)} cells, {len(cfg['organs'])} organs, {len(man['excluded'])} excluded")
