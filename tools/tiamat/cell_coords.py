#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Grid Chain coordinates of the Tiamat cells (tools/tiamat/cell_coords.json).

A cell's coordinate is a 64-bit Grid Chain word, fields from the most
significant: domain 8 | place 16 | item 8 | ext 16 | format 8 | lang 8.
    domain  0x60 (models)
    place   the cell's organ number, ORGANS below (1-based, append-only)
    item    the cell's ordinal within its organ (1-based, never reused)
    ext, format, lang  0
The @cell.<name> handle is an alias for the same cell, not part of the word.
The chiglet governor's model_exec call (chg_abi A7) resolves a coordinate
through a lookup generated from the JSON file.

    python3 tools/tiamat/cell_coords.py --write   add new cells, keep every old entry
    python3 tools/tiamat/cell_coords.py --check   fail if the file and roster.py disagree
    python3 tools/tiamat/cell_coords.py --check --against origin/main
                                                 also fail if an assignment differs from REF's file

Append-only: --write never changes or drops an assignment already in the file,
and --check fails when an entry's organ, place or item has changed, when a
roster cell has no entry, or when an entry names a cell the roster no longer
has (a removed cell keeps its entry, marked "retired": true, so its number is
never given to another cell).
"""
import argparse
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import roster  # noqa: E402

FILE = HERE / "cell_coords.json"
DOMAIN = 0x60
# Organ numbers: append new organs at the end, never reorder.
ORGANS = ["arbiter", "librarian", "ear", "reader", "sentinel", "voice_of_tiamat", "scribe", "augur",
          "hand", "clerk", "trunk", "warden", "reckoner", "voice", "painter", "smith", "herald",
          "natural_philosopher", "bard", "interpreter", "council"]


def word(place, item):
    if not (1 <= place <= 0xFFFF and 1 <= item <= 0xFF):
        raise ValueError(f"place {place} or item {item} out of range")
    return (DOMAIN << 56) | (place << 40) | (item << 32)


def entry(c, place, item):
    return {"name": c["name"], "organ": c["organ"], "place": place, "item": item,
            "coord": f"0x{word(place, item):016x}", "alias": f"@cell.{c['name'].lower()}",
            "repo": c["id"], "placement": c["place"], "engine": c["engine"]}


def load():
    return json.loads(FILE.read_text()) if FILE.exists() else {"domain": f"0x{DOMAIN:02x}", "cells": []}


def plan(doc):
    """The file's entries plus new ones for roster cells it lacks; the problems found."""
    cells = {c["name"]: c for c in roster.ADMITTED}
    out, problems, taken = [], [], set()
    for e in doc["cells"]:
        c = cells.get(e["name"])
        if e.get("retired"):
            out.append(e)
        elif c is None:
            problems.append(f"{e['name']}: in the file but not in roster.py (mark it retired)")
            out.append(e)
        else:
            if c["organ"] != e["organ"] or ORGANS.index(c["organ"]) + 1 != e["place"]:
                problems.append(f"{e['name']}: organ or place changed ({e['organ']} {e['place']})")
            if e["coord"] != f"0x{word(e['place'], e['item']):016x}":
                problems.append(f"{e['name']}: coord does not match place and item")
            out.append(entry(c, e["place"], e["item"]))
        key = (e["place"], e["item"])
        if key in taken:
            problems.append(f"{e['name']}: place {key[0]} item {key[1]} used twice")
        taken.add(key)
    have = {e["name"] for e in out}
    for c in roster.ADMITTED:
        if c["name"] in have:
            continue
        if c["organ"] not in ORGANS:
            problems.append(f"{c['name']}: organ {c['organ']} has no number (append it to ORGANS)")
            continue
        place = ORGANS.index(c["organ"]) + 1
        item = 1 + max([i for p, i in taken if p == place], default=0)
        taken.add((place, item))
        out.append(entry(c, place, item))
    return {"domain": f"0x{DOMAIN:02x}", "layout": "domain 8 | place 16 | item 8 | ext 16 | format 8 | lang 8",
            "organs": ORGANS, "cells": out}, problems


def against(rev, new):
    """Assignments in rev's file that new changes or drops (none when rev has no file)."""
    rel = FILE.relative_to(HERE.parent.parent).as_posix()
    r = subprocess.run(["git", "-C", str(HERE), "show", f"{rev}:{rel}"], capture_output=True, text=True)
    if r.returncode != 0:
        return [] if "does not exist" in r.stderr or "exists on disk, but not in" in r.stderr \
            else [f"git show {rev}:{rel}: {r.stderr.strip()}"]
    now = {e["name"]: (e["place"], e["item"]) for e in new["cells"]}
    return [f"{e['name']}: was place {e['place']} item {e['item']} at {rev}, now {now.get(e['name'])}"
            for e in json.loads(r.stdout)["cells"] if now.get(e["name"]) != (e["place"], e["item"])]


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--write", action="store_true")
    g.add_argument("--check", action="store_true")
    ap.add_argument("--against", help="a git revision whose cell_coords.json every assignment must keep")
    a = ap.parse_args()
    old = load()
    if old.get("organs") and old["organs"] != ORGANS[:len(old["organs"])]:
        raise SystemExit("ORGANS was reordered or shortened; organ numbers are append-only")
    new, problems = plan(old)
    text = json.dumps(new, indent=1) + "\n"
    if a.check:
        if not FILE.exists():
            problems.append(f"{FILE.name} is missing (run --write)")
        elif FILE.read_text() != text and not problems:
            problems.append(f"{FILE.name} is out of date with roster.py (run --write)")
        if a.against:
            problems += against(a.against, new)
        for p in problems:
            print(f"cell_coords: {p}", file=sys.stderr)
        if problems:
            raise SystemExit(1)
        print(f"cell_coords: {len(new['cells'])} cells, every assignment unchanged")
        return
    if problems:
        for p in problems:
            print(f"cell_coords: {p}", file=sys.stderr)
        raise SystemExit(1)
    FILE.write_text(text)
    print(f"cell_coords: wrote {len(new['cells'])} cells to {FILE.name}")


if __name__ == "__main__":
    main()
