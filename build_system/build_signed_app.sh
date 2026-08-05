#!/bin/bash
# build_signed_app.sh — build the sample app, sign it under a root key,
# and regenerate the embeddable header (root pubkey + signed .zsp).
#
#   build_system/build_signed_app.sh      (run from 05_KERNEL/)
#
# The root private key lives at build_system/keys/root_priv.pem and is
# generated once if absent. In a real release the private key is held
# offline (HSM / air-gapped); it is NOT required to BUILD the kernel,
# only to (re)sign the app.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)   # 05_KERNEL/
cd "$here"

KEYS=build_system/keys
mkdir -p "$KEYS"
if [ ! -f "$KEYS/root_priv.pem" ]; then
    echo "[keys] generating root Ed25519 keypair (first run)"
    openssl genpkey -algorithm ed25519 -out "$KEYS/root_priv.pem" 2>/dev/null
fi
openssl pkey -in "$KEYS/root_priv.pem" -pubout -outform DER 2>/dev/null \
    | tail -c 32 > "$KEYS/root_pub.bin"

# 1) build the ELF app
( cd kernel/userapp && ./build_hello.sh >/dev/null )

# 2) sign it -> hello.zsp + kernel/userapp/hello_signed.h
python3 build_system/sign_package.py \
    kernel/userapp/hello.elf \
    "$KEYS/root_priv.pem" "$KEYS/root_pub.bin" \
    kernel/userapp/hello.zsp \
    kernel/userapp/hello_signed.h \
    hello

echo "signed app ready: kernel/userapp/hello.zsp + hello_signed.h"
