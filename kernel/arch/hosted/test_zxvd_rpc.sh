#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_rpc.sh — build libzxv (incrementally) and run test_zxvd_rpc.c
# against zxvd_rpc.c, then test_zxvd_rpc_modules.c (the module methods). The
# mutation harness's test command for zxvd_rpc.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-rpc.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c), so both
# tests link them, from the archive zxvd_mod_archive.sh builds once and reuses.
# shellcheck disable=SC2086 # RPCINC is a list of -I flags without spaces
cc_() {
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror -I"$HERE" -I"$K/src/web4" -I"$K/src/robin_debanks" \
        -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" \
        -I"$K/src/surplus" -I"$K/src/freight" -I"$K/src/harmonic" -I"$K/src/stream" -I"$K/src/ehop" \
        -I"$K/src/ipfs_node" -I"$K/src/pqsec" -I"$K/src/lpres" -I"$K/src/edp_risk" -I"$K/src/juris" \
        $RPCINC -I"$R/sdk/libzxv/include" "$@"
}
A="$(sh "$HERE/zxvd_mod_archive.sh")" || exit 3
for t in test_zxvd_rpc test_zxvd_rpc_modules; do
    # shellcheck disable=SC2086 # RPCDEPS is a list of paths without spaces
    cc_ "$HERE/$t.c" "$HERE/zxvd_rpc.c" $RPCDEPS "$A" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/$t" ||
        exit 3
done
"$T/test_zxvd_rpc"
"$T/test_zxvd_rpc_modules"
