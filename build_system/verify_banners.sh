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
# PER-ARCH CHECK LISTS. A fixed list would be WRONG: 13 subsystems are arm64-only
# (syscall, zab, zxvfs_tri, display, zmedia, invproof, bombsquad, ...), so
# demanding them on riscv or arm32 would manufacture failures for capabilities
# those kernels never announce. That is the same defect as checking a symbol
# name the code does not export -- a verifier that cries wolf gets bypassed, and
# a bypassed gate protects nothing.
#
# Check the symbol prefix the code ACTUALLY exports, not the marketing name.
# Every arm64 entry below was confirmed against real `nm` output.
case "$ELF" in
  *arm64*)
    check x25519  "X25519 key agreement (RFC 7748)"
    check chg_    "Chiglet inference runtime"
    check oseq_   "causal ordering / happens-before"
    check mlkem   "post-quantum key establishment (ML-KEM-768)"
    check vault   "encrypted vault"
    check zxvfs_  "persistent filesystem"
    check hkdf    "TLS 1.3 key schedule"
    check aead    "TLS record AEAD (ChaCha20-Poly1305)"
    check e8_     "E8 lattice from the icosians"
    check zphi_   "exact Z[phi] golden integers"
    check mixmat_ "exact rational mixing matrices"
    check modbind_ "module construction rules"
    check ddna_   "phi-proportioned integrity checksum"
    ;;
  *)
    # PROVISIONAL. These four arches have not been built on this host (no
    # riscv backend, x86_64 needs nasm), so their banner sets are UNVERIFIED.
    # Start from the core every kernel_main announces and widen only once each
    # has actually been built and its banners read. Understating here is the
    # safe direction: a missing check fails to catch drift, whereas a wrong
    # check breaks a build that was fine.
    check mlkem   "post-quantum key establishment (ML-KEM-768)"
    echo "  NOTE: $ELF uses the PROVISIONAL check list -- widen it once this"
    echo "        arch has been built and its boot banners audited."
    ;;
esac
echo
if [ "$fail" -gt 0 ]; then
  echo "FAIL: $fail subsystem(s) announced but absent from the ELF."
  echo "Either the banner must go, or the subsystem must be CALLED — being in"
  echo "KERNEL_SRCS is not enough under --gc-sections."
  exit 1
fi
echo "PASS: every checked banner is backed by symbols in the binary."
