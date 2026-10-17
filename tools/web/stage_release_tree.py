#!/usr/bin/env python3
# stage_release_tree.py — the web app as a release tree: the doorway, the
# boot and its files, the icons, the bank and the docs, plus the seal-time
# boot.json/boot.pack built from the staged tree, and links.json for
# mk_bucket.py's --tree.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: stage_release_tree.py OUT --web web/intro web/boot [--icons DIR]
       [--bank FILE] [--docs DIR] [--manifest-builder tools/web/make_boot_manifest.py]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys


def cid_tsize(path):
    r = subprocess.run(["ipfs", "add", "-q", "--only-hash", "--cid-version=1", path],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip(), os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--web", nargs="+", required=True)
    ap.add_argument("--icons", default=None)
    ap.add_argument("--bank", default=None)
    ap.add_argument("--docs", default=None)
    ap.add_argument("--manifest-builder", default="tools/web/make_boot_manifest.py")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    links = {}
    staged = []

    def stage(src, rel):
        dst = os.path.join(args.out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        staged.append(dst)
        cid, tsize = cid_tsize(dst)
        links[rel] = [cid, tsize]

    for d in args.web:
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            if os.path.isfile(p):
                stage(p, name)
    if args.icons:
        for name in sorted(os.listdir(args.icons)):
            stage(os.path.join(args.icons, name), "icons/" + name)
    if args.bank:
        stage(args.bank, "seedbank/" + os.path.basename(args.bank))
    if args.docs:
        for name in sorted(os.listdir(args.docs)):
            stage(os.path.join(args.docs, name), "docs/" + name)

    # the seal-time boot.json + boot.pack over the staged tree: the
    # manifest names every icon by path; the pack concatenates the icon
    # bytes in the manifest's order, so the browser's off/size slices work.
    subprocess.run([sys.executable, args.manifest_builder, args.out],
                   check=True, capture_output=True)
    manifest = json.load(open(os.path.join(args.out, "boot.json")))
    pack = bytearray()
    for e in manifest["apps"]:
        with open(os.path.join(args.out, e["path"]), "rb") as f:
            pack += f.read()
    with open(os.path.join(args.out, "boot.pack"), "wb") as f:
        f.write(pack)
    cid, tsize = cid_tsize(os.path.join(args.out, "boot.pack"))
    links["boot.pack"] = [cid, tsize]
    cid, tsize = cid_tsize(os.path.join(args.out, "boot.json"))
    links["boot.json"] = [cid, tsize]

    with open(os.path.join(args.out, "links.json"), "w") as f:
        json.dump(links, f, indent=1, sort_keys=True)
    print(f"staged {len(staged) + 2} files -> {args.out}; boot.pack {len(pack)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())