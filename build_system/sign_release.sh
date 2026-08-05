#!/usr/bin/env bash
# sign_release.sh — bind a release identity to a Tri-Space triad, OFFLINE.
#
# The kernel VERIFIES releases; it never signs. Signing happens here, off the
# target, with a root key that must live offline (an HSM or an air-gapped box).
# This produces the ZSP envelope whose signed payload IS the triad's seal, so
# zxpkg_verify_release() on the target accepts it as "the root key released THIS
# exact triad."
#
#   sign_release.sh <base.seal> <root-ed25519-key.pem> [out.zsp]
# writes <base>.zsp (136 bytes: ZSP header + the seal) and prints the raw root
# public key you compile into the kernel as the trust anchor.
#
# Generate a root key once, offline:
#   openssl genpkey -algorithm ed25519 -out root-ed25519-key.pem
set -euo pipefail
SEAL="${1:-}"; KEY="${2:-}"; OUT="${3:-}"
[ -f "$SEAL" ] && [ -f "$KEY" ] || { echo "usage: $0 <base.seal> <root-key.pem> [out.zsp]" >&2; exit 2; }
[ "$(wc -c < "$SEAL")" -eq 32 ] || { echo "sign_release: seal must be exactly 32 bytes" >&2; exit 1; }
base="${SEAL%.seal}"; OUT="${OUT:-$base.zsp}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

# ZSP preimage = magic "ZSP1" | payload_len(=32, LE) | sha256(seal)
printf 'ZSP1' > "$tmp/pre"
printf '\x20\x00\x00\x00' >> "$tmp/pre"                 # 32, little-endian
openssl dgst -sha256 -binary "$SEAL" >> "$tmp/pre"

# Ed25519 signature over the preimage (pure EdDSA, no prehash)
openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$tmp/pre" -out "$tmp/sig"
[ "$(wc -c < "$tmp/sig")" -eq 64 ] || { echo "sign_release: unexpected signature length" >&2; exit 1; }

# envelope = preimage(40) | sig(64) | payload(=the seal, 32) = 136 bytes
cat "$tmp/pre" "$tmp/sig" "$SEAL" > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"

# the raw 32-byte public key = last 32 bytes of the DER SubjectPublicKeyInfo
openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 > "$tmp/pub.raw"
echo "root public key (compile this into the kernel as the trust anchor):"
xxd -i < "$tmp/pub.raw" | sed 's/^/    /'
