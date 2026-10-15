#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_appicons.py: pack the 407 app icons and the app catalogue into one file
that zui reads on every screen (kernel/src/zui/zui_apps.h).

    tools/zui/gen_appicons.py            write assets/icons/apps/zxv-apps.zxap
    tools/zui/gen_appicons.py --check    exit 1 when the pack is stale

Sources (both in the repository):
    assets/icons/apps/catalog.csv        the 407 apps: number, layer, name,
                                         category, what it does, what it is
                                         built from, its pillar languages and
                                         how it keeps contradictions
    assets/icons/apps/svg/l{1,2,3}/      one vector icon per app, named
                                         NNN-anything.svg

Every icon is converted with tools/zxvg/svg2zxvg.py, so the pack holds ZXVG
vectors only (never a picture). The format is documented in zui_apps.h; this
file must agree with it. Python standard library only.
"""
import csv
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "assets", "icons", "apps")
CSV = os.path.join(SRC, "catalog.csv")
OUT = os.path.join(SRC, "zxv-apps.zxap")
sys.path.insert(0, os.path.join(ROOT, "tools", "zxvg"))
import svg2zxvg  # noqa: E402

MAGIC = b"ZXAP"
VERSION = 1
HEAD = 16  # magic, u16 version, u16 count, u32 strings offset, u32 total size
ENTRY = 36  # u16 number, u8 layer, u8 0, 6 x u32 string, u32 icon off, u32 len
FIELDS = ("app", "category", "does", "built_from", "pillars", "paraconsistent")
LAYERS = {"L1": 1, "L2": 2, "L3": 3}


def fail(msg):
    sys.stderr.write("gen_appicons: %s\n" % msg)
    sys.exit(1)


def icons():
    """number -> svg path, refusing duplicates and strays."""
    out = {}
    for layer in ("l1", "l2", "l3"):
        d = os.path.join(SRC, "svg", layer)
        for name in sorted(os.listdir(d)):
            if not name.endswith(".svg"):
                continue
            head = name.split("-", 1)[0]
            if not head.isdigit():
                fail("%s/%s: the name does not start with the app number" % (layer, name))
            n = int(head)
            if n in out:
                fail("two icons for app %d" % n)
            out[n] = (layer, os.path.join(d, name))
    return out


def build():
    with open(CSV, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    files = icons()
    apps = []
    for r in rows:
        n = int(r["number"])
        if r["layer"] not in LAYERS:
            fail("app %d: layer %r" % (n, r["layer"]))
        if n not in files:
            fail("app %d (%s) has no icon" % (n, r["app"]))
        layer, path = files.pop(n)
        if layer != r["layer"].lower():
            fail("app %d is %s but its icon is in %s" % (n, r["layer"], layer))
        with open(path, "rb") as f:
            try:
                data, _ = svg2zxvg.convert(f.read())
            except svg2zxvg.Refuse as e:
                fail("%s: refused: %s" % (path, e))
        apps.append((n, LAYERS[r["layer"]], [r[k].strip() for k in FIELDS], data))
    if files:
        fail("icons with no app: %s" % ", ".join(str(n) for n in sorted(files)))
    apps.sort(key=lambda a: a[0])
    for i, a in enumerate(apps):
        if a[0] != i + 1:
            fail("the catalogue is not numbered 1..%d (app %d at %d)" % (len(apps), a[0], i + 1))

    strings = bytearray()
    index = {}

    def s(text):
        b = text.encode("utf-8")
        if b"\0" in b:
            fail("a NUL inside %r" % text)
        if b not in index:
            index[b] = len(strings)
            strings.extend(b + b"\0")
        return index[b]

    soffs = [[s(t) for t in a[2]] for a in apps]
    strings_off = HEAD + ENTRY * len(apps)
    pos = strings_off + len(strings)
    pos += (-pos) & 3
    body = bytearray()
    entries = bytearray()
    for a, so in zip(apps, soffs):
        off = pos + len(body)
        body.extend(a[3])
        body.extend(b"\0" * ((-len(body)) & 3))
        entries += struct.pack("<HBB6III", a[0], a[1], 0, *so, off, len(a[3]))
    out = bytearray()
    total = pos + len(body)
    out += struct.pack("<4sHHII", MAGIC, VERSION, len(apps), strings_off, total)
    out += entries
    out += strings
    out += b"\0" * ((-len(out)) & 3)
    assert len(out) == pos
    out += body
    assert len(out) == total
    return bytes(out), len(apps)


def main(argv):
    data, n = build()
    if "--check" in argv[1:]:
        try:
            with open(OUT, "rb") as f:
                old = f.read()
        except OSError:
            old = b""
        if old != data:
            print("[FAIL] %s is stale: run tools/zui/gen_appicons.py" % os.path.relpath(OUT, ROOT))
            return 1
        print("[PASS] app icon pack matches the catalogue and icons (%d apps)" % n)
        return 0
    with open(OUT, "wb") as f:
        f.write(data)
    print("gen_appicons: %d apps, %d bytes -> %s" % (n, len(data), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
