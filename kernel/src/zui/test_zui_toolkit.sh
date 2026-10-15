#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# The zui toolkit's system pieces: accessibility, input, notices, privacy,
# clipboard, sharing, capture and the lock screen. Each line builds one test
# and runs it. Honours CC and CFLAGS; run from anywhere.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R"
CC=${CC:-cc}
F="-std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/zui -Isrc/font $CFLAGS"
O=${TMPDIR:-/tmp}/zui_toolkit.$$
mkdir -p "$O"
trap 'rm -rf "$O"' EXIT
Z=src/zui
UI="$Z/zui_widget.c $Z/zui_input.c $Z/zui_present_fb.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c"
DRAW="$Z/zui_canvas.c src/font/truetype.c"
run() { # name, extra flags, sources...
    n=$1
    x=$2
    shift 2
    # shellcheck disable=SC2086
    $CC $F $x "$@" -o "$O/$n" 2>"$O/$n.log" || { cat "$O/$n.log"; echo "[FAIL] $n (build)"; exit 1; }
    "$O/$n" >"$O/$n.out" 2>&1 || { cat "$O/$n.out"; echo "[FAIL] $n"; exit 1; }
    tail -1 "$O/$n.out"
}
run test_zui_palette "" $Z/test_zui_palette.c $Z/zui_palette.c
run test_zui_magnifier "" $Z/test_zui_magnifier.c $Z/zui_magnifier.c $Z/zui_layout.c $DRAW
run test_zui_reader "" $Z/test_zui_reader.c $Z/zui_reader.c $UI
run test_zui_keymap "" $Z/test_zui_keymap.c $Z/zui_keymap.c $Z/zui_input.c
run test_zui_ime "" $Z/test_zui_ime.c $Z/zui_ime.c $Z/zui_keymap.c $Z/zui_input.c
run test_zui_osk "" $Z/test_zui_osk.c $Z/zui_osk.c $Z/zui_keymap.c $Z/zui_input.c $Z/zui_layout.c $DRAW
run test_zui_gesture "" $Z/test_zui_gesture.c $Z/zui_gesture.c $Z/zui_input.c
run test_zui_notice "" $Z/test_zui_notice.c $Z/zui_notice.c $DRAW
run test_zui_privacy "" $Z/test_zui_privacy.c $Z/zui_privacy.c $DRAW
run test_zui_cliphist "" $Z/test_zui_cliphist.c $Z/zui_cliphist.c $UI
run test_zui_share "-Isrc/apps -Isrc/modbind" $Z/test_zui_share.c $Z/zui_share.c src/apps/zxapp.c src/robin_debanks/sha256.c
run test_zui_capture "" $Z/test_zui_capture.c $Z/zui_capture.c $Z/zui_wm.c $Z/zui_dock.c $Z/zui_motion.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c
run test_zui_lock "" $Z/test_zui_lock.c $Z/zui_lock.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c
run test_zui_settings "" $Z/test_zui_settings.c $Z/zui_settings.c $Z/zui_palette.c
run test_zui_workspace "" $Z/test_zui_workspace.c $Z/zui_workspace.c $Z/zui_settings.c $Z/zui_palette.c $Z/zui_wm.c $Z/zui_dock.c $Z/zui_motion.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c
M="$Z/zui_widget.c $Z/zui_input.c $Z/zui_present_fb.c $Z/zui_wm.c $Z/zui_dock.c $Z/zui_motion.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c"
run test_zui_switch "" $Z/test_zui_switch.c $Z/zui_switch.c $M
run test_zui_desk "" $Z/test_zui_desk.c $Z/zui_desk.c $M
run test_zui_voicectl "" $Z/test_zui_voicectl.c $Z/zui_voicectl.c $M
run test_zui_caption "" $Z/test_zui_caption.c $Z/zui_caption.c $Z/zui_motion.c $Z/zui_palette.c $Z/zui_canvas.c $Z/zui_layout.c $Z/zui_input.c src/font/truetype.c
run test_zui_motion "" $Z/test_zui_motion.c $Z/zui_motion.c
run test_zui_bidi "" $Z/test_zui_bidi.c $Z/zui_bidi.c
run test_zui_shape "" $Z/test_zui_shape.c $Z/zui_shape.c $Z/zui_bidi.c
run test_zui_locale "" $Z/test_zui_locale.c $Z/zui_locale.c $Z/zui_bidi.c $Z/zui_reader.c $UI
run test_zui_terms "-Isrc/terms -Isrc/robin_debanks -Isrc/modbind" $Z/test_zui_terms.c $Z/zui_terms.c src/terms/terms.c src/terms/terms_text.c src/robin_debanks/sha256.c $DRAW
run test_zui_dock "" $Z/test_zui_dock.c $Z/zui_dock.c $Z/zui_motion.c $Z/zui_layout.c $Z/zui_canvas.c $Z/zui_progress.c src/font/truetype.c
run test_zui_kinetic "" $Z/test_zui_kinetic.c $Z/zui_kinetic.c $Z/zui_motion.c
# zui_vec: the vector renderer; a 100 px band width exercises the band split (V3)
run test_zui_vec "-DZUI_VEC_MAX_W=100" $Z/test_zui_vec.c $Z/zui_vec.c $Z/zui_progress.c $DRAW
run test_zui_brand "" $Z/test_zui_brand.c $Z/zui_brand.c $Z/zui_vec.c $Z/zui_progress.c $Z/zui_layout.c $DRAW
sh $Z/views/test_display_view.sh >"$O/display.out" 2>&1 || { cat "$O/display.out"; echo "[FAIL] test_display_view"; exit 1; }
tail -1 "$O/display.out"
# Studio's Swift theme is generated from zui_tokens.h; it must not drift.
python3 ../tools/zui/gen_theme_tokens.py --check
