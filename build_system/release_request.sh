#!/usr/bin/env bash
# release_request.sh — build host, NO KEY REQUIRED.
#
# Emits a small canonical text file listing every release artifact by SHA-256.
# That request — not the multi-hundred-megabyte artifacts — is what you carry to
# the air-gapped machine to be signed. Signing the digest list is equivalent to
# signing the artifacts (SHA-256 is collision-resistant), so the root key never
# has to see the payloads and the payloads never have to leave the build host.
#
#   build_system/release_request.sh <version> <artifact-dir> [out.request]
#
# The format is deterministic (LC_ALL=C sorted, fixed field order) so the same
# inputs always produce byte-identical requests — which makes the signature
# reproducible and lets anyone re-derive the request to check it.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -euo pipefail

VER="${1:-}"; DIR="${2:-}"; OUT="${3:-}"
[ -n "$VER" ] && [ -d "${DIR:-}" ] || {
  echo "usage: $0 <version> <artifact-dir> [out.request]" >&2; exit 2; }
OUT="${OUT:-zxv-release-$VER.request}"

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

# Artifacts we release. Anything not matched is NOT signed and must not ship.
# (Kept explicit rather than "everything in the dir" so a stray file can never
# be silently blessed by the root key.)
patterns='zxv-universal.img zxv-universal.iso BOOTX64.EFI BOOTAA64.EFI
BOOTRISCV64.EFI BOOTARM.EFI kernel_arm64.bin kernel_x86_64.bin
kernel_riscv64.bin kernel_riscv32.bin kernel_arm32.bin'

tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
count=0
for p in $patterns; do
  f="$DIR/$p"
  [ -f "$f" ] || continue
  printf '%s  %s  %s\n' "$(sha256 "$f")" "$(wc -c < "$f" | tr -d ' ')" "$p" >> "$tmp"
  count=$((count+1))
done
[ "$count" -gt 0 ] || { echo "release_request: no release artifacts found in $DIR" >&2; exit 1; }

{
  echo "ZXV-RELEASE-REQUEST-1"
  echo "version: $VER"
  echo "created: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "artifacts: $count"
  echo "--"
  LC_ALL=C sort -k3 "$tmp"
} > "$OUT"

echo "wrote $OUT ($count artifacts)"
echo "  carry THIS file to the air-gapped machine and run:"
echo "    build_system/sign_release_offline.sh $(basename "$OUT") <root_priv.pem>"
cat "$OUT"
