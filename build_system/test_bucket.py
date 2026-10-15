#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_bucket.py - mk_bucket.py and tools/zxv-get, end to end, offline.

    python3 build_system/test_bucket.py

Makes small stand-in discs (two core discs, one deep enough for a two-level
DAG, two swarm discs and a cartridge), lays them out with mk_bucket.py,
serves the bucket's blocks from a local trustless gateway on 127.0.0.1 and
downloads with zxv-get. Then checks that every way the bucket, the gateway or
the manifest can lie is refused, and that a refused disc leaves no file.
Needs no network and no IPFS node.
"""

import hashlib
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from mk_bucket import read_upstream, tree_cid  # noqa: E402
from zxv_publish_update import cid_str  # noqa: E402

GET = os.path.join(ROOT, "tools", "zxv-get")
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


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path, _, q = self.path.partition("?")
        cid = path[len("/ipfs/"):]
        Store.seen.append(self.path)
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
        man = json.load(open(os.path.join(bucket, "manifest.json")))
        layout = [(s["dir"], s["file"], s["kind"], s["arch"], s.get("disc"), s.get("group"))
                  for s in man["stages"]]
        check(layout == [
            ("00_base", "zxv-x86_64-disc1-core.img", "core", "x86_64", 1, None),
            ("00_base", "zxv-arm64-disc1-core.img", "core", "arm64", 1, None),
            ("01_swarm_reason", "zxv-swarm-disc2-reason.img", "swarm", "all", 2, "REASON"),
            ("02_swarm_vision", "zxv-swarm-disc3-vision.img", "swarm", "all", 3, "VISION"),
            ("03_cartridges", "MEGAROM.TVL", "cartridge", "all", None, None)], "layout %r" % layout)
        check(man["upstream"] == {"cid": CID, "ipns": None, "gateways": ["http://10.0.2.2:8080"],
                                  "seal": "PROMPT_USER"} and man["max_disc_bytes"] == 1800000000 and
              man["post_install_action"] == "SEAL_AIRGAP" and
              man["allowed_modes"] == ["POWERDOWN_NIC", "LOCAL_MESH_ONLY", "PROMPT_USER"],
              "manifest upstream and cap")
        s0 = man["stages"][1]
        check(s0["bytes"] == len(files[s0["file"]]) and
              s0["sha256"] == hashlib.sha256(files[s0["file"]]).hexdigest(), "manifest size and sha256")
        txt = open(os.path.join(bucket, "manifest.txt")).read().splitlines()
        sw = man["stages"][2]
        check(len(txt) == 5 and txt[2] == "swarm all 2 REASON %d %s %s 01_swarm_reason/"
              "zxv-swarm-disc2-reason.img" % (sw["bytes"], sw["sha256"], sw["cid"]) and
              txt[0].startswith("core x86_64 1 - ") and txt[4].startswith("cartridge all - - "),
              "manifest.txt for the kernel: %r" % txt)
        check(root == cid_str(tree_cid(bucket)[0]) and root in os.listdir(blocks), "root CID and blocks")
        rc, out = run(*mk)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "same discs, same root CID")
        # the architecture section rides along in 00_base, and changes the root
        arch = os.path.join(t, "ARCH.md")
        open(arch, "w").write("# doc\nbefore\n<!-- zxv:distribution:start -->\n### 5.4 Distribution\n"
                              "split discs\n<!-- zxv:distribution:end -->\nafter\n")
        rc, out = run(*mk, "--architecture", arch)
        got = open(os.path.join(bucket, "00_base", "ARCHITECTURE.md")).read() if rc == 0 else ""
        check(rc == 0 and got.endswith("-->\n### 5.4 Distribution\nsplit discs\n") and
              "before" not in got and "after" not in got and "SPDX-License-Identifier" in got and
              open(bucket + ".cid").read().strip() != root, "the distribution section: " + out)
        open(arch, "w").write("# doc\n<!-- zxv:distribution:end -->\n<!-- zxv:distribution:start -->\n")
        rc, out = run(*mk, "--architecture", arch)
        check(rc == 1 and "has no distribution section" in out, "markers out of order: " + out)
        rc, out = run(*mk)
        check(rc == 0 and open(bucket + ".cid").read().strip() == root, "and without it, the same root")
        load_blocks(blocks)

        get = [GET, "--no-ipfs", "--gateway", gw, "--cid", root, "--quiet", "--upstream", up]
        dl = os.path.join(t, "get")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        want = ["zxv-arm64-disc1-core.img", "zxv-swarm-disc2-reason.img",
                "zxv-swarm-disc3-vision.img", "MEGAROM.TVL"]
        check(rc == 0 and sorted(os.listdir(dl)) == sorted(want), "zxv-get arm64: " + out)
        for n in want[:3]:
            check(open(os.path.join(dl, n), "rb").read() == files[n], "downloaded %s intact" % n)
        check("ok     00_base/zxv-arm64-disc1-core.img %d bytes, sha256 matches"
              % len(files[want[0]]) in out, "ok line: " + out)
        check(all(p.endswith("?format=raw") for p in Store.seen), "only raw blocks requested")
        check(out.index("arm64-disc1") < out.index("disc2-reason") < out.index("disc3-vision")
              < out.index("MEGAROM"), "core first, swarm in disc order, cartridge last")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        check(rc == 0 and out.count("  have   ") == 4, "a second run keeps verified discs: " + out)
        with open(os.path.join(dl, want[1]), "r+b") as f:
            f.seek(1000)
            f.write(b"\xff")
        rc, out = run(*get, "--arch", "arm64", "--out", dl)
        check(rc == 0 and out.count("  have   ") == 3 and "ok     01_swarm_reason" in out and
              open(os.path.join(dl, want[1]), "rb").read() == files[want[1]],
              "a changed local disc of the right size is fetched again: " + out)

        rc, out = run(*get, "--arch", "x86_64", "--core-only", "--out", os.path.join(t, "x86"))
        check(rc == 0 and os.listdir(os.path.join(t, "x86")) == ["zxv-x86_64-disc1-core.img"],
              "--core-only: " + out)
        rc, out = run(*get, "--arch", "riscv64", "--out", os.path.join(t, "rv"))
        check(rc == 1 and "no core disc for riscv64 (it has: arm64 x86_64)" in out, "riscv64: " + out)
        rc, out = run(*get, "--list")
        check(rc == 0 and "02_swarm_vision" in out and "x86_64" in out, "--list: " + out)
        rc, out = run(GET, "--no-ipfs", "--gateway", gw, "--upstream", up, "--list")
        check(rc == 1 and CID in out, "the baked CID is the default bucket: " + out)

        # the gateway changes one byte of one block of the vision disc
        refused = os.path.join(t, "refused")
        vision = man["stages"][3]["cid"]
        Store.tamper = {vision}
        rc, out = run(*get, "--arch", "arm64", "--out", refused)
        check(rc == 1 and ("block %s: %s sent a block that does not match its CID" % (vision, gw)) in out,
              "tampered block: " + out)
        check(not any(n.startswith("zxv-swarm-disc3") for n in os.listdir(refused)),
              "a refused disc leaves no file")
        Store.tamper = set()
        rc, out = run(*get, "--arch", "x86_64", "--out", refused)
        check(rc == 0, "a later run fetches what was refused: " + out)

        # fallback: a dead gateway and a lying one, then a good one
        fb = [GET, "--no-ipfs", "--gateway", "http://127.0.0.1:1", "--gateway", bad, "--gateway", gw,
              "--cid", root, "--quiet", "--upstream", up, "--core-only", "--arch", "arm64"]
        rc, out = run(*fb, "--out", os.path.join(t, "fb"))
        check(rc == 0 and "ok     00_base/zxv-arm64-disc1-core.img" in out, "gateway fallback: " + out)
        rc, out = run(*fb[:6], "--cid", root, "--quiet", "--upstream", up, "--list")
        check(rc == 1 and "127.0.0.1:1" in out and "does not match its CID" in out,
              "every gateway failing names each one: " + out)
        rc, out = run(GET, "--no-ipfs", "--cid", root, "--upstream", up, "--list")
        check(out.count("http://10.0.2.2:8080, then https://trustless-gateway.link") == 1,
              "the upstream file's gateways come first: " + out)

        # manifests that lie, in buckets whose blocks are all genuine
        def lie(field, value, i=2):
            m = json.loads(json.dumps(man))
            m["stages"][i][field] = value
            open(os.path.join(bucket, "manifest.json"), "w").write(json.dumps(m))
            sink = {}
            r = cid_str(tree_cid(bucket, lambda cb, b: sink.__setitem__(cid_str(cb), b))[0])
            Store.blocks.update(sink)
            return run(GET, "--no-ipfs", "--gateway", gw, "--cid", r, "--quiet", "--upstream", up,
                       "--arch", "arm64", "--out", os.path.join(t, "lie-" + field))

        reason = man["stages"][2]
        rc, out = lie("sha256", "0" * 64)
        check(rc == 1 and "01_swarm_reason/zxv-swarm-disc2-reason.img: sha256 differs" in out,
              "wrong sha256: " + out)
        rc, out = lie("bytes", reason["bytes"] - 1)
        check(rc == 1 and "longer than manifest.json's" in out, "short size: " + out)
        rc, out = lie("bytes", reason["bytes"] + 1)
        check(rc == 1 and "bytes, manifest.json says" in out, "long size: " + out)
        rc, out = lie("cid", man["stages"][3]["cid"])
        check(rc == 1 and "the bucket's directory gives CID" in out, "wrong cid: " + out)
        rc, out = lie("file", "../../etc/passwd")
        check(rc == 1 and "bad stage" in out, "path outside the bucket: " + out)
        rc, out = lie("arch", "x86_64")
        check(rc == 1 and "bad stage" in out, "a swarm disc tied to an arch: " + out)
        rc, out = lie("bytes", 1800000001)
        check(rc == 1 and "bad stage" in out, "a disc over 1.8 GB: " + out)
        m = dict(man, format="zxv-bucket 2")
        open(os.path.join(bucket, "manifest.json"), "w").write(json.dumps(m))
        sink = {}
        r = cid_str(tree_cid(bucket, lambda cb, b: sink.__setitem__(cid_str(cb), b))[0])
        Store.blocks.update(sink)
        rc, out = run(*get[:5], r, "--upstream", up, "--list")
        check(rc == 1 and "is not a 'zxv-bucket 1' manifest" in out, "unknown format: " + out)

        # mk_bucket refuses what the bucket must not hold
        open(os.path.join(bucket, "manifest.json"), "w").write(json.dumps(man))
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
