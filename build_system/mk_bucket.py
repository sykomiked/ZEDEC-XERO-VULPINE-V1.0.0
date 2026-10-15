#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_bucket.py - lay the golden discs out as the release bucket, locally.

    python3 build_system/mk_bucket.py [--discs dist/discs] [--out build/bucket]
                                      [--cartridge FILE ...] [--blocks DIR]
                                      [--architecture docs/ARCHITECTURE.md]
                                      [--layers build/layers]
    python3 build_system/mk_bucket.py --self-test

Stages the discs mk_golden_disc.sh built into a directory that `ipfs add -r
--cid-version=1` would turn into the bucket, every file named by its own CID,
writes the installer file and prints the origin CID (the root) that add would
give, computed here the way Kubo does it under the pinned parameters of
docs/BUCKET.md (256 KiB chunks, raw leaves, balanced DAG of 174 links,
sha2-256, plain UnixFS directories). It adds nothing to any IPFS node and
publishes nothing; uploading is the owner's call.

The release is built leaves first and named the same way (docs/BUCKET.md):
each file is named <its CID>.<ext>, the installer file lists them all and is
named by its own CID, and the root directory, whose links carry those names
and CIDs, is the origin CID, computed last. One CID installs everything.

The bucket:
    <cid>.install                 the installer file (below)
    00_base/<cid>.img             a core disc (disc 1), one per architecture
    00_base/<cid>.md              with --architecture: the distribution section
                                  of that file (between the zxv:distribution
                                  start and end markers), so the bucket says
                                  what it is
    01_swarm/<cid>.img            every swarm disc
    02_cartridges/<cid>.<ext>     when --cartridge given
    03_layers/<cid>.json          with --layers: the layer manifests L0 to L2
                                  (mk_layers.py, docs/LAYERS.md), each naming
                                  its parent's CID

The numbering is fixed for the whole release, so a directory keeps its name
whether or not the ones before it are present: 04_overlays (L4, community
overlays), 05_alexandria (L3, the Library of Alexandria), 06_tiamat (the
Tiamat cells, part of L2), 07_matrix (the 144-layer matrix), 08_vectors (its
vector plane) and 09_source (the source bundle) are laid out by the threads
that build them and added beside these; this tool does not write them.

Every disc is at most 1,800,000,000 bytes. Only the core disc is specific to
an architecture; the swarm discs are the same for all of them.

The installer file (ASCII; tools/zxv-get reads it):
    # zxv-install 1
    max_disc_bytes 1800000000
    seal <POWERDOWN_NIC|LOCAL_MESH_ONLY|PROMPT_USER>
    gateway <url>                       (the upstream file's, any number)
    <kind> <arch> <disc|-> <GROUP|-> <bytes> <sha256> <cid> <dir>/<cid>.<ext>
kind is core, layer, swarm, cartridge or doc, in that order: the core discs,
the layers they stand on, the swarm discs, the rest. "seal" is the mode the
core discs carry (their UPSTREAM.CID "seal" line, PROMPT_USER when absent).
The upstream file (build_system/zxv_upstream.cid) is the trusted list the
core discs carry: where to fetch from. It is not this bucket's root, and a
core disc never needs it: the swarm discs it installs are baked into its
kernel by CID (build_system/mk_table.py).

--blocks DIR also writes every block of the bucket to DIR/<cid> (a local
block store, for tests and offline gateways).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxv_publish_update import cid_bin, cid_str, file_dag, varint  # noqa: E402

FORMAT = "zxv-install 1"
MAX_DISC = 1800000000
ARCHS = ("x86_64", "arm64", "riscv64")
CID_RE = re.compile(r"b[a-z2-7]{20,120}")
IPNS_RE = re.compile(r"k[0-9a-z]{20,120}")
SEAL_MODES = ("POWERDOWN_NIC", "LOCAL_MESH_ONLY", "PROMPT_USER")
GW_RE = re.compile(r"https?://[!-~]+")


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
    up = {"cid": None, "ipns": None, "gateways": [], "seal": None}
    for n, raw in enumerate(open(path, encoding="ascii"), 1):
        f = raw.split("#", 1)[0].split()
        if not f:
            continue
        if len(f) == 2 and f[0] == "gateway":
            if not GW_RE.fullmatch(f[1]) or len(f[1]) >= 80:
                die("%s line %d: '%s' is not an http:// or https:// URL" % (path, n, f[1]))
            up["gateways"].append(f[1])
            continue
        if len(f) == 2 and f[0] == "seal":
            if up["seal"] or f[1] not in SEAL_MODES:
                die("%s line %d: one 'seal' line, one of %s" % (path, n, ", ".join(SEAL_MODES)))
            up["seal"] = f[1]
            continue
        if len(f) != 2 or f[0] not in ("cid", "ipns") or up[f[0]]:
            die("%s line %d: want one 'cid <bafy...>', at most one 'ipns <k51...>' and "
                "'gateway <url>' lines" % (path, n))
        if not (CID_RE if f[0] == "cid" else IPNS_RE).fullmatch(f[1]):
            die("%s line %d: '%s' is not a valid %s" % (path, n, f[1], f[0]))
        up[f[0]] = f[1]
    if not up["cid"]:
        die(path + ": no cid line")
    up["seal"] = up["seal"] or "PROMPT_USER"
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


MARK_START = "<!-- zxv:distribution:start -->"
MARK_END = "<!-- zxv:distribution:end -->"


def distribution_section(path):
    """The text between the distribution markers of path, which must hold
    each marker exactly once and in order."""
    t = open(path, encoding="utf-8").read()
    if t.count(MARK_START) != 1 or t.count(MARK_END) != 1 or t.index(MARK_START) > t.index(MARK_END):
        die("%s has no distribution section (one %s ... %s pair)" % (path, MARK_START, MARK_END))
    body = t[t.index(MARK_START) + len(MARK_START):t.index(MARK_END)].strip("\n")
    return ("<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC\n"
            "     SPDX-License-Identifier: Apache-2.0 -->\n"
            "<!-- From %s, the distribution section, by build_system/mk_bucket.py. -->\n%s\n"
            % (os.path.basename(path), body))


def find_layers(d):
    """[(n, path)] of d/L0.json, d/L1.json, ...: from L0 up without a gap,
    the chain checked by mk_layers.py first."""
    found = []
    while os.path.isfile(os.path.join(d, "L%d.json" % len(found))):
        found.append((len(found), os.path.join(d, "L%d.json" % len(found))))
    if not found:
        die("%s holds no L0.json (build_system/mk_layers.py, make build-all-layers)" % d)
    names = {os.path.basename(p) for _, p in found}
    extra = sorted(f for f in os.listdir(d) if re.fullmatch(r"L\d+\.json", f) and f not in names)
    if extra:
        die("%s: %s without the layers below it" % (d, ", ".join(extra)))
    r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     "mk_layers.py"), "check"] +
                       [p for _, p in found], capture_output=True, text=True)
    if r.returncode != 0:
        die("the layers in %s do not form a chain: %s" % (d, r.stderr.strip()))
    return found


def place(src, dst):
    """Hard-link src into the bucket (the discs are large), else copy."""
    try:
        os.link(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


def ours(out):
    """Is out a bucket this tool made: one <cid>.install at its root whose
    first line is the header?"""
    try:
        inst = [n for n in os.listdir(out) if n.endswith(".install")]
        return len(inst) == 1 and open(os.path.join(out, inst[0]), encoding="ascii").readline() \
            == "# %s\n" % FORMAT
    except (OSError, UnicodeDecodeError):
        return False


def build(a):
    up = read_upstream(a.upstream)
    cores, swarm = find_discs(a.discs, a.prefix)
    arch_doc = distribution_section(a.architecture) if a.architecture else None
    layers = find_layers(a.layers) if a.layers else []
    out = a.out
    if os.path.lexists(out):
        if not ours(out):
            die("%s exists and is not a bucket this tool made; remove it yourself" % out)
        shutil.rmtree(out)
    os.makedirs(out)

    stages = []

    def add(d, src, **kw):
        """Place src as d/<its CID>.<ext>: every file is named by its own CID."""
        size = os.path.getsize(src)
        if size > a.max_bytes:
            die("%s is %d bytes, over the %d-byte disc limit" % (src, size, a.max_bytes))
        cb, _, n, sha = file_dag(src)
        ext = os.path.splitext(src)[1].lower().lstrip(".")
        if not re.fullmatch(r"[a-z0-9]{1,8}", ext):
            die("%s: its extension is not 1-8 letters or digits" % src)
        name = "%s.%s" % (cid_str(cb), ext)
        os.makedirs(os.path.join(out, d), exist_ok=True)
        dst = os.path.join(out, d, name)
        if os.path.exists(dst):
            die("%s and another file in %s have the same bytes" % (src, d))
        place(src, dst)
        st = {"dir": d, "file": name, "from": os.path.basename(src), "bytes": n, "sha256": sha,
              "cid": cid_str(cb)}
        st.update(kw)
        stages.append(st)

    for arch, p in cores:
        add("00_base", p, kind="core", arch=arch, disc=1)
    for n, p in layers:
        add("03_layers", p, kind="layer", arch="all", layer=n)
    for n, g, p in swarm:
        add("01_swarm", p, kind="swarm", arch="all", disc=n, group=g.upper())
    for p in a.cartridge or []:
        if not os.path.isfile(p):
            die("cartridge %s not found" % p)
        add("02_cartridges", p, kind="cartridge", arch="all")
    if arch_doc:
        doc = os.path.join(out, "ARCHITECTURE.md")
        with open(doc, "w", encoding="utf-8") as f:
            f.write(arch_doc)
        add("00_base", doc, kind="doc", arch="all")
        os.remove(doc)

    # The installer file: everything above, in install order, by CID; named
    # by its own CID. It is the last file made; only the root is after it.
    text = "# %s\nmax_disc_bytes %d\nseal %s\n" % (FORMAT, a.max_bytes, up["seal"])
    text += "".join("gateway %s\n" % g for g in up["gateways"])
    for st in stages:
        text += "%s %s %s %s %d %s %s %s/%s\n" % (
            st["kind"], st["arch"], st.get("disc", "-"), st.get("group", "-"), st["bytes"],
            st["sha256"], st["cid"], st["dir"], st["file"])
    tmp = os.path.join(out, "installer.tmp")
    with open(tmp, "w", encoding="ascii") as f:
        f.write(text)
    inst = cid_str(file_dag(tmp)[0]) + ".install"
    os.replace(tmp, os.path.join(out, inst))

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
        print("  %-14s %-9s %-36s %11d  %s" % (st["dir"], st["kind"], st["from"], st["bytes"],
                                               st["file"]))
    print("  %-14s %-9s %-36s %11d  %s" % (".", "installer", "", len(text), inst))
    print("bucket %s: origin %s (also in %s.cid)" % (out, root, out.rstrip("/")))
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
    ap.add_argument("--architecture")
    ap.add_argument("--layers")
    ap.add_argument("--max-bytes", type=int, default=MAX_DISC)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    build(a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
