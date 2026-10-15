#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_aipi.sh — build libzxv and the aipi kit (incrementally) and run
# test_zxvd_aipi.c: zxvd_aipi.c behind zxvd_rpc.c, on the real AIPI core and
# the Z-Wasm sandbox. The mutation harness's test command for zxvd_aipi.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
A="$R/sdk/kits/aipi/build"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-aipi.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
make -s -C "$R/sdk/kits/aipi" CC="$CC" OUT="$A" all "$A/zcontract/libzcontract_wasm.a" \
    "$A/zcontract/libzcontract.a" "$A/zwasm/libzwasm.a" >"$T/kit.log" 2>&1 ||
    { tail -20 "$T/kit.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
# zxvd_rpc.c falls back to the module methods: link their archive.
M="$(sh "$HERE/zxvd_mod_archive.sh")" || exit 3
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -I"$HERE" -I"$K/src/web4" -I"$K/src/aipi" \
    -I"$K/include" -I"$K/src/robin_debanks" -I"$K/src/mlkem" -I"$K/src/e8" -I"$K/src/lpres" \
    -I"$K/src/event_space" -I"$K/src/modbind" -I"$K/src/surplus" -I"$K/src/juris" \
    -I"$R/sdk/libzxv/include" -I"$R/sdk/kits/aipi/include" -I"$R/sdk/kits/aipi/test" \
    -I"$R/sdk/kits/z-wasm/include" -I"$R/sdk/kits/z-wasm/test" \
    "$HERE/test_zxvd_aipi.c" "$HERE/zxvd_aipi.c" "$HERE/zxvd_rpc.c" "$HERE/zxv_files.c" "$HERE/zxv_spaces.c" "$K/src/web4/web4_json.c" \
    "$K/src/web4/web4_util.c" "$K/src/mlkem/keccak.c" \
    "$K/src/juris/juris.c" "$K/src/acct/acct.c" "$M" \
    "$A/libaipi_zwasm.a" "$A/libaipi_core.a" "$A/zcontract/libzcontract_wasm.a" \
    "$A/zcontract/libzcontract.a" "$A/zwasm/libzwasm.a" \
    -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_aipi" 2>"$T/cc.log" ||
    { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
(cd "$T" && ./test_zxvd_aipi)
