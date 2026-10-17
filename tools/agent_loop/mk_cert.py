#!/usr/bin/env python3
# mk_cert.py — the verification certificate. Every compiled artifact gets
# a `.zxv_cert` ELF section binding it to the exact verification it
# passed: the build hash, the Merkle root of the proof traces, the
# test-to-code ratio, the invariants checked, and an ML-DSA signature
# over the header. The kernel's module loader reads the section and
# rejects anything that does not validate against the trust anchor.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_cert.py <binary> --ratio N --invariants N --traces DIR [--sign KEY]
                    [--emit-json]

Writes cert.bin (the packed header) and, when objcopy is available,
injects it as the .zxv_cert section. The signature uses the kernel's own
ML-DSA-87 (the same identity machinery the bearers use), so the anchor is
one cryptographic family, not two.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
CERT_MAGIC = b"ZXVCERT\x00"


def sha3_256(data):
    """The kernel's sha3_256 (the same used for the CIDs and the seal)."""
    try:
        h = hashlib.sha3_256()
        h.update(data)
        return h.digest()
    except Exception:
        return hashlib.sha256(data).digest()


def merkle_root(leaves):
    """The Merkle root of the proof traces: each trace hashed, then paired
    and folded, so the certificate binds every proof without carrying it."""
    if not leaves:
        return b"\x00" * 32
    level = [sha3_256(x) for x in leaves]
    while len(level) > 1:
        nxt = []
        for i in range(0, len(level), 2):
            a = level[i]
            b = level[i + 1] if i + 1 < len(level) else a
            nxt.append(sha3_256(a + b))
        level = nxt
    return level[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--ratio", type=int, default=0)
    ap.add_argument("--invariants", type=int, default=0)
    ap.add_argument("--traces", default="")
    ap.add_argument("--sign", default="")
    ap.add_argument("--emit-json", action="store_true")
    args = ap.parse_args()

    # the build hash: the compiler environment plus the binary's own bytes
    with open(args.binary, "rb") as f:
        bin_bytes = f.read()
    env = subprocess.run(["cc", "--version"], capture_output=True).stdout[:256]
    build_hash = sha3_256(env + bin_bytes)

    # the proof-trace Merkle root
    leaves = []
    if args.traces and os.path.isdir(args.traces):
        for f in sorted(os.listdir(args.traces)):
            p = os.path.join(args.traces, f)
            if os.path.isfile(p):
                leaves.append(open(p, "rb").read())
    proof_digest = merkle_root(leaves)

    # the header: magic(8) + build_hash(32) + proof_digest(32) + ratio(4)
    # + invariants(4) + the signature slot (ML-DSA-87: 4627 bytes)
    header = (CERT_MAGIC + build_hash + proof_digest +
              struct.pack("<II", args.ratio, args.invariants))

    # the signature: ML-DSA-87 over the header when a key is given.
    # Without a key the slot is zero and the certificate is unsigned
    # (the loader then requires a signed build, so this is a build-time
    # gate, not a runtime weakening).
    sig = b"\x00" * 4627
    signed = False
    if args.sign and os.path.exists(args.sign):
        try:
            out = subprocess.run([os.path.join(ROOT, "build/zxv_sign"),
                                  args.sign, "-"], input=header,
                                 capture_output=True)
            if out.returncode == 0 and len(out.stdout) == 4627:
                sig = out.stdout
                signed = True
        except OSError:
            pass
    blob = header + sig

    cert = os.path.splitext(args.binary)[0] + ".cert"
    with open(cert, "wb") as f:
        f.write(blob)

    injected = False
    if subprocess.run(["sh", "-c", "command -v objcopy || command -v llvm-objcopy"],
                      capture_output=True).returncode == 0:
        tool = "llvm-objcopy"
        if subprocess.run(["sh", "-c", "command -v llvm-objcopy"],
                          capture_output=True).returncode != 0:
            tool = "objcopy"
        r = subprocess.run([tool, "--add-section", ".zxv_cert=%s" % cert, args.binary],
                           capture_output=True)
        injected = r.returncode == 0

    report = {
        "binary": os.path.basename(args.binary),
        "build_hash": build_hash.hex(),
        "proof_digest": proof_digest.hex(),
        "traces": len(leaves),
        "ratio": args.ratio,
        "invariants": args.invariants,
        "signed": signed,
        "injected": injected,
        "cert_bytes": len(blob),
    }
    if args.emit_json:
        print(json.dumps(report))
    else:
        print("== the verification certificate ==")
        for k, v in report.items():
            print("  %-14s %s" % (k, v))
        print("  the kernel's loader reads .zxv_cert and rejects an")
        print("  unsigned or mismatched module before it executes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())