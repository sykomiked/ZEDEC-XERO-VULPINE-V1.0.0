#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/zxv_board.py: the manifest validator, the packaging plan and
the packaging driver against a fake build (no Docker, no QEMU, no cross
compiler). Standard library unittest only."""
import contextlib
import copy
import hashlib
import io
import json
import os
import shutil
import stat
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(KIT, "tools"))

import zxv_board as B  # noqa: E402

BOARDS = os.path.join(KIT, "boards")
R1 = os.path.join(KIT, "profiles", "r1.zhp")
with open(R1, "rb") as _f:
    R1_DIGEST = hashlib.sha256(_f.read()).hexdigest()

BASE = {
    "format": "zxv-board-manifest",
    "version": 1,
    "board": {"id": "test-board", "vendor": "Acme", "model": "Model 1"},
    "arch": "x86_64",
    "firmware": {"type": "uefi", "secure_boot": "off"},
    "console": {"serial": {"uart": "16550", "base": "0x3f8", "baud": 38400},
                "framebuffer": "uefi-gop"},
    "memory": {"ram_mib": 2048},
    "hw_profile": {"digest": R1_DIGEST, "file": "r1.zhp"},
}
ARM_UEFI = {"arch": "arm64",
            "console": {"serial": {"uart": "pl011", "base": "0x9000000", "baud": 115200},
                        "framebuffer": "none"}}
ARM_DT = dict(ARM_UEFI, firmware={"type": "devicetree", "machine": "qemu-virt"},
              memory={"ram_mib": 256})
RV_DT = {"arch": "riscv64", "firmware": {"type": "devicetree", "machine": "qemu-virt"},
         "console": {"serial": {"uart": "ns16550a", "base": "0x10000000", "baud": 115200},
                     "framebuffer": "none"},
         "memory": {"ram_mib": 256}}


def setp(m, path, value):
    """Set (or with value=DEL, delete) a dotted path in m."""
    keys = path.split(".")
    for k in keys[:-1]:
        m = m[k]
    if value is DEL:
        del m[keys[-1]]
    else:
        m[keys[-1]] = value


DEL = object()
COUNT = {"valid": 0, "invalid": 0}


def tearDownModule():
    print("\nmanifest cases: %d valid, %d invalid (each invalid one checked for its message)"
          % (COUNT["valid"], COUNT["invalid"]))


class Base(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        shutil.copyfile(R1, os.path.join(self.dir, "r1.zhp"))
        self.schema = B.load_schema()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, m, name="m.json", raw=None):
        p = os.path.join(self.dir, name)
        with open(p, "wb") as f:
            f.write(raw if raw is not None else json.dumps(m).encode("utf-8"))
        return p

    def manifest(self, overlay=None, **edits):
        m = copy.deepcopy(BASE)
        m.update(copy.deepcopy(overlay or {}))
        for path, value in edits.items():
            setp(m, path.replace("__", "."), value)
        return m

    def errs(self, m=None, raw=None):
        errs, _ = B.validate(self.write(m, raw=raw), self.schema)
        return errs

    def assertValid(self, m):
        COUNT["valid"] += 1
        self.assertEqual(self.errs(m), [])

    def assertInvalid(self, m, needle, raw=None):
        COUNT["invalid"] += 1
        errs = self.errs(m, raw=raw)
        self.assertTrue(errs, "expected an error containing %r" % needle)
        self.assertTrue(any(needle in e for e in errs), "%r not in %r" % (needle, errs))


class Valid(Base):
    def test_example_boards(self):
        names = sorted(n for n in os.listdir(BOARDS) if n.endswith(".json"))
        self.assertEqual(len(names), 5)
        for n in names:
            errs, m = B.validate(os.path.join(BOARDS, n), self.schema)
            COUNT["valid"] += 1
            self.assertEqual(errs, [], n)
            self.assertEqual(m["board"]["id"] + ".json", n)

    def test_variants(self):
        cases = [
            self.manifest(),
            self.manifest(console__serial=DEL),                     # GOP only, no COM1
            self.manifest(hw_profile={"digest": "0" * 64}),         # digest pinned, no file
            self.manifest(firmware__secure_boot="owner-signs"),
            self.manifest(board__revision="B2", notes="n" * 2000, image={"esp_mib": 33}),
            self.manifest(image={"esp_mib": 4096}),
            self.manifest(image={}),
            self.manifest(board__id="a"),
            self.manifest(board__id="a" * 64),
            self.manifest(board__id="0-9"),
            self.manifest(board__vendor="Müller & Söhne"),
            self.manifest(memory__ram_mib=512),
            self.manifest(memory__ram_mib=16777216),
            self.manifest(console__serial__base="0x00000000000003f8"),
            self.manifest(hw_profile__name="Acme profile"),
            self.manifest(ARM_UEFI),
            self.manifest(ARM_DT),
            self.manifest(RV_DT),
            self.manifest(ARM_DT, memory={"ram_mib": 1024}),
        ]
        for i, m in enumerate(cases):
            with self.subTest(i=i):
                self.assertValid(m)


class InvalidJson(Base):
    def test_raw(self):
        cases = [
            (b"", "not valid JSON"),
            (b"{", "not valid JSON"),
            (b"[]", "must be an object"),
            (b"\xff\xfe{}", "not UTF-8"),
            (b'{"format": "zxv-board-manifest", "format": "zxv-board-manifest"}', "duplicate key"),
            (json.dumps(BASE).replace('"ram_mib": 2048', '"ram_mib": 2048.0').encode(),
             "floating-point"),
            (json.dumps(BASE).replace('"ram_mib": 2048', '"ram_mib": 2e3').encode(),
             "floating-point"),
            (json.dumps(BASE).replace('"ram_mib": 2048', '"ram_mib": NaN').encode(),
             "floating-point"),
            (json.dumps(BASE).replace('"ram_mib": 2048', '"ram_mib": Infinity').encode(),
             "floating-point"),
            (b" " * (1 << 20) + b"{}", "larger than 1 MiB"),
        ]
        for raw, needle in cases:
            with self.subTest(needle=needle, raw=raw[:30]):
                self.assertInvalid(None, needle, raw=raw)

    def test_missing_file(self):
        errs, m = B.validate(os.path.join(self.dir, "nope.json"), self.schema)
        self.assertTrue(errs)
        self.assertIsNone(m)


class InvalidStructure(Base):
    def test_top_level(self):
        for key in ("format", "version", "board", "arch", "firmware", "console", "memory",
                    "hw_profile"):
            with self.subTest(missing=key):
                self.assertInvalid(self.manifest(**{key: DEL}), "missing required %r" % key)
        cases = [
            (self.manifest(format="zxv-board"), "$.format"),
            (self.manifest(version=2), "$.version"),
            (self.manifest(version="1"), "$.version"),
            (self.manifest(version=True), "$.version"),
            (self.manifest(extra=1), "unknown key 'extra'"),
            (self.manifest(arch="riscv32"), "$.arch"),
            (self.manifest(arch="x86-64"), "$.arch"),
            (self.manifest(notes="n" * 2001), "$.notes"),
            (self.manifest(notes=5), "$.notes"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)

    def test_board(self):
        cases = [
            (self.manifest(board__id="Test"), "$.board.id"),
            (self.manifest(board__id="-test"), "$.board.id"),
            (self.manifest(board__id="test-"), "$.board.id"),
            (self.manifest(board__id="a" * 65), "$.board.id"),
            (self.manifest(board__id=""), "$.board.id"),
            (self.manifest(board__id="te_st"), "$.board.id"),
            (self.manifest(board__id="../x"), "$.board.id"),
            (self.manifest(board__vendor=""), "$.board.vendor"),
            (self.manifest(board__vendor="a\nb"), "$.board.vendor"),
            (self.manifest(board__vendor="a\x7fb"), "$.board.vendor"),
            (self.manifest(board__vendor="v" * 129), "$.board.vendor"),
            (self.manifest(board__model=DEL), "missing required 'model'"),
            (self.manifest(board__serial="123"), "unknown key 'serial'"),
            (self.manifest(board=[]), "$.board: must be an object"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)

    def test_firmware_console_memory_image(self):
        cases = [
            (self.manifest(firmware__type="bios"), "$.firmware.type"),
            (self.manifest(firmware__secure_boot="on"), "$.firmware.secure_boot"),
            (self.manifest(firmware__machine="rpi4"), "$.firmware.machine"),
            (self.manifest(firmware__signed=True), "unknown key 'signed'"),
            (self.manifest(console__framebuffer=DEL), "missing required 'framebuffer'"),
            (self.manifest(console__framebuffer="vga"), "$.console.framebuffer"),
            (self.manifest(console__serial__baud=DEL), "missing required 'baud'"),
            (self.manifest(console__serial__baud=0), "below the minimum"),
            (self.manifest(console__serial__baud=True), "must be an integer"),
            (self.manifest(console__serial__baud="38400"), "must be an integer"),
            (self.manifest(console__serial__base="0x3F8"), "$.console.serial.base"),
            (self.manifest(console__serial__base="3f8"), "$.console.serial.base"),
            (self.manifest(console__serial__base="0x" + "1" * 17), "$.console.serial.base"),
            (self.manifest(console__serial__uart="8250"), "$.console.serial.uart"),
            (self.manifest(console__serial__parity="none"), "unknown key 'parity'"),
            (self.manifest(memory__ram_mib=0), "below the minimum"),
            (self.manifest(memory__ram_mib=16777217), "above the maximum"),
            (self.manifest(memory__ram_mib="4096"), "must be an integer"),
            (self.manifest(memory__base="0x0"), "unknown key 'base'"),
            (self.manifest(image={"esp_mib": 32}), "below the minimum"),
            (self.manifest(image={"esp_mib": 4097}), "above the maximum"),
            (self.manifest(image={"size": 1}), "unknown key 'size'"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)


class InvalidRules(Base):
    def test_targets(self):
        cases = [
            (self.manifest(RV_DT, firmware={"type": "uefi", "secure_boot": "off"}),
             "not yet: no RISC-V UEFI boot path"),
            (self.manifest(firmware={"type": "devicetree", "machine": "qemu-virt"}),
             "x86_64 boots through UEFI only"),
            (self.manifest(firmware__secure_boot=DEL), "required for UEFI"),
            (self.manifest(firmware__machine="qemu-virt"), "only for device-tree"),
            (self.manifest(ARM_DT, firmware={"type": "devicetree"}), "required for device-tree"),
            (self.manifest(ARM_DT, firmware={"type": "devicetree", "machine": "qemu-virt",
                                             "secure_boot": "off"}), "only for UEFI"),
            (self.manifest(ARM_DT, image={"esp_mib": 48}), "only UEFI targets"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)

    def test_console(self):
        cases = [
            (self.manifest(console__serial__uart="pl011"), "drives a 16550 UART"),
            (self.manifest(console__serial__base="0x2f8"), "UART is at 0x3f8"),
            (self.manifest(console__serial__baud=115200), "programs 38400 baud"),
            (self.manifest(ARM_UEFI, console={"serial": ARM_UEFI["console"]["serial"],
                                              "framebuffer": "uefi-gop"}), "supports \"none\""),
            (self.manifest(ARM_UEFI, console={"serial": {"uart": "16550", "base": "0x3f8",
                                                         "baud": 38400}, "framebuffer": "none"}),
             "drives a pl011 UART"),
            (self.manifest(RV_DT, console={"serial": {"uart": "ns16550a", "base": "0x10000100",
                                                      "baud": 115200}, "framebuffer": "none"}),
             "UART is at 0x10000000"),
            (self.manifest(console={"framebuffer": "none"}), "no console"),
            (self.manifest(ARM_DT, console={"framebuffer": "none"}), "no console"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)

    def test_memory(self):
        self.assertInvalid(self.manifest(memory__ram_mib=511), "below 512 MiB")
        self.assertInvalid(self.manifest(ARM_UEFI, memory={"ram_mib": 256}), "below 512 MiB")
        self.assertInvalid(self.manifest(ARM_DT, memory={"ram_mib": 255}), "below 256 MiB")
        self.assertInvalid(self.manifest(RV_DT, memory={"ram_mib": 128}), "below 256 MiB")

    def test_hw_profile(self):
        with open(R1, "rb") as f:
            good = f.read()

        def prof(name, data):
            with open(os.path.join(self.dir, name), "wb") as f:
                f.write(data)
            return {"digest": hashlib.sha256(data).hexdigest(), "file": name}

        cases = [
            (self.manifest(hw_profile__digest=R1_DIGEST.upper()), "$.hw_profile.digest"),
            (self.manifest(hw_profile__digest=R1_DIGEST[:63]), "$.hw_profile.digest"),
            (self.manifest(hw_profile__digest="1" * 64), "does not match"),
            (self.manifest(hw_profile__digest=DEL), "missing required 'digest'"),
            (self.manifest(hw_profile__file="missing.zhp"), "cannot read"),
            (self.manifest(hw_profile__file="/etc/r1.zhp"), "relative path"),
            (self.manifest(hw_profile__file="r1.bin"), "$.hw_profile.file"),
            (self.manifest(hw_profile__file="r 1.zhp"), "$.hw_profile.file"),
            (self.manifest(hw_profile__vendor_key="x"), "unknown key 'vendor_key'"),
            (self.manifest(hw_profile=prof("bad_magic.zhp", b"ZHP2" + good[4:])), "magic"),
            (self.manifest(hw_profile=prof("short.zhp", good[:10])), "16-byte"),
            (self.manifest(hw_profile=prof("trunc.zhp", good[:-1])), "header length"),
            (self.manifest(hw_profile=prof("long.zhp", good + b"\0")), "header length"),
            (self.manifest(hw_profile=prof("fmt.zhp", good[:4] + b"\x02\x00" + good[6:])),
             "format 2"),
            (self.manifest(hw_profile=prof("flags.zhp", good[:6] + b"\x01\x00" + good[8:])),
             "reserved"),
            (self.manifest(hw_profile=prof("nodes.zhp", good[:8] + b"\x41\x00" + good[10:])),
             "65 nodes"),
        ]
        for m, needle in cases:
            with self.subTest(needle=needle):
                self.assertInvalid(m, needle)

    def test_relative_profile_path(self):
        os.mkdir(os.path.join(self.dir, "sub"))
        p = os.path.join(self.dir, "sub", "m.json")
        with open(p, "w", encoding="utf-8") as f:
            json.dump(self.manifest(hw_profile__file="../r1.zhp"), f)
        self.assertEqual(B.validate(p, self.schema)[0], [])


class Schema(unittest.TestCase):
    def test_unsupported_keyword_is_an_error(self):
        s = {"type": "object", "oneOf": []}
        with self.assertRaises(B.ManifestError):
            B.schema_errors({}, s, s)

    def test_schema_is_complete(self):
        s = B.load_schema()
        self.assertEqual(set(s["required"]),
                         {"format", "version", "board", "arch", "firmware", "console", "memory",
                          "hw_profile"})


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            rc = B.main(argv)
        except SystemExit as e:
            rc = e.code
    return rc, out.getvalue(), err.getvalue()


class Plan(Base):
    def plan(self, m, **kw):
        return B.make_plan(m, "m.json", repo="/repo", **kw)

    def test_uefi_docker(self):
        p = self.plan(self.manifest(image={"esp_mib": 64}))
        self.assertEqual([s["argv"] for s in p["steps"]], [
            ["docker", "build", "-f", "build_system/Dockerfile.baremetal", "-t", "zxv-baremetal", "."],
            ["docker", "run", "--rm", "-e", "ESP_MB=64", "-v", "/repo:/src", "-w", "/src",
             "zxv-baremetal", "bash", "build_system/baremetal_images.sh", "x86_64"]])
        self.assertEqual(p["artifact_src"], "dist/discs/zxv-x86_64-uefi.img")
        self.assertEqual(p["artifact"], "zxv-test-board-x86_64-uefi.img")
        self.assertEqual(p["out"], "/repo/dist/boards/test-board")

    def test_uefi_host_boot_check(self):
        p = self.plan(self.manifest(), host=True, boot_check=True)
        self.assertEqual([s["argv"] for s in p["steps"]], [
            ["bash", "build_system/baremetal_images.sh", "x86_64"],
            ["bash", "build_system/uefi_boot_check.sh", "x86_64", "dist/discs/zxv-x86_64-uefi.img",
             "90", "[AI_OK]", "UEFI GOP text console"]])
        p = self.plan(self.manifest(console__framebuffer="none"), host=True, boot_check=True)
        self.assertNotIn("UEFI GOP text console", p["steps"][1]["argv"])
        p = self.plan(self.manifest(ARM_UEFI), host=True, boot_check=True)
        self.assertEqual(p["steps"][1]["argv"][2:], ["arm64", "dist/discs/zxv-arm64-uefi.img",
                                                      "90", "[AI_OK]"])

    def test_devicetree(self):
        p = self.plan(self.manifest(ARM_DT))
        self.assertEqual(p["steps"][-1]["argv"][-4:],
                         ["-f", "build_system/Makefile.arm64", "CROSS_COMPILE=aarch64-linux-gnu-", "all"])
        self.assertEqual(p["artifact_src"], "kernel_arm64.elf")
        with self.assertRaises(B.ManifestError):
            self.plan(self.manifest(RV_DT))
        p = self.plan(self.manifest(RV_DT), host=True)
        self.assertEqual([s["argv"][-1] for s in p["steps"]], ["clean", "all"])
        self.assertIsNone(p["artifact_sums"])

    def test_scripts_referenced_exist_in_tree(self):
        for m in (self.manifest(), self.manifest(ARM_UEFI), self.manifest(ARM_DT),
                  self.manifest(RV_DT)):
            p = B.make_plan(m, "m.json", host=True,
                            boot_check=m["firmware"]["type"] == "uefi")
            for s in p["steps"]:
                for a in s["argv"]:
                    if a.startswith("build_system/"):
                        self.assertTrue(os.path.isfile(os.path.join(B.REPO, a)), a)

    def test_dry_run_writes_nothing(self):
        out = os.path.join(self.dir, "out")
        path = self.write(self.manifest())
        rc, text, _ = run_main(["package", path, "--dry-run", "--out", out])
        self.assertEqual(rc, 0)
        self.assertIn("baremetal_images.sh x86_64", text)
        self.assertFalse(os.path.exists(out))
        rc, text, _ = run_main(["plan", path, "--json", "--host"])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(text)["steps"][0]["argv"][1], "build_system/baremetal_images.sh")

    def test_cli_exit_codes(self):
        good = self.write(self.manifest(), "good.json")
        bad = self.write(self.manifest(arch="mips"), "bad.json")
        self.assertEqual(run_main(["validate", good])[0], 0)
        rc, text, _ = run_main(["validate", good, bad])
        self.assertEqual(rc, 1)
        self.assertIn("INVALID", text)
        self.assertEqual(run_main(["package", bad, "--dry-run"])[0], 1)
        self.assertEqual(run_main([])[0], 2)
        self.assertEqual(run_main(["frobnicate"])[0], 2)
        dt = self.write(self.manifest(ARM_DT), "dt.json")
        self.assertEqual(run_main(["package", dt, "--boot-check", "--dry-run"])[0], 2)
        rv = self.write(self.manifest(RV_DT), "rv.json")
        self.assertEqual(run_main(["plan", rv])[0], 1)


FAKE_BUILD = """#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[ "${FAKE_FAIL:-}" = 1 ] && exit 3
mkdir -p dist/discs
printf 'disc for %s, ESP_MB=%s\\n' "$1" "${ESP_MB:-default}" > "dist/discs/zxv-$1-uefi.img"
( cd dist/discs && sha256sum zxv-*-uefi.img > SHA256SUMS )
[ "${FAKE_BADSUM:-}" = 1 ] && sed -i 's/^./0/' dist/discs/SHA256SUMS
exit 0
"""


class FakePackage(Base):
    """`package --host` against a fake repo whose baremetal_images.sh writes a
    stand-in disc: exercises run, collect and the bundle without a toolchain."""

    def setUp(self):
        super().setUp()
        if not shutil.which("bash") or not shutil.which("sha256sum"):
            self.skipTest("needs bash and sha256sum")
        self.repo = os.path.join(self.dir, "repo")
        os.makedirs(os.path.join(self.repo, "build_system"))
        script = os.path.join(self.repo, "build_system", "baremetal_images.sh")
        with open(script, "w", encoding="utf-8") as f:
            f.write(FAKE_BUILD)
        os.chmod(script, os.stat(script).st_mode | stat.S_IXUSR)
        self.out = os.path.join(self.dir, "bundle")

    def package(self, m, env=None):
        path = self.write(m)
        old = dict(os.environ)
        os.environ.update(env or {})
        try:
            return run_main(["package", path, "--host", "--repo", self.repo, "--out", self.out])
        finally:
            os.environ.clear()
            os.environ.update(old)

    def test_bundle(self):
        rc, text, err = self.package(self.manifest(image={"esp_mib": 40}))
        self.assertEqual(rc, 0, err)
        names = sorted(os.listdir(self.out))
        self.assertEqual(names, ["BOOT.txt", "SHA256SUMS", "board.json",
                                 "zxv-test-board-x86_64-uefi.img"])
        with open(os.path.join(self.out, "zxv-test-board-x86_64-uefi.img")) as f:
            self.assertEqual(f.read(), "disc for x86_64, ESP_MB=40\n")
        with open(os.path.join(self.out, "SHA256SUMS")) as f:
            lines = f.read().splitlines()
        self.assertEqual(len(lines), 3)
        for line in lines:
            digest, name = line.split("  ")
            with open(os.path.join(self.out, name), "rb") as f:
                self.assertEqual(hashlib.sha256(f.read()).hexdigest(), digest)
        with open(os.path.join(self.out, "BOOT.txt")) as f:
            boot = f.read()
        self.assertIn("NOT signed", boot)
        self.assertIn("Turn Secure Boot off", boot)
        self.assertIn(R1_DIGEST, boot)
        self.assertIn("16550 UART at 0x3f8, 38400 baud", boot)
        with open(os.path.join(self.out, "board.json")) as f:
            self.assertEqual(json.load(f)["board"]["id"], "test-board")
        self.assertIn("packaged test-board", text)

    def test_owner_signs_text(self):
        rc, _, err = self.package(self.manifest(firmware__secure_boot="owner-signs"))
        self.assertEqual(rc, 0, err)
        with open(os.path.join(self.out, "BOOT.txt")) as f:
            boot = f.read()
        self.assertIn("machine owner", boot)
        self.assertIn("BOOTX64.EFI", boot)
        self.assertNotIn("Turn Secure Boot off", boot)

    def test_build_failure(self):
        rc, _, err = self.package(self.manifest(), {"FAKE_FAIL": "1"})
        self.assertEqual(rc, 1)
        self.assertIn("failed with exit 3", err)
        self.assertFalse(os.path.exists(self.out))

    def test_checksum_mismatch(self):
        rc, _, err = self.package(self.manifest(), {"FAKE_BADSUM": "1"})
        self.assertEqual(rc, 1)
        self.assertIn("does not match", err)
        self.assertFalse(os.path.exists(self.out))


if __name__ == "__main__":
    unittest.main(verbosity=1)
