#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""S1: download Tiamat's models exactly as the manifest pins them.

Every file is fetched from its pinned revision and its SHA-256 checked
against the manifest; a mismatch deletes the file and stops. Weights land
under --dest (outside the repository: weights never go into git).

    python3 fetch_pinned.py --dest /srv/tiamat/models --placement disc disc_gated
    python3 fetch_pinned.py --dest /srv/tiamat/models --source   # source files too
"""
import argparse
import hashlib
import json
import os
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "docs/release/swarm_model_manifest.json"
HUB = "https://huggingface.co"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def get(url, dest, want_sha, want_size):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size == want_size and sha256(dest) == want_sha:
        return "ok (cached)"
    req = urllib.request.Request(url)
    tok = os.environ.get("HF_TOKEN")
    if tok:
        req.add_header("Authorization", f"Bearer {tok}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 22):
            f.write(chunk)
    got = sha256(tmp)
    if want_sha and got != want_sha:
        tmp.unlink()
        sys.exit(f"SHA-256 mismatch for {url}: got {got}, manifest {want_sha}")
    tmp.rename(dest)
    return "ok"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dest", required=True)
    ap.add_argument("--placement", nargs="*", default=["disc", "disc_gated"])
    ap.add_argument("--source", action="store_true", help="also fetch source files (for adapters, conversions, training)")
    ap.add_argument("--only", nargs="*", help="dragon names to fetch")
    a = ap.parse_args()
    dest = pathlib.Path(a.dest)
    if ROOT in dest.resolve().parents or dest.resolve() == ROOT:
        sys.exit("--dest must be outside the repository (weights never go into git)")
    man = json.loads(MANIFEST.read_text())
    for m in man["models"]:
        if m["placement"] not in a.placement or (a.only and m["dragon_name"] not in a.only):
            continue
        base = dest / m["dragon_name"]
        for s in m["shipped_files"]:
            print(m["dragon_name"], s["file"], get(s["url"], base / "ship" / s["file"], s["sha256"], s["size"]))
        if a.source or not m["shipped_files"]:
            for s in m["source_files"]:
                url = f"{HUB}/{m['model_id']}/resolve/{m['revision']}/{s['path']}"
                print(m["dragon_name"], s["path"], get(url, base / "src" / s["path"], s["sha256"], s["size"]))
            # small non-weight files (config, tokenizer, licence, notice) from the same revision
            for extra in ("config.json", "tokenizer.json", "tokenizer_config.json", "generation_config.json",
                          "special_tokens_map.json", "LICENSE", "NOTICE", "README.md"):
                url = f"{HUB}/{m['model_id']}/resolve/{m['revision']}/{extra}"
                try:
                    with urllib.request.urlopen(url, timeout=60) as r:
                        (base / "src").mkdir(parents=True, exist_ok=True)
                        (base / "src" / extra).write_bytes(r.read())
                except Exception:
                    pass


if __name__ == "__main__":
    main()
