#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build the Library of Alexandria's index and booklets (docs/ALEXANDRIA.md).

Reads the registered blocks (kernel/src/apps/blocks/*.zxblock, and apps with
a version that provide their own entries, zxapp.h C1) and the slot registry
(kernel/src/alexandria/SLOTS.tsv), checks them against each other, packs
every division's blocks into one booklet, and writes:

  --out-tbl  ALEXANDRIA.TBL, the binary index the kernel reads (alx.h)
  --out-json the slots, places, booklets and language offsets, for people
             and models
  --out-hdr  alx_slots.h, one #define per slot for C code
  --out-dir  the 05_alexandria tree for the bucket: the index, the JSON and
             one file per booklet (B<division>.booklet)

--check fails when a block entry has no slot, a slot names nothing, or the
checked-in header is stale. --assign BLOCK NAMESPACE SECTION appends slots
for every entry of BLOCK that has none, numbered within SECTION ("512").
Same input, same bytes out: nothing depends on the time, the machine or the
order files are listed in.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "kernel", "src")
SLOTS = os.path.join(SRC, "alexandria", "SLOTS.tsv")
HDR = os.path.join(SRC, "alexandria", "alx_slots.h")
LANGS_DEF = os.path.join(SRC, "lingua", "zl_langs.def")
CLASS_OF = {"mem": 1, "crypto": 2, "drv": 3, "logic": 4, "num": 5, "fin": 6, "prob": 6,
            "strat": 6, "wire": 7, "text": 8, "time": 9, "swarm": 9,
            "cell": 9}
# Language numbers: 1..99 are Lingua's own numbers (zl_langs.def); 101..120
# the other runtimes, in zxapp's order. Never renumbered.
OTHER = ["c", "sutra", "eve", "kron", "trebago", "myra", "cpp", "rust", "go", "swift", "kotlin",
         "java", "dotnet", "python", "typescript", "dart", "ruby", "php", "wasm", "zlir"]
BASE_END = 0x10000
ALIGN = 64
BOOKLET_MAX = 1 << 20  # one raw IPFS block: the booklet's CID is that block's
KIND = {"num": 1, "truth": 2}
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*\.[a-z0-9_]+$")
PLACE_RE = re.compile(r"^([1-9][0-9]{2})\.([0-9]{2})$")


def fail(msg):
    sys.exit("build_index: " + msg)


def cid_raw(data):
    """CIDv1, raw codec, sha2-256: what `ipfs add --cid-version=1 --raw-leaves`
    gives a file of at most one block."""
    return bytes([0x01, 0x55, 0x12, 0x20]) + hashlib.sha256(data).digest()


def cid_str(b):
    return "b" + base64.b32encode(b).decode().lower().rstrip("=")


def lang_numbers():
    nums = {}
    for ln in open(LANGS_DEF, encoding="utf-8"):
        m = re.match(r'ZL_LANG_X\(\w+, (\d+), .*"([a-z0-9_]+)"\)', ln)
        if m:
            nums[m.group(2)] = int(m.group(1))
    if len(nums) < 8 or max(nums.values()) > 99:
        fail("could not read the Lingua language numbers")
    for k, rt in enumerate(OTHER):
        nums[rt] = 101 + k
    return nums


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
        if w[0] == "id":
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


def entries(ms, nums):
    """block.entry -> what the index needs to know about it."""
    e = {}
    for m in ms:
        ver = tuple(int(x) for x in m["version"].split("."))
        for name, kind, inputs in m["provides"]:
            if kind not in KIND:
                fail("%s: %s answers %s, not num or truth" % (m["id"], name, kind))
            langs = []
            for lang, ent, path in m["logic"]:
                if ent != name:
                    continue
                if lang not in nums:
                    fail("%s: %s: no language number for %s" % (m["id"], name, lang))
                if any(nums[lang] == n for n, _ in langs):
                    fail("%s: %s: %s given twice" % (m["id"], name, lang))
                langs.append((nums[lang], path))
            if not langs:
                fail("%s: %s has no logic" % (m["id"], name))
            if len(inputs) > 15:
                fail("%s: %s takes more than 15 inputs" % (m["id"], name))
            e[m["id"] + "." + name] = {"m": m, "entry": name, "kind": kind, "inputs": inputs,
                                       "version": ver, "langs": langs}
    return e


def read_slots():
    rows = []
    for n, ln in enumerate(open(SLOTS, encoding="utf-8").read().split("\n"), 1):
        if not ln or ln.startswith("#"):
            continue
        f = ln.split("\t")
        if len(f) != 5:
            fail("SLOTS.tsv:%d: five tab-separated fields" % n)
        if not re.fullmatch(r"[1-9][0-9]*", f[0]):
            fail("SLOTS.tsv:%d: slot %r" % (n, f[0]))
        rows.append((int(f[0]), f[1], f[2], f[3], f[4], n))
    for k, (slot, short, place, target, state, n) in enumerate(rows):
        if slot != k + 1 or slot >= BASE_END:
            fail("SLOTS.tsv:%d: slots run 1, 2, 3 ... with none missing (want %d)" % (n, k + 1))
        if not NAME_RE.match(short) or len(short) > 31 or short.split(".")[0] not in CLASS_OF:
            fail("SLOTS.tsv:%d: shorthand %r (namespace.name, at most 31 bytes)" % (n, short))
        pm = PLACE_RE.match(place)
        if not pm or pm.group(2) == "00":
            fail("SLOTS.tsv:%d: place %r (CCC.II, item 01..99)" % (n, place))
        if int(pm.group(1)) // 100 != CLASS_OF[short.split(".")[0]]:
            fail("SLOTS.tsv:%d: %s belongs in class %d00" % (n, short, CLASS_OF[short.split(".")[0]]))
        if len(target) > 43 or not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)+", target):
            fail("SLOTS.tsv:%d: target %r" % (n, target))
        if state not in ("base", "retired"):
            fail("SLOTS.tsv:%d: state %r" % (n, state))
    for col, what in ((1, "shorthand"), (2, "place"), (3, "block entry")):
        seen = [r[col] for r in rows]
        dup = sorted({x for x in seen if seen.count(x) > 1})
        if dup:
            fail("SLOTS.tsv: %s used twice: %s" % (what, ", ".join(dup)))
    return rows


def check(rows, ents):
    named = {r[3] for r in rows}
    missing = sorted(set(ents) - named)
    if missing:
        fail("no slot for: %s (run --assign BLOCK NAMESPACE SECTION)" % ", ".join(missing))
    for slot, short, place, target, state, n in rows:
        if state == "base" and target not in ents:
            fail("SLOTS.tsv:%d: %s names no registered entry (mark it retired)" % (n, target))


def pad(b):
    return b + bytes(-len(b) % ALIGN)


def booklets(rows, ents):
    """division -> (bytes, {path: (offset, length)}): each division's manifests
    and logic files, each at a 64-byte boundary after a 64-byte header."""
    files = {}
    for slot, short, place, target, state, n in rows:
        if state != "base":
            continue
        e = ents[target]
        div = int(place[:2]) * 10
        rel = [os.path.relpath(e["m"]["path"], SRC)] + [p for _, p in e["langs"] if p]
        lst = files.setdefault(div, [])
        for r in rel:
            if r not in lst:
                lst.append(r)
    out = {}
    for div, lst in sorted(files.items()):
        body, where = bytearray(), {}
        for r in lst:
            data = open(os.path.join(SRC, r), "rb").read()
            if not data:
                fail("%s is empty" % r)
            where[r] = (ALIGN + len(body), len(data))
            body += pad(data)
        head = b"ALXB" + struct.pack(">BBHII", 1, 0, div, len(lst), ALIGN + len(body))
        data = pad(head) + bytes(body)
        if len(data) > BOOKLET_MAX:
            fail("booklet %d is %d bytes, more than one block; split the division" % (div, len(data)))
        out[div] = (data, where)
    return out


def table(rows, ents, books):
    divs = sorted(books)
    bk = bytearray()
    for div in divs:
        data = books[div][0]
        bk += struct.pack(">HHII", div, 0, len(data), 0) + cid_raw(data)
    sl, tg = bytearray(), bytearray()
    ntarget = 0
    for slot, short, place, target, state, _ in rows:
        cls, item = int(place[:3]), int(place[4:])
        names = short.encode().ljust(32, b"\0") + target.encode().ljust(44, b"\0")
        if state == "retired":
            sl += struct.pack(">IBBBBHHHH", slot, 0, 0, 1, 0, 0, 0, 0, 0) + names
            sl += struct.pack(">HHHHIII", cls, item, 0xFFFF, 0, 0, 0, 0) + bytes(32)
            continue
        e = ents[target]
        div = cls // 10 * 10
        data, where = books[div]
        mrel = os.path.relpath(e["m"]["path"], SRC)
        moff, mlen = where[mrel]
        nlang = len(e["langs"])
        flags = 2 if nlang >= 2 else 0  # 2: answers can be witnessed
        ver = e["version"]
        sl += struct.pack(">IBBBBHHHH", slot, KIND[e["kind"]], len(e["inputs"]), flags, nlang,
                          ver[0], ver[1], ver[2], 0) + names
        sl += struct.pack(">HHHHIII", cls, item, divs.index(div), 0, ntarget, moff, mlen)
        sl += hashlib.sha256(e["m"]["text"]).digest()
        for lang, path in sorted(e["langs"]):
            off, ln = where[path] if path else (moff, mlen)
            tg += struct.pack(">IHHII", slot, lang, 0, off, ln)
            ntarget += 1
    head = b"ALXT" + struct.pack(">BBBBIIII", 1, 0, 0, 0, len(rows), len(divs), ntarget,
                                 BASE_END) + bytes(8)
    data = head + bytes(bk) + bytes(sl) + bytes(tg)
    return data + hashlib.sha256(data).digest()


def index_json(rows, ents, books, tbl, nums):
    lang_name = {v: k for k, v in nums.items()}
    out = {"version": 1, "index_cid": cid_str(cid_raw(tbl)),
           "booklets": [{"division": d, "file": "B%d.booklet" % d, "bytes": len(books[d][0]),
                         "cid": cid_str(cid_raw(books[d][0]))} for d in sorted(books)],
           "slots": []}
    for slot, short, place, target, state, _ in rows:
        r = {"slot": slot, "shorthand": "@" + short, "hex": "@S:%04X" % slot,
             "place": place, "target": target, "state": state}
        if state == "base":
            e = ents[target]
            div = int(place[:3]) // 10 * 10
            where = books[div][1]
            mrel = os.path.relpath(e["m"]["path"], SRC)
            r.update({"block": e["m"]["id"], "version": e["m"]["version"],
                      "contract": " ".join([e["kind"]] + e["inputs"]),
                      "content_id": "sha256:" + hashlib.sha256(e["m"]["text"]).hexdigest(),
                      "booklet": div, "manifest": {"path": mrel, "offset": where[mrel][0],
                                                   "bytes": where[mrel][1]},
                      "targets": [{"language": "%03d" % lang, "runtime": lang_name[lang],
                                   "name": "%s-%03d" % (place, lang),
                                   "path": path or mrel,
                                   "offset": (where[path] if path else where[mrel])[0],
                                   "bytes": (where[path] if path else where[mrel])[1]}
                                  for lang, path in sorted(e["langs"])]})
        out["slots"].append(r)
    out["languages"] = {"%03d" % v: k for k, v in sorted(nums.items(), key=lambda kv: kv[1])}
    return json.dumps(out, indent=1, sort_keys=True) + "\n"


def header(rows):
    lines = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
             "/* SPDX-License-Identifier: Apache-2.0 */",
             "/* alx_slots.h - generated by tools/alexandria/build_index.py from SLOTS.tsv.",
             " * Do not edit: append a slot there and run the script. */",
             "#ifndef ZXV_ALX_SLOTS_H", "#define ZXV_ALX_SLOTS_H", "", "/* clang-format off */"]
    for slot, short, place, target, state, _ in rows:
        name = "ALX_SLOT_" + short.upper().replace(".", "_")
        lines.append("#define %-40s %uu /* %s %s%s */" % (name, slot, place, target,
                                                          ", retired" if state == "retired" else ""))
    lines += ["#define ALX_SLOT__COUNT %uu" % len(rows), "/* clang-format on */", "", "#endif", ""]
    return "\n".join(lines)


def assign(rows, ents, block, ns, section):
    if ns not in CLASS_OF:
        fail("namespace %r is not one of %s" % (ns, " ".join(sorted(CLASS_OF))))
    if not re.fullmatch(r"[1-9][0-9]{2}", section) or int(section) // 100 != CLASS_OF[ns]:
        fail("section %r is not in class %d00" % (section, CLASS_OF[ns]))
    named = {r[3] for r in rows}
    new = sorted(t for t in ents if t.split(".")[0] == block and t not in named)
    if not new:
        fail("every entry of %s already has a slot" % block)
    used = {int(r[2][4:]) for r in rows if r[2][:3] == section}
    free = [i for i in range(1, 100) if i not in used]
    if len(free) < len(new):
        fail("section %s is full" % section)
    with open(SLOTS, "a", encoding="utf-8") as f:
        for k, t in enumerate(new):
            f.write("%d\t%s.%s\t%s.%02d\t%s\tbase\n" % (len(rows) + k + 1, ns, ents[t]["entry"],
                                                       section, free[k], t))
    print("assigned %d slot(s) to %s" % (len(new), block))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--assign", nargs=3, metavar=("BLOCK", "NAMESPACE", "SECTION"))
    ap.add_argument("--out-tbl")
    ap.add_argument("--out-json")
    ap.add_argument("--out-hdr", nargs="?", const=HDR)
    ap.add_argument("--out-dir")
    a = ap.parse_args()
    nums = lang_numbers()
    ents = entries(manifests(), nums)
    rows = read_slots()
    if a.assign:
        assign(rows, ents, *a.assign)
        rows = read_slots()
    check(rows, ents)
    books = booklets(rows, ents)
    tbl = table(rows, ents, books)
    js = index_json(rows, ents, books, tbl, nums)
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
        open(os.path.join(d, "alexandria_index.json"), "w").write(js)
        for div, (data, _) in books.items():
            open(os.path.join(d, "B%d.booklet" % div), "wb").write(data)
    print("%d slots, %d booklets, index %s" % (len(rows), len(books), cid_str(cid_raw(tbl))))


if __name__ == "__main__":
    main()
