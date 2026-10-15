#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_table.py - the table the x86_64 kernel bakes into its .rodata.

    python3 build_system/mk_table.py [--out build/include/manifest_hashes.h]
                                     [--machines build_system/layer0.machines]
                                     [SWARM_DISC.img ...]

The release is built leaves first (docs/BUCKET.md): the swarm discs and L0
exist before the kernel, so the kernel can carry their CIDs. This writes
them as a C header:

    ZXV_TABLE_L0_CID, ZXV_TABLE_L0_CID_BIN   L0's CID (the manifest of
                                             layer0.machines, mk_layers.py l0)
    ZXV_TABLE_SWARM_N, ZXV_TABLE_SWARM       one {disc, GROUP, bytes, sha256,
                                             cid} per swarm disc

Each swarm disc's CID is the CIDv1 of its UnixFS DAG under the pinned add
parameters (docs/BUCKET.md: 256 KiB chunks, raw leaves, balanced DAG of 174
links, sha2-256). The kernel fetches each swarm disc by that CID alone and
checks every block against it, and refuses a core disc whose LAYER0.JSN is
not that L0. L1 and L2 cannot be in the table: L1 lists the boot file that
holds the kernel, and L2 names L1.

Swarm images are named <prefix>-swarm-disc<N>-<group>.img, as
mk_golden_disc.sh writes them, numbered 2, 3, ... without gaps. With none
given the table has no swarm discs: such a kernel fetches nothing and asks
for discs to be attached. The header is written only when its bytes change,
so make does not rebuild the kernel for nothing.
"""

import argparse
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mk_layers import l0_bytes, layer_cid  # noqa: E402
from zxv_publish_update import cid_bin, file_dag  # noqa: E402

SWARM_RE = re.compile(r"(?:.*-)?swarm-disc([0-9]+)-([a-z0-9]{1,16})\.img")


def die(msg):
    print("mk_table.py: " + msg, file=sys.stderr)
    sys.exit(1)


def c_bytes(raw, indent):
    rows = [", ".join("0x%02x" % b for b in raw[i:i + 12]) for i in range(0, len(raw), 12)]
    return "{" + (",\n" + indent).join(rows) + "}"


def swarm_rows(paths):
    rows = []
    for p in paths:
        m = SWARM_RE.fullmatch(os.path.basename(p))
        if not m or not os.path.isfile(p):
            die("%s is not an existing <prefix>-swarm-disc<N>-<group>.img" % p)
        cb, _, n, sha = file_dag(p)
        rows.append((int(m.group(1)), m.group(2).upper(), n, bytes.fromhex(sha), cb, p))
    rows.sort()
    for i, r in enumerate(rows):
        if r[0] != i + 2:
            die("swarm discs must be numbered 2, 3, ... without gaps or repeats; got disc %d (%s)"
                % (r[0], r[5]))
    return rows


def render(l0, rows):
    cid = layer_cid(l0)
    raw = cid_bin(0x55, hashlib.sha256(l0).digest())
    out = ["/* manifest_hashes.h - written by build_system/mk_table.py; do not edit.",
           " * The leaves this kernel was built after: L0 and the swarm discs, each by",
           " * its CID (docs/BUCKET.md). */",
           "#ifndef ZXV_MANIFEST_HASHES_H",
           "#define ZXV_MANIFEST_HASHES_H",
           "",
           '#define ZXV_TABLE_L0_CID "%s"' % cid,
           "/* clang-format off */",
           "#define ZXV_TABLE_L0_CID_BIN \\",
           "    " + c_bytes(raw, "     ").replace("\n", " \\\n"),
           "#define ZXV_TABLE_SWARM_N %du" % len(rows)]
    if rows:
        ents = []
        for disc, group, n, sha, cb, _ in rows:
            ents.append("{%du, \"%s\", %dull,\n      %s,\n      %s}" % (
                disc, group, n, c_bytes(sha, "       "), c_bytes(cb, "       ")))
        body = "{" + ",\n     ".join(ents) + "}"
    else:
        body = "{{0u, \"\", 0ull, {0}, {0}}}"
    out.append("#define ZXV_TABLE_SWARM \\")
    out.append("    " + body.replace("\n", " \\\n"))
    out += ["/* clang-format on */", "", "#endif", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("swarm", nargs="*")
    ap.add_argument("--out", default="build/include/manifest_hashes.h")
    ap.add_argument("--machines", default="build_system/layer0.machines")
    a = ap.parse_args()
    rows = swarm_rows(a.swarm)
    text = render(l0_bytes(a.machines), rows)
    old = open(a.out, encoding="ascii").read() if os.path.isfile(a.out) else None
    if old != text:
        os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
        tmp = a.out + ".tmp"
        with open(tmp, "w", encoding="ascii") as f:
            f.write(text)
        os.replace(tmp, a.out)
    print("table %s: L0 %s, %d swarm disc%s%s" % (
        a.out, layer_cid(l0_bytes(a.machines)), len(rows), "" if len(rows) == 1 else "s",
        "".join("\n  disc %d %s %d bytes" % (r[0], r[1], r[2]) for r in rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
