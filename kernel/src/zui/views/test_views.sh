#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_views.sh — build libzxv and the apl-kron and ada-eve pillars (incrementally) and run
# test_views.c: the five built-in apps' native screens on their models and
# zxvd's dispatcher in process, with Murmuration's KRON and Eve logic. Exits 3 when
# only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
Z="$(cd "$HERE/.." && pwd)"
S="$(cd "$Z/.." && pwd)"
K="$(cd "$S/.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
H="$K/arch/hosted"
A="$S/apps"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-views.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
make -s -C "$R/sdk/pillars/apl-kron" CC="$CC" build/libzkron.a >"$T/kron.log" 2>&1 || { tail -20 "$T/kron.log"; exit 3; }
make -s -C "$R/sdk/pillars/ada-eve" CC="$CC" build/libzeve.a >"$T/eve.log" 2>&1 || { tail -20 "$T/eve.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$HERE" -I"$Z" -I"$A" -I"$H" -I"$K/include" -I"$S/font" \
    -I"$S/web4" -I"$S/robin_debanks" -I"$S/mlkem" -I"$S/modbind" -I"$S/e8" -I"$S/event_space" -I"$S/surplus" \
    -I"$S/swarm" -I"$S/lpres" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/pillars/ada-eve/include" -I"$R/sdk/libzxv/include" \
    "$HERE/test_views.c" "$HERE/zv.c" "$HERE/burrow.c" "$HERE/quillon.c" "$HERE/glint.c" "$HERE/spindle.c" \
    "$HERE/murmuration.c" "$HERE/autoview.c" "$Z/zui_widget.c" "$Z/zui_input.c" "$Z/zui_present_fb.c" "$Z/zui_layout.c" \
    "$Z/zui_canvas.c" "$Z/zui_progress.c" "$Z/zui_emote.c" "$S/font/truetype.c" \
    "$A/zxm.c" "$A/burrow_model.c" "$A/quillon_model.c" "$A/glint_model.c" "$A/spindle_model.c" \
    "$A/murmuration_model.c" "$A/zxapp.c" "$A/zxapp_kron.c" "$A/zxapp_eve.c" "$S/swarm/swarm_emotion.c" "$S/swarm/swarm_budget.c" \
    "$H/zxvd_rpc.c" $RPCINC $RPCDEPS "$S/web4/web4_json.c" "$S/web4/web4_util.c" \
    "$S/mlkem/keccak.c" "$S/robin_debanks/sha256.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" "$R/sdk/pillars/ada-eve/build/libzeve.a" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lpthread -lm \
    -o "$T/test_views" || exit 3
"$T/test_views" "$S"
