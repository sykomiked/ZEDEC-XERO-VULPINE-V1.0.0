#!/usr/bin/env python3
# fetch_versioned.py — strict version control for every download. Each run
# creates a NEW timestamped version under versions/, copies the fetched
# state in, and writes a SHA-256 manifest. Prior versions are never
# modified or deleted, so the full lineage of the system is preserved
# locally.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: fetch_versioned.py [--note TEXT] [--source DIR] [--root DIR]"""
import argparse
import hashlib
import json
import os
import shutil
import time
from pathlib import Path


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--note", default="")
    ap.add_argument("--source", default="")
    ap.add_argument("--root", default="")
    args = ap.parse_args()

    root = Path(args.root) if args.root else Path(
        os.environ.get("ZXV_LOCAL_ROOT", str(Path.home() / "zxv-ipfs-build")))
    source = Path(args.source) if args.source else root / "fetched"
    versions = root / "versions"
    versions.mkdir(parents=True, exist_ok=True)

    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    ver = versions / stamp
    n = 0
    if source.is_dir():
        for p in source.rglob("*"):
            if not p.is_file():
                continue
            rel = p.relative_to(source)
            dst = ver / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dst)
            n += 1

    rows = []
    for p in sorted(ver.rglob("*")):
        if p.is_file() and p.name != "MANIFEST.sha256":
            rows.append("%s  %d  %s" % (sha256_of(p), p.stat().st_size,
                                        p.relative_to(ver)))
    (ver / "MANIFEST.sha256").write_text("\n".join(rows) + "\n")
    (ver / "VERSION.json").write_text(json.dumps({
        "version": stamp, "files": n, "note": args.note,
        "source": str(source),
    }, indent=1) + "\n")

    print("version:   %s" % stamp)
    print("files:     %d" % n)
    print("manifest:  %d entries (MANIFEST.sha256)" % len(rows))
    if args.note:
        print("note:      %s" % args.note)
    print("versions:  %d kept (all prior versions preserved)"
          % len([d for d in versions.iterdir() if d.is_dir()]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
