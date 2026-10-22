#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""One owner for every shared idea across the built-in apps.

Reads kernel/src/apps/CONCEPTS.tsv (the concept registry) and every app and
block manifest, and writes docs/APP_BLOCKS.md: the concepts and their
owners, then every merged app and block with what it uses and provides, so
whoever writes the next app (a person or the build server) builds on what
exists. --check rebuilds it in memory and fails when the committed file
differs, or when:

  * a concept's owner is not a manifest that provides that entry, or, for
    an owner written zxvd:<method> (a daemon method group every app calls),
    api/zxvd.openrpc.json lacks the method and the meaning does not start
    "Planned:";
  * a manifest other than the owner provides a name a concept owns (use
    the owner instead of writing it again);
  * two manifests provide the same name and it is neither a concept nor a
    local word (register it, with one owner, or list it as local);
  * a name is claimed by two concepts, or by a concept and the local list.

Each app's reuse figure is printed and written into the table: the blocks
it uses (each `use` line, an earlier app or a shared block) against the new
entries it writes itself (its `provides` lines, re-exports of a composed
entry not counted), as used / (used + new) in whole per cent. Apps are
built in matrix order (tools/server-tasks/MATRIX_ORDER.tsv) so that later
ones find more to use.
"""
import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APPS = os.path.join(ROOT, "kernel", "src", "apps")
REG = os.path.join(APPS, "CONCEPTS.tsv")
OUT = os.path.join(ROOT, "docs", "APP_BLOCKS.md")


def read_registry():
    concepts, local, errors = [], set(), []
    with open(REG, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if line.startswith("#") or not line.strip():
                continue
            col = line.rstrip("\n").split("\t")
            if len(col) != 4:
                errors.append(f"CONCEPTS.tsv:{n}: four tab-separated columns expected")
                continue
            names = [x.strip() for x in col[2].split(",") if x.strip()]
            if col[0] == "-":
                local.update(names)
            else:
                concepts.append({"concept": col[0], "owner": col[1], "names": names,
                                 "meaning": col[3], "line": n})
    return concepts, local, errors


def read_manifests():
    out = []
    paths = [os.path.join(APPS, f) for f in sorted(os.listdir(APPS)) if f.endswith(".zxapp")]
    blocks = os.path.join(APPS, "blocks")
    paths += [os.path.join(blocks, f) for f in sorted(os.listdir(blocks)) if f.endswith(".zxblock")]
    for p in paths:
        m = {"file": os.path.relpath(p, ROOT), "id": "", "name": "", "slot": 0, "layer": "",
             "version": "", "uses": [], "provides": [], "langs": {}}
        inline = False
        with open(p, encoding="utf-8") as f:
            for line in f:
                if inline:
                    inline = line.strip() != "."
                    continue
                w = line.split("#", 1)[0].split()
                if not w:
                    continue
                if w[0] in ("id", "layer", "version") and len(w) == 2:
                    m[w[0]] = w[1]
                elif w[0] == "slot" and len(w) == 2:
                    m["slot"] = int(w[1], 10)
                elif w[0] == "name":
                    m["name"] = " ".join(w[1:])
                elif w[0] == "use" and len(w) >= 3:
                    m["uses"].append(f"{w[1]} {w[2]}")
                elif w[0] == "provides" and len(w) >= 3:
                    m["provides"].append((w[1], w[2], w[3:]))
                elif w[0] == "logic" and len(w) >= 4:
                    m["langs"].setdefault(w[2], []).append(w[1])
                    inline = w[3] == "<<"
        out.append(m)
    return out


def build():
    concepts, local, errors = read_registry()
    mans = read_manifests()
    by_id = {m["id"]: m for m in mans}
    owner_of = {}
    spec = json.load(open(os.path.join(ROOT, "api", "zxvd.openrpc.json"), encoding="utf-8"))
    methods = {x["name"] for x in spec["methods"]}
    for c in concepts:
        if c["owner"].startswith("zxvd:"):  # a daemon method every app calls, never rewrites
            oid, entry = "zxvd", c["owner"][5:]
            if entry not in methods and not c["meaning"].startswith("Planned:"):
                errors.append(f"concept {c['concept']}: zxvd has no method {entry} "
                              "(api/zxvd.openrpc.json); mark the meaning \"Planned:\" until it lands")
        else:
            oid, _, entry = c["owner"].partition(".")
            m = by_id.get(oid)
            if not m or entry not in [p[0] for p in m["provides"]]:
                errors.append(f"concept {c['concept']}: owner {c['owner']} provides no such entry")
        c["entry"] = entry
        c["oid"] = oid
        for nm in c["names"] + [entry]:
            if nm in local:
                errors.append(f"concept {c['concept']}: '{nm}' is also a local word")
            if nm in owner_of and owner_of[nm] is not c:
                errors.append(f"'{nm}' is claimed by concepts {owner_of[nm]['concept']} and {c['concept']}")
            owner_of[nm] = c
    defined = {}
    for m in mans:
        for name, _, _ in m["provides"]:
            if "." in name:  # a re-export of a composed entry (C1): defined by its part
                continue
            c = owner_of.get(name)
            if c and c["oid"] == "zxvd":
                errors.append(f"{m['file']}: provides {name}, which concept {c['concept']} owns: "
                              f"call the daemon method {c['entry']}, never write it in an app")
            elif c and m["id"] != c["oid"]:
                errors.append(f"{m['file']}: provides {name}, which concept {c['concept']} owns: "
                              f"use {c['oid']} and call {c['oid']}.{c['entry']}")
            defined.setdefault(name, []).append(m["file"])
    for name, files in sorted(defined.items()):
        if len(files) > 1 and name not in local and name not in owner_of:
            errors.append(f"'{name}' is defined by {', '.join(files)}: register it in CONCEPTS.tsv "
                          "with one owner, or as a local word")
    return concepts, mans, errors


def reuse(m):
    """(blocks used, new entries written, whole per cent used)."""
    used = len(m["uses"])
    new = sum(1 for n, _, _ in m["provides"] if "." not in n)
    return used, new, (used * 100 // (used + new)) if used + new else 0


def render(concepts, mans):
    out = [
        "<!--",
        "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
        "SPDX-License-Identifier: Apache-2.0",
        "-->",
        "",
        "# Built-in app blocks",
        "",
        "Generated by `tools/apps/concepts.py` from `kernel/src/apps/CONCEPTS.tsv` and",
        "the app and block manifests. Do not edit by hand.",
        "",
        "Every idea more than one app needs has one owner. An app reaches it with",
        "`use <owner> <major>` and calls `<owner>.<entry>`; it never writes it again",
        "(`concepts.py --check` refuses a second definition). Read this before",
        "writing an app: what is listed here already exists.",
        "",
        "## Concepts and their owners",
        "",
        "| Concept | Owner | Also called | Meaning |",
        "|---|---|---|---|",
    ]
    for c in concepts:
        out.append(f"| {c['concept']} | `{c['owner']}` | {', '.join(c['names'])} | {c['meaning']} |")
    out += ["", "## Apps and blocks", "",
            "Reuse is blocks used against new entries written, used / (used + new).",
            "",
            "| Manifest | Slot | Layer | Uses | Provides (entry kind inputs: languages) | Reuse |",
            "|---|---|---|---|---|---|"]
    for m in mans:
        prov = "<br>".join(
            f"`{n} {k}{(' ' + ' '.join(a)) if a else ''}`: {', '.join(m['langs'].get(n, ['(composed)']))}"
            for n, k, a in m["provides"]) or "-"
        kind = "block" if m["file"].endswith(".zxblock") else (m["layer"] or "example")
        u, n, pc = reuse(m)
        out.append(f"| {m['id']} {m['version']} | {m['slot'] or '-'} | {kind} | "
                   f"{', '.join(m['uses']) or '-'} | {prov} | {u} used, {n} new: {pc}% |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    concepts, mans, errors = build()
    text = render(concepts, mans)
    for e in errors:
        print("concepts:", e, file=sys.stderr)
    if a.check:
        have = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if have != text:
            print("concepts: docs/APP_BLOCKS.md is stale; run tools/apps/concepts.py", file=sys.stderr)
            return 1
        return 1 if errors else 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}")
    for m in mans:
        if m["slot"]:
            u, n, pc = reuse(m)
            print(f"reuse {m['id']}: used {u}, new {n}, {pc}%")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
