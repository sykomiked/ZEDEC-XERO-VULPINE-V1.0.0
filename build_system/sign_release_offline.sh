#!/usr/bin/env bash
# sign_release_offline.sh — AIR-GAPPED MACHINE. Sign a release request with the
# offline root key.
#
# The whole point of an air-gapped ceremony is that a HUMAN looks at what is
# about to be signed, so this prints the request in full and requires an explicit
# confirmation before the key is touched. A signing step you always say yes to
# without reading provides no protection that an online key would not.
#
#   build_system/sign_release_offline.sh <file.request> <root_priv.pem> [out.sig]
#
# Produces a detached 64-byte raw Ed25519 signature over the EXACT request bytes.
# Carry back only the .sig (and the request, if it did not come from the build
# host). The private key never leaves this machine.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -euo pipefail

REQ="${1:-}"; KEY="${2:-}"; OUT="${3:-}"
[ -f "${REQ:-}" ] && [ -f "${KEY:-}" ] || {
  echo "usage: $0 <file.request> <root_priv.pem> [out.sig]" >&2; exit 2; }
OUT="${OUT:-$REQ.sig}"

red()  { printf '\033[1;31m%s\033[0m\n' "$*"; }
grn()  { printf '\033[1;32m%s\033[0m\n' "$*"; }
cyan() { printf '\033[1;36m%s\033[0m\n' "$*"; }

head -1 "$REQ" | grep -q '^ZXV-RELEASE-REQUEST-1$' || {
  red "not a ZXV release request: $REQ"; exit 1; }

# A root key with no passphrase is a root key that anyone who touches the media
# owns. This also catches the obvious accident of pointing this at the in-tree
# development key (build_system/keys/root_priv.pem), which is unencrypted and
# must never sign a release.
head -1 "$KEY" | grep -q 'ENCRYPTED PRIVATE KEY' || {
  red "REFUSING: $KEY is not passphrase-protected."
  echo "  A ZXV root key must be an encrypted PEM (keyceremony_root.sh makes one)."
  echo "  If this is the in-tree DEV key, it must never sign a release."
  exit 1; }

cyan "================ ABOUT TO SIGN — READ THIS ================"
cat "$REQ"
cyan "==========================================================="
echo "request sha256: $(openssl dgst -sha256 -hex < "$REQ" | awk '{print $NF}')"
echo
echo "Signing binds the root key to EXACTLY these digests. If any line is not"
echo "what you built, or there are artifacts you do not recognise, say no."
printf 'Sign this release? type exactly "sign": '
read -r answer
[ "$answer" = "sign" ] || { red "aborted — nothing signed"; exit 1; }

# Pure Ed25519 (no prehash) over the raw request bytes. Prompts for the key
# passphrase; the passphrase is never taken from argv or the environment.
openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$REQ" -out "$OUT"
[ "$(wc -c < "$OUT" | tr -d ' ')" -eq 64 ] || { red "unexpected signature length"; exit 1; }

grn "wrote $OUT (64-byte detached Ed25519 signature)"
echo "carry back: $OUT   (and $REQ if it did not come from the build host)"
echo "do NOT carry back: $KEY"
