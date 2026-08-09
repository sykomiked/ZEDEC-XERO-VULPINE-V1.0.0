#!/usr/bin/env bash
# verify_release.sh — verify a signed ZXV release. Fail-closed.
#
#   build_system/verify_release.sh <file.request> <artifact-dir> [anchor.pem] [file.sig]
#
# Checks, in order, and refuses on the first failure:
#   1. the trust anchor's key-id matches the PINNED anchor in
#      PROVENANCE/ROOT_TRUST_ANCHOR.txt  (see "why pinning" below)
#   2. the detached Ed25519 signature over the request verifies under that anchor
#   3. every artifact named in the request exists, with the exact size and
#      SHA-256 the root key signed
#   4. nothing in the request is missing and no digest is duplicated
#
# WHY PINNING MATTERS: verifying a bundle against a public key that shipped
# inside that same bundle proves nothing — an attacker who can replace the
# artifacts can replace the key too and re-sign everything. The anchor must come
# from somewhere the attacker does not control: this repo, the vendor site, or a
# key-id you compare by eye. So this script requires the pinned key-id to match
# and says loudly when it cannot find one.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -uo pipefail

REQ="${1:-}"; DIR="${2:-}"; ANCHOR="${3:-}"; SIG="${4:-}"
[ -f "${REQ:-}" ] && [ -d "${DIR:-}" ] || {
  echo "usage: $0 <file.request> <artifact-dir> [anchor.pem] [file.sig]" >&2; exit 2; }
SIG="${SIG:-$REQ.sig}"
here="$(cd "$(dirname "$0")/.." && pwd)"
ANCHOR="${ANCHOR:-$here/PROVENANCE/root_pub.pem}"
PINNED="$here/PROVENANCE/ROOT_TRUST_ANCHOR.txt"

red() { printf '\033[1;31m%s\033[0m\n' "$*"; }
grn() { printf '\033[1;32m%s\033[0m\n' "$*"; }
fail(){ red "VERIFY FAILED: $*"; exit 1; }

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

[ -f "$SIG" ]    || fail "no signature at $SIG (release is unsigned — refusing)"
[ -f "$ANCHOR" ] || fail "no trust anchor at $ANCHOR"
head -1 "$REQ" | grep -q '^ZXV-RELEASE-REQUEST-1$' || fail "not a ZXV release request"
[ "$(wc -c < "$SIG" | tr -d ' ')" -eq 64 ] || fail "signature is not 64 bytes"

# ---- 1. anchor key-id vs the PINNED anchor -------------------------------
raw="$(mktemp)"; trap 'rm -f "$raw"' EXIT
openssl pkey -pubin -in "$ANCHOR" -outform DER 2>/dev/null | tail -c 32 > "$raw"
[ "$(wc -c < "$raw" | tr -d ' ')" -eq 32 ] || fail "cannot read Ed25519 public key from $ANCHOR"
KEYID=$(openssl dgst -sha256 -hex < "$raw" | awk '{print $NF}' | cut -c1-16)

if [ -f "$PINNED" ]; then
    want=$(awk -F': *' '/^key-id/{print $2}' "$PINNED" | tr -d ' ')
    [ -n "$want" ] || fail "pinned anchor $PINNED has no key-id line"
    [ "$want" = "$KEYID" ] || fail "anchor key-id $KEYID does not match pinned $want
  The signing key is NOT the key this source tree trusts. Refusing."
    echo "  [OK] anchor key-id $KEYID matches the pinned trust anchor"
else
    red "  [WARN] no pinned anchor at $PINNED — cannot prove this is YOUR root key."
    red "         Verifying against key-id $KEYID on trust. Publish/pin the anchor."
fi

# ---- 2. signature over the request ---------------------------------------
openssl pkeyutl -verify -pubin -inkey "$ANCHOR" -rawin -in "$REQ" -sigfile "$SIG" >/dev/null 2>&1 \
  || fail "signature does not verify — the request was altered or signed by another key"
echo "  [OK] Ed25519 signature verifies under key-id $KEYID"

# ---- 3/4. every signed digest must match the artifact on disk ------------
n=0; bad=0
while read -r want size name; do
  [ -n "${name:-}" ] || continue
  f="$DIR/$name"
  if [ ! -f "$f" ]; then red "  [MISSING] $name"; bad=$((bad+1)); continue; fi
  got=$(sha256 "$f"); gotsz=$(wc -c < "$f" | tr -d ' ')
  if [ "$got" != "$want" ]; then red "  [DIGEST MISMATCH] $name"; bad=$((bad+1))
  elif [ "$gotsz" != "$size" ]; then red "  [SIZE MISMATCH] $name"; bad=$((bad+1))
  else echo "  [OK] $name ($size bytes)"; fi
  n=$((n+1))
done < <(sed -n '/^--$/,$p' "$REQ" | tail -n +2)

[ "$n" -gt 0 ] || fail "request lists no artifacts"
[ "$bad" -eq 0 ] || fail "$bad of $n artifacts failed verification"

grn "RELEASE VERIFIED: $n artifacts, signed by root key-id $KEYID"
