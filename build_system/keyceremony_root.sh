#!/usr/bin/env bash
# keyceremony_root.sh — mint the ZXV OFFLINE ROOT KEY. Run this ONCE, on an
# air-gapped machine, and never anywhere else.
#
# WHY AIR-GAPPED IS NOT A FORMALITY
# ---------------------------------
# This key is the root of trust for every ZXV release: the kernel refuses code
# it does not sign, and the installer refuses bundles it does not sign. A root
# key that has ever existed on a networked, shared or rented machine (a cloud
# build box, a CI runner) must be considered compromised from birth — anyone who
# has ever had root on that host, or a copy of its disk image, can sign releases
# that every ZXV install on earth will accept. There is no way to walk that back
# except re-keying every deployed system.
#
# So: a laptop with the network physically off (or a live USB), generate, write
# the private key to encrypted removable media, and carry only root_pub.pem out.
#
#   build_system/keyceremony_root.sh <output-dir>
#
# Produces in <output-dir>:
#   root_priv.pem            the PRIVATE key, AES-256 encrypted under YOUR
#                            passphrase. NEVER copy this to a networked box.
#   root_pub.pem             the public trust anchor (safe to publish/commit)
#   root_pub.bin             the raw 32-byte public key (kernel trust anchor)
#   ROOT_TRUST_ANCHOR.txt    key-id + fingerprint to PIN and publish widely
#
# The day-to-day release flow never needs this key on the build host: the build
# host emits a digest-only signing request (release_request.sh), you carry that
# small file here, sign it (sign_release_offline.sh), and carry back a 64-byte
# signature. Artifacts never travel; the private key never leaves.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -euo pipefail

OUT="${1:-}"
[ -n "$OUT" ] || { echo "usage: $0 <output-dir>" >&2; exit 2; }

red()  { printf '\033[1;31m%s\033[0m\n' "$*"; }
grn()  { printf '\033[1;32m%s\033[0m\n' "$*"; }
cyan() { printf '\033[1;36m%s\033[0m\n' "$*"; }

# ---- 1. Refuse to run on an obviously networked machine -------------------
# Not a security boundary (nothing here can be), but it catches the most common
# and most damaging mistake: running the ceremony on the build server.
networked=0
if command -v ip >/dev/null 2>&1; then
    ip route show default 2>/dev/null | grep -q . && networked=1
elif command -v route >/dev/null 2>&1; then
    route -n get default >/dev/null 2>&1 && networked=1
fi
if [ "$networked" = 1 ]; then
    red "REFUSING: this machine has a default route — it is on a network."
    echo
    echo "  The ZXV root key must be generated air-gapped. Disable networking"
    echo "  (turn off Wi-Fi, unplug ethernet) and run this again."
    echo
    echo "  If you have deliberately accepted the risk (e.g. a disposable VM"
    echo "  for a TEST key that will never sign a real release), re-run with:"
    echo "      ZXV_ALLOW_NETWORKED=1 $0 $OUT"
    [ "${ZXV_ALLOW_NETWORKED:-0}" = "1" ] || exit 1
    red "ZXV_ALLOW_NETWORKED=1 set — proceeding. This key is NOT production-grade."
fi

command -v openssl >/dev/null || { red "openssl not found"; exit 1; }
# LibreSSL (stock macOS) cannot do Ed25519 raw sign; require real OpenSSL.
openssl genpkey -algorithm ed25519 -out /dev/null 2>/dev/null || {
    red "this openssl cannot generate Ed25519 keys ($(openssl version))"; exit 1; }

mkdir -p "$OUT"
PRIV="$OUT/root_priv.pem"
[ -e "$PRIV" ] && { red "REFUSING: $PRIV already exists — will not overwrite a root key."; exit 1; }

# ---- 2. Generate, encrypted under a passphrase the operator types ---------
cyan "==> generating Ed25519 root key (you will be asked for a passphrase)"
echo "    Use a long, unique passphrase. If you lose it the key is gone; if"
echo "    someone else learns it, they can sign releases as you."
umask 077
openssl genpkey -algorithm ed25519 -aes256 -out "$PRIV"
chmod 400 "$PRIV"

# ---- 3. Derive the public trust anchor + key id ---------------------------
# Touch the private key exactly ONCE (one passphrase prompt): export the public
# half, then derive everything else from that public PEM — no passphrase needed.
openssl pkey -in "$PRIV" -pubout -out "$OUT/root_pub.pem"
# raw 32-byte public key = last 32 bytes of the DER SubjectPublicKeyInfo
openssl pkey -pubin -in "$OUT/root_pub.pem" -outform DER | tail -c 32 > "$OUT/root_pub.bin"
[ "$(wc -c < "$OUT/root_pub.bin")" -eq 32 ] || { red "bad public key length"; exit 1; }

# key_id = first 8 bytes of SHA-256(raw pubkey)  (matches zsp.c / sign_package.py)
FULL=$(openssl dgst -sha256 -hex < "$OUT/root_pub.bin" | awk '{print $NF}')
KEYID=${FULL:0:16}
FPR=$(printf '%s' "$FULL" | sed 's/\(..\)/\1:/g; s/:$//' | tr 'a-f' 'A-F')

{
  echo "ZXV ROOT TRUST ANCHOR"
  echo "====================="
  echo
  echo "key-id      : $KEYID"
  echo "sha256(pub) : $FPR"
  echo "algorithm   : Ed25519"
  echo "created     : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo "PIN THIS. Publish the key-id widely (site, README, social, print it)."
  echo "Verification must compare against a PINNED copy of this anchor — never"
  echo "against a public key that shipped inside the artifact being verified,"
  echo "which would let anyone who replaces the artifact also replace the key."
} > "$OUT/ROOT_TRUST_ANCHOR.txt"

grn "==> root key ceremony complete"
cat "$OUT/ROOT_TRUST_ANCHOR.txt"
echo
cyan "CARRY OUT of the air-gapped machine (public, safe):"
echo "    $OUT/root_pub.pem"
echo "    $OUT/root_pub.bin"
echo "    $OUT/ROOT_TRUST_ANCHOR.txt"
red   "NEVER COPY OFF (private):"
echo "    $PRIV   <- encrypted removable media / safe. Back it up; losing it"
echo "                 means you can never sign another release for existing"
echo "                 installs."
