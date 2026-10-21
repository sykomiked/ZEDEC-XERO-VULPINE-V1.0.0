#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_entry_json.py — write entry.json for a sealed root: what the root's
front page (web/entry, the root's index.html) offers.

    make_entry_json.py ROOT [--run PAGE] [--image ARCH:FORMAT:PATH ...]
                            [--script OS:PATH ...] [--vm PATH]
                            [--cid PATH=CID ...]

    --run     the page "Run here" opens (default boot.html, the boot screen)
    --image   a disc image to install or boot: ARCH x86_64 | arm64, FORMAT
              img | iso | qcow2
    --script  an install script: OS unix | windows
    --vm      the VM description (vm.json: discs and launchers)
    --cid     the CID of a file, when the root's builder knows it (a file named
              <CID>.<ext> names its own CID and needs no --cid)

Every PATH is relative to ROOT and must be there: its size and SHA-256 are
measured here, never copied from elsewhere. A CID is only written when it is
given or the file names it; a CID is never computed from part of a file. The
root is put together inner files first, so entry.json is written after
everything it names, and the root's CID is taken after that.
"""
import hashlib
import json
import os
import re
import sys

ARCHES = ("x86_64", "arm64")
FORMATS = ("img", "iso", "qcow2")
OSES = ("unix", "windows")
CID_RE = re.compile(r"^(baf[a-z2-7]{20,}|Qm[1-9A-HJ-NP-Za-km-z]{44})$")


def measure(root, rel):
    if not rel or rel.startswith("/") or ".." in rel.split("/"):
        raise ValueError("%s: a path must be relative to the root" % rel)
    path = os.path.join(root, rel)
    h = hashlib.sha256()
    n = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
            n += len(chunk)
    return {"path": rel, "bytes": n, "sha256": h.hexdigest()}


def with_cid(e, cids):
    stem = os.path.basename(e["path"]).split(".", 1)[0]
    cid = cids.get(e["path"]) or (stem if CID_RE.match(stem) else None)
    if cid:
        if not CID_RE.match(cid):
            raise ValueError("%s: %r is not a CID" % (e["path"], cid))
        e["cid"] = cid
    return e


def parse(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        raise SystemExit(__doc__.strip())
    root, rest = argv[1], argv[2:]
    o = {"run": "boot.html", "images": [], "scripts": [], "vm": None, "cids": {}}
    while rest:
        if len(rest) < 2:
            raise ValueError("%s needs a value" % rest[0])
        flag, val, rest = rest[0], rest[1], rest[2:]
        if flag == "--run":
            o["run"] = val
        elif flag == "--image":
            parts = val.split(":", 2)
            if len(parts) != 3 or parts[0] not in ARCHES or parts[1] not in FORMATS:
                raise ValueError("--image wants ARCH:FORMAT:PATH (ARCH %s, FORMAT %s), not %r" %
                                 ("|".join(ARCHES), "|".join(FORMATS), val))
            o["images"].append(parts)
        elif flag == "--script":
            parts = val.split(":", 1)
            if len(parts) != 2 or parts[0] not in OSES:
                raise ValueError("--script wants OS:PATH (OS %s), not %r" % ("|".join(OSES), val))
            o["scripts"].append(parts)
        elif flag == "--vm":
            o["vm"] = val
        elif flag == "--cid":
            p, _, c = val.partition("=")
            if not p or not c:
                raise ValueError("--cid wants PATH=CID, not %r" % val)
            o["cids"][p] = c
        else:
            raise ValueError("unknown option %s" % flag)
    return root, o


def main(argv):
    try:
        root, o = parse(argv)
        if not os.path.isfile(os.path.join(root, o["run"])):
            raise ValueError("%s: the page Run here opens is not in the root" % o["run"])
        out = {"zxv_entry": 1, "run": {"path": o["run"]}, "install": {"images": [], "scripts": []}}
        for arch, fmt, rel in o["images"]:
            e = with_cid(measure(root, rel), o["cids"])
            out["install"]["images"].append(dict({"arch": arch, "format": fmt}, **e))
        for osname, rel in o["scripts"]:
            e = with_cid(measure(root, rel), o["cids"])
            out["install"]["scripts"].append(dict({"os": osname}, **e))
        if o["vm"]:
            with open(os.path.join(root, o["vm"])) as f:
                vm = json.load(f)
            if not isinstance(vm, dict) or vm.get("zxv_vm") != 1:
                raise ValueError("%s: not a vm.json (zxv_vm 1)" % o["vm"])
            out["vm"] = with_cid(measure(root, o["vm"]), o["cids"])
    except (OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 1
    with open(os.path.join(root, "entry.json"), "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    print("wrote %s: run %s, %d images, %d scripts, vm %s" %
          (os.path.join(root, "entry.json"), o["run"], len(out["install"]["images"]),
           len(out["install"]["scripts"]), o["vm"] or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
