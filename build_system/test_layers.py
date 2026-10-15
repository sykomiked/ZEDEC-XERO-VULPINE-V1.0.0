#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_layers.py - build_system/mk_layers.py, offline.

    python3 build_system/test_layers.py

Builds L0 to L4 over small stand-in files, checks the chain, then that every
way a manifest or the chain can be wrong is refused: a changed byte below, a
layer skipped, another layout of the same data, a parent where none belongs.
The kernel reads the same manifests (kernel/src/layers/test_layers.c).
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MK = os.path.join(HERE, "mk_layers.py")
SHA = "74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db"

checks = failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: " + what)


def mk(*args):
    r = subprocess.run([sys.executable, MK] + list(args), cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    t = tempfile.mkdtemp(prefix="zxv-layers-")
    out = os.path.join(t, "out")
    efi = os.path.join(t, "BOOTX64.EFI")
    with open(efi, "wb") as f:
        f.write(b"efi bytes")
    models = os.path.join(t, "models")
    with open(models, "w") as f:
        f.write("core.gguf %s 491400032 https://example.invalid/a MDL000.GGU\n" % SHA)
        f.write("swarm.gguf %s 7 https://example.invalid/b MDL001.GGU\n" % SHA)
    core = os.path.join(t, "core")
    with open(core, "w") as f:
        f.write("# core\ncore.gguf\n")
    swarm = os.path.join(t, "swarm")
    with open(swarm, "w") as f:
        f.write("reason swarm.gguf\n")
    booklets = os.path.join(t, "booklets")
    os.makedirs(os.path.join(booklets, "b"))
    with open(os.path.join(booklets, "a.txt"), "w") as f:
        f.write("hello booklet\n")
    with open(os.path.join(booklets, "b", "c.txt"), "w") as f:
        f.write("deeper\n")

    def cid(n):
        with open(os.path.join(out, "L%d.cid" % n)) as f:
            return f.read().strip()

    def path(n):
        return os.path.join(out, "L%d.json" % n)

    rc, o = mk("l0", "--out", out)
    check(rc == 0 and "L0 HARDWARE_ENVELOPE: 2 objects" in o, "l0: " + o)
    rc, o = mk("l1", "--out", out, "--parent", cid(0), "--boot", "x86_64=" + efi,
               "--models", models, "--core", core)
    check(rc == 0 and "L1 CORE_BOOT_ENGINE: 2 objects" in o, "l1: " + o)
    rc, o = mk("l2", "--out", out, "--parent", cid(1), "--models", models, "--swarm", swarm)
    check(rc == 0 and "L2 SWARM: 1 objects" in o, "l2: " + o)
    rc, o = mk("l3", "--out", out, "--parent", cid(2), "--dir", booklets, "--compat", "evo_comm")
    check(rc == 0 and "L3 ALEXANDRIA: 2 objects" in o, "l3: " + o)
    rc, o = mk("l4", "--out", out, "--parent", cid(3), "--dir", booklets)
    check(rc == 0, "l4: " + o)
    chain = [path(n) for n in range(5)]
    rc, o = mk("check", *chain)
    check(rc == 0 and "lineage ok: 5 layers, top " + cid(4) in o, "check: " + o)

    # what the manifests say
    m1 = json.load(open(path(1)))
    check(m1["parent_layer_cid"] == cid(0) and m1["parent_compat"] == "exact", "L1 parent")
    boot, model = m1["objects"]
    check(boot == {"kind": "boot", "arch": "x86_64", "name": "BOOTX64.EFI", "bytes": 9,
                   "sha256": "b57af4343ba7a27d5120be37a8f6547e2a60312c333f3a4ace26cb600e3816bd",
                   "cid": "bafkreifvpl2dio5huj6vcif6g6upmvd6fjqdclbth45evtrgznqa4oawxu"},
          "L1 boot entry %r" % boot)
    check(model == {"kind": "model", "arch": "all", "name": "core.gguf", "bytes": 491400032,
                    "sha256": SHA}, "L1 model entry %r" % model)
    m2 = json.load(open(path(2)))
    check(m2["objects"][0]["group"] == "REASON", "L2 group")
    m3 = json.load(open(path(3)))
    check(m3["parent_compat"] == "evo_comm" and [o["name"] for o in m3["objects"]] ==
          ["a.txt", "c.txt"], "L3 objects in walk order")
    check(cid(0).startswith("bafkrei") and len(cid(0)) == 59, "a layer CID is raw sha2-256")
    # the build is a function of its inputs
    again = os.path.join(t, "again")
    rc, _ = mk("l0", "--out", again)
    check(rc == 0 and open(os.path.join(again, "L0.json"), "rb").read() ==
          open(path(0), "rb").read(), "l0 is reproducible")

    # a broken chain
    rc, o = mk("check", path(0), path(2))
    check(rc == 1 and "L2 cannot stand on L0" in o, "skip: " + o)
    rc, o = mk("check", path(1), path(2))
    check(rc == 1 and "the chain starts at L1, not L0" in o, "root: " + o)
    rc, o = mk("check", path(0), path(1), path(2), path(3), path(4), path(4))
    check(rc == 1 and "names parent " + cid(3) in o, "L4 on a different L4: " + o)
    bent = os.path.join(t, "bent")
    os.makedirs(bent)
    data = open(path(0), "rb").read().replace(b"0x100000", b"0x100001")
    open(os.path.join(bent, "L0.json"), "wb").write(data)
    rc, o = mk("check", os.path.join(bent, "L0.json"), path(1))
    check(rc == 1 and "names parent %s, but the layer below is" % cid(0) in o, "bent L0: " + o)

    # other layouts of the same data
    def refused(data, want, what):
        p = os.path.join(t, "x.json")
        with open(p, "wb") as f:
            f.write(data)
        rc, o = mk("check", p)
        check(rc == 1 and want in o, what + ": " + o)

    base = open(path(0), "rb").read()
    refused(json.dumps(json.loads(base), indent=1).encode() + b"\n",
            "not in the one layout mk_layers.py writes", "re-indented")
    refused(base.rstrip(b"\n"), "not in the one layout", "no final newline")
    refused(base.replace(b'"zxv-layer 1"', b'"zxv-layer 2"'), "not a 'zxv-layer 1' manifest",
            "format")
    refused(base.replace(b'"layer_depth": 0', b'"layer_depth": 5'), "layer_depth 5 is not 0..4",
            "depth")
    refused(b"\xff", "not ASCII JSON", "binary")
    l1 = open(path(1), "rb").read()
    refused(l1.replace(b'"parent_compat": "exact"', b'"parent_compat": "evo_comm"'),
            "parent_compat 'evo_comm' at L1", "evo_comm below L3")
    refused(base.replace(b'"parent_layer_cid": null', b'"parent_layer_cid": "%s"' %
                         cid(1).encode()), "L0 and only L0 has no parent", "L0 parent")

    # refused at build time
    rc, o = mk("l1", "--out", out, "--boot", "x86_64=" + efi, "--models", models, "--core", core)
    check(rc == 1 and "L1 needs --parent with the CID of L0" in o, "no parent: " + o)
    rc, o = mk("l2", "--out", out, "--parent", "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354")
    check(rc == 1 and "is not a layer CID" in o, "dag-pb parent: " + o)
    rc, o = mk("l0", "--out", out, "--parent", cid(0))
    check(rc == 1 and "L0 has no parent" in o, "l0 parent: " + o)
    rc, o = mk("l1", "--out", out, "--parent", cid(0), "--models", models, "--core", core)
    check(rc == 1 and "l1 needs --boot" in o, "no boot: " + o)
    rc, o = mk("l1", "--out", out, "--parent", cid(0), "--boot", "sparc=" + efi,
               "--models", models, "--core", core)
    check(rc == 1 and "want ARCH=FILE" in o, "bad arch: " + o)
    with open(swarm, "a") as f:
        f.write("vision ghost.gguf\n")
    rc, o = mk("l2", "--out", out, "--parent", cid(1), "--models", models, "--swarm", swarm)
    check(rc == 1 and "names no model of" in o, "unknown swarm model: " + o)
    os.makedirs(os.path.join(t, "bad"))
    open(os.path.join(t, "bad", "a b.txt"), "w").close()
    rc, o = mk("l3", "--out", out, "--parent", cid(2), "--dir", os.path.join(t, "bad"))
    check(rc == 1 and "name not of" in o, "bad booklet name: " + o)
    rc, o = mk("l4", "--out", out, "--parent", cid(3), "--dir", os.path.join(t, "empty"))
    check(rc == 1 and "l4 needs --dir" in o, "no dir: " + o)
    machines = os.path.join(t, "machines")
    with open(machines, "w") as f:
        f.write("x86_64 BIOS 0x100000 4096 512 512\n")
    rc, o = mk("l0", "--out", out, "--machines", machines)
    check(rc == 1 and "bad machine line" in o, "BIOS machine: " + o)

    print("test_layers: %d checks, %d failures" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
