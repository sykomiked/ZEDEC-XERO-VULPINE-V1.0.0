#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_mk_table.py - build_system/mk_table.py, offline.

    python3 build_system/test_mk_table.py

Writes the kernel's baked table over small stand-in swarm discs and checks
what it says: L0's CID as mk_layers.py gives it, one row per disc with its
size, SHA-256 and CID, the empty table, the numbering rule, and that the
header is rewritten only when its bytes change. The C side reads the same
rows (kernel/src/swarmnet/test_swarmnet.c, test_table).
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MK = os.path.join(HERE, "mk_table.py")
sys.path.insert(0, HERE)
from mk_layers import l0_bytes, layer_cid  # noqa: E402
from zxv_publish_update import cid_str, file_dag  # noqa: E402

checks = failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: " + what)


def mk(*args):
    r = subprocess.run([sys.executable, MK] + list(args), cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def c_array(text, name):
    """The bytes of the first {0x.., ...} after name in text."""
    i = text.index(name)
    body = text[text.index("{", i):text.index("}", i) + 1]
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", body))


def main():
    t = tempfile.mkdtemp(prefix="zxv-table-")
    out = os.path.join(t, "inc", "manifest_hashes.h")
    l0cid = layer_cid(l0_bytes(os.path.join(HERE, "layer0.machines")))

    # the empty table: L0 only
    rc, o = mk("--out", out)
    check(rc == 0 and ("L0 %s, 0 swarm discs" % l0cid) in o, "empty: " + o)
    h = open(out).read()
    check('#define ZXV_TABLE_L0_CID "%s"' % l0cid in h, "L0 CID text")
    raw = c_array(h, "ZXV_TABLE_L0_CID_BIN")
    check(len(raw) == 36 and raw[:4] == b"\x01\x55\x12\x20" and
          raw[4:] == hashlib.sha256(l0_bytes(os.path.join(HERE, "layer0.machines"))).digest(),
          "L0 CID bytes are the raw sha2-256 of L0")
    check("#define ZXV_TABLE_SWARM_N 0u" in h and '{{0u, "", 0ull, {0}, {0}}}' in h, "empty rows")

    # two discs, one over a chunk so its CID is a dag-pb root
    d2 = os.path.join(t, "zxv-swarm-disc2-reason.img")
    d3 = os.path.join(t, "zxv-swarm-disc3-vision.img")
    with open(d2, "wb") as f:
        f.write(b"reason disc\n")
    with open(d3, "wb") as f:
        f.write(bytes(range(256)) * 1100)
    rc, o = mk("--out", out, d3, d2)
    check(rc == 0 and "2 swarm discs" in o and "disc 2 REASON 12 bytes" in o and
          "disc 3 VISION 281600 bytes" in o, "two discs: " + o)
    h = open(out).read()
    check("#define ZXV_TABLE_SWARM_N 2u" in h, "count")
    for disc, group, p in ((2, "REASON", d2), (3, "VISION", d3)):
        cb, _, n, sha = file_dag(p)
        i = h.index('{%du, "%s", %dull,' % (disc, group, n))
        rows = re.findall(r"\{(0x[^}]*)\}", h[i:])
        got_sha = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", rows[0]))
        got_cid = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", rows[1]))
        check(got_sha.hex() == sha, "disc %d sha256" % disc)
        check(got_cid == cb, "disc %d CID %s" % (disc, cid_str(cb)))
    check(h.index('{2u, "REASON"') < h.index('{3u, "VISION"'), "rows in disc order")
    check(cid_str(file_dag(d3)[0]).startswith("bafybei"), "disc 3 is a dag-pb root")

    # written only when it changes
    os.utime(out, (1, 1))
    rc, o = mk("--out", out, d2, d3)
    check(rc == 0 and os.stat(out).st_mtime == 1, "unchanged table is not rewritten")
    with open(d2, "ab") as f:
        f.write(b"!")
    rc, o = mk("--out", out, d2, d3)
    check(rc == 0 and os.stat(out).st_mtime != 1 and "disc 2 REASON 13 bytes" in o,
          "a changed disc rewrites the table: " + o)

    # refused
    d4 = os.path.join(t, "zxv-swarm-disc4-math.img")
    open(d4, "wb").write(b"m")
    before = open(out).read()
    rc, o = mk("--out", out, d2, d4)
    check(rc == 1 and "without gaps or repeats; got disc 4" in o, "gap: " + o)
    rc, o = mk("--out", out, d3)
    check(rc == 1 and "got disc 3" in o, "no disc 2: " + o)
    dup = os.path.join(t, "other-swarm-disc2-reason.img")
    open(dup, "wb").write(b"x")
    rc, o = mk("--out", out, d2, dup)
    check(rc == 1 and "got disc 2" in o, "repeat: " + o)
    rc, o = mk("--out", out, os.path.join(t, "zxv-swarm-disc5-math.img"))
    check(rc == 1 and "is not an existing" in o, "missing file: " + o)
    bad = os.path.join(t, "zxv-swarm-disc2-Big.img")
    open(bad, "wb").write(b"x")
    rc, o = mk("--out", out, bad)
    check(rc == 1 and "is not an existing" in o, "upper-case group: " + o)
    check(open(out).read() == before, "a refused run leaves the table as it was")

    print("test_mk_table: %d checks, %d failures" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
