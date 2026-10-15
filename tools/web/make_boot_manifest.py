#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_boot_manifest.py — write boot.json for a sealed root, so the browser
boot (web/boot/) streams and checks exactly the files that make up the system.

    make_boot_manifest.py ROOT [--next PAGE] [SYSTEM_FILE ...]

ROOT holds boot.html, boot.js and zxv_boot.wasm (from web/boot/) and the five
gold logos as ZXVG at brand/{36n9,acoto,ascw,smic,cis}-gold.zxvg (made from the
brand vectors with tools/zxvg/svg2zxvg.py). SYSTEM_FILE paths are relative to
ROOT: everything the system loads before it is ready, in the order it loads.
Each entry gets its size and SHA-256; the page counts every byte as it
arrives and checks every file against its hash. A logo that is missing is
left out (its tile stays empty) and named on stderr.
"""
import hashlib
import json
import os
import sys

LOGOS = ["36n9", "acoto", "ascw", "smic", "cis"]


def entry(root, rel):
    path = os.path.join(root, rel)
    with open(path, "rb") as f:
        data = f.read()
    return {"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 2
    root, rest, nxt = argv[1], argv[2:], None
    if rest[:1] == ["--next"]:
        if len(rest) < 2:
            print("--next needs a page", file=sys.stderr)
            return 2
        nxt, rest = rest[1], rest[2:]
    logos = {}
    for k in LOGOS:
        rel = "brand/%s-gold.zxvg" % k
        if os.path.exists(os.path.join(root, rel)):
            logos[k] = entry(root, rel)
        else:
            print("missing logo: %s (its tile stays empty)" % rel, file=sys.stderr)
    out = {"logos": logos, "system": [entry(root, rel) for rel in rest]}
    fp = os.path.join(root, "font.ttf")
    if os.path.exists(fp):
        # the login screen behind the vault doors reads its names
        out["font"] = entry(root, "font.ttf")
    if nxt:
        out["next"] = nxt
    with open(os.path.join(root, "boot.json"), "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    total = sum(e["size"] for e in list(logos.values()) + out["system"] +
                ([out["font"]] if "font" in out else []))
    print("wrote %s: %d logos, %d system files, %d bytes" % (os.path.join(root, "boot.json"), len(logos), len(out["system"]), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
