#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_rpc.sh — build libzxv (incrementally) and run test_zxvd_rpc.c
# against zxvd_rpc.c. The mutation harness's test command for zxvd_rpc.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-rpc.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -I"$HERE" -I"$K/src/web4" -I"$K/src/robin_debanks" -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" -I"$K/src/surplus" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxvd_rpc.c" "$HERE/zxvd_rpc.c" "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" \
    "$K/src/mlkem/keccak.c" "$K/src/robin_debanks/sha256.c" \
    -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_rpc" || exit 3
"$T/test_zxvd_rpc"
