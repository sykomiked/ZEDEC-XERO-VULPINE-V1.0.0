#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_bucket.py - mk_bucket.py and tools/zxv-get, end to end, offline.

    python3 build_system/test_bucket.py

Makes small stand-in discs (two core discs, one deep enough for a two-level
DAG, two swarm discs and a cartridge), lays them out with mk_bucket.py,
serves the bucket's blocks from a local trustless gateway on 127.0.0.1 and
downloads with zxv-get. Then checks that every way the bucket, the gateway or
the installer file can lie is refused, that a refused disc leaves no finished
file, and that zxv-get waits out a gateway that is down and resumes from a
.part it re-hashes.
Needs no network and no IPFS node.
"""

import hashlib
import http.server
import importlib.machinery
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from mk_bucket import dir_node, read_upstream, tree_cid  # noqa: E402
from zxv_publish_update import cid_str, file_dag  # noqa: E402

GET = os.path.join(ROOT, "tools", "zxv-get")
ZXV_GET = importlib.util.module_from_spec(
    importlib.util.spec_from_loader("zxv_get", importlib.machinery.SourceFileLoader("zxv_get", GET)))
ZXV_GET.__spec__.loader.exec_module(ZXV_GET)
MK = os.path.join(HERE, "mk_bucket.py")
CID = "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354"

checks = failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: " + what)


def data(n, seed):
    out, x = bytearray(), seed
    while len(out) < n:
        x = (x * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        out += x.to_bytes(8, "little")
    return bytes(out[:n])


class Store:
    blocks = {}
    tamper = set()
    seen = []
    down = 0  # answer 503 to this many more requests


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path, _, q = self.path.partition("?")
        cid = path[len("/ipfs/"):]
        Store.seen.append(self.path)
        if Store.down > 0:
            Store.down -= 1
            self.send_response(503)
            self.end_headers()
            return
        if not path.startswith("/ipfs/") or q != "format=raw" or cid not in Store.blocks:
            self.send_response(404)
            self.end_headers()
            return
        b = Store.blocks[cid]
        if cid in Store.tamper or getattr(self, "lies", False):
            b = bytes([b[0] ^ 1]) + b[1:]
        self.send_response(200)
        self.send_header("Content-Type", "application/vnd.ipld.raw")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def log_message(self, *a):
        pass


class Liar(Handler):
    """A gateway that changes the first byte of every block it serves."""
    lies = True


def run(*args):
    r = subprocess.run([sys.executable] + list(args), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, cwd=ROOT)
    return r.returncode, r.stdout


def layers(t, out):
    """L0, L1 and L2 over a stand-in boot file and model lists, as
    `make build-all-layers` builds them."""
    lk = os.path.join(HERE, "mk_layers.py")
    efi = os.path.join(t, "BOOTX64.EFI")
    open(efi, "wb").write(b"efi bytes")
    sha = hashlib.sha256(b"m").hexdigest()
    models = os.path.join(t, "models.lst")
    open(models, "w").write("core.gguf %s 1 u A.GGU\nswarm.gguf %s 1 u B.GGU\n" % (sha, sha))
    core, swarm = os.path.join(t, "core.lst"), os.path.join(t, "swarm.lst")
    open(core, "w").write("core.gguf\n")
    open(swarm, "w").write("reason swarm.gguf\n")
    steps = [["l0"], ["l1", "--boot", "x86_64=" + efi, "--core", core], ["l2", "--swarm", swarm]]
    for n, st in enumerate(steps):
        par = ["--parent", open(os.path.join(out, "L%d.cid" % (n - 1))).read().strip()] if n else []
        rc, o = run(lk, *st, "--out", out, "--models", models, *par)
        check(rc == 0, "mk_layers %s: %s" % (st[0], o))
    return out


def named(path, ext=None):
    """<the file's CID>.<ext>, ext defaulting to path's own."""
    ext = ext or os.path.splitext(path)[1].lstrip(".").lower()
    return "%s.%s" % (cid_str(file_dag(path)[0]), ext)


def installer(bucket):
    """(name, text, stages) of the bucket's installer file, read by zxv-get's
    own parser."""
    n = [x for x in os.listdir(bucket) if x.endswith(".install")]
    check(len(n) == 1, "one installer file in %s: %r" % (bucket, n))
    text = open(os.path.join(bucket, n[0]), encoding="ascii").read()
    return n[0], text, ZXV_GET.parse_installer(text)


def write_tree(d, entries, blocks, drop=None):
    """--tree DIR: links.json and a CAR v1 of blocks (minus drop)."""
    import json
    os.makedirs(d, exist_ok=True)
    json.dump({n: [cid_str(c), ts] for n, (c, ts) in entries.items()},
              open(os.path.join(d, "links.json"), "w"))

    def uv(x):
        o = bytearray()
        while True:
            b = x & 0x7F
            x >>= 7
            o.append(b | (0x80 if x else 0))
            if not x:
                return bytes(o)
    any_root = next(iter(entries.values()))[0]
    head = b"\xa2\x65roots\x81\xd8\x2a\x58\x25\x00" + any_root + b"\x67version\x01"
    with open(os.path.join(d, "blocks.car"), "wb") as f:
        f.write(uv(len(head)) + head)
        for c, b in blocks.items():
            if c != drop:
                f.write(uv(len(c) + len(b)) + c + b)


def raw_cid(b):
    from zxv_publish_update import cid_bin
    return cid_bin(0x55, hashlib.sha256(b).digest())


def load_blocks(d):
    Store.blocks = {n: open(os.path.join(d, n), "rb").read() for n in os.listdir(d)}


def main():
    shipped = read_upstream(os.path.join(ROOT, "build_system", "zxv_upstream.cid"))
    check(shipped["cid"] and shipped["cid"].startswith("bafy") and shipped["seal"] == "PROMPT_USER",
          "the shipped upstream file reads; seal defaults to PROMPT_USER")
    with tempfile.TemporaryDirectory() as t:
        u = os.path.join(t, "u")
        open(u, "w").write("cid %s\nseal LOCAL_MESH_ONLY\n" % CID)
        check(read_upstream(u)["seal"] == "LOCAL_MESH_ONLY", "a seal line is read")
        for bad in ("seal NOPE", "seal PROMPT_USER\nseal POWERDOWN_NIC"):
            open(u, "w").write("cid %s\n%s\n" % (CID, bad))
            try:
                read_upstream(u)
                check(False, "refused: " + bad)
            except SystemExit:
                check(True, "refused: " + bad)
    rc, out = run(MK, "--self-test")
    check(rc == 0 and "ok" in out, "mk_bucket self-test against Kubo vectors: " + out)

    t = tempfile.mkdtemp(prefix="zxv-bucket-test-")
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    gw = "http://127.0.0.1:%d" % srv.server_address[1]
    liar = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Liar)
    threading.Thread(target=liar.serve_forever, daemon=True).start()
    bad = "http://127.0.0.1:%d" % liar.server_address[1]
    try:
        discs = os.path.join(t, "discs")
        os.makedirs(discs)
        files = {
            "zxv-x86_64-disc1-core.img": data(3 << 20, 1),
            "zxv-arm64-disc1-core.img": data(174 * 262144 + 70000, 2),  # two-level DAG
            "zxv-swarm-disc2-reason.img": data(1 << 20, 3),
            "zxv-swarm-disc3-vision.img": data(300000, 4),
        }
        for n, b in files.items():
            open(os.path.join(discs, n), "wb").write(b)
        cart = os.path.join(t, "MEGAROM.TVL")
        open(cart, "wb").write(data(5000, 5))
        up = os.path.join(t, "upstream.cid")
        open(up, "w").write("# test\ncid %s\ngateway http://10.0.2.2:8080\n" % CID)
        bucket, blocks = os.path.join(t, "bucket"), os.path.join(t, "blocks")
        mk = [MK, "--discs", discs, "--out", bucket, "--upstream", up, "--cartridge", cart]

        rc, out = run(*mk, "--blocks", blocks)
        check(rc == 0 and "nothing was added to IPFS or published" in out, "mk_bucket: " + out)
        root = open(bucket + ".cid").read().strip()
        name = {n: named(os.path.join(discs, n)) for n in files}
        name["MEGAROM.TVL"] = named(cart, "tvl")
        iname, text, man = installer(bucket)
        layout = [(s["dir"], s["file"], s["kind"], s["arch"], s["disc"], s["group"]) for s in man]
        check(layout == [
            ("00_base", name["zxv-x86_64-disc1-core.img"], "core", "x86_64", 1, None),
            ("00_base", name["zxv-arm64-disc1-core.img"], "core", "arm64", 1, None),
            ("01_swarm", name["zxv-swarm-disc2-reason.img"], "swarm", "all", 2, "REASON"),
            ("01_swarm", name["zxv-swarm-disc3-vision.img"], "swarm", "all", 3, "VISION"),
            ("02_cartridges", name["MEGAROM.TVL"], "cartridge", "all", None, None)],
            "layout %r" % layout)
        check(sorted(os.listdir(bucket)) == sorted(["00_base", "01_swarm", "02_cartridges", iname]),
              "the root holds the directories and the installer file only")
        check(iname == named(os.path.join(bucket, iname), "install"),
              "the installer file is named by its own CID")
        check(text.splitlines()[:4] == ["# zxv-install 1", "max_disc_bytes 1800000000",
                                        "seal PROMPT_USER", "gateway http://10.0.2.2:8080"],
              "installer head: %r" % text.splitlines()[:4])
        s0 = man[1]
        arm = files["zxv-arm64-disc1-core.img"]
        check(s0["bytes"] == len(arm) and s0["sha256"] == hashlib.sha256(arm).hexdigest() and
              s0["file"] == s0["cid"] + ".img", "size, sha256 and the name is the CID")
        sw = man[2]
        check(text.splitlines()[6] == "swarm all 2 REASON %d %s %s 01_swarm/%s.img" % (
            sw["bytes"], sw["sha256"], sw["cid"], sw["cid"]), "a swarm line: %r" % text)
        check(root == cid_str(tree_cid(bucket)[0]) and root in os.listdir(blocks), "root CID and blocks")
        rc, out = run(*mk)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "same discs, same root CID")
        # the architecture section rides along in 00_base, and changes the root
        arch = os.path.join(t, "ARCH.md")
        open(arch, "w").write("# doc\nbefore\n<!-- zxv:distribution:start -->\n### 5.4 Distribution\n"
                              "split discs\n<!-- zxv:distribution:end -->\nafter\n")
        rc, out = run(*mk, "--architecture", arch)
        docs = [s for s in installer(bucket)[2] if s["kind"] == "doc"] if rc == 0 else []
        got = open(os.path.join(bucket, "00_base", docs[0]["file"])).read() if docs else ""
        check(rc == 0 and len(docs) == 1 and docs[0]["file"].endswith(".md") and
              got.endswith("-->\n### 5.4 Distribution\nsplit discs\n") and
              "before" not in got and "after" not in got and "SPDX-License-Identifier" in got and
              open(bucket + ".cid").read().strip() != root, "the distribution section: " + out)
        open(arch, "w").write("# doc\n<!-- zxv:distribution:end -->\n<!-- zxv:distribution:start -->\n")
        rc, out = run(*mk, "--architecture", arch)
        check(rc == 1 and "has no distribution section" in out, "markers out of order: " + out)
        rc, out = run(*mk)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "and without it, the same root")
        lay = layers(t, os.path.join(t, "layers"))
        rc, out = run(*mk, "--layers", lay, "--blocks", blocks)
        lman = installer(bucket)[2] if rc == 0 else []
        mid = [(s["dir"], s["file"], s["kind"]) for s in lman[2:5]]
        check(rc == 0 and mid == [("03_layers", named(os.path.join(lay, "L%d.json" % n)), "layer")
                                  for n in range(3)], "03_layers after the cores: %r %s" % (mid, out))
        check(rc == 0 and lman[2]["cid"] == open(os.path.join(lay, "L0.cid")).read().strip(),
              "a layer's file CID is its layer CID")
        lroot = open(bucket + ".cid").read().strip()
        check(lroot != root, "the layers change the root")
        lget = os.path.join(t, "lget")
        Store.blocks = {n: open(os.path.join(blocks, n), "rb").read() for n in os.listdir(blocks)}
        l1 = named(os.path.join(lay, "L1.json"))
        rc, out = run(GET, "--no-ipfs", "--gateway", gw, lroot, "--quiet", "--upstream", up,
                      "--arch", "x86_64", "--out", lget)
        check(rc == 0 and "ok     03_layers/%s" % l1 in out and
              open(os.path.join(lget, l1), "rb").read() ==
              open(os.path.join(lay, "L1.json"), "rb").read(),
              "zxv-get takes the origin CID and the layers: " + out)
        os.rename(os.path.join(lay, "L1.json"), os.path.join(lay, "L1.x"))
        rc, out = run(*mk, "--layers", lay)
        check(rc == 1 and "L2.json without the layers below it" in out, "a layer gap: " + out)
        with open(os.path.join(lay, "L1.json"), "wb") as f:
            f.write(open(os.path.join(lay, "L1.x"), "rb").read().replace(b'"bytes": 9',
                                                                         b'"bytes": 8'))
        rc, out = run(*mk, "--layers", lay)
        check(rc == 1 and "do not form a chain" in out and "names parent" in out,
              "a changed L1 breaks the chain: " + out)
        rc, out = run(*mk, "--layers", os.path.join(t, "discs"))
        check(rc == 1 and "holds no L0.json" in out, "no layers: " + out)
        shutil.rmtree(blocks)
        rc, out = run(*mk, "--blocks", blocks)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "back to the plain bucket")
        load_blocks(blocks)

        # the standalone verifier: every name in the bucket is its CID
        vfy = os.path.join(ROOT, "tools", "zxv-verify.sh")
        r = subprocess.run(["sh", vfy, os.path.join(bucket, iname)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        check(r.returncode == 0 and r.stdout.count("ok    ") == 6 and
              r.stdout.endswith("all names are their CIDs\n"), "zxv-verify.sh: " + r.stdout)
        vis = os.path.join(bucket, "01_swarm", name["zxv-swarm-disc3-vision.img"])
        keep = open(vis, "rb").read()
        os.remove(vis)  # a hard link to the disc: write a copy, not through it
        open(vis, "wb").write(keep[:1000] + bytes([keep[1000] ^ 1]) + keep[1001:])
        r = subprocess.run(["sh", vfy, vis, os.path.join(bucket, iname)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        check(r.returncode == 1 and r.stdout.startswith("FALSE %s: its CID is b" % vis) and
              "FALSE %s: sha256 differs" % vis in r.stdout, "a flipped bit makes the name false: "
              + r.stdout)
        os.remove(vis)
        open(vis, "wb").write(keep[:-1])  # cut short: said as the size, before the hash
        r = subprocess.run(["sh", vfy, os.path.join(bucket, iname)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        check(r.returncode == 1 and "FALSE %s: not %d bytes" % (vis, len(keep)) in r.stdout,
              "a short file is said to be short: " + r.stdout)
        os.remove(vis)
        open(vis, "wb").write(keep)
        # the C helper agrees with mk_bucket's DAG, also deep: 16-byte chunks, 3 links
        helper = os.path.join(t, "zxv-cid-small")
        r = subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DCHUNK=16u",
                            "-DLINKS=3u", "-o", helper, os.path.join(ROOT, "tools", "zxv-cid.c")])
        check(r.returncode == 0, "zxv-cid.c builds")
        import zxv_publish_update as zpu
        sizes = [0, 1, 16, 17, 48, 49, 144, 145, 432, 433, 1296, 1297, 5000]
        paths = []
        for n in sizes:
            paths.append(os.path.join(t, "deep%d" % n))
            open(paths[-1], "wb").write(data(n, n + 7))
        out = subprocess.run([helper] + paths, stdout=subprocess.PIPE, text=True).stdout.split("\n")
        saved = zpu.CHUNK, zpu.LINKS
        zpu.CHUNK, zpu.LINKS = 16, 3
        want = [cid_str(file_dag(p)[0]) for p in paths]
        zpu.CHUNK, zpu.LINKS = saved
        check([o.split()[0] for o in out if o] == want, "zxv-cid.c = file_dag at depth: %r" % out)

        get = [GET, "--no-ipfs", "--gateway", gw, "--cid", root, "--quiet", "--upstream", up]
        dl = os.path.join(t, "get")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        order = ["zxv-arm64-disc1-core.img", "zxv-swarm-disc2-reason.img",
                 "zxv-swarm-disc3-vision.img", "MEGAROM.TVL"]
        want = [name[n] for n in order]
        check(rc == 0 and sorted(os.listdir(dl)) == sorted(want), "zxv-get arm64: " + out)
        for n in order[:3]:
            check(open(os.path.join(dl, name[n]), "rb").read() == files[n], "downloaded %s intact" % n)
        check("ok     00_base/%s %d bytes, sha256 matches" % (want[0], len(arm)) in out,
              "ok line: " + out)
        check("install: %s (named by its own CID)" % iname in out, "the installer named: " + out)
        check(all(p.endswith("?format=raw") for p in Store.seen), "only raw blocks requested")
        check(out.index(want[0]) < out.index(want[1]) < out.index(want[2]) < out.index(want[3]),
              "core first, swarm in disc order, cartridge last")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        check(rc == 0 and out.count("  have   ") == 4, "a second run keeps verified discs: " + out)
        with open(os.path.join(dl, want[1]), "r+b") as f:
            f.seek(1000)
            f.write(b"\xff")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        check(rc == 0 and out.count("  have   ") == 3 and "ok     01_swarm/" + want[1] in out and
              open(os.path.join(dl, want[1]), "rb").read() == files[order[1]],
              "a changed local disc of the right size is fetched again: " + out)

        rc, out = run(*get, "--arch", "x86_64", "--core-only", "--out", os.path.join(t, "x86"))
        check(rc == 0 and os.listdir(os.path.join(t, "x86")) == [name["zxv-x86_64-disc1-core.img"]],
              "--core-only: " + out)
        rc, out = run(*get, "--arch", "riscv64", "--out", os.path.join(t, "rv"))
        check(rc == 1 and "no core disc for riscv64 (it has: arm64 x86_64)" in out, "riscv64: " + out)
        rc, out = run(*get, "--list")
        check(rc == 0 and want[2] in out and "x86_64" in out, "--list: " + out)
        rc, out = run(GET, "--no-ipfs", "--gateway", gw, "--upstream", up, "--list", "--patience", "0")
        check(rc == 1 and CID in out, "the baked CID is the default bucket: " + out)

        # the gateway changes one byte of one block of the vision disc
        refused = os.path.join(t, "refused")
        vision = man[3]["cid"]
        Store.tamper = {vision}
        rc, out = run(*get, "--arch", "arm64", "--out", refused)
        check(rc == 1 and ("block %s: %s sent a block that does not match its CID" % (vision, gw)) in out,
              "tampered block: " + out)
        check(want[2] not in os.listdir(refused) and "kept   01_swarm/%s.part" % want[2] in out,
              "a refused disc leaves no finished file, only its verified part: " + out)
        Store.tamper = set()
        rc, out = run(*get, "--arch", "x86_64", "--out", refused)
        check(rc == 0 and "resume 01_swarm/%s at" % want[2] in out and
              open(os.path.join(refused, want[2]), "rb").read() == files[order[2]],
              "a later run fetches what was refused, resuming from its part: " + out)

        # elastic: a gateway that is down for a while is waited for, 1 s then 2 s
        x86 = name["zxv-x86_64-disc1-core.img"]
        Store.down = 2
        rc, out = run(*get, "--arch", "x86_64", "--core-only", "--out", os.path.join(t, "late"))
        check(rc == 0 and "waiting for peers" in out and "retry 1 in 1 s" in out and
              "retry 2 in 2 s" in out and "retry 3" not in out and
              "ok     00_base/" + x86 in out, "a late gateway: " + out)
        Store.down = 1000
        rc, out = run(*get, "--arch", "x86_64", "--core-only", "--out", os.path.join(t, "late2"),
                      "--patience", "3")
        check(rc == 1 and "gave up after waiting 3 s, --patience" in out, "--patience: " + out)
        Store.down = 0
        # resume: most of a two-level disc is already in its .part
        res = os.path.join(t, "resume")
        os.makedirs(res)
        part = os.path.join(res, want[0] + ".part")
        open(part, "wb").write(arm[:170 * 262144 + 1000])
        Store.seen = []
        rc, out = run(*get, "--arch", "arm64", "--core-only", "--out", res)
        check(rc == 0 and "resume 00_base/%s at %d of %d bytes" % (want[0], 170 * 262144 + 1000,
                                                                     len(arm)) in out and
              open(os.path.join(res, want[0]), "rb").read() == arm and
              not os.path.exists(part), "resumed from a .part: " + out)
        check(len(Store.seen) <= 14, "a resume asks only for the rest (%d requests)"
              % len(Store.seen))
        # a .part that is not the image's start: fetched again from the start
        bent = bytearray(arm[:300000])
        bent[1000] ^= 1
        os.remove(os.path.join(res, want[0]))
        open(part, "wb").write(bent)
        rc, out = run(*get, "--arch", "arm64", "--core-only", "--out", res)
        check(rc == 0 and "again  00_base/%s: the kept part was not the image" % want[0] in out and
              open(os.path.join(res, want[0]), "rb").read() == arm, "a bad .part is refetched: " + out)
        # a .part longer than the disc is dropped
        os.remove(os.path.join(res, want[0]))
        open(part, "wb").write(arm + b"x")
        rc, out = run(*get, "--arch", "arm64", "--core-only", "--out", res)
        check(rc == 0 and "  resume " not in out and open(os.path.join(res, want[0]), "rb").read() == arm,
              "an overlong .part is dropped: " + out)

        # fallback: a dead gateway and a lying one, then a good one
        fb = [GET, "--no-ipfs", "--gateway", "http://127.0.0.1:1", "--gateway", bad, "--gateway", gw,
              "--cid", root, "--quiet", "--upstream", up, "--core-only", "--arch", "arm64"]
        rc, out = run(*fb, "--out", os.path.join(t, "fb"))
        check(rc == 0 and "ok     00_base/" + want[0] in out, "gateway fallback: " + out)
        rc, out = run(*fb[:6], "--cid", root, "--quiet", "--upstream", up, "--list",
                      "--patience", "0")
        check(rc == 1 and "127.0.0.1:1" in out and "does not match its CID" in out,
              "every gateway failing names each one: " + out)
        rc, out = run(GET, "--no-ipfs", "--cid", root, "--upstream", up, "--list", "--patience", "0")
        check(out.count("http://10.0.2.2:8080, then https://trustless-gateway.link") == 1,
              "the upstream file's gateways come first: " + out)

        # --tree: a matrix-like tree linked into the root by CID, never re-chunked
        big = data(600000, 9)  # one raw block over 256 KiB, as a matrix booklet can be
        small = b"index page\n"
        tb = {raw_cid(big): big, raw_cid(small): small}
        layer = dir_node([("000.0000.r0:000", raw_cid(big), len(big))],
                         lambda c, b: tb.__setitem__(c, b))
        matrix = dir_node([("000", layer[0], layer[1])], lambda c, b: tb.__setitem__(c, b))
        ents = {"07_matrix": matrix, "index.html": (raw_cid(small), len(small))}
        tdir = os.path.join(t, "tree")
        write_tree(tdir, ents, tb)
        tblocks = os.path.join(t, "tblocks")
        rc, out = run(*mk, "--tree", tdir, "--blocks", tblocks)
        tiname, ttext, tman = installer(bucket) if rc == 0 else (None, "", [])
        trees = [x for x in tman if x["kind"] == "tree"]
        check(rc == 0 and [(x["file"], x["cid"]) for x in trees] ==
              [("07_matrix", cid_str(matrix[0])), ("index.html", cid_str(raw_cid(small)))] and
              "tree all - - %d - %s 07_matrix" % (matrix[1], cid_str(matrix[0])) in ttext,
              "tree lines: " + out + ttext)
        troot = open(bucket + ".cid").read().strip()
        kids = [(n, *tree_cid(os.path.join(bucket, n))) for n in os.listdir(bucket)]
        check(troot == cid_str(dir_node(kids + [(n, c, ts) for n, (c, ts) in ents.items()])[0]),
              "the origin links the trees as they are")
        check(cid_str(raw_cid(big)) in os.listdir(tblocks) and troot in os.listdir(tblocks),
              "--blocks holds the tree's raw 600000-byte block whole, and the root")
        check(sorted(os.listdir(bucket)) == sorted(["00_base", "01_swarm", "02_cartridges", tiname]),
              "the trees are linked, not copied into the bucket")
        r = subprocess.run(["sh", os.path.join(ROOT, "tools", "zxv-verify.sh"),
                            os.path.join(bucket, tiname)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        check(r.returncode == 0 and "ok    tree 07_matrix (%s)" % cid_str(matrix[0]) in r.stdout and
              "ok    tree index.html" in r.stdout, "zxv-verify.sh checks the tree lines: " + r.stdout)
        load_blocks(tblocks)
        tget = os.path.join(t, "tget")
        rc, out = run(GET, "--no-ipfs", "--gateway", gw, troot, "--quiet", "--upstream", up,
                      "--arch", "x86_64", "--out", tget)
        check(rc == 0 and "ok     07_matrix: 1 files, 600000 bytes" in out and
              open(os.path.join(tget, "07_matrix", "000", "000.0000.r0:000"), "rb").read() == big and
              open(os.path.join(tget, "index.html"), "rb").read() == small,
              "zxv-get fetches the trees, every block checked: " + out)
        # lies about trees
        write_tree(tdir, ents, tb, drop=raw_cid(big))
        rc, out = run(*mk, "--tree", tdir)
        check(rc == 1 and "is not in blocks.car" in out, "a tree whose block is missing: " + out)
        write_tree(tdir, {"00_base": matrix}, tb)
        rc, out = run(*mk, "--tree", tdir)
        check(rc == 1 and "already a bucket folder" in out, "a tree over a bucket folder: " + out)
        write_tree(tdir, {"../x": matrix}, tb)
        rc, out = run(*mk, "--tree", tdir)
        check(rc == 1 and "is not a plain name" in out, "a tree name with a path: " + out)
        tlines = ttext.splitlines(True)
        ti = [i for i, ln in enumerate(tlines) if ln.startswith("tree ") and ln.endswith(" 07_matrix\n")][0]
        f = tlines[ti].split()
        for k, v, why in ((5, "0" * 64, "a tree line with a sha256"),
                          (6, cid_str(raw_cid(small)), "a tree line naming another CID")):
            g = list(f)
            g[k] = v
            bent = "".join(tlines[:ti] + [" ".join(g) + "\n"] + tlines[ti + 1:])
            try:
                ZXV_GET.parse_installer(bent)
                parsed = True
            except ZXV_GET.Fail:
                parsed = False
            if k == 5:
                check(not parsed, why + " is refused")
            else:  # well formed, but the root links another CID under that name
                for n in os.listdir(bucket):
                    if n.endswith(".install"):
                        os.remove(os.path.join(bucket, n))
                pth = os.path.join(bucket, "x.tmp")
                open(pth, "w").write(bent)
                os.rename(pth, os.path.join(bucket, named(pth, "install")))
                sink = {}
                kids = []
                for n in os.listdir(bucket):
                    c, ts = tree_cid(os.path.join(bucket, n), lambda cb, b: sink.__setitem__(cid_str(cb), b))
                    kids.append((n, c, ts))
                r2 = dir_node(kids + [(n, c, ts) for n, (c, ts) in ents.items()],
                              lambda cb, b: sink.__setitem__(cid_str(cb), b))
                Store.blocks.update(sink)
                rc, out = run(GET, "--no-ipfs", "--gateway", gw, cid_str(r2[0]), "--quiet",
                              "--upstream", up, "--arch", "x86_64", "--out", os.path.join(t, "tlie"))
                check(rc == 1 and "07_matrix: the bucket's root gives CID %s" % cid_str(matrix[0]) in out,
                      why + ": " + out)
        rc, out = run(*mk, "--blocks", blocks)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "without --tree, the old root")
        load_blocks(blocks)

        # installer files that lie, in buckets whose blocks are all genuine
        def lie(new_text, rename=None, extra=None):
            """Replace the installer file (named by its new CID unless rename
            gives a name), optionally add one more file; return zxv-get's run."""
            for n in os.listdir(bucket):
                if n.endswith(".install"):
                    os.remove(os.path.join(bucket, n))
            p = os.path.join(bucket, "x.tmp")
            open(p, "w").write(new_text)
            os.rename(p, os.path.join(bucket, rename or named(p, "install")))
            if extra:
                open(os.path.join(bucket, extra), "w").write(text)
            sink = {}
            r = cid_str(tree_cid(bucket, lambda cb, b: sink.__setitem__(cid_str(cb), b))[0])
            Store.blocks.update(sink)
            rr = run(GET, "--no-ipfs", "--gateway", gw, "--cid", r, "--quiet", "--upstream", up,
                     "--arch", "arm64", "--out", os.path.join(t, "lie%d" % len(sink)))
            if extra:
                os.remove(os.path.join(bucket, extra))
            return rr

        lines = text.splitlines(True)
        rline = lines[6].split()

        def swap(i, v):
            f = list(rline)
            f[i] = v
            return "".join(lines[:6] + [" ".join(f) + "\n"] + lines[7:])

        rc, out = lie(swap(5, "0" * 64))
        check(rc == 1 and "01_swarm/%s: sha256 differs" % want[1] in out, "wrong sha256: " + out)
        rc, out = lie(swap(4, str(int(rline[4]) - 1)))
        check(rc == 1 and "longer than the installer file's" in out, "short size: " + out)
        rc, out = lie(swap(4, str(int(rline[4]) + 1)))
        check(rc == 1 and "bytes, the installer file says" in out, "long size: " + out)
        rc, out = lie(swap(6, man[3]["cid"]))
        check(rc == 1 and "is not a stage named by its CID" in out, "a CID that is not the name: " + out)
        # a file whose name is not its CID: the directory link says so
        rpath = os.path.join(bucket, "01_swarm", want[1])
        os.remove(rpath)
        shutil.copyfile(os.path.join(discs, "zxv-swarm-disc3-vision.img"), rpath)
        rc, out = lie(text)
        check(rc == 1 and "01_swarm/%s: the bucket's directory gives CID %s" % (want[1], man[3]["cid"])
              in out, "a file under another file's name: " + out)
        os.remove(rpath)
        shutil.copyfile(os.path.join(discs, "zxv-swarm-disc2-reason.img"), rpath)
        rc, out = lie(swap(7, "../../etc/" + rline[6] + ".img"))
        check(rc == 1 and "is not a stage named by its CID" in out, "path outside the bucket: " + out)
        rc, out = lie(swap(1, "x86_64"))
        check(rc == 1 and "is not a stage named by its CID" in out, "a swarm disc tied to an arch: " + out)
        rc, out = lie(swap(4, "1800000001"))
        check(rc == 1 and "is not a stage named by its CID" in out, "a disc over 1.8 GB: " + out)
        rc, out = lie(text.replace("# zxv-install 1", "# zxv-install 2"))
        check(rc == 1 and "does not start with '# zxv-install 1'" in out, "unknown format: " + out)
        rc, out = lie(text, rename="b" + "a" * 58 + ".install")
        check(rc == 1 and "is not named by its CID" in out, "an installer not named by its CID: " + out)
        rc, out = lie(text, extra="baaaa" + "b" * 54 + ".install")
        check(rc == 1 and "holds 2 <cid>.install files, not one" in out, "two installers: " + out)

        # mk_bucket refuses what the bucket must not hold
        other = os.path.join(t, "other")
        os.makedirs(other)
        rc, out = run(MK, "--discs", discs, "--out", other, "--upstream", up)
        check(rc == 1 and "is not a bucket this tool made" in out, "foreign out dir: " + out)
        rc, out = run(*mk, "--max-bytes", str(3 << 20))
        check(rc == 1 and "over the 3145728-byte disc limit" in out, "disc cap: " + out)
        os.rename(os.path.join(discs, "zxv-swarm-disc3-vision.img"),
                  os.path.join(discs, "zxv-swarm-disc4-vision.img"))
        rc, out = run(*mk)
        check(rc == 1 and "without gaps; found disc 4" in out, "swarm numbering gap: " + out)
        open(up, "w").write("cid %s\ncid %s\n" % (CID, CID))
        rc, out = run(*mk)
        check(rc == 1 and "want one 'cid" in out, "upstream listed twice: " + out)
    finally:
        srv.shutdown()
        liar.shutdown()
        shutil.rmtree(t)
    print("test_bucket: %d checks, %d failures" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
