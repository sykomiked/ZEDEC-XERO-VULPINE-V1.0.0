#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_irc.sh — the IRC gateway (zxvd_irc.h, docs/IRC_GATEWAY.md):
#  1. test_zxv_irc.c: the protocol engine (zxv_irc.h E1-E9) with no network
#  2. test_zxvd_irc.c: irc.* behind the real RPC layer, over real sockets,
#     the repo's TLS client pinned to the gateway's certificate, ML-DSA-87
#     checks of every signed record, the bridge against the gateway itself
#  3. test_zxvd_irc.py: OpenSSL (Python's ssl) against the gateway inside a
#     real zxvd built by build_zxvd.sh (ZXVD=path reuses one), verifying the
#     pinned certificate and asking the swarm; OPENSSL=0 skips it
# Both C tests build with ASan/UBSan where the compiler has them. Exits 3
# when only the build failed. POSIX only.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
TMPB="${ZXV_TMP:-${TMPDIR:-/tmp}}"
T="$(mktemp -d "$TMPB/zxvd-irc.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' >"$T/s.c"
# shellcheck disable=SC2086
$CC $SAN "$T/s.c" -o "$T/s" 2>/dev/null || SAN=""
FLAGS="-std=c11 -O1 -g -Wall -Wextra"

# 1. the engine
# shellcheck disable=SC2086
$CC $FLAGS -Werror $SAN "$HERE/test_zxv_irc.c" "$HERE/zxv_irc.c" -o "$T/test_zxv_irc" 2>"$T/cc.log" ||
    { tail -20 "$T/cc.log"; exit 3; }
"$T/test_zxv_irc"

# 2. the group
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RPCDEPS="$(sh "$HERE/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$HERE/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
INCS="$RPCINC -I$HERE -I$K/src/web4 -I$K/src/tls -I$K/src/pqsec -I$K/src/lpres -I$K/src/robin_debanks
      -I$K/src/mlkem -I$K/include -I$K/src/modbind -I$K/src/e8 -I$K/src/event_space -I$R/sdk/libzxv/include"
set -- "$HERE/zxvd_rpc.c" $RPCDEPS "$HERE/zxv_http_guard.c" "$K/src/web4/web4_json.c" \
    "$K/src/web4/web4_util.c" "$K/src/mlkem/keccak.c" "$K/src/robin_debanks/sha256.c" \
    "$K/src/pqsec/pq_mldsa87.c" "$K/src/pqsec/mldsa/fips202.c"
for f in server handshake record aead hkdf x25519 selfsigned ecdsa ecdsa_sign bn sha512 digest rsa \
         x509 trust; do
    set -- "$@" "$K/src/tls/$f.c"
done
# -Werror for the gateway and its test only (gcc-11 warns in keccak.c)
mkdir -p "$T/o"
i=0
for f in "$@"; do
    i=$((i + 1))
    # shellcheck disable=SC2086
    $CC $FLAGS $SAN $INCS -c "$f" -o "$T/o/$i.o" 2>>"$T/cc.log" ||
        { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
done
# shellcheck disable=SC2086
$CC $FLAGS -Werror $SAN $INCS "$HERE/test_zxvd_irc.c" "$HERE/zxvd_irc.c" "$HERE/zxv_irc.c" "$T"/o/*.o \
    -L"$R/sdk/libzxv/build" -lzxv "-Wl,-rpath,$R/sdk/libzxv/build" -lm -o "$T/test_zxvd_irc" \
    2>"$T/cc.log" || { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
ZXV_TMP="$T" "$T/test_zxvd_irc"

# 3. OpenSSL against a real zxvd
if [ "${OPENSSL:-}" = 0 ] || ! python3 -c "import ssl" >/dev/null 2>&1; then
    echo "[SKIP] OpenSSL interop (needs python3 with ssl; OPENSSL=0 skips it)"
    exit 0
fi
Z="${ZXVD:-}"
if [ -z "$Z" ]; then
    sh "$HERE/build_zxvd.sh" "$T/zxvd" >"$T/build.log" 2>&1 || { tail -30 "$T/build.log"; exit 3; }
    Z="$T/zxvd/zxvd"
fi
python3 "$HERE/test_zxvd_irc.py" "$Z"
