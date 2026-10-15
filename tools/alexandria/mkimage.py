#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mkimage.py — one ALXI image of a packed matrix (alx_lattice.h).

    mkimage.py MATRIX-DIR IMAGE

MATRIX-DIR is what pack_matrix.py --out-dir wrote (blocks/ and MATRIX.ROOT).
The image holds the root and every block, a table in rising CID order, and
each block at a 4096-byte sector in table order. The same image is what bare
metal reads by sector and what a browser maps into WASM memory; both resolve
it with alx_lat_resolve over alx_img_fetch. The bytes depend only on the
blocks, so the same matrix always gives the same image."""
import base64
import hashlib
import os
import struct
import sys

SECTOR = 4096


def cid_raw(b):
    return b"\x01\x55\x12\x20" + hashlib.sha256(b).digest()


def from_str(s):
    s = s[1:].upper()
    return base64.b32decode(s + "=" * (-len(s) % 8))


def main():
    src, dst = sys.argv[1], sys.argv[2]
    root = open(os.path.join(src, "MATRIX.ROOT"), "rb").read()
    blocks = {cid_raw(root): root}
    for name in os.listdir(os.path.join(src, "blocks")):
        b = open(os.path.join(src, "blocks", name), "rb").read()
        c = cid_raw(b)
        if c != from_str(name):
            sys.exit("mkimage: blocks/%s is not the block its name says" % name)
        blocks[c] = b
    cids = sorted(blocks)
    at = 64 + 48 * len(cids)
    at = (at + SECTOR - 1) // SECTOR * SECTOR
    head = struct.pack(">4sBBHII", b"ALXI", 1, 0, 0, len(cids), SECTOR) + cid_raw(root) + bytes(12)
    table, body = bytearray(), bytearray()
    for c in cids:
        table += c + struct.pack(">III", at // SECTOR, len(blocks[c]), 0)
        body += blocks[c] + bytes((-len(blocks[c])) % SECTOR)
        at += len(blocks[c]) + (-len(blocks[c])) % SECTOR
    img = head + table
    img += bytes((-len(img)) % SECTOR) + body
    with open(dst, "wb") as f:
        f.write(img)
    print("image: %d blocks, %d bytes, root %s" % (len(cids), len(img), cid_raw(root).hex()))


if __name__ == "__main__":
    main()
