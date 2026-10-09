#!/usr/bin/env bash
# test_release_signing.sh — self-test for the offline-root release chain.
#
# Proves the verifier actually refuses the attacks it claims to stop. A signing
# pipeline that only ever gets tested on the happy path is indistinguishable
# from one that returns "OK" unconditionally, so every case here is a NEGATIVE
# case except the first.
#
# Uses a throwaway key generated in a temp dir — never the production root key,
# which by design cannot be reached from an automated test (it lives offline).
#
#   bash build_system/test_release_signing.sh
#
# Author: H.M. Michael-Laurence: Curzi (c)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
ROOT="$(pwd)"

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
pass=0; fail=0
ok(){ echo "  [PASS] $1"; pass=$((pass+1)); }
no(){ echo "  [FAIL] $1"; fail=$((fail+1)); }
# expect_refuse <desc> <cmd...>
expect_refuse(){ d="$1"; shift; if "$@" >/dev/null 2>&1; then no "$d (WAS ACCEPTED)"; else ok "$d"; fi; }

command -v openssl >/dev/null || { echo "openssl missing — skipping"; exit 0; }
openssl genpkey -algorithm ed25519 -out "$T/k.pem" 2>/dev/null || {
  echo "this openssl cannot do Ed25519 — skipping"; exit 0; }

# ---- fixture: a tiny "release" + a throwaway signing key -------------------
mkdir -p "$T/art"
printf 'fake-bootx64-payload'  > "$T/art/BOOTX64.EFI"
printf 'fake-arm64-kernel'     > "$T/art/kernel_arm64.bin"
openssl pkey -in "$T/k.pem" -pubout -out "$T/pub.pem" 2>/dev/null
openssl pkey -pubin -in "$T/pub.pem" -outform DER 2>/dev/null | tail -c 32 > "$T/pub.bin"
KEYID=$(openssl dgst -sha256 -hex < "$T/pub.bin" | awk '{print $NF}' | cut -c1-16)
mkdir -p "$T/anchor"; printf 'key-id      : %s\n' "$KEYID" > "$T/anchor/ROOT_TRUST_ANCHOR.txt"

bash build_system/release_request.sh 0.0.0-test "$T/art" "$T/r.request" >/dev/null 2>&1 \
  || { echo "  [FAIL] release_request.sh did not produce a request"; exit 1; }
openssl pkeyutl -sign -inkey "$T/k.pem" -rawin -in "$T/r.request" -out "$T/r.request.sig" 2>/dev/null

# verify_release.sh pins against the REPO's anchor; point it at our fixture by
# running it from a throwaway tree that carries only the test anchor.
mkdir -p "$T/tree/build_system" "$T/tree/PROVENANCE"
cp build_system/verify_release.sh "$T/tree/build_system/"
cp "$T/anchor/ROOT_TRUST_ANCHOR.txt" "$T/tree/PROVENANCE/"
cp "$T/pub.pem" "$T/tree/PROVENANCE/root_pub.pem"
V="bash $T/tree/build_system/verify_release.sh"

echo "release signing chain:"
# 1. happy path
if $V "$T/r.request" "$T/art" >/dev/null 2>&1; then ok "valid signed release verifies"
else no "valid signed release REJECTED"; fi

# 2. tampered artifact
cp -r "$T/art" "$T/art2"; printf 'X' >> "$T/art2/BOOTX64.EFI"
expect_refuse "tampered artifact refused" $V "$T/r.request" "$T/art2"

# 3. tampered request (digest edited to match a swapped payload)
sed 's/^\([0-9a-f]\)/f/' "$T/r.request" > "$T/bad.request"; cp "$T/r.request.sig" "$T/bad.request.sig"
expect_refuse "tampered request refused" $V "$T/bad.request" "$T/art"

# 4. valid signature from a DIFFERENT root key (the substitution attack)
openssl genpkey -algorithm ed25519 -out "$T/evil.pem" 2>/dev/null
openssl pkey -in "$T/evil.pem" -pubout -out "$T/evil_pub.pem" 2>/dev/null
openssl pkeyutl -sign -inkey "$T/evil.pem" -rawin -in "$T/r.request" -out "$T/evil.sig" 2>/dev/null
expect_refuse "signature by a non-pinned root key refused" \
  $V "$T/r.request" "$T/art" "$T/evil_pub.pem" "$T/evil.sig"

# 5. unsigned release
cp "$T/r.request" "$T/uns.request"
expect_refuse "unsigned release refused" $V "$T/uns.request" "$T/art"

# 6. missing artifact
cp -r "$T/art" "$T/art3"; rm -f "$T/art3/kernel_arm64.bin"
expect_refuse "missing artifact refused" $V "$T/r.request" "$T/art3"

# 7. an unencrypted (passphrase-less) key must never sign a release
echo "sign" | bash build_system/sign_release_offline.sh "$T/r.request" "$T/k.pem" "$T/x.sig" >/dev/null 2>&1 \
  && no "unencrypted key was allowed to sign" || ok "unencrypted signing key refused"

echo
if [ "$fail" -eq 0 ]; then echo "ALL PASS release signing self-check ($pass checks)"; exit 0
else echo "FAILED: $fail of $((pass+fail)) release signing checks"; exit 1; fi
