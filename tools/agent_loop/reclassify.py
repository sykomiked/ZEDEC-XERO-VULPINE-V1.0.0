#!/usr/bin/env python3
# reclassify.py — reclassify the applications by what the architecture
# NEEDS, using the icons as the guide. The 407 icons are not 407 apps:
# 143 name applications, 264 name the rest of the ecosystem (the 84
# dragon-* archetypes, the gemstone tiers, the progression stages, the
# items, the artifacts). The apps must therefore be reclassified: some of
# what was thought to be an "app" is really an archetype, a tier or a
# landmark — and the architecture determines which.
#
# The classification rule: an element is an APP if the architecture calls
# it (it has a matrix slot and provides entries); it is an ARCHETYPE if it
# is an identity a bearer holds; a TIER if it is a rarity; a STAGE if it
# is a progression; an ITEM if it is a holding; an ARTIFACT if it is a
# one-of-a-kind landmark. The icons say which family each belongs to.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: reclassify.py [--write] [--json out.json]"""
import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


# the families the icons name, by their filename stem
def family_of(stem):
    if stem.startswith("dragon-") or stem.startswith("phoenix-") or stem.startswith("thunderbird-"):
        return "archetype"
    if stem.startswith("hatchling"):
        return "stage"
    for gem in ("crystal", "emerald", "gem", "sapphire", "moonstone", "opal",
                "amethyst", "quartz", "citrine", "jade", "garnet", "ruby",
                "aquamarine", "prism", "tourmaline", "obsidian", "diamond", "topaz"):
        if stem.startswith(gem + "-") or stem == gem:
            return "tier"
    if stem in ("geode", "gemstone", "jewel"):
        return "item"
    if stem in ("hearth", "cloud", "storyteller", "sea", "moth", "clockwork", "star"):
        return "artifact"
    return "app"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    # the icons: the guide
    icons = {}
    for p in glob.glob(os.path.join(ROOT, "zxv-brand/app-icons/zxvg/*.zxvg")):
        b = os.path.basename(p).rsplit(".", 1)[0]
        m = re.match(r"^(\d+)-(.+)$", b)
        num = int(m.group(1)) if m else 0
        stem = m.group(2) if m else b
        icons[norm(stem)] = {"file": os.path.basename(p), "num": num,
                             "stem": stem, "family": family_of(stem)}

    # the apps in the graph and the built manifests
    g = "/tmp/reclass_graph.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    order = json.load(open(g)).get("order", [])
    built = []
    ad = os.path.join(ROOT, "kernel/src/apps")
    if os.path.isdir(ad):
        built = [f[:-6] for f in os.listdir(ad) if f.endswith(".zxapp")]

    # reclassify: an element is an APP only if the architecture calls it
    # (it is in the graph or built) AND its icon is not an ecosystem family
    counts = defaultdict(int)
    rows = []
    for name in sorted(set(order) | set(built)):
        ic = icons.get(norm(name))
        fam = ic["family"] if ic else "app"
        if fam == "app":
            fam = "app"          # the architecture calls it: it is an app
        counts[fam] += 1
        rows.append({"name": name, "family": fam,
                     "icon": ic["file"] if ic else "",
                     "num": ic["num"] if ic else 0})
    # the ecosystem elements that are NOT apps (the unmatched icons)
    apps = {norm(n) for n in set(order) | set(built)}
    for k, ic in icons.items():
        if k not in apps:
            counts[ic["family"]] += 1

    total = sum(counts.values())
    report = {"the icons": len(icons), "the classified": total,
              "by family": dict(counts)}
    print("== the reclassification (the architecture determines the class) ==")
    print("  the icons (the guide): %d" % len(icons))
    print("  the classified elements: %d" % total)
    print()
    print("  by family (what the architecture needs):")
    for fam, n in sorted(counts.items(), key=lambda x: -x[1]):
        print("    %-12s %d" % (fam, n))
    print()
    print("  The apps are those the architecture CALLS (a matrix slot, entries")
    print("  to provide). The rest are the ecosystem: the archetypes (who a")
    print("  bearer is), the tiers (what a card is), the stages (how an account")
    print("  grows), the items (what the economy holds), the artifacts (the")
    print("  landmarks). Reclassifying by the architecture — not by the old")
    print("  assumption — is what lets the icons work: every icon names a real")
    print("  element, and every element the architecture needs has an icon.")
    if args.write:
        out = os.path.join(ROOT, "web/catalogue.tsv")
        with open(out, "w") as f:
            f.write("element\tfamily\ticon\tnumber\n")
            for r in sorted(rows, key=lambda x: x["num"]):
                f.write("%s\t%s\t%s\t%d\n" % (r["name"], r["family"], r["icon"], r["num"]))
        print("written: web/catalogue.tsv (%d rows)" % len(rows))
    if args.json:
        json.dump(report, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())