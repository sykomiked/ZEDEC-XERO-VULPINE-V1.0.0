#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_root_tools.py — the sealed root's front matter: entry.json and the
boot.json models list are measured from the files, and anything that does
not match is refused, never written.

    test_root_tools.py WEB_ROOT     (a root from build_web_root.sh)

Checks the root's layout (the front page at index.html, the boot screen at
boot.html), then runs make_entry_json.py and make_boot_manifest.py --model on
a copy with a few files, good and bad. Prints one line per check, FAIL on
any, and the count at the end.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
    print("%s %s" % ("ok  " if ok else "FAIL", what))


def run(tool, *args):
    p = subprocess.run([sys.executable, os.path.join(HERE, tool)] + list(args),
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main(argv):
    src = argv[1]
    front = open(os.path.join(src, "index.html")).read()
    check('src="entry.js"' in front, "index.html is the front page (web/entry)")
    check(os.path.isfile(os.path.join(src, "entry.js")), "entry.js is in the root")
    boot = open(os.path.join(src, "boot.html")).read()
    check('src="boot.js"' in boot, "boot.html is the boot screen (web/boot)")
    with open(os.path.join(src, "boot.json")) as f:
        b = json.load(f)
    check(b.get("next") == "session/index.html", "boot.json hands over to the login")

    t = tempfile.mkdtemp()
    try:
        root = os.path.join(t, "root")
        shutil.copytree(src, root)
        os.makedirs(os.path.join(root, "00_base"))
        os.makedirs(os.path.join(root, "10_vm"))
        img = b"\x00ZXV disc" * 1000
        cid = "bafkreib5x4l2pj2k7myepuhu7ygzhzd4nvzzwmprwqlwijj2kmn7slvaqe"
        with open(os.path.join(root, "00_base", "zxv-x86_64.img"), "wb") as f:
            f.write(img)
        with open(os.path.join(root, "00_base", cid + ".iso"), "wb") as f:
            f.write(b"iso")
        with open(os.path.join(root, "install.sh"), "w") as f:
            f.write("#!/bin/sh\n")
        with open(os.path.join(root, "10_vm", "vm.json"), "w") as f:
            json.dump({"zxv_vm": 1, "discs": {}, "launchers": []}, f)

        rc, out = run("make_entry_json.py", root, "--image", "x86_64:img:00_base/zxv-x86_64.img",
                      "--image", "x86_64:iso:00_base/%s.iso" % cid, "--script", "unix:install.sh",
                      "--vm", "10_vm/vm.json", "--cid", "00_base/zxv-x86_64.img=" + cid)
        check(rc == 0, "make_entry_json writes a whole entry.json (%s)" % out.strip())
        with open(os.path.join(root, "entry.json")) as f:
            e = json.load(f)
        im = e["install"]["images"]
        check(e["zxv_entry"] == 1 and e["run"]["path"] == "boot.html", "Run here opens boot.html")
        check(im[0]["bytes"] == len(img) and im[0]["sha256"] == sha(img), "an image's size and SHA-256 are measured")
        check(im[0].get("cid") == cid, "a given CID is written")
        check(im[1].get("cid") == cid, "a file named <CID>.<ext> names its own CID")
        check("cid" not in e["install"]["scripts"][0], "no CID is made up for a file without one")
        check(e["vm"]["path"] == "10_vm/vm.json", "the VM description is named")
        good = open(os.path.join(root, "entry.json")).read()

        bad = [
            (["--image", "x86:img:00_base/zxv-x86_64.img"], "an unknown arch"),
            (["--image", "x86_64:vhd:00_base/zxv-x86_64.img"], "an unknown format"),
            (["--image", "x86_64:img:../root/install.sh"], "a path out of the root"),
            (["--image", "x86_64:img:/etc/hostname"], "an absolute path"),
            (["--image", "x86_64:img:00_base/missing.img"], "a file the root lacks"),
            (["--script", "beos:install.sh"], "an unknown system"),
            (["--run", "nowhere.html"], "a Run here page the root lacks"),
            (["--image", "x86_64:img:00_base/zxv-x86_64.img", "--cid",
              "00_base/zxv-x86_64.img=QmNotACid"], "a CID that is not one"),
            (["--vm", "install.sh"], "a VM description that is not one"),
            (["--image"], "an option with no value"),
        ]
        for args, what in bad:
            rc, out = run("make_entry_json.py", root, *args)
            same = open(os.path.join(root, "entry.json")).read() == good
            check(rc != 0 and same, "make_entry_json refuses %s and writes nothing" % what)

        model = b"GGUF" + b"\x01" * 4000
        with open(os.path.join(root, "m.gguf"), "wb") as f:
            f.write(model)
        ent = {"name": "m.gguf", "cid": cid, "bytes": len(model), "sha256": sha(model),
               "path": "m.gguf", "quant": "Q4_K_M"}
        mj = os.path.join(t, "m.json")
        with open(mj, "w") as f:
            json.dump(ent, f)
        rc, out = run("make_boot_manifest.py", root, "--next", "session/index.html", "--model", mj,
                      "session/session.js")
        with open(os.path.join(root, "boot.json")) as f:
            b = json.load(f)
        check(rc == 0 and b.get("models") == [ent], "boot.json carries the model entry as given")
        check(b.get("next") == "session/index.html" and b["system"][0]["path"] == "session/session.js",
              "the rest of boot.json is unchanged")
        before = open(os.path.join(root, "boot.json")).read()
        wrong = [
            (dict(ent, sha256="0" * 64), "a SHA-256 the file does not have"),
            (dict(ent, bytes=len(model) + 1), "a size the file does not have"),
            (dict(ent, sha256="XYZ"), "a SHA-256 that is not one"),
            (dict(ent, path="../m.gguf"), "a path out of the root"),
            ({k: v for k, v in ent.items() if k != "quant"}, "an entry missing a key"),
        ]
        for m, what in wrong:
            with open(mj, "w") as f:
                json.dump(m, f)
            rc, out = run("make_boot_manifest.py", root, "--next", "session/index.html", "--model", mj,
                          "session/session.js")
            same = open(os.path.join(root, "boot.json")).read() == before
            check(rc != 0 and same, "make_boot_manifest refuses %s and writes nothing" % what)
    finally:
        shutil.rmtree(t)
    print("%d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
