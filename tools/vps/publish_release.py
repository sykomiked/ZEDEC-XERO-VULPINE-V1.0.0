#!/usr/bin/env python3
# publish_release.py — the clean Filebase migration: one S3-pipeline pass
# over the release bucket, then the pin manifest for the fleet.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Upload the release bucket to Filebase in one pass, every file where it
belongs, and write the pin manifest: each object's CID plus the root CID
(the list the nine nodes pin). Run after ipfs-dist seals:

    python3 tools/vps/publish_release.py build/ipfs-dist/bucket --prefix zxv-release

The manifest lands at <out>/pin-manifest.json:
    {"root": "<cid>", "objects": {"<path>": "<cid>", ...}}

usage: publish_release.py <bucket-dir> [--prefix PREFIX] [--out FILE]
"""
import argparse
import json
import os
import subprocess
import sys

# the Filebase credentials come from the deploy env (never the CLI)
ENV_PATH = os.path.expanduser("~/.config/zxv-deploy.env")
if os.path.exists(ENV_PATH):
    with open(ENV_PATH) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip().strip('"'))

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from s3_sigv4 import put_object  # noqa: E402

CTYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".json": "application/json",
    ".wasm": "application/wasm",
    ".pack": "application/octet-stream",
    ".zxvg": "application/octet-stream",
    ".bank": "application/octet-stream",
    ".car": "application/octet-stream",
    ".img": "application/octet-stream",
    ".qcow2": "application/octet-stream",
    ".md": "text/markdown; charset=utf-8",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".ttf": "font/ttf",
}


def cid_of(path):
    r = subprocess.run(["ipfs", "add", "-q", "--only-hash", "--cid-version=1", path],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bucket_dir")
    ap.add_argument("--prefix", default="zxv-release")
    ap.add_argument("--out", default="build/ipfs-dist/pin-manifest.json")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.bucket_dir)
    if not os.path.isdir(root):
        print(f"no sealed bucket at {root} — the release build has not "
              f"finished (or the bucket was not rsynced from the A6000)")
        return 1
    objects = {}
    n = 0
    for dirpath, _dirs, files in os.walk(root):
        for f in sorted(files):
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, root)
            key = args.prefix + "/" + rel
            ext = os.path.splitext(f)[1].lower()
            ct = CTYPES.get(ext, "application/octet-stream")
            with open(path, "rb") as fh:
                data = fh.read()
            if args.dry_run:
                print(f"[dry] {key} ({len(data)} bytes)")
            else:
                st, body = put_object("zedec", key, data, content_type=ct, acl="public-read")
                if st != 200:
                    print(f"FAIL {key}: {st} {body[:120]}")
                    return 1
                objects[key] = cid_of(path)
            n += 1
    root_cid = None
    if not args.dry_run:
        root_cid = cid_of(root)
        manifest = {"root": root_cid, "count": n, "objects": objects}
        os.makedirs(os.path.dirname(args.out), exist_ok=True)
        with open(args.out, "w") as f:
            json.dump(manifest, f, indent=1, sort_keys=True)
        print(f"published {n} objects; root {root_cid}")
        print(f"pin manifest: {args.out}")
    else:
        print(f"[dry] {n} objects; no upload, no manifest")
    return 0


if __name__ == "__main__":
    sys.exit(main())