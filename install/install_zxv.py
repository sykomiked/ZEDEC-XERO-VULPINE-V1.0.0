#!/usr/bin/env python3
"""install_zxv.py — Universal ZXV cell installer

Detects the target platform, selects the matching native kernel cell from the
universal install bundle, verifies SHA-256 digests, and stages artifacts for
boot.

Author: H.M. Michael-Laurence: Curzi (c)
License: SEL-3.3
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from transactional_disk import TransactionalDiskInstall


ARCH_ALIASES = {
    "x86_64": ["x86_64", "amd64"],
    "arm64": ["aarch64", "arm64"],
    "riscv64": ["riscv64"],
    "vit-dsp-v1": ["vit-dsp-v1"],
    "vit-fpga-v1": ["vit-fpga-v1"],
}


def normalize_arch(raw):
    """Map a host architecture name to the bundle's canonical architecture."""
    raw = raw.lower().strip()
    for canonical, aliases in ARCH_ALIASES.items():
        if raw in aliases:
            return canonical
    return raw


def detect_firmware():
    """Guess the platform's boot firmware."""
    if Path("/sys/firmware/efi").exists():
        return "uefi"
    if Path("/sys/firmware/devicetree/base").exists():
        return "devicetree"
    if Path("/proc/device-tree").is_symlink() or Path("/proc/device-tree").exists():
        return "devicetree"
    return "bios"


def detect_features():
    """Collect CPU and platform features relevant to payload selection."""
    features = {
        "cpu_vendor": "unknown",
        "cpu_flags": [],
        "secure_boot": False,
        "tpm": False,
        "virtualized": False,
        "hypervisor": "none",
    }

    proc_info = Path("/proc/cpuinfo")
    if proc_info.exists():
        text = proc_info.read_text(errors="ignore")
        for line in text.splitlines():
            if line.startswith("vendor_id"):
                features["cpu_vendor"] = line.split(":", 1)[-1].strip()
            elif line.startswith("flags"):
                flags = line.split(":", 1)[-1].strip().split()
                features["cpu_flags"] = flags
                if any(f in flags for f in ("hypervisor", "hyperv", "vmx", "svm")):
                    features["virtualized"] = True
                    features["hypervisor"] = "detected"

    if Path("/dev/tpm0").exists() or Path("/dev/tpmrm0").exists():
        features["tpm"] = True

    try:
        result = subprocess.run(
            ["mokutil", "--sb-state"],
            capture_output=True,
            text=True,
            check=False,
            timeout=2,
        )
        if "enabled" in result.stdout.lower():
            features["secure_boot"] = True
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        pass

    # macOS / BSD fallbacks
    if features["cpu_vendor"] == "unknown":
        try:
            result = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.vendor"],
                capture_output=True,
                text=True,
                check=False,
                timeout=2,
            )
            if result.stdout.strip():
                features["cpu_vendor"] = result.stdout.strip()
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
            pass

    return features


def detect_platform():
    """Build a platform profile for payload matching."""
    machine = platform.machine()
    bitness = "64" if sys.maxsize > 2**32 else "32"
    return {
        "arch": normalize_arch(machine),
        "bitness": int(bitness),
        "firmware": detect_firmware(),
        "os": platform.system().lower(),
        "machine_raw": machine,
        "features": detect_features(),
    }


def select_payloads(payloads, profile):
    """Return the payloads compatible with the detected platform."""
    selected = []
    for p in payloads:
        if p.get("status") != "available":
            continue
        if p.get("architecture") != profile["arch"]:
            continue
        bitness = p.get("bitness")
        if bitness is not None and bitness != profile["bitness"]:
            continue
        firmware = p.get("firmware")
        if firmware is not None and firmware != profile["firmware"]:
            continue
        selected.append(p)
    return selected


def verify_digest(path, expected):
    """Check a SHA-256 digest of the form 'sha256:hex'."""
    if not isinstance(expected, str) or not expected.startswith("sha256:"):
        return False, "digest format not supported"
    if not path.exists():
        return False, f"file not found: {path}"
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    digest = h.hexdigest().lower()
    expected_hex = expected.split(":", 1)[1].lower()
    if digest == expected_hex:
        return True, digest
    return False, f"expected {expected_hex}, got {digest}"


def stage_payload(bundle_dir, p, target_dir, dry_run):
    """Copy a payload artifact into the target tree if its digest validates."""
    src = bundle_dir / p["path"]
    dst = target_dir / p["path"]
    if not src.exists():
        return False, f"source artifact not found: {src}"

    digest_ok, digest_msg = verify_digest(src, p.get("digest", ""))
    if not digest_ok:
        return False, f"digest mismatch for {p['path']}: {digest_msg}"

    if not dry_run:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    return True, {
        "id": p["id"],
        "path": str(dst.relative_to(target_dir) if dst.is_absolute() else dst),
        "digest": p.get("digest"),
        "verified": True,
    }


def install(bundle_dir, target_dir, overrides, dry_run=False, install_to=None):
    """Run platform detection, payload selection, verify, stage, and optionally install."""
    bundle_dir = Path(bundle_dir).resolve()
    target_dir = Path(target_dir).resolve()
    manifest_path = bundle_dir / "PROVENANCE" / "universal_install_bundle_manifest.json"
    if not manifest_path.exists():
        manifest_path = bundle_dir / "universal_install_bundle_manifest.json"

    if not manifest_path.exists():
        raise FileNotFoundError(f"Bundle manifest not found: {manifest_path}")

    with open(manifest_path, "r") as f:
        manifest = json.load(f)

    profile = detect_platform()
    if overrides.get("arch"):
        profile["arch"] = normalize_arch(overrides["arch"])
    if overrides.get("firmware"):
        profile["firmware"] = overrides["firmware"].lower()
    if overrides.get("bitness"):
        profile["bitness"] = int(overrides["bitness"])

    payloads = manifest["zxv_install_bundle"]["payloads"]
    selected = select_payloads(payloads, profile)

    staged = []
    failed = []
    for p in selected:
        ok, result = stage_payload(bundle_dir, p, target_dir, dry_run)
        if ok:
            staged.append(result)
        else:
            failed.append({"id": p.get("id"), "error": result})

    installed = []
    if install_to and not failed and not dry_run:
        try:
            txn = TransactionalDiskInstall(Path(install_to))
            installed = txn.install(bundle_dir, selected)
        except Exception as exc:
            failed.append({"id": "disk_install", "error": f"transactional install failed: {exc}"})

    report = {
        "installer": "install_zxv.py",
        "bundle": manifest["zxv_install_bundle"]["bundle_id"],
        "platform": profile,
        "overrides": overrides,
        "selected_payloads": [p["id"] for p in selected],
        "staged": staged,
        "failed": failed,
        "installed": installed,
        "dry_run": dry_run,
    }

    return report


def finalize_install(target: str) -> dict:
    """Finalize a previously committed transactional installation.

    Removes rollback backups and the journal from an existing target after the
    new install has been verified stable.  This is the explicit post-probation
    step that must be called separately from install().
    """
    txn = TransactionalDiskInstall(Path(target))
    txn.finalize()
    return {
        "installer": "install_zxv.py",
        "action": "finalize",
        "target": str(Path(target).resolve()),
        "status": "finalized",
    }


def main():
    parser = argparse.ArgumentParser(description="Universal ZXV cell installer")
    parser.add_argument(
        "--bundle-dir",
        default=".",
        help="Root of the universal install bundle (default: current directory)",
    )
    parser.add_argument(
        "--target-dir",
        default="./zxv_staged",
        help="Directory to stage selected payloads",
    )
    parser.add_argument("--arch", default=None, help="Override detected architecture")
    parser.add_argument("--firmware", default=None, help="Override detected firmware")
    parser.add_argument("--bitness", default=None, help="Override bitness")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Detect and select payloads but do not copy files",
    )
    parser.add_argument(
        "--report",
        default="detection_report.json",
        help="Path for the JSON detection/staging report",
    )
    parser.add_argument(
        "--install-to",
        default=None,
        help="Target mount/directory for transactional disk installation",
    )
    parser.add_argument(
        "--action",
        choices=("install", "finalize"),
        default="install",
        help="Action to perform: install (default) or finalize an existing install",
    )

    args = parser.parse_args()
    overrides = {
        "arch": args.arch,
        "firmware": args.firmware,
        "bitness": args.bitness,
    }

    if args.action == "finalize":
        if not args.install_to:
            parser.error("--install-to is required for finalize action")
        report = finalize_install(args.install_to)
    else:
        report = install(args.bundle_dir, args.target_dir, overrides, args.dry_run, args.install_to)

    with open(args.report, "w") as f:
        json.dump(report, f, indent=2)

    if args.action == "finalize":
        print("ZXV install finalization")
        print("=" * 30)
        print(f"  target : {report['target']}")
        print(f"  status : {report['status']}")
        print()
        print(f"Report written to {args.report}")
        return 0

    print("ZXV platform detection")
    print("=" * 30)
    print(f"  architecture : {report['platform']['arch']}")
    print(f"  bitness      : {report['platform']['bitness']}")
    print(f"  firmware     : {report['platform']['firmware']}")
    print(f"  os           : {report['platform']['os']}")
    print(f"  hypervisor   : {report['platform']['features']['hypervisor']}")
    print()
    print("Selected payloads:")
    for pid in report["selected_payloads"]:
        print(f"  - {pid}")
    print()
    if report["staged"]:
        action = "would stage" if args.dry_run else "staged"
        print(f"{action.capitalize()} artifacts:")
        for s in report["staged"]:
            print(f"  - {s['id']} -> {s['path']} (verified)")

    if report["installed"]:
        print(f"Installed {len(report['installed'])} payload(s) to {args.install_to}:")
        for i in report["installed"]:
            print(f"  - {i['id']} -> {i['target']} (verified)")

    if report["failed"]:
        print("Failed:")
        for f in report["failed"]:
            print(f"  - {f['id']}: {f['error']}")
    print()
    print(f"Report written to {args.report}")

    return 0 if not report["failed"] else 1


if __name__ == "__main__":
    sys.exit(main())
