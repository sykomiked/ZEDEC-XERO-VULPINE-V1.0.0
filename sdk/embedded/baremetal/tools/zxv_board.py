#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ZXV-BareMetal: validate a board manifest and package the ZXV kernel for it.

usage:
  zxv_board.py validate MANIFEST...
  zxv_board.py plan     MANIFEST [--host] [--boot-check] [--out DIR] [--json]
  zxv_board.py package  MANIFEST [--host] [--boot-check] [--out DIR] [--dry-run]

validate  checks each manifest against schema/board-manifest-1.schema.json and
          the cross-field rules below; exit 0 only if every one is valid.
plan      prints the commands `package` would run, and writes nothing.
package   runs them: the PR #25 build scripts in build_system/ (in the pinned
          Docker toolchain, or on this machine with --host), then collects the
          result into DIR (default dist/boards/<board id>/) with the manifest,
          BOOT.txt and SHA256SUMS. --dry-run = plan.

The build itself is never reimplemented here: UEFI targets run
build_system/baremetal_images.sh (and, with --boot-check,
build_system/uefi_boot_check.sh); device-tree targets run the arch Makefile.
Nothing here signs an image or writes to a physical disk.

Python 3 standard library only. Exit codes: 0 ok, 1 invalid manifest or a
failed step, 2 usage.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

KIT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(KIT)))
SCHEMA = os.path.join(KIT, "schema", "board-manifest-1.schema.json")
DOCKER_IMAGE = "zxv-baremetal"

# What the kernel in this tree can actually boot, per (arch, firmware).
# Addresses and baud rates are the kernel's own constants:
#   x86_64  kernel/arch/x86_64/uart_16550.c   COM1 0x3F8, divisor 3 = 38400
#           kernel/arch/x86_64/efifb_con.c    UEFI GOP text console
#   arm64   kernel/arch/arm64/board_profile.h QEMU virt PL011 0x09000000, 115200
#   riscv64 kernel/arch/riscv/riscv_arch.h    NS16550A 0x10000000 (QEMU virt)
TARGETS = {
    ("x86_64", "uefi"): {
        "status": "QEMU (OVMF) in CI; physical x86-64 UEFI PCs per docs/BAREMETAL.md",
        "serial": ("16550", 0x3F8, 38400),
        "framebuffer": ("uefi-gop", "none"),
        "min_ram_mib": 512,
        "efi": "BOOTX64.EFI",
        "screen_marker": "UEFI GOP text console",
    },
    ("arm64", "uefi"): {
        "status": "QEMU virt (QEMU_EFI) in CI; no physical arm64 board validated",
        "serial": ("pl011", 0x09000000, 115200),
        "framebuffer": ("none",),
        "min_ram_mib": 512,
        "efi": "BOOTAA64.EFI",
    },
    ("arm64", "devicetree"): {
        "status": "QEMU virt only: kernel ELF for -kernel; the device tree is not parsed",
        "serial": ("pl011", 0x09000000, 115200),
        "framebuffer": ("none",),
        "min_ram_mib": 256,
        "makefile": "build_system/Makefile.arm64",
        "elf": "kernel_arm64.elf",
        "make_vars": ["CROSS_COMPILE=aarch64-linux-gnu-"],
        "qemu": "qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53",
    },
    ("riscv64", "devicetree"): {
        "status": "QEMU virt only: kernel ELF for OpenSBI (-kernel); the device tree "
                  "pointer is captured but not parsed; no physical RISC-V board",
        "serial": ("ns16550a", 0x10000000, 115200),
        "framebuffer": ("none",),
        "min_ram_mib": 256,
        "makefile": "build_system/Makefile.riscv",
        "elf": "kernel_riscv.elf",
        "make_vars": ["CROSS_COMPILE=riscv64-linux-gnu-"],
        "qemu": "qemu-system-riscv64 -M virt -cpu rv64",
        "host_only": True,
    },
}
NOT_YET = {
    ("riscv64", "uefi"): "not yet: no RISC-V UEFI boot path in the kernel (it boots on QEMU "
                         "virt through OpenSBI only; use firmware type \"devicetree\")",
    ("x86_64", "devicetree"): "x86_64 boots through UEFI only (there is no device-tree path "
                              "on x86_64)",
}


class ManifestError(Exception):
    pass


# --------------------------------------------------------------- JSON input

def _no_float(text):
    raise ManifestError("floating-point number %s: manifests use integers only" % text)


def _no_dupes(pairs):
    out = {}
    for k, v in pairs:
        if k in out:
            raise ManifestError("duplicate key %r" % k)
        out[k] = v
    return out


def load_json(path):
    """Strict JSON: no duplicate keys, no floats, no NaN/Infinity, UTF-8."""
    try:
        with open(path, "rb") as f:
            raw = f.read(1 << 20)
            if f.read(1):
                raise ManifestError("larger than 1 MiB")
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise ManifestError("not UTF-8")
    try:
        return json.loads(text, object_pairs_hook=_no_dupes, parse_float=_no_float,
                          parse_constant=_no_float)
    except json.JSONDecodeError as e:
        raise ManifestError("not valid JSON: %s" % e)


# ------------------------------------------------- JSON Schema (the subset)

SUPPORTED = {"$schema", "$id", "title", "description", "$defs", "$ref", "type", "enum",
             "const", "pattern", "minimum", "maximum", "minLength", "maxLength",
             "properties", "required", "additionalProperties"}


def _type_ok(v, t):
    if t == "object":
        return isinstance(v, dict)
    if t == "string":
        return isinstance(v, str)
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool)
    if t == "boolean":
        return isinstance(v, bool)
    raise ManifestError("schema uses unsupported type %r" % t)


def _same(a, b):
    return type(a) is type(b) and a == b


def schema_errors(v, s, root, path="$"):
    """Validate v against schema s. Only the keywords in SUPPORTED exist;
    anything else in the schema is an error, never silently ignored."""
    unknown = set(s) - SUPPORTED
    if unknown:
        raise ManifestError("schema keyword(s) not supported: %s" % ", ".join(sorted(unknown)))
    if "$ref" in s:
        ref = s["$ref"]
        if not ref.startswith("#/$defs/"):
            raise ManifestError("schema $ref %r not supported" % ref)
        return schema_errors(v, root["$defs"][ref[len("#/$defs/"):]], root, path)
    errs = []
    if "type" in s and not _type_ok(v, s["type"]):
        return ["%s: must be %s" % (path, "an " + s["type"] if s["type"][0] in "aeiou"
                                     else "a " + s["type"])]
    if "const" in s and not _same(v, s["const"]):
        errs.append("%s: must be %s" % (path, json.dumps(s["const"])))
    if "enum" in s and not any(_same(v, e) for e in s["enum"]):
        errs.append("%s: must be one of %s" % (path, ", ".join(json.dumps(e) for e in s["enum"])))
    if isinstance(v, str):
        if "minLength" in s and len(v) < s["minLength"]:
            errs.append("%s: shorter than %d characters" % (path, s["minLength"]))
        if "maxLength" in s and len(v) > s["maxLength"]:
            errs.append("%s: longer than %d characters" % (path, s["maxLength"]))
        if "pattern" in s and not re.search(s["pattern"], v):
            errs.append("%s: %s does not match %s" % (path, json.dumps(v), s["pattern"]))
    if isinstance(v, int) and not isinstance(v, bool):
        if "minimum" in s and v < s["minimum"]:
            errs.append("%s: below the minimum %d" % (path, s["minimum"]))
        if "maximum" in s and v > s["maximum"]:
            errs.append("%s: above the maximum %d" % (path, s["maximum"]))
    if isinstance(v, dict):
        props = s.get("properties", {})
        for k in s.get("required", []):
            if k not in v:
                errs.append("%s: missing required %r" % (path, k))
        for k in sorted(v):
            if k in props:
                errs.extend(schema_errors(v[k], props[k], root, "%s.%s" % (path, k)))
            elif s.get("additionalProperties", True) is False:
                errs.append("%s: unknown key %r" % (path, k))
    return errs


def load_schema():
    s = load_json(SCHEMA)
    schema_errors({}, s, s)  # raises on an unsupported keyword at the top
    return s


# ------------------------------------------------------- cross-field rules

def zhp_header_errors(data):
    """Light ZHP-1 checks (HW_PROFILE.md wire format); tools/zhp_tool does the
    full canonical decode through libzxv."""
    if len(data) < 16:
        return ["shorter than the 16-byte ZHP-1 header"]
    errs = []
    if data[0:4] != b"ZHP1":
        errs.append("magic is not ZHP1")
    fmt, flags, nodes, resv = (int.from_bytes(data[i:i + 2], "little") for i in (4, 6, 8, 10))
    total = int.from_bytes(data[12:16], "little")
    if fmt != 1:
        errs.append("format %d is not 1" % fmt)
    if flags or resv:
        errs.append("reserved header fields are not zero")
    if nodes > 64:
        errs.append("%d nodes (at most 64)" % nodes)
    if total != len(data):
        errs.append("header length %d but the file has %d bytes" % (total, len(data)))
    return errs


def rule_errors(m, base_dir):
    errs = []
    arch, fw = m["arch"], m["firmware"]
    key = (arch, fw["type"])
    if key in NOT_YET:
        return ["$.firmware.type: %s" % NOT_YET[key]]
    t = TARGETS[key]
    if fw["type"] == "uefi":
        if "secure_boot" not in fw:
            errs.append("$.firmware.secure_boot: required for UEFI (\"off\", or \"owner-signs\" "
                        "if the machine owner signs the loader with their own key)")
        if "machine" in fw:
            errs.append("$.firmware.machine: only for device-tree targets")
    else:
        if "machine" not in fw:
            errs.append("$.firmware.machine: required for device-tree targets (\"qemu-virt\": "
                        "the kernel does not parse the device tree, its addresses are QEMU virt's)")
        if "secure_boot" in fw:
            errs.append("$.firmware.secure_boot: only for UEFI targets")
    con = m["console"]
    if con["framebuffer"] not in t["framebuffer"]:
        errs.append("$.console.framebuffer: %s/%s supports %s" % (
            arch, fw["type"], " or ".join(json.dumps(x) for x in t["framebuffer"])))
    ser = con.get("serial")
    if ser is None:
        if con["framebuffer"] == "none":
            errs.append("$.console: no serial port and no framebuffer: the board has no console")
    else:
        uart, addr, baud = t["serial"]
        if ser["uart"] != uart:
            errs.append("$.console.serial.uart: the %s kernel drives a %s UART, not %s"
                        % (arch, uart, ser["uart"]))
        if int(ser["base"], 16) != addr:
            errs.append("$.console.serial.base: the %s kernel's UART is at 0x%x, not %s"
                        % (arch, addr, ser["base"]))
        if ser["baud"] != baud:
            errs.append("$.console.serial.baud: the %s kernel programs %d baud, not %d"
                        % (arch, baud, ser["baud"]))
    if m["memory"]["ram_mib"] < t["min_ram_mib"]:
        errs.append("$.memory.ram_mib: %d MiB is below %d MiB, the smallest size this target "
                    "is boot-tested with" % (m["memory"]["ram_mib"], t["min_ram_mib"]))
    if "image" in m and fw["type"] != "uefi":
        errs.append("$.image: only UEFI targets produce a disc image")
    hp = m["hw_profile"]
    if "file" in hp:
        f = hp["file"]
        if f.startswith("/") or "\\" in f:
            errs.append("$.hw_profile.file: must be a relative path")
        else:
            p = os.path.normpath(os.path.join(base_dir, f))
            try:
                with open(p, "rb") as fh:
                    data = fh.read(1 << 20)
            except OSError as e:
                errs.append("$.hw_profile.file: cannot read %s (%s)" % (f, e.strerror))
            else:
                errs.extend("$.hw_profile.file: %s" % e for e in zhp_header_errors(data))
                d = hashlib.sha256(data).hexdigest()
                if d != hp["digest"]:
                    errs.append("$.hw_profile.digest: %s does not match %s (SHA-256 %s)"
                                % (hp["digest"], f, d))
    return errs


def validate(path, schema=None):
    """Errors for one manifest file ([] = valid), and the parsed manifest."""
    schema = schema or load_schema()
    try:
        m = load_json(path)
    except (ManifestError, OSError) as e:
        return ["%s" % e], None
    errs = schema_errors(m, schema, schema)
    if errs:
        return errs, None
    errs = rule_errors(m, os.path.dirname(os.path.abspath(path)))
    return errs, (None if errs else m)


# ------------------------------------------------------------- packaging

def target_of(m):
    return TARGETS[(m["arch"], m["firmware"]["type"])]


def make_plan(m, manifest_path, out_dir=None, host=False, boot_check=False, repo=REPO):
    arch, fw = m["arch"], m["firmware"]["type"]
    t = target_of(m)
    bid = m["board"]["id"]
    out = os.path.abspath(out_dir or os.path.join(repo, "dist", "boards", bid))
    steps = []
    env = {}
    if fw == "uefi":
        esp = (m.get("image") or {}).get("esp_mib")
        if esp is not None:
            env["ESP_MB"] = str(esp)
        build = ["bash", "build_system/baremetal_images.sh", arch]
        if host:
            steps.append({"what": "build the %s kernel, %s and the UEFI disc (PR #25 script)"
                                  % (arch, t["efi"]), "argv": build, "env": env})
        else:
            steps.append({"what": "build the pinned toolchain image",
                          "argv": ["docker", "build", "-f", "build_system/Dockerfile.baremetal",
                                   "-t", DOCKER_IMAGE, "."], "env": {}})
            denv = []
            for k, v in sorted(env.items()):
                denv += ["-e", "%s=%s" % (k, v)]
            steps.append({"what": "build the %s kernel, %s and the UEFI disc in Docker "
                                  "(PR #25 script)" % (arch, t["efi"]),
                          "argv": ["docker", "run", "--rm"] + denv +
                                  ["-v", "%s:/src" % repo, "-w", "/src", DOCKER_IMAGE] + build,
                          "env": {}})
        src = "dist/discs/zxv-%s-uefi.img" % arch
        artifact = "zxv-%s-%s-uefi.img" % (bid, arch)
        if boot_check:
            check = ["bash", "build_system/uefi_boot_check.sh", arch, src, "90", "[AI_OK]"]
            if m["console"]["framebuffer"] == "uefi-gop":
                check.append(t["screen_marker"])
            if host:
                steps.append({"what": "boot the disc under UEFI firmware in QEMU (PR #25 check)",
                              "argv": check, "env": {}})
            else:
                steps.append({"what": "boot the disc under UEFI firmware in QEMU, in Docker "
                                      "(PR #25 check)",
                              "argv": ["docker", "run", "--rm", "-v", "%s:/src" % repo, "-w",
                                       "/src", DOCKER_IMAGE] + check, "env": {}})
        sums = "dist/discs/SHA256SUMS"
    else:
        if t.get("host_only") and not host:
            raise ManifestError("%s/%s builds on the host only (the pinned Docker image has no "
                                "%s cross compiler): pass --host" % (arch, fw, arch))
        build = ["make", "-f", t["makefile"]] + t["make_vars"]
        if host:
            steps.append({"what": "clean", "argv": build + ["clean"], "env": {}})
            steps.append({"what": "build %s" % t["elf"], "argv": build + ["all"], "env": {}})
        else:
            for what, goal in (("clean", "clean"), ("build %s in Docker" % t["elf"], "all")):
                steps.append({"what": what, "argv": ["docker", "run", "--rm", "-v",
                                                     "%s:/src" % repo, "-w", "/src",
                                                     DOCKER_IMAGE] + build + [goal], "env": {}})
            steps.insert(0, {"what": "build the pinned toolchain image",
                             "argv": ["docker", "build", "-f", "build_system/Dockerfile.baremetal",
                                      "-t", DOCKER_IMAGE, "."], "env": {}})
        src = t["elf"]
        artifact = "zxv-%s-%s.elf" % (bid, arch)
        sums = None
    return {"board": bid, "arch": arch, "firmware": fw, "status": t["status"], "repo": repo,
            "manifest": os.path.abspath(manifest_path), "out": out, "steps": steps,
            "artifact_src": src, "artifact": artifact, "artifact_sums": sums}


def plan_text(p):
    lines = ["board %s: %s / %s (%s)" % (p["board"], p["arch"], p["firmware"], p["status"]),
             "in %s:" % p["repo"]]
    for i, s in enumerate(p["steps"], 1):
        env = " ".join("%s=%s" % kv for kv in sorted(s["env"].items()))
        lines.append("  %d. %s\n     %s%s" % (i, s["what"], env + " " if env else "",
                                             " ".join(shlex.quote(a) for a in s["argv"])))
    lines.append("  %d. collect %s -> %s/%s, with board.json, BOOT.txt, SHA256SUMS"
                 % (len(p["steps"]) + 1, p["artifact_src"], p["out"], p["artifact"]))
    return "\n".join(lines)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def boot_text(m, p, digest):
    fw, arch = m["firmware"], m["arch"]
    t = target_of(m)
    lines = ["ZXV bare-metal package for %s %s (%s)" % (m["board"]["vendor"], m["board"]["model"],
                                                      m["board"]["id"]),
             "", "arch:       %s" % arch, "firmware:   %s" % fw["type"],
             "artifact:   %s" % p["artifact"], "sha256:     %s" % digest,
             "hw profile: ZHP-1 %s%s" % (m["hw_profile"]["digest"],
                                         " (%s)" % m["hw_profile"]["name"]
                                         if "name" in m["hw_profile"] else ""),
             "tested:     %s" % t["status"], ""]
    ser = m["console"].get("serial")
    if ser:
        lines.append("Console: %s UART at %s, %d baud 8N1." % (ser["uart"], ser["base"], ser["baud"]))
    if m["console"]["framebuffer"] == "uefi-gop":
        lines.append("Console: the boot log is also drawn on the UEFI GOP framebuffer.")
    lines.append("Success is the line [BOOT_OK] on the console, after [AI_OK].")
    lines.append("")
    if fw["type"] == "uefi":
        lines += ["Write the image to a removable disk with a tool that shows the target",
                  "drive (see docs/BAREMETAL.md). No script in this kit writes to a disk.",
                  "",
                  "SECURE BOOT: this image is NOT signed and this kit holds no keys."]
        if fw["secure_boot"] == "off":
            lines.append("Turn Secure Boot off in the firmware setup before booting it.")
        else:
            lines += ["The manifest says the machine owner signs it: sign /EFI/BOOT/%s on the" % t["efi"],
                      "ESP with the owner's own db key (for example sbsign) and enroll that",
                      "key in the firmware. That step is the machine owner's, not this kit's."]
    else:
        lines += ["Device-tree target, QEMU virt only (the kernel does not parse the device",
                  "tree). Run:",
                  "  %s -m %dM -kernel %s -nographic -serial mon:stdio"
                  % (t["qemu"], m["memory"]["ram_mib"], p["artifact"])]
    lines += ["", "Not validated on physical boards by this kit; see sdk/embedded/baremetal/README.md."]
    return "\n".join(lines) + "\n"


def run_steps(p, log=print):
    for i, s in enumerate(p["steps"], 1):
        log("==> [%d/%d] %s" % (i, len(p["steps"]), s["what"]))
        env = dict(os.environ)
        env.update(s["env"])
        r = subprocess.run(s["argv"], cwd=p["repo"], env=env)
        if r.returncode != 0:
            raise ManifestError("step %d (%s) failed with exit %d" % (i, s["what"], r.returncode))


def collect(m, p):
    src = os.path.join(p["repo"], p["artifact_src"])
    if not os.path.isfile(src):
        raise ManifestError("build produced no %s" % p["artifact_src"])
    digest = sha256_file(src)
    if p["artifact_sums"]:
        sums = os.path.join(p["repo"], p["artifact_sums"])
        name = os.path.basename(src)
        want = None
        with open(sums, encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) == 2 and parts[1].lstrip("*") == name:
                    want = parts[0]
        if want != digest:
            raise ManifestError("%s: SHA-256 %s does not match %s (%s)"
                                % (p["artifact_src"], digest, p["artifact_sums"], want))
    os.makedirs(p["out"], exist_ok=True)
    shutil.copyfile(src, os.path.join(p["out"], p["artifact"]))
    with open(os.path.join(p["out"], "board.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(m, f, indent=2, sort_keys=True)
        f.write("\n")
    with open(os.path.join(p["out"], "BOOT.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(boot_text(m, p, digest))
    names = sorted(n for n in os.listdir(p["out"]) if n != "SHA256SUMS")
    with open(os.path.join(p["out"], "SHA256SUMS"), "w", encoding="utf-8", newline="\n") as f:
        for n in names:
            f.write("%s  %s\n" % (sha256_file(os.path.join(p["out"], n)), n))
    return digest


# ------------------------------------------------------------------- CLI

def main(argv=None):
    ap = argparse.ArgumentParser(prog="zxv_board.py", description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("validate")
    v.add_argument("manifests", nargs="+")
    for name in ("plan", "package"):
        s = sub.add_parser(name)
        s.add_argument("manifest")
        s.add_argument("--host", action="store_true",
                       help="build with this machine's toolchain instead of Docker")
        s.add_argument("--boot-check", action="store_true",
                       help="UEFI only: boot the disc under QEMU firmware before collecting it")
        s.add_argument("--out", help="output directory (default dist/boards/<board id>)")
        s.add_argument("--repo", default=REPO, help=argparse.SUPPRESS)
        if name == "plan":
            s.add_argument("--json", action="store_true")
        else:
            s.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    try:
        schema = load_schema()
    except (ManifestError, OSError) as e:
        print("zxv_board: schema %s: %s" % (SCHEMA, e), file=sys.stderr)
        return 1
    if a.cmd == "validate":
        bad = 0
        for path in a.manifests:
            errs, _ = validate(path, schema)
            if errs:
                bad += 1
                print("INVALID %s" % path)
                for e in errs:
                    print("  %s" % e)
            else:
                print("valid   %s" % path)
        return 1 if bad else 0
    errs, m = validate(a.manifest, schema)
    if errs:
        print("INVALID %s" % a.manifest, file=sys.stderr)
        for e in errs:
            print("  %s" % e, file=sys.stderr)
        return 1
    if a.boot_check and m["firmware"]["type"] != "uefi":
        print("zxv_board: --boot-check is for UEFI targets", file=sys.stderr)
        return 2
    try:
        p = make_plan(m, a.manifest, a.out, a.host, a.boot_check, os.path.abspath(a.repo))
    except ManifestError as e:
        print("zxv_board: %s" % e, file=sys.stderr)
        return 1
    if a.cmd == "plan" or a.dry_run:
        print(json.dumps(p, indent=2, sort_keys=True) if getattr(a, "json", False) else plan_text(p))
        return 0
    try:
        run_steps(p)
        digest = collect(m, p)
    except (ManifestError, OSError) as e:
        print("zxv_board: %s" % e, file=sys.stderr)
        return 1
    print("packaged %s -> %s/%s (sha256 %s)" % (p["board"], p["out"], p["artifact"], digest))
    return 0


if __name__ == "__main__":
    sys.exit(main())
