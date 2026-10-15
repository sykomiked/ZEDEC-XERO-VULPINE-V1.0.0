#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_apps.sh — build libzxv (incrementally) and run test_apps.c: the app
# models (the Banker, Quillon, Glint, Spindle, Murmuration, the utilities, Scent,
# Platen, Keepsake, Wake, Shepherd, Waymark) against zxvd's dispatcher in
# process. Exits 3 when only the build failed.
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
H="$K/arch/hosted"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-apps.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
# one model test file per app (tests/<id>_apps.inc): its model, and the index that includes them all
ROWS=""
MODELS=""
: >"$T/apps_tests.inc"
for f in "$HERE"/tests/*_apps.inc; do
    [ -e "$f" ] || continue
    id="$(basename "$f" _apps.inc)"
    echo "#include \"tests/${id}_apps.inc\"" >>"$T/apps_tests.inc"
    ROWS="$ROWS APPS_TEST_ROW($id)"
    MODELS="$MODELS $HERE/${id}_model.c"
done
echo "#define APPS_TESTS_ROWS$ROWS" >>"$T/apps_tests.inc"
# shellcheck disable=SC2086 # MODELS is a list of paths without spaces
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -DAPPS_TESTS_INDEX="\"$T/apps_tests.inc\"" -I"$HERE" -I"$H" -I"$K/src/web4" -I"$K/src/robin_debanks" -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" -I"$K/src/surplus" -I"$K/src/swarm" -I"$R/sdk/libzxv/include" \
    "$HERE/test_apps.c" "$HERE/zxm.c" "$HERE/burrow_model.c" "$HERE/quillon_model.c" "$HERE/glint_model.c" "$HERE/spindle_model.c" \
    "$HERE/murmuration_model.c" "$HERE/almanac_model.c" "$HERE/wake_model.c" "$HERE/tidewatch_model.c" "$HERE/sounding_model.c" "$HERE/scent_model.c" \
    "$HERE/platen_model.c" "$HERE/keepsake_model.c" "$HERE/shepherd_model.c" "$HERE/waymark_model.c" $MODELS "$K/src/swarm/swarm_emotion.c" "$K/src/swarm/swarm_budget.c" \
    "$H/zxvd_rpc.c" $RPCINC $RPCDEPS "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" \
    "$K/src/mlkem/keccak.c" "$K/src/robin_debanks/sha256.c" \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_apps" || exit 3
"$T/test_apps"
