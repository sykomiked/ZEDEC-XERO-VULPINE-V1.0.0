#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_bucket.py - lay the golden discs out as the release bucket, locally.

    python3 build_system/mk_bucket.py [--discs dist/discs] [--out build/bucket]
                                      [--cartridge FILE ...] [--blocks DIR]
    python3 build_system/mk_bucket.py --self-test

Stages the discs mk_golden_disc.sh built into a directory that `ipfs add -r
--cid-version=1` would turn into the bucket, writes its manifest.json and
prints the root CID that add would give, computed here the way Kubo does it
(256 KiB raw leaves, balanced DAG, plain UnixFS directories). It adds nothing
to any IPFS node and publishes nothing; uploading is the owner's call.

The bucket:
    manifest.json                 what is in it (below)
    00_base/zxv-<arch>-disc1-core.img        one per architecture built
    01_swarm_<group>/zxv-swarm-disc2-<group>.img
    02_swarm_<group>/...          one directory per swarm disc, disc order
    NN_cartridges/<file>          after the swarm discs, when --cartridge given

Every disc is at most 1,800,000,000 bytes. Only the core disc is specific to
an architecture; the swarm discs are the same for all of them.

manifest.json (keys sorted, one stage per disc or file):
    {"format": "zxv-bucket 1",
     "max_disc_bytes": 1800000000,
     "upstream": {"cid": "<bafy...>", "ipns": "<k51...>" | null},
     "stages": [{"dir": "00_base", "file": "zxv-x86_64-disc1-core.img",
                 "kind": "core", "arch": "x86_64", "disc": 1,
                 "bytes": N, "sha256": "<hex>", "cid": "<bafy...>"},
                {"dir": "01_swarm_reason", ..., "kind": "swarm",
                 "arch": "all", "disc": 2, "group": "REASON", ...},
                {"dir": "03_cartridges", ..., "kind": "cartridge",
                 "arch": "all", ...}]}
"upstream" is build_system/zxv_upstream.cid, the bucket the core discs name.
It is not this bucket's root: a core disc inside the bucket cannot carry the
CID of the bucket that holds it. Updates reach users through the IPNS name.

tools/zxv-get fetches a bucket like this one and checks each disc against
manifest.json. --blocks DIR also writes every block of the bucket to
DIR/<cid> (a local block store, for tests and offline gateways).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxv_publish_update import cid_bin, cid_str, file_dag, varint  # noqa: E402

FORMAT = "zxv-bucket 1"
MAX_DISC = 1800000000
ARCHS = ("x86_64", "arm64", "riscv64")
CID_RE = re.compile(r"b[a-z2-7]{20,120}")
IPNS_RE = re.compile(r"k[0-9a-z]{20,120}")


def die(msg):
    print("mk_bucket: " + msg, file=sys.stderr)
    sys.exit(1)


def dir_node(entries, sink=None):
    """entries: [(name, cid_bin, tsize)] -> (cid_bin, tsize) of a plain UnixFS
    directory, links sorted by name bytes as Kubo writes them."""
    body = bytearray()
    for name, cb, ts in sorted(entries, key=lambda e: e[0].encode()):
        nb = name.encode()
        link = b"\x0a" + varint(len(cb)) + cb + b"\x12" + varint(len(nb)) + nb
        link += b"\x18" + varint(ts)
        body += b"\x12" + varint(len(link)) + link
    body += b"\x0a\x02\x08\x01"
    if len(body) > 256 * 1024:
        die("a directory this large would be HAMT-sharded by Kubo; not supported")
    cb = cid_bin(0x70, hashlib.sha256(bytes(body)).digest())
    if sink:
        sink(cb, bytes(body))
    return cb, sum(e[2] for e in entries) + len(body)


def tree_cid(path, sink=None):
    """(cid_bin, tsize) of a file or directory tree, as `ipfs add -r
    --cid-version=1` gives it."""
    if os.path.isdir(path):
        kids = []
        for name in os.listdir(path):
            cb, ts = tree_cid(os.path.join(path, name), sink)
            kids.append((name, cb, ts))
        return dir_node(kids, sink)
    cb, ts, _, _ = file_dag(path, sink)
    return cb, ts


def read_upstream(path):
    up = {"cid": None, "ipns": None}
    for n, raw in enumerate(open(path, encoding="ascii"), 1):
        f = raw.split("#", 1)[0].split()
        if not f:
            continue
        if len(f) != 2 or f[0] not in up or up[f[0]]:
            die("%s line %d: want one 'cid <bafy...>' and at most one 'ipns <k51...>'" % (path, n))
        if not (CID_RE if f[0] == "cid" else IPNS_RE).fullmatch(f[1]):
            die("%s line %d: '%s' is not a valid %s" % (path, n, f[1], f[0]))
        up[f[0]] = f[1]
    if not up["cid"]:
        die(path + ": no cid line")
    return up


def find_discs(discs, prefix):
    cores, swarm = [], []
    for arch in ARCHS:
        p = os.path.join(discs, "%s-%s-disc1-core.img" % (prefix, arch))
        if os.path.isfile(p):
            cores.append((arch, p))
    pat = re.compile(re.escape(prefix) + r"-swarm-disc([0-9]+)-([a-z0-9]{1,16})\.img")
    for name in sorted(os.listdir(discs)):
        m = pat.fullmatch(name)
        if m:
            swarm.append((int(m.group(1)), m.group(2), os.path.join(discs, name)))
    swarm.sort()
    if not cores:
        die("no %s-<arch>-disc1-core.img in %s: run mk_golden_disc.sh <arch>" % (prefix, discs))
    for i, (n, g, _) in enumerate(swarm):
        if n != i + 2:
            die("swarm discs must be numbered 2, 3, ... without gaps; found disc %d (%s)" % (n, g))
    return cores, swarm


def place(src, dst):
    """Hard-link src into the bucket (the discs are large), else copy."""
    try:
        os.link(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


def build(a):
    up = read_upstream(a.upstream)
    cores, swarm = find_discs(a.discs, a.prefix)
    out = a.out
    if os.path.lexists(out):
        mf = os.path.join(out, "manifest.json")
        try:
            ours = json.load(open(mf)).get("format") == FORMAT
        except (OSError, ValueError, AttributeError):
            ours = False
        if not ours:
            die("%s exists and is not a bucket this tool made; remove it yourself" % out)
        shutil.rmtree(out)
    os.makedirs(out)

    stages = []

    def add(d, src, **kw):
        size = os.path.getsize(src)
        if size > a.max_bytes:
            die("%s is %d bytes, over the %d-byte disc limit" % (src, size, a.max_bytes))
        os.makedirs(os.path.join(out, d), exist_ok=True)
        dst = os.path.join(out, d, os.path.basename(src))
        if os.path.exists(dst):
            die("two files named %s in %s" % (os.path.basename(src), d))
        place(src, dst)
        cb, _, n, sha = file_dag(dst)
        st = {"dir": d, "file": os.path.basename(src), "bytes": n, "sha256": sha, "cid": cid_str(cb)}
        st.update(kw)
        stages.append(st)

    for arch, p in cores:
        add("00_base", p, kind="core", arch=arch, disc=1)
    for i, (n, g, p) in enumerate(swarm, 1):
        add("%02d_swarm_%s" % (i, g), p, kind="swarm", arch="all", disc=n, group=g.upper())
    for p in a.cartridge or []:
        if not os.path.isfile(p):
            die("cartridge %s not found" % p)
        add("%02d_cartridges" % (len(swarm) + 1), p, kind="cartridge", arch="all")

    man = {"format": FORMAT, "max_disc_bytes": a.max_bytes, "upstream": up, "stages": stages}
    with open(os.path.join(out, "manifest.json"), "w", encoding="ascii") as f:
        json.dump(man, f, indent=1, sort_keys=True)
        f.write("\n")

    sink = None
    if a.blocks:
        os.makedirs(a.blocks, exist_ok=True)

        def sink(cb, block):
            with open(os.path.join(a.blocks, cid_str(cb)), "wb") as f:
                f.write(block)

    root, _ = tree_cid(out, sink)
    root = cid_str(root)
    with open(out.rstrip("/") + ".cid", "w", encoding="ascii") as f:
        f.write(root + "\n")
    for st in stages:
        print("  %-18s %-36s %11d  %s" % (st["dir"], st["file"], st["bytes"], st["cid"]))
    print("bucket %s: root %s (also in %s.cid)" % (out, root, out.rstrip("/")))
    print("the core discs name upstream cid %s%s" %
          (up["cid"], ", ipns " + up["ipns"] if up["ipns"] else ""))
    print("nothing was added to IPFS or published")


def self_test():
    """The directory encoding against Kubo 0.32.1 (kernel/src/ipfs_node/
    test_ipfsn_dir_vectors.h, dirA): same tree, same root and sub CIDs."""
    import tempfile
    here = os.path.dirname(os.path.abspath(__file__))
    vec = open(os.path.join(here, "..", "kernel", "src", "ipfs_node",
                            "test_ipfsn_dir_vectors.h"), encoding="utf-8").read()

    def want(name):
        return re.search(r"#define %s\s+(?:\\\s*)?\"(b[a-z2-7]+)\"" % name, vec).group(1)

    files = {"Z": b"Z", "a b.txt": b"space\n", "data.bin": bytes((i * 7 + 3) & 255 for i in range(1000)),
             "empty": b"", "hello.txt": b"hello world\n", "sub/inner.txt": b"inner\n",
             "sub/deeper/x": b"x", "ünïcödé.txt": b"u\n"}
    with tempfile.TemporaryDirectory() as t:
        d = os.path.join(t, "dirA")
        for rel, data in files.items():
            p = os.path.join(d, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(data)
        got_root = cid_str(tree_cid(d)[0])
        got_sub = cid_str(tree_cid(os.path.join(d, "sub"))[0])
    ok = got_root == want("KUBO_DIR_A_ROOT") and got_sub == want("KUBO_DIR_A_SUB")
    print("mk_bucket self-test: dirA root %s, sub %s: %s" % (got_root, got_sub, "ok" if ok else "MISMATCH"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--discs", default="dist/discs")
    ap.add_argument("--prefix", default="zxv")
    ap.add_argument("--out", default="build/bucket")
    ap.add_argument("--upstream", default="build_system/zxv_upstream.cid")
    ap.add_argument("--cartridge", action="append")
    ap.add_argument("--blocks")
    ap.add_argument("--max-bytes", type=int, default=MAX_DISC)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    build(a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
