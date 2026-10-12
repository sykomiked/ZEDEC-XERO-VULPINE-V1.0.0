#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_apps.sh — build libzxv (incrementally) and run test_apps.c: the app
# models (Burrow, Quillon, Glint, Spindle, the utilities, Scent, Platen, Keepsake) against zxvd's dispatcher in
# process. Exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
H="$K/arch/hosted"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-apps.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$HERE" -I"$H" -I"$K/src/web4" -I"$K/src/print" -I"$K/src/robin_debanks" -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" -I"$K/src/surplus" -I"$R/sdk/libzxv/include" \
    "$HERE/test_apps.c" "$HERE/zxm.c" "$HERE/burrow_model.c" "$HERE/quillon_model.c" "$HERE/glint_model.c" "$HERE/spindle_model.c" "$HERE/almanac_model.c" "$HERE/wake_model.c" "$HERE/tidewatch_model.c" "$HERE/sounding_model.c" "$HERE/scent_model.c" "$HERE/platen_model.c" "$HERE/keepsake_model.c" \
    "$H/zxvd_rpc.c" "$H/zxv_files.c" "$H/zxv_spaces.c" "$H/zxv_sysinfo.c" "$H/zxvd_log.c" "$H/zxv_search.c" "$H/zxv_print_host.c" "$H/zxv_keepsake.c" "$K/src/print/ipp.c" "$K/src/print/escl.c" "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" \
    "$K/src/mlkem/keccak.c" "$K/src/robin_debanks/sha256.c" \
    -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_apps" || exit 3
"$T/test_apps"
