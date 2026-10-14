#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_display_view.sh — build and run test_display_view.c: the Display
# settings panel (views/display.h) on its model, with the kernel's display
# modes (display/display.c) and the zui widget set. No library or service is
# needed. Exits 3 when only the build failed. CFLAGS adds flags (for example
# -fsanitize=address,undefined); CC picks the compiler.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
Z="$(cd "$HERE/.." && pwd)"
S="$(cd "$Z/.." && pwd)"
K="$(cd "$S/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-display-view.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -DTEST_HOST ${CFLAGS:-} -I"$HERE" -I"$Z" -I"$S/apps" \
    -I"$S/web4" -I"$K/include" -I"$S/modbind" -I"$S/font" \
    "$HERE/test_display_view.c" "$HERE/display.c" "$HERE/zv.c" "$Z/zui_widget.c" "$Z/zui_input.c" \
    "$Z/zui_present_fb.c" "$Z/zui_layout.c" "$Z/zui_canvas.c" "$Z/zui_progress.c" \
    "$S/font/truetype.c" "$S/display/display.c" \
    -o "$T/test_display_view" || exit 3
"$T/test_display_view"
