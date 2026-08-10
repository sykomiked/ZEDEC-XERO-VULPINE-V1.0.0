#!/bin/bash
# verify_banners.sh — a boot banner may only name a subsystem whose symbols are
# in the ELF. Phase 0 gate.
#
# WHY THIS EXISTS. Adding a file to KERNEL_SRCS is NECESSARY BUT NOT SUFFICIENT.
# The arm64 build uses -ffunction-sections -fdata-sections --gc-sections, so the
# linker DISCARDS any section nothing references. A module can compile, link,
# pass its tests, appear in the Makefile — and be absent from the binary.
# Measured 2026-08-09: tls/x25519/aead/record/handshake, oseq, zphi, e8, mixmat,
# modbind all had 0 symbols in kernel_arm64.elf immediately after being added to
# KERNEL_SRCS and a forced full rebuild. The .o files had 3/18/23/10 symbols each.
#
# So "linked" is not a claim about the build files. It is a claim about `nm`.
set -u
ELF=${1:-kernel_arm64.elf}
[ -f "$ELF" ] || { echo "verify_banners: no $ELF — build first"; exit 2; }
fail=0
check() {  # check <symbol-substring> <what the banner claims>
  n=$(nm "$ELF" 2>/dev/null | grep -ci "$1")
  if [ "$n" -eq 0 ]; then
    printf "  ABSENT  %-14s  banner claims: %s\n" "$1" "$2"; fail=$((fail+1))
  else
    printf "  present %-14s  (%s syms)  %s\n" "$1" "$n" "$2"
  fi
}
echo "verify_banners: $ELF"
# NOTE: check the symbol prefix the code ACTUALLY exports, not the marketing
# name. An earlier revision of this script grepped for "tls_", "chiglet" and
# "oseq_dag" -- none of which any of those modules export -- so it reported
# ABSENT for three subsystems that were present. A verifier that checks the
# wrong name is worse than none: it manufactures false failures and hides real
# ones. Every entry below was confirmed against `nm` output.
check x25519  "X25519 key agreement (RFC 7748)"
check chg_    "Chiglet inference runtime"
check oseq_   "causal ordering / happens-before"
check mlkem   "post-quantum key establishment (ML-KEM-768)"
check vault   "encrypted vault"
check zxvfs_  "persistent filesystem"
# Still absent by design -- NOT called anywhere, so gc-sections drops them.
# Listed so their absence stays visible rather than forgotten:
check hkdf    "TLS key schedule (NOT wired -- no caller)"
check aead    "TLS record encryption (NOT wired -- no caller)"
echo
if [ "$fail" -gt 0 ]; then
  echo "FAIL: $fail subsystem(s) announced but absent from the ELF."
  echo "Either the banner must go, or the subsystem must be CALLED — being in"
  echo "KERNEL_SRCS is not enough under --gc-sections."
  exit 1
fi
echo "PASS: every checked banner is backed by symbols in the binary."
