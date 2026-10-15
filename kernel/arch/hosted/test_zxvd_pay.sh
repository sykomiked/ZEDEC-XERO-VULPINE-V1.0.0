#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_pay.sh — build libzxv (incrementally) and run test_zxvd_pay.c
# against zxvd_pay.c and zxvd_rpc.c, with ident's passkey check and the
# post-quantum code it links. Exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c): their
# archive goes last on the link line, after every object (zxvd_mod_archive.sh)
MODA="$(sh "$K/arch/hosted/zxvd_mod_archive.sh")" || exit 3
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-pay.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
I=""
for d in "$HERE" src/web4 src/robin_debanks src/mlkem src/pqsec src/ident include src/modbind src/e8 \
         src/event_space src/surplus src/lpres src/edp_risk src/trispace; do
    case "$d" in /*) I="$I -I$d" ;; *) I="$I -I$K/$d" ;; esac
done
S=""
for f in src/web4/web4_json.c src/web4/web4_util.c src/mlkem/keccak.c src/robin_debanks/sha256.c \
         src/ident/id_passkey.c src/ident/id_core.c src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c \
         src/pqsec/pq_matrix.c src/pqsec/pq_mlkem1024.c src/pqsec/pq_mldsa87.c src/pqsec/pq_hqc5.c \
         src/pqsec/pq_slh256s.c src/pqsec/slhdsa/[a-z]*.c src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c \
         src/mlkem/mlkem_sample.c src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c src/tls/x25519.c; do
    S="$S $K/$f"
done
# shellcheck disable=SC2086
"$CC" -std=c11 -O1 -Wall -Wextra -Werror $I -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxvd_pay.c" "$HERE/zxvd_pay.c" "$HERE/zxvd_rpc.c" $RPCINC $RPCDEPS $S \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_pay" || exit 3
"$T/test_zxvd_pay"
