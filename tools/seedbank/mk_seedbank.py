#!/usr/bin/env python3
# mk_seedbank.py — pack (digest, bytes) pairs into a Seed Bank object.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The Seed Bank builder: reads lines of `hex-digest path` (or `path`
alone, digesting the file with sha256) and writes one bank object with
the jump table sorted by digest, entries aligned to 16, exactly the
layout kernel/src/seedbank/seedbank.h documents.

usage: mk_seedbank.py <manifest> <out.bank>
"""
import hashlib
import struct
import sys

SB_ALIGN = 16
SB_HEADER = 32
SB_ENTRY = 40


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    manifest, out = sys.argv[1], sys.argv[2]
    entries = []
    with open(manifest) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 2:
                digest, path = parts
                digest = bytes.fromhex(digest)
            else:
                path = parts[0]
                digest = None
            with open(path, "rb") as fh:
                data = fh.read()
            if digest is None:
                digest = hashlib.sha256(data).digest()
            entries.append((digest, data))
    entries.sort(key=lambda e: e[0])
    table_off = SB_HEADER
    payload_off = table_off + len(entries) * SB_ENTRY
    buf = bytearray()
    buf += b"ZXSB"
    buf += bytes([1])
    buf += struct.pack(">I", len(entries))
    buf += struct.pack(">I", table_off)
    buf += struct.pack(">I", payload_off)
    buf += struct.pack(">I", 0)  # bank_len filled below
    buf += b"\x00" * 11          # reserved: the header is 32 bytes exactly
    assert len(buf) == SB_HEADER
    at = payload_off
    for digest, data in entries:
        off = at - payload_off
        
        buf += digest
        buf += struct.pack(">I", off)
        buf += struct.pack(">I", len(data))
        at += len(data)
        at = (at + SB_ALIGN - 1) & ~(SB_ALIGN - 1)
    payload = bytearray(b"\x00" * (at - payload_off))
    at = payload_off
    for digest, data in entries:
        payload[at - payload_off:at - payload_off + len(data)] = data
        at += len(data)
        at = (at + SB_ALIGN - 1) & ~(SB_ALIGN - 1)
    buf += payload
    struct.pack_into(">I", buf, 17, len(buf))
    with open(out, "wb") as f:
        f.write(buf)
    print(f"seedbank: {len(entries)} seeds, {len(buf)} bytes -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())