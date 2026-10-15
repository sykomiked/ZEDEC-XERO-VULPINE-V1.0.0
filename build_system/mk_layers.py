#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_layers.py - the release's layer manifests, each anchored to the one below.

    python3 build_system/mk_layers.py l0 [--machines build_system/layer0.machines]
    python3 build_system/mk_layers.py l1 --parent CID --boot ARCH=EFI ...
    python3 build_system/mk_layers.py l2 --parent CID
    python3 build_system/mk_layers.py l3 --parent CID --dir BOOKLETS [--compat evo_comm]
    python3 build_system/mk_layers.py l4 --parent CID --dir OVERLAY [--compat evo_comm]
    python3 build_system/mk_layers.py check build/layers/L0.json build/layers/L1.json ...
    (all take --out build/layers; `make layers` runs l0, l1 and l2 in order)

The release is a stack of layers (docs/LAYERS.md):
    L0  the hardware envelope        build_system/layer0.machines
    L1  the core boot engine         each arch's boot file (BOOT<arch>.EFI, which
                                     carries the kernel) and the core models
    L2  the swarm                    the swarm models, by group
    L3  the Library of Alexandria    its booklets
    L4  community overlays           an overlay may sit on another overlay
Each layer is named by the CID of its manifest (CIDv1 raw sha2-256 of the
file, "bafkrei..."; at most 1 MiB), and each manifest names its parent's CID,
so a changed byte anywhere below changes every CID above it. A layer lists
what it holds, never the disc images that carry it: a disc cannot carry its
own hash, and listing content lets the core disc carry L0, L1 and L2 while
a kernel fix leaves the swarm discs byte for byte the same.

Each step writes OUT/L<n>.json and OUT/L<n>.cid. The manifest is JSON in one
fixed layout, so the kernel (kernel/src/layers) reads it without a JSON
parser and any other layout of the same data is a different CID:

{
 "format": "zxv-layer 1",
 "layer_depth": 1,
 "layer_name": "CORE_BOOT_ENGINE",
 "parent_layer_cid": "bafkrei...",        null at L0
 "parent_compat": "exact",                or "evo_comm" (L3, L4 only)
 "objects": [
  {"kind": "boot", "arch": "x86_64", "name": "BOOTX64.EFI", "bytes": N, "sha256": "..."},
  ...
 ]
}

"exact" means the parent is that CID and no other. "evo_comm" lets L3 or L4
also stand on a parent admitted under the evo_comm compatibility contract,
so a small kernel fix need not re-anchor every booklet; discs (L1, L2) are
always exact. The manifest cannot hold its own CID; the child names it.
"""

import argparse
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxv_publish_update import cid_bin, cid_str, file_dag  # noqa: E402

FORMAT = "zxv-layer 1"
NAMES = {0: "HARDWARE_ENVELOPE", 1: "CORE_BOOT_ENGINE", 2: "SWARM", 3: "ALEXANDRIA",
         4: "COMMUNITY_OVERLAY"}
MAX_BYTES = 1 << 20
RAW = 0x55
SHA_RE = re.compile(r"[0-9a-f]{64}")
NAME_RE = re.compile(r"[A-Za-z0-9._+:@-]{1,63}")
ARCHS = ("x86_64", "arm64", "riscv64")


def die(msg):
    print("mk_layers: " + msg, file=sys.stderr)
    sys.exit(1)


def layer_cid(data):
    return cid_str(cid_bin(RAW, hashlib.sha256(data).digest()))


def obj_line(o):
    """One object, keys in a fixed order, on one line."""
    order = ["kind", "arch", "group", "name", "firmware", "link_address", "page_bytes",
             "sector_bytes", "min_ram_mib", "bytes", "sha256", "cid"]
    extra = set(o) - set(order)
    if extra:
        die("object keys %s are not part of the format" % sorted(extra))
    return "{" + ", ".join("%s: %s" % (json.dumps(k), json.dumps(o[k]))
                           for k in order if k in o) + "}"


def render(depth, parent, compat, objects):
    if not objects:
        die("L%d would hold nothing" % depth)
    lines = ["{",
             ' "format": %s,' % json.dumps(FORMAT),
             ' "layer_depth": %d,' % depth,
             ' "layer_name": %s,' % json.dumps(NAMES[depth]),
             ' "parent_layer_cid": %s,' % json.dumps(parent),
             ' "parent_compat": %s,' % json.dumps(compat),
             ' "objects": [']
    lines += ["  " + obj_line(o) + ("," if i + 1 < len(objects) else "")
              for i, o in enumerate(objects)]
    lines += [" ]", "}", ""]
    data = "\n".join(lines).encode("ascii")
    if len(data) > MAX_BYTES:
        die("L%d manifest is %d bytes, over 1 MiB" % (depth, len(data)))
    return data


def parse(data):
    """Read a manifest back, refusing anything render would not write."""
    try:
        m = json.loads(data.decode("ascii"))
    except (UnicodeDecodeError, ValueError) as e:
        raise ValueError("not ASCII JSON: %s" % e)
    if not isinstance(m, dict) or m.get("format") != FORMAT:
        raise ValueError("not a '%s' manifest" % FORMAT)
    d = m.get("layer_depth")
    if d not in NAMES:
        raise ValueError("layer_depth %r is not 0..4" % d)
    if render(d, m.get("parent_layer_cid"), m.get("parent_compat"), m.get("objects")) != data:
        raise ValueError("not in the one layout mk_layers.py writes")
    if (d == 0) != (m["parent_layer_cid"] is None):
        raise ValueError("L0 and only L0 has no parent")
    if m["parent_compat"] != "exact" and not (d >= 3 and m["parent_compat"] == "evo_comm"):
        raise ValueError("parent_compat %r at L%d" % (m["parent_compat"], d))
    return m


def check(paths):
    """Each manifest's parent is the CID of the one before it; depth rises by
    one, except that L4 may follow L4."""
    prev = None
    for p in paths:
        data = open(p, "rb").read()
        try:
            m = parse(data)
        except ValueError as e:
            die("%s: %s" % (p, e))
        d = m["layer_depth"]
        if prev is None:
            if d != 0:
                die("%s: the chain starts at L%d, not L0" % (p, d))
        else:
            pd, pcid = prev
            if not (d == pd + 1 or d == pd == 4):
                die("%s: L%d cannot stand on L%d" % (p, d, pd))
            if m["parent_layer_cid"] != pcid:
                die("%s: names parent %s, but the layer below is %s"
                    % (p, m["parent_layer_cid"], pcid))
        prev = (d, layer_cid(data))
        print("  L%d %s %s" % (d, prev[1], p))
    print("lineage ok: %d layers, top %s" % (len(paths), prev[1]))


def lines(path):
    for raw in open(path, encoding="utf-8"):
        f = raw.split("#", 1)[0].split()
        if f:
            yield f


def models(listing):
    out = {}
    for f in lines(listing):
        if len(f) < 5 or not SHA_RE.fullmatch(f[1]) or not f[2].isdigit():
            die("%s: bad model line %r" % (listing, " ".join(f)))
        out[f[0]] = (int(f[2]), f[1])
    return out


def write(a, depth, objects, compat="exact"):
    if depth and not (a.parent and re.fullmatch(r"b[a-z2-7]{20,120}", a.parent)):
        die("L%d needs --parent with the CID of L%d" % (depth, depth - 1))
    if a.parent and not a.parent.startswith("bafkrei"):
        die("--parent %s is not a layer CID (CIDv1 raw sha2-256, bafkrei...)" % a.parent)
    data = render(depth, a.parent if depth else None, compat, objects)
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, "L%d.json" % depth)
    with open(path, "wb") as f:
        f.write(data)
    cid = layer_cid(data)
    with open(os.path.join(a.out, "L%d.cid" % depth), "w", encoding="ascii") as f:
        f.write(cid + "\n")
    print("L%d %s: %d objects, %s" % (depth, NAMES[depth], len(objects), cid))


def file_obj(kind, path, **kw):
    cb, _, n, sha = file_dag(path)
    o = {"kind": kind}
    o.update(kw)
    o.update({"name": os.path.basename(path), "bytes": n, "sha256": sha, "cid": cid_str(cb)})
    return o


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("step", choices=["l0", "l1", "l2", "l3", "l4", "check"])
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--out", default="build/layers")
    ap.add_argument("--parent")
    ap.add_argument("--machines", default="build_system/layer0.machines")
    ap.add_argument("--boot", action="append", default=[], metavar="ARCH=FILE")
    ap.add_argument("--models", default="build_system/golden.models")
    ap.add_argument("--core", default="build_system/golden.core")
    ap.add_argument("--swarm", default="build_system/golden.swarm")
    ap.add_argument("--dir")
    ap.add_argument("--compat", choices=["exact", "evo_comm"], default="exact")
    a = ap.parse_args()

    if a.step == "check":
        if not a.paths:
            die("check needs the manifests, root first")
        check(a.paths)
        return 0
    if a.step != "l0" and a.paths:
        die("unexpected arguments %s" % a.paths)
    if a.step == "l0":
        objs = []
        for f in lines(a.machines):
            if len(f) != 6 or f[0] not in ARCHS or f[1] != "UEFI" or \
                    not re.fullmatch(r"0x[0-9a-f]+", f[2]) or not all(x.isdigit() for x in f[3:]):
                die("%s: bad machine line %r" % (a.machines, " ".join(f)))
            objs.append({"kind": "machine", "arch": f[0], "firmware": f[1], "link_address": f[2],
                         "page_bytes": int(f[3]), "sector_bytes": int(f[4]),
                         "min_ram_mib": int(f[5])})
        if a.parent:
            die("L0 has no parent")
        write(a, 0, objs)
    elif a.step == "l1":
        if not a.boot:
            die("l1 needs --boot ARCH=FILE for each architecture")
        mdl = models(a.models)
        objs = []
        for b in sorted(a.boot):
            arch, _, path = b.partition("=")
            if arch not in ARCHS or not os.path.isfile(path):
                die("--boot %s: want ARCH=FILE with an existing file" % b)
            objs.append(file_obj("boot", path, arch=arch))
        for f in lines(a.core):
            if f[0] not in mdl:
                die("%s names %s, which %s does not list" % (a.core, f[0], a.models))
            n, sha = mdl[f[0]]
            objs.append({"kind": "model", "arch": "all", "name": f[0], "bytes": n, "sha256": sha})
        write(a, 1, objs)
    elif a.step == "l2":
        mdl = models(a.models)
        objs = []
        for f in lines(a.swarm):
            if len(f) != 2 or f[1] not in mdl:
                die("%s: %r names no model of %s" % (a.swarm, " ".join(f), a.models))
            n, sha = mdl[f[1]]
            objs.append({"kind": "model", "arch": "all", "group": f[0].upper(), "name": f[1],
                         "bytes": n, "sha256": sha})
        write(a, 2, objs)
    else:
        depth = int(a.step[1])
        if not a.dir or not os.path.isdir(a.dir):
            die("%s needs --dir with the %s" % (a.step, "booklets" if depth == 3 else "overlay"))
        objs = []
        for root, dirs, files in os.walk(a.dir):
            dirs.sort()
            for name in sorted(files):
                p = os.path.join(root, name)
                if not NAME_RE.fullmatch(name):
                    die("%s: name not of A-Za-z0-9._+:@- (1-63)" % p)
                objs.append(file_obj("booklet" if depth == 3 else "overlay", p, arch="all"))
        write(a, depth, objs, a.compat)
    return 0


if __name__ == "__main__":
    sys.exit(main())
