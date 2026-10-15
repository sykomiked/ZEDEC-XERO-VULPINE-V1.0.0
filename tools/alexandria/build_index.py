#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build the Library of Alexandria's index (docs/ALEXANDRIA.md).

Reads the registered blocks (kernel/src/apps/blocks/*.zxblock, and apps with
a version that provide their own entries, zxapp.h C1) and the slot registry
(kernel/src/alexandria/SLOTS.tsv), checks them against each other, and
writes:

  --out-tbl  ALEXANDRIA.TBL, the binary table the kernel reads (alx.h)
  --out-json the shorthands with every CID, for people and models
  --out-hdr  alx_slots.h, one #define per slot for C code
  --out-dir  the 05_alexandria tree for the bucket: the table, the JSON and
             every manifest and logic file under its own path

--check fails when a block entry has no slot, a slot names nothing, or the
checked-in header is stale. --assign BLOCK NAMESPACE appends slots for every
entry of BLOCK that has none. Same input, same bytes out: nothing depends on
the time, the machine or the order files are listed in.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "kernel", "src")
SLOTS = os.path.join(SRC, "alexandria", "SLOTS.tsv")
HDR = os.path.join(SRC, "alexandria", "alx_slots.h")
NAMESPACES = "mem crypto drv logic wire num fin prob strat time swarm text".split()
BASE_END = 0x10000
MAGIC = b"ALXT"
VERSION = 1
ENTRY = 128
KIND = {"num": 1, "truth": 2}
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*\.[a-z0-9_]+$")


def fail(msg):
    sys.exit("build_index: " + msg)


def cid_raw(data):
    """CIDv1, raw codec, sha2-256: what `ipfs add --cid-version=1` gives a small file."""
    return bytes([0x01, 0x55, 0x12, 0x20]) + hashlib.sha256(data).digest()


def cid_str(b):
    return "b" + base64.b32encode(b).decode().lower().rstrip("=")


def parse_manifest(path):
    text = open(path, "rb").read()
    m = {"path": path, "text": text, "provides": [], "logic": [], "version": None}
    lines = text.decode("utf-8").split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i].strip()
        i += 1
        if not ln or ln.startswith("#"):
            continue
        w = ln.split()
        if w[0] in ("zxblock", "zxapp"):
            m["kind"] = w[0]
        elif w[0] == "id":
            m["id"] = w[1]
        elif w[0] == "version":
            m["version"] = w[1]
        elif w[0] == "provides":
            m["provides"].append((w[1], w[2], w[3:]))
        elif w[0] == "logic":
            if w[3] == "<<":  # inline: the text runs to a line "end"
                while i < len(lines) and lines[i].strip() != "end":
                    i += 1
                i += 1
                m["logic"].append((w[1], w[2], None))
            else:
                m["logic"].append((w[1], w[2], w[3]))
    return m


def manifests():
    out = []
    for dirpath, _, files in os.walk(os.path.join(SRC, "apps")):
        for f in files:
            if f.endswith(".zxblock") or f.endswith(".zxapp"):
                m = parse_manifest(os.path.join(dirpath, f))
                own = [p for p in m["provides"] if "." not in p[0]]
                if m.get("id") and m["version"] and own:
                    m["provides"] = own
                    out.append(m)
    out.sort(key=lambda m: m["id"])
    ids = [m["id"] for m in out]
    if len(set(ids)) != len(ids):
        fail("two manifests share an id")
    return out


def entries(ms):
    """block.entry -> what the index needs to know about it."""
    e = {}
    for m in ms:
        ver = tuple(int(x) for x in m["version"].split("."))
        for name, kind, inputs in m["provides"]:
            if kind not in KIND:
                fail("%s: %s answers %s, not num or truth" % (m["id"], name, kind))
            langs = [(lang, path) for lang, ent, path in m["logic"] if ent == name]
            if not langs:
                fail("%s: %s has no logic" % (m["id"], name))
            e[m["id"] + "." + name] = {"m": m, "entry": name, "kind": kind, "inputs": inputs,
                                       "version": ver, "langs": langs}
    return e


def read_slots():
    rows = []
    for n, ln in enumerate(open(SLOTS, encoding="utf-8").read().split("\n"), 1):
        if not ln or ln.startswith("#"):
            continue
        f = ln.split("\t")
        if len(f) != 4:
            fail("SLOTS.tsv:%d: four tab-separated fields" % n)
        if not re.fullmatch(r"[1-9][0-9]*", f[0]):
            fail("SLOTS.tsv:%d: slot %r" % (n, f[0]))
        rows.append((int(f[0]), f[1], f[2], f[3], n))
    for k, (slot, short, target, state, n) in enumerate(rows):
        if slot != k + 1 or slot >= BASE_END:
            fail("SLOTS.tsv:%d: slots run 1, 2, 3 ... with none missing (want %d)" % (n, k + 1))
        if not NAME_RE.match(short) or len(short) > 31 or short.split(".")[0] not in NAMESPACES:
            fail("SLOTS.tsv:%d: shorthand %r (namespace.name, at most 31 bytes)" % (n, short))
        if len(target) > 43 or not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)+", target):
            fail("SLOTS.tsv:%d: target %r" % (n, target))
        if state not in ("base", "retired"):
            fail("SLOTS.tsv:%d: state %r" % (n, state))
    for col, what in ((1, "shorthand"), (2, "block entry")):
        seen = [r[col] for r in rows]
        dup = sorted({x for x in seen if seen.count(x) > 1})
        if dup:
            fail("SLOTS.tsv: %s used twice: %s" % (what, ", ".join(dup)))
    return rows


def check(rows, ents):
    named = {r[2] for r in rows}
    missing = sorted(set(ents) - named)
    if missing:
        fail("no slot for: %s (run --assign BLOCK NAMESPACE)" % ", ".join(missing))
    for slot, short, target, state, n in rows:
        if state == "base" and target not in ents:
            fail("SLOTS.tsv:%d: %s names no registered entry (mark it retired)" % (n, target))


def table(rows, ents):
    body = bytearray()
    for slot, short, target, state, _ in rows:
        e = ents.get(target)
        if state == "retired" or e is None:
            kind, arity, flags, nlang, ver, cid = 0, 0, 1, 0, (0, 0, 0), bytes(36)
        else:
            kind, arity = KIND[e["kind"]], len(e["inputs"])
            nlang = len(e["langs"])
            flags = 2 if nlang >= 2 else 0  # 2: answers can be witnessed
            ver, cid = e["version"], cid_raw(e["m"]["text"])
        if arity > 15:
            fail("%s takes more than 15 inputs" % target)
        body += struct.pack(">IBBBBHHHH", slot, kind, arity, flags, nlang, ver[0], ver[1], ver[2], 0)
        body += short.encode().ljust(32, b"\0") + target.encode().ljust(44, b"\0") + cid
    head = MAGIC + struct.pack(">BBBBIII", VERSION, 0, 0, 0, len(rows), ENTRY, BASE_END) + bytes(12)
    data = head + bytes(body)
    return data + hashlib.sha256(data).digest()


def shorthands(rows, ents, tbl):
    out = {"version": VERSION, "table_cid": cid_str(cid_raw(tbl)), "slots": []}
    for slot, short, target, state, _ in rows:
        r = {"slot": slot, "shorthand": "@" + short, "hex": "@S:%04X" % slot,
             "target": target, "state": state}
        e = ents.get(target)
        if state == "base" and e:
            m = e["m"]
            r.update({"block": m["id"], "version": m["version"],
                      "contract": " ".join([e["kind"]] + e["inputs"]),
                      "cid": cid_str(cid_raw(m["text"])),
                      "manifest": os.path.relpath(m["path"], SRC),
                      "targets": [{"language": lang,
                                   "path": path if path else os.path.relpath(m["path"], SRC),
                                   "cid": cid_str(cid_raw(open(os.path.join(SRC, path), "rb").read()))
                                   if path else cid_str(cid_raw(m["text"]))}
                                  for lang, path in e["langs"]]})
        out["slots"].append(r)
    return json.dumps(out, indent=1, sort_keys=True) + "\n"


def header(rows):
    lines = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
             "/* SPDX-License-Identifier: Apache-2.0 */",
             "/* alx_slots.h - generated by tools/alexandria/build_index.py from SLOTS.tsv.",
             " * Do not edit: append a slot there and run the script. */",
             "#ifndef ZXV_ALX_SLOTS_H", "#define ZXV_ALX_SLOTS_H", "", "/* clang-format off */"]
    for slot, short, target, state, _ in rows:
        name = "ALX_SLOT_" + short.upper().replace(".", "_")
        lines.append("#define %-40s %uu /* %s%s */" % (name, slot, target,
                                                       ", retired" if state == "retired" else ""))
    lines += ["#define ALX_SLOT__COUNT %uu" % len(rows), "/* clang-format on */", "", "#endif", ""]
    return "\n".join(lines)


def assign(rows, ents, block, ns):
    if ns not in NAMESPACES:
        fail("namespace %r is not one of %s" % (ns, " ".join(NAMESPACES)))
    named = {r[2] for r in rows}
    new = sorted(t for t in ents if t.split(".")[0] == block and t not in named)
    if not new:
        fail("every entry of %s already has a slot" % block)
    with open(SLOTS, "a", encoding="utf-8") as f:
        for k, t in enumerate(new):
            f.write("%d\t%s.%s\t%s\tbase\n" % (len(rows) + k + 1, ns, ents[t]["entry"], t))
    print("assigned %d slot(s) to %s" % (len(new), block))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--assign", nargs=2, metavar=("BLOCK", "NAMESPACE"))
    ap.add_argument("--out-tbl")
    ap.add_argument("--out-json")
    ap.add_argument("--out-hdr", nargs="?", const=HDR)
    ap.add_argument("--out-dir")
    a = ap.parse_args()
    ents = entries(manifests())
    rows = read_slots()
    if a.assign:
        assign(rows, ents, *a.assign)
        rows = read_slots()
    check(rows, ents)
    tbl = table(rows, ents)
    js = shorthands(rows, ents, tbl)
    hdr = header(rows)
    if a.check and (not os.path.exists(HDR) or open(HDR).read() != hdr):
        fail("kernel/src/alexandria/alx_slots.h is stale (run with --out-hdr)")
    if a.out_hdr:
        open(a.out_hdr, "w").write(hdr)
    if a.out_tbl:
        open(a.out_tbl, "wb").write(tbl)
    if a.out_json:
        open(a.out_json, "w").write(js)
    if a.out_dir:
        d = a.out_dir
        if os.path.exists(d):
            fail("%s exists; give a new directory" % d)
        os.makedirs(d)
        open(os.path.join(d, "ALEXANDRIA.TBL"), "wb").write(tbl)
        open(os.path.join(d, "alexandria_shorthands.json"), "w").write(js)
        files = set()
        for e in ents.values():
            files.add(os.path.relpath(e["m"]["path"], SRC))
            files.update(p for _, p in e["langs"] if p)
        for rel in sorted(files):
            os.makedirs(os.path.dirname(os.path.join(d, "blocks", rel)), exist_ok=True)
            shutil.copyfile(os.path.join(SRC, rel), os.path.join(d, "blocks", rel))
    print("%d slots, %d entries, table %s" % (len(rows), len(ents), cid_str(cid_raw(tbl))))


if __name__ == "__main__":
    main()
