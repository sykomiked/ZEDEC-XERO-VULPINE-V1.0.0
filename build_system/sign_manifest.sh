#!/bin/bash
# sign_manifest.sh — sign an install-bundle manifest with the OFFLINE root key so
# install/install_zxv.py accepts the bundle (it is fail-closed on an unsigned
# manifest — audit P0-2). Produces a detached raw Ed25519 signature at
# <manifest>.sig and drops the matching root_pub.pem beside the manifest.
#
#   build_system/sign_manifest.sh <manifest.json> <root_priv.pem>
#
# In a real release the private key is held offline (HSM / air-gapped) and this is
# run there, never on the build host with a committed dev key.
set -e
MANIFEST="$1"; KEY="$2"
[ -f "$MANIFEST" ] && [ -f "$KEY" ] || {
    echo "usage: sign_manifest.sh <manifest.json> <root_priv.pem>" >&2; exit 2; }

openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$MANIFEST" -out "$MANIFEST.sig"
[ "$(wc -c < "$MANIFEST.sig")" -eq 64 ] || {
    echo "sign_manifest: unexpected signature length" >&2; exit 1; }
openssl pkey -in "$KEY" -pubout -out "$(dirname "$MANIFEST")/root_pub.pem"

echo "signed manifest: $MANIFEST.sig"
echo "trust anchor:    $(dirname "$MANIFEST")/root_pub.pem"
