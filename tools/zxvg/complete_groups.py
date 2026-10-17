#!/usr/bin/env python3
# complete_groups.py — finish the L2/L3 icon SVGs for the ZXVG desk.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The desk's runtime (zui_vec) wants three named groups per icon:
chassis (the static base), core-artifact and kinetic-element. The L1 set
has them; the L2/L3 sets carry the chassis but not the other two. This
tool completes the structure without touching the art: every top-level
drawable moves into the chassis (or stays), and the two missing groups
are added empty — the icons render, and the kinetic engine can later
claim elements for the animation.

usage: complete_groups.py [--check] [SVG_DIR ...]
"""
import argparse
import sys
import xml.etree.ElementTree as ET

GROUPS = ("chassis", "core-artifact", "kinetic-element")
NS = "{http://www.w3.org/2000/svg}"
DRAWABLE = ("path", "rect", "circle", "ellipse", "line", "polyline", "polygon",
            "g", "use", "image")


def is_title(el):
    return el.tag == NS + "title" or el.tag == NS + "desc" or el.tag == NS + "metadata"


def complete(path):
    tree = ET.parse(path)
    root = tree.getroot()
    have = {g: False for g in GROUPS}
    stray = []
    for el in list(root):
        if is_title(el):
            continue
        if el.tag == NS + "g" and el.get("id") in have:
            have[el.get("id")] = True
        elif el.tag in (NS + d for d in DRAWABLE):
            stray.append(el)
    changed = False
    if stray:
        chassis = None
        for el in list(root):
            if el.tag == NS + "g" and el.get("id") == "chassis":
                chassis = el
                break
        if chassis is None:
            chassis = ET.SubElement(root, NS + "g")
            chassis.set("id", "chassis")
        for el in stray:
            root.remove(el)
            chassis.append(el)
        changed = True
    for g in GROUPS:
        if not have[g]:
            ET.SubElement(root, NS + "g").set("id", g)
            changed = True
    if changed:
        # keep the one-line form the set uses
        ET.indent(tree, space="")
        import re
        s = ET.tostring(root, encoding="unicode")
        s = re.sub(r">\s+<", "><", s)
        with open(path, "w") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n' + s + "\n")
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="*", default=["zxv-brand/app-icons/svg"])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    n_changed = 0
    n_ok = 0
    for d in args.dirs:
        from pathlib import Path
        for p in sorted(Path(d).glob("*.svg")):
            changed = complete(str(p))
            if args.check and changed:
                print(f"FAIL {p}: missing groups (run complete_groups.py)")
                return 1
            n_changed += changed
            n_ok += 1
    print(f"{n_ok} icons, {n_changed} completed")
    return 0


if __name__ == "__main__":
    sys.exit(main())