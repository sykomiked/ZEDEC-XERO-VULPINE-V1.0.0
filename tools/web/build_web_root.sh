#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_web_root.sh OUT [--brand DIR] — put together the browser surface that
# zxvd serves at /web/ (zxvd --web-root OUT): the boot screen, then the
# passport login, the spaces and the companion.
#
#   OUT/index.html boot.js zxv_boot.wasm     the boot screen (web/boot)
#   OUT/brand/*-gold.zxvg                    the five logos, from --brand DIR
#                                            (the gold brand SVGs; the crest is
#                                            ascw-small.svg), else left out
#   OUT/session/index.html session.js passport_worker.js
#                                            the login, space and companion
#                                            (web/session)
#   OUT/session/zxv_passport.wasm            the passport, built here from
#                                            kernel/src/passport and ident
#   OUT/session/ASCW-Golden-Latin2.ttf       the face the session draws in
#   OUT/session/fox.svg                      the fox (kernel/src/zui/brand)
#   OUT/boot.json                            every file the boot streams, with
#                                            its size and SHA-256; next is the
#                                            login
#
# The boot screen counts the session's own files as the system it loads, so
# its bars move exactly as fast as the login really arrives, and a file that
# does not match its hash stops the boot before the login can run. The
# passport worker checks zxv_passport.wasm against boot.json again before it
# runs it, because the login page fetches its own copy.
#
# zxv_passport.wasm is built here, not committed: the root is sealed when it
# is put together (boot.json holds each file's hash), so any clang with the
# wasm32 target builds a root that checks itself. Needs clang, wasm-ld and
# python3.
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
K="$R/kernel"
OUT=${1:?usage: build_web_root.sh OUT [--brand DIR]}
shift
BRAND=""
while [ $# -gt 0 ]; do
    case "$1" in
    --brand)
        [ -n "${2:-}" ] || { echo "--brand needs a folder" >&2; exit 2; }
        BRAND=$2
        shift 2
        ;;
    *) echo "usage: build_web_root.sh OUT [--brand DIR]" >&2; exit 2 ;;
    esac
done
CLANG=${CLANG:-clang}
mkdir -p "$OUT/session" "$OUT/brand"

cp "$R/web/boot/index.html" "$R/web/boot/boot.js" "$R/web/boot/zxv_boot.wasm" "$OUT/"
cp "$R/web/session/index.html" "$R/web/session/session.js" "$R/web/session/passport_worker.js" \
    "$OUT/session/"
cp "$R/assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf" "$R/kernel/src/zui/brand/fox.svg" \
    "$OUT/session/"

# The passport: pp_core (seed text, passphrase, identity) on ident's root
# keys (ML-DSA-87 + SLH-DSA-SHAKE-256s through pq_matrix), with the four
# memory functions from the glue and nothing else from a C library.
M="$K/src/pqsec/mldsa"
S="$K/src/pqsec/slhdsa"
"$CLANG" --target=wasm32 -mcpu=generic -std=c11 -O2 -ffreestanding -nostdlib \
    -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion \
    -I"$R/web/session/libc" -I"$K/include" -I"$K/src/passport" -I"$K/src/ident" \
    -I"$K/src/pqsec" -I"$K/src/mlkem" -I"$K/src/lpres" -I"$K/src/surplus" -I"$K/src/edp_risk" \
    -I"$K/src/event_space" -I"$K/src/modbind" -I"$K/src/trispace" -I"$K/src/tls" \
    -Wl,--no-entry -Wl,--strip-all -Wl,--gc-sections -Wl,-z,stack-size=4194304 \
    "$R/web/session/zxv_passport_wasm.c" "$K/src/passport/pp_core.c" \
    "$K/src/ident/id_argon2.c" "$K/src/ident/id_core.c" "$K/src/mlkem/keccak.c" \
    "$K/src/pqsec/pq_matrix.c" "$K/src/pqsec/pq_mlkem1024.c" "$K/src/pqsec/pq_mldsa87.c" \
    "$K/src/pqsec/pq_hqc5.c" "$K/src/pqsec/pq_slh256s.c" "$K/src/pqsec/pq_mldsa65.c" \
    "$M/ntt.c" "$M/packing.c" "$M/poly.c" "$M/polyvec.c" "$M/reduce.c" "$M/rounding.c" \
    "$M/sign.c" "$M/symmetric-shake.c" "$M/fips202.c" \
    "$S/sha3_api.c" "$S/sha3_f1600.c" "$S/slh_dsa.c" "$S/slh_shake.c" \
    "$K/src/tls/x25519.c" "$K/src/robin_debanks/sha256.c" \
    -o "$OUT/session/zxv_passport.wasm"

# the logos, when the brand vectors are at hand
if [ -n "$BRAND" ]; then
    for k in 36n9 acoto smic cis ascw; do
        src="$BRAND/$k.svg"
        [ "$k" = ascw ] && src="$BRAND/ascw-small.svg"
        if [ -f "$src" ]; then
            python3 "$R/tools/zxvg/svg2zxvg.py" "$src" "$OUT/brand/$k-gold.zxvg" >/dev/null
        else
            echo "no $src: its tile stays empty" >&2
        fi
    done
fi

# Every file but the pages: zxvd puts the window's token plumbing into each
# page it serves (zxv_host.c send_web), so a page never matches its file.
python3 "$R/tools/web/make_boot_manifest.py" "$OUT" --next session/index.html \
    session/session.js session/passport_worker.js \
    session/zxv_passport.wasm session/ASCW-Golden-Latin2.ttf session/fox.svg
