#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_boot_manifest.py — write boot.json for a sealed root, so the browser
boot (web/boot/) streams and checks exactly the files that make up the system.

    make_boot_manifest.py ROOT [--next PAGE] [--model ENTRY.json ...] [SYSTEM_FILE ...]

ROOT holds index.html, boot.js and zxv_boot.wasm (from web/boot/) and the five
gold logos as ZXVG at brand/{36n9,acoto,ascw,smic,cis}-gold.zxvg (made from the
brand vectors with tools/zxvg/svg2zxvg.py). SYSTEM_FILE paths are relative to
ROOT: everything the system loads before it is ready, in the order it loads.
Each entry gets its size and SHA-256; the page counts every byte as it
arrives and checks every file against its hash. A logo that is missing is
left out (its tile stays empty) and named on stderr.

--model adds a model the page may run in the tab (web/ai) to boot.json's
models list. ENTRY.json is one object {name, cid, bytes, sha256, path, quant}
(path relative to ROOT); when the file is in ROOT its size and SHA-256 are
checked against the entry, so the list never names bytes the root lacks. The
boot screen does not stream the models: the companion fetches one when it
opens, and checks it against the same sha256.
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


MODEL_KEYS = ("name", "cid", "bytes", "sha256", "path", "quant")


def model_entry(root, src):
    with open(src) as f:
        m = json.load(f)
    if not isinstance(m, dict) or any(k not in m for k in MODEL_KEYS):
        raise ValueError("%s: a model entry needs %s" % (src, ", ".join(MODEL_KEYS)))
    if not isinstance(m["bytes"], int) or m["bytes"] <= 0:
        raise ValueError("%s: bytes must be a whole number above 0" % src)
    if not isinstance(m["sha256"], str) or len(m["sha256"]) != 64 or \
            any(c not in "0123456789abcdef" for c in m["sha256"]):
        raise ValueError("%s: sha256 must be 64 lower-case hex digits" % src)
    rel = m["path"]
    if not isinstance(rel, str) or not rel or rel.startswith("/") or ".." in rel.split("/"):
        raise ValueError("%s: path must be relative to the root" % src)
    path = os.path.join(root, rel)
    if os.path.exists(path):
        h = hashlib.sha256()
        n = 0
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
                n += len(chunk)
        if n != m["bytes"] or h.hexdigest() != m["sha256"]:
            raise ValueError("%s: %s is %d bytes, sha256 %s; the entry says %d, %s" %
                             (src, rel, n, h.hexdigest(), m["bytes"], m["sha256"]))
    else:
        print("model %s is not in the root (it is fetched from its urls)" % rel, file=sys.stderr)
    return {k: m[k] for k in MODEL_KEYS + ("urls",) if k in m}


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
    models = []
    while rest[:1] == ["--model"]:
        if len(rest) < 2:
            print("--model needs an entry file", file=sys.stderr)
            return 2
        try:
            models.append(model_entry(root, rest[1]))
        except (OSError, ValueError) as e:
            print(e, file=sys.stderr)
            return 1
        rest = rest[2:]
    logos = {}
    for k in LOGOS:
        rel = "brand/%s-gold.zxvg" % k
        if os.path.exists(os.path.join(root, rel)):
            logos[k] = entry(root, rel)
        else:
            print("missing logo: %s (its tile stays empty)" % rel, file=sys.stderr)
    out = {"logos": logos, "system": [entry(root, rel) for rel in rest]}
    if nxt:
        out["next"] = nxt
    if models:
        out["models"] = models
    with open(os.path.join(root, "boot.json"), "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    total = sum(e["size"] for e in list(logos.values()) + out["system"])
    print("wrote %s: %d logos, %d system files, %d bytes" % (os.path.join(root, "boot.json"), len(logos), len(out["system"]), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
