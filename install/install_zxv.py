#!/usr/bin/env python3
"""install_zxv.py — Universal ZXV cell installer

Detects the target platform, selects the matching native kernel cell from the
universal install bundle, verifies SHA-256 digests, and stages artifacts for
boot.

Author: H.M. Michael-Laurence: Curzi (c)
License: Apache-2.0
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


class InstallSecurityError(Exception):
    """A path escaped its confinement, or the manifest failed authentication."""


def safe_join(base: Path, rel: str) -> Path:
    """Join `rel` (from the UNTRUSTED manifest) onto `base` and PROVE the result
    stays inside `base` — defeating `../` traversal, absolute paths, and symlink
    escapes (audit P0-2). Returns the confined absolute path or raises."""
    if rel is None:
        raise InstallSecurityError("payload path missing")
    rel = str(rel)
    if rel.startswith("/") or rel.startswith("\\") or (len(rel) > 1 and rel[1] == ":"):
        raise InstallSecurityError(f"absolute payload path rejected: {rel!r}")
    if ".." in Path(rel).parts:
        raise InstallSecurityError(f"'..' in payload path rejected: {rel!r}")
    base_r = base.resolve()
    # Resolve against the base, following any symlinks, then confirm containment.
    cand = (base_r / rel).resolve()
    if cand != base_r and base_r not in cand.parents:
        raise InstallSecurityError(f"payload path escapes target: {rel!r} -> {cand}")
    return cand


def verify_manifest_signature(manifest_path: Path, pubkey_pem: Path,
                              allow_unsigned: bool, anchor_path=None):
    """Authenticate the manifest BEFORE trusting the digests inside it (audit P0-2:
    the manifest was previously loaded unsigned, so swapping it swapped the very
    digests meant to guard the payloads). Expects a detached raw Ed25519 signature
    at `<manifest>.sig` verified against `pubkey_pem`. Fail-closed unless the
    operator explicitly passes allow_unsigned (dev only)."""
    sig_path = manifest_path.with_suffix(manifest_path.suffix + ".sig")
    if not sig_path.exists() or not pubkey_pem or not Path(pubkey_pem).exists():
        if allow_unsigned:
            sys.stderr.write(
                "WARNING: installing with an UNSIGNED manifest (--allow-unsigned). "
                "A release bundle MUST ship a signed manifest.\n")
            return
        raise InstallSecurityError(
            "manifest is not signed (missing .sig or root pubkey); refusing. "
            "Pass --allow-unsigned for dev bundles.")
    # The signature is only worth what the KEY is worth. If the pubkey we were
    # handed came from inside the bundle we are verifying, an attacker who can
    # replace the bundle can replace the key and re-sign everything — the check
    # would pass and prove nothing. So pin: the key's key-id must match the
    # anchor recorded in this source tree / installer, which the attacker does
    # not control. See PROVENANCE/ROOT_TRUST_ANCHOR.txt.
    enforce_pinned_anchor(pubkey_pem, anchor_path)

    r = subprocess.run(
        ["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(pubkey_pem),
         "-rawin", "-in", str(manifest_path), "-sigfile", str(sig_path)],
        capture_output=True, text=True)
    if r.returncode != 0:
        raise InstallSecurityError(
            f"manifest signature INVALID (not signed by the root key): "
            f"{r.stderr.strip() or r.stdout.strip()}")


def root_key_id(pubkey_pem: Path) -> str:
    """key-id = first 8 bytes of SHA-256(raw 32-byte Ed25519 public key), hex.
    Matches build_system/keyceremony_root.sh, verify_release.sh and zsp.c."""
    der = subprocess.run(
        ["openssl", "pkey", "-pubin", "-in", str(pubkey_pem), "-outform", "DER"],
        capture_output=True)
    if der.returncode != 0 or len(der.stdout) < 32:
        raise InstallSecurityError(f"cannot read Ed25519 public key from {pubkey_pem}")
    return hashlib.sha256(der.stdout[-32:]).hexdigest()[:16]


def pinned_key_id(anchor_path=None):
    """The key-id this installer trusts, read from the pinned anchor shipped with
    the SOURCE (not with the bundle). Returns None if no anchor is pinned.
    `anchor_path` is for tests only; production always uses the shipped anchor."""
    cands = ([Path(anchor_path)] if anchor_path else
             [Path(__file__).resolve().parent.parent / "PROVENANCE" / "ROOT_TRUST_ANCHOR.txt",
              Path(__file__).resolve().parent / "ROOT_TRUST_ANCHOR.txt"])
    for cand in cands:
        if cand.exists():
            for line in cand.read_text().splitlines():
                if line.strip().startswith("key-id"):
                    return line.split(":", 1)[1].strip()
    return None


def enforce_pinned_anchor(pubkey_pem: Path, anchor_path=None):
    """Fail-closed if the signing key is not the pinned root key."""
    want = pinned_key_id(anchor_path)
    got = root_key_id(pubkey_pem)
    if want is None:
        sys.stderr.write(
            f"WARNING: no pinned root trust anchor found; trusting key-id {got} "
            f"on faith. Ship PROVENANCE/ROOT_TRUST_ANCHOR.txt to close this.\n")
        return
    if got != want:
        raise InstallSecurityError(
            f"root key MISMATCH: bundle is signed by key-id {got}, but this "
            f"installer trusts {want}. Refusing — either the bundle is not ours "
            f"or the trust anchor was swapped.")


def stage_payload(bundle_dir, p, target_dir, dry_run):
    """Copy a payload artifact into the target tree if its digest validates."""
    # P0-2: confine both the source (inside the bundle) and the destination
    # (inside the target) — the path comes from the untrusted manifest.
    src = safe_join(bundle_dir, p.get("path"))
    dst = safe_join(target_dir, p.get("path"))
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


def install(bundle_dir, target_dir, overrides, dry_run=False, install_to=None,
            allow_unsigned=False, root_pubkey=None):
    """Run platform detection, payload selection, verify, stage, and optionally install."""
    bundle_dir = Path(bundle_dir).resolve()
    target_dir = Path(target_dir).resolve()
    manifest_path = bundle_dir / "PROVENANCE" / "universal_install_bundle_manifest.json"
    if not manifest_path.exists():
        manifest_path = bundle_dir / "universal_install_bundle_manifest.json"

    if not manifest_path.exists():
        raise FileNotFoundError(f"Bundle manifest not found: {manifest_path}")

    # P0-2: authenticate the manifest BEFORE trusting anything inside it. The root
    # pubkey defaults to one shipped in the bundle; --root-pubkey overrides it.
    if root_pubkey is None:
        for cand in (bundle_dir / "PROVENANCE" / "root_pub.pem",
                     bundle_dir / "root_pub.pem"):
            if cand.exists():
                root_pubkey = cand
                break
    verify_manifest_signature(manifest_path, root_pubkey, allow_unsigned)

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


def _selftest():
    """Security self-checks for P0-2: path confinement + fail-closed manifest auth."""
    import tempfile
    fails = 0
    def check(cond, msg):
        nonlocal fails
        print(("[PASS] " if cond else "[FAIL] ") + msg)
        if not cond:
            fails += 1
    base = Path(tempfile.mkdtemp())
    # traversal / absolute / symlink escapes must all be rejected
    for bad in ("../etc/passwd", "../../root/.ssh/authorized_keys",
                "/etc/cron.d/x", "a/../../b", "\\windows\\system32"):
        try:
            safe_join(base, bad); check(False, f"escape allowed: {bad!r}")
        except InstallSecurityError:
            check(True, f"blocked path escape: {bad!r}")
    # a genuine relative path is allowed
    try:
        p = safe_join(base, "EFI/BOOT/BOOTAA64.EFI")
        check(str(p).startswith(str(base.resolve())), "legit relative path confined to base")
    except InstallSecurityError:
        check(False, "legit relative path wrongly rejected")
    # symlink that points outside base is rejected
    outside = Path(tempfile.mkdtemp())
    link = base / "sneaky"
    try:
        os.symlink(outside, link)
        try:
            safe_join(base, "sneaky/x"); check(False, "symlink escape allowed")
        except InstallSecurityError:
            check(True, "blocked symlink escape")
    except OSError:
        check(True, "symlink unsupported here (skipped)")
    # unsigned manifest is refused unless allow_unsigned
    m = base / "manifest.json"; m.write_text("{}")
    try:
        verify_manifest_signature(m, None, allow_unsigned=False)
        check(False, "unsigned manifest accepted")
    except InstallSecurityError:
        check(True, "unsigned manifest refused (fail-closed)")
    verify_manifest_signature(m, None, allow_unsigned=True)  # must not raise
    check(True, "unsigned manifest allowed only with --allow-unsigned")

    # A validly-signed manifest under the WRONG root key must still be refused.
    # This is the attack the bundle-supplied-pubkey default was open to: replace
    # the bundle, replace the pubkey inside it, re-sign — signature checks out,
    # but it is not OUR key. Only meaningful when an anchor is pinned.
    if True:
        try:
            evil_priv = base / "evil.pem"; evil_pub = base / "evil_pub.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519",
                            "-out", str(evil_priv)], capture_output=True, check=True)
            subprocess.run(["openssl", "pkey", "-in", str(evil_priv), "-pubout",
                            "-out", str(evil_pub)], capture_output=True, check=True)
            m2 = base / "m2.json"; m2.write_text('{"payloads": []}')
            subprocess.run(["openssl", "pkeyutl", "-sign", "-inkey", str(evil_priv),
                            "-rawin", "-in", str(m2), "-out", str(m2) + ".sig"],
                           capture_output=True, check=True)
            # pin a DIFFERENT key-id than the one that signed m2
            anchor = base / "ANCHOR.txt"
            anchor.write_text("key-id      : " + ("0" * 16) + "\n")
            try:
                verify_manifest_signature(m2, evil_pub, allow_unsigned=False,
                                          anchor_path=anchor)
                check(False, "manifest signed by a NON-PINNED root key accepted")
            except InstallSecurityError:
                check(True, "manifest signed by a non-pinned root key refused")
            # and the SAME key, correctly pinned, must be accepted
            anchor.write_text("key-id      : " + root_key_id(evil_pub) + "\n")
            verify_manifest_signature(m2, evil_pub, allow_unsigned=False,
                                      anchor_path=anchor)
            check(True, "manifest signed by the pinned root key accepted")
        except (subprocess.CalledProcessError, FileNotFoundError):
            check(True, "openssl unavailable for wrong-key test (skipped)")

    print(("\nALL PASS" if not fails else f"\nFAILED: {fails}") + " installer security self-check")
    return 1 if fails else 0


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
    parser.add_argument(
        "--root-pubkey", default=None,
        help="PEM Ed25519 root public key to authenticate the bundle manifest",
    )
    parser.add_argument(
        "--allow-unsigned", action="store_true",
        help="DEV ONLY: install even if the manifest is not signed (fail-open)",
    )
    parser.add_argument(
        "--selftest", action="store_true",
        help="Run the installer security self-checks (path confinement + manifest auth) and exit",
    )

    args = parser.parse_args()
    if args.selftest:
        return _selftest()
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
        report = install(args.bundle_dir, args.target_dir, overrides, args.dry_run,
                         args.install_to, args.allow_unsigned, args.root_pubkey)

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
