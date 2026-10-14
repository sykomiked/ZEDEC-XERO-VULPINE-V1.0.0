#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_chat.sh — build libzxv (incrementally) and run test_zxvd_chat.c
# against zxvd_chat.c and zxvd_rpc.c. Exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-chat.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
I=""
for d in "$HERE" src/web4 src/robin_debanks src/mlkem src/pqsec src/social src/tensor src/reputation \
         include src/modbind src/e8 src/event_space src/surplus src/lpres src/edp_risk; do
    case "$d" in /*) I="$I -I$d" ;; *) I="$I -I$K/$d" ;; esac
done
S=""
for f in src/web4/web4_json.c src/web4/web4_util.c src/mlkem/keccak.c src/robin_debanks/sha256.c \
         src/social/social_post.c src/social/social_store.c src/social/social_feed.c \
         src/social/social_bucket.c src/social/social_group.c src/social/social_sign_mldsa.c \
         src/tensor/zt.c src/tensor/zt_isf.c src/reputation/reputation.c src/pqsec/pq_mldsa65.c \
         src/pqsec/mldsa/[a-z]*.c; do
    S="$S $K/$f"
done
# shellcheck disable=SC2086
"$CC" -std=c11 -O1 -Wall -Wextra -Werror $I -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxvd_chat.c" "$HERE/zxvd_chat.c" "$HERE/zxvd_rpc.c" $RPCINC $RPCDEPS "$HERE/zxv_http_guard.c" $S \
    -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_chat" || exit 3
"$T/test_zxvd_chat"
