#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_curriculum_car.py — Deterministically pack the ZXV Architectural Monograph
Curriculum into a Content-Addressable Archive (CARv1) with CIDv1 raw leaves.

Usage:
    python3 build_system/mk_curriculum_car.py [out.car]
"""
import hashlib
import json
import pathlib
import sys

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
CUR_DIR = ROOT_DIR / "docs" / "curriculum"
CHUNK = 262144
MAX_LINKS = 174
RAW, DAG_PB = 0x55, 0x70
_B32 = "abcdefghijklmnopqrstuvwxyz234567"


def uvarint(x: int) -> bytes:
    out = bytearray()
    while True:
        b = x & 0x7F
        x >>= 7
        if x:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def cid_bytes(codec: int, block: bytes) -> bytes:
    return b"\x01" + uvarint(codec) + b"\x12\x20" + hashlib.sha256(block).digest()


def cid_str(cb: bytes) -> str:
    bits, val, out = 0, 0, []
    for byte in cb:
        val = (val << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            out.append(_B32[(val >> bits) & 31])
    if bits:
        out.append(_B32[(val << (5 - bits)) & 31])
    return "b" + "".join(out)


def _key(field: int, wt: int) -> bytes:
    return uvarint((field << 3) | wt)


def _ld(field: int, b: bytes) -> bytes:
    return _key(field, 2) + uvarint(len(b)) + b


def _vi(field: int, x: int) -> bytes:
    return _key(field, 0) + uvarint(x)


def pb_node(links, data: bytes) -> bytes:
    out = b"".join(_ld(2, _ld(1, c) + _ld(2, n.encode()) + _vi(3, t)) for c, n, t in links)
    return out + _ld(1, data)


def unixfs_file(filesize: int, blocksizes) -> bytes:
    return _vi(1, 2) + _vi(3, filesize) + b"".join(_vi(4, s) for s in blocksizes)


UNIXFS_DIR = _vi(1, 1)


def car_header(root: bytes) -> bytes:
    cidb = b"\x00" + root
    body = (
        b"\xa2"
        + b"\x65roots"
        + b"\x81"
        + b"\xd8\x2a"
        + b"\x58"
        + bytes([len(cidb)])
        + cidb
        + b"\x67version"
        + b"\x01"
    )
    return uvarint(len(body)) + body


def build_curriculum(out_car: pathlib.Path) -> str:
    sources = [
        ("BUCKET.md", ROOT_DIR / "docs" / "BUCKET.md"),
        ("INDEX.md", CUR_DIR / "INDEX.md"),
        ("MANUAL.md", ROOT_DIR / "MANUAL.md"),
        ("PAPER_1_PARACONSISTENT_RING0.md", CUR_DIR / "PAPER_1_PARACONSISTENT_RING0.md"),
        ("PAPER_2_TRAFFIC_RESOURCE_ECONOMY.md", CUR_DIR / "PAPER_2_TRAFFIC_RESOURCE_ECONOMY.md"),
        ("PAPER_3_IMMUTABLE_EPISTEMIC_GRAPH.md", CUR_DIR / "PAPER_3_IMMUTABLE_EPISTEMIC_GRAPH.md"),
        ("SYNOPSIS.md", ROOT_DIR / "docs" / "SYNOPSIS.md"),
    ]
    blocks = {}
    entries = {}
    for name, path in sources:
        data = path.read_bytes()
        cuts = [(o, min(CHUNK, len(data) - o)) for o in range(0, len(data), CHUNK)] or [(0, 0)]
        level = []
        for off, ln in cuts:
            b = data[off : off + ln]
            c = cid_bytes(RAW, b)
            blocks.setdefault(c, b)
            level.append((c, ln, ln))
        while len(level) > 1:
            nxt = []
            for i in range(0, len(level), MAX_LINKS):
                grp = level[i : i + MAX_LINKS]
                fs = sum(g[2] for g in grp)
                blk = pb_node([(g[0], "", g[1]) for g in grp], unixfs_file(fs, [g[2] for g in grp]))
                c = cid_bytes(DAG_PB, blk)
                blocks.setdefault(c, blk)
                nxt.append((c, len(blk) + sum(g[1] for g in grp), fs))
            level = nxt
        entries[name] = (level[0][0], level[0][1])

    links = [(entries[n][0], n, entries[n][1]) for n in sorted(entries, key=lambda s: s.encode())]
    dir_blk = pb_node(links, UNIXFS_DIR)
    root = cid_bytes(DAG_PB, dir_blk)
    blocks.setdefault(root, dir_blk)

    order = [root] + [c for c, _, _ in links]
    out_car.parent.mkdir(parents=True, exist_ok=True)
    with out_car.open("wb") as f:
        f.write(car_header(root))
        for c in order:
            blk = blocks[c]
            f.write(uvarint(len(c) + len(blk)) + c + blk)

    return cid_str(root)


def main() -> int:
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("/tmp/zxv-curriculum.car")
    root_cid = build_curriculum(out)
    expected = (CUR_DIR / "CURRICULUM.CID").read_text().strip()
    car_sha = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f"root_cid={root_cid} bytes={out.stat().st_size} sha256={car_sha}")
    if root_cid != expected:
        print(f"ERROR: expected {expected}, got {root_cid}", file=sys.stderr)
        return 1
    manifest = json.loads((CUR_DIR / "manifest.json").read_text())
    if manifest["car_sha256"] != car_sha:
        print(f"ERROR: manifest car_sha256 mismatch", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
