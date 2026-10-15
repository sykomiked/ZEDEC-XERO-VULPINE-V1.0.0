#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_zxv_desktop.sh OUT — build zxv-desktop (zxv_desktop_main.c): the
# hosted ZXV desktop in a window (zxv_desktop.h). On Linux it has the X11
# present layer, and Vulkan too when <vulkan/vulkan.h> is installed; on a Mac
# it has Metal. Exit 0 and write OUT, 1 when the compile fails, 3 when this
# system has no present layer to build (no X11 headers, not a Mac).
#
# build_zxv_desktop.sh --list srcs|incs prints the portable part (the desktop,
# zui, the app views and models, the font rasteriser, zxvd's socket client),
# one path per line from the repo root, for other builds that link the
# desktop in (macos/build_macos_shell.sh, test_zxv_desktop.sh). The present
# layers are not in it: each build adds its own.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"

srcs() {
    cat <<'EOF'
kernel/arch/hosted/zxv_desktop.c
kernel/arch/hosted/zui_present_host.c
macos/zxv_rpc_client.c
kernel/src/zui/zui_desk.c
kernel/src/zui/zui_wm.c
kernel/src/zui/zui_motion.c
kernel/src/zui/zui_widget.c
kernel/src/zui/zui_input.c
kernel/src/zui/zui_present_fb.c
kernel/src/zui/zui_layout.c
kernel/src/zui/zui_canvas.c
kernel/src/zui/zui_progress.c
kernel/src/zui/zui_emote.c
kernel/src/font/truetype.c
kernel/src/zui/views/zv.c
kernel/src/zui/views/burrow.c
kernel/src/zui/views/quillon.c
kernel/src/zui/views/glint.c
kernel/src/zui/views/spindle.c
kernel/src/zui/views/murmuration.c
kernel/src/apps/zxm.c
kernel/src/apps/burrow_model.c
kernel/src/apps/quillon_model.c
kernel/src/apps/glint_model.c
kernel/src/apps/spindle_model.c
kernel/src/apps/murmuration_model.c
kernel/src/apps/zxapp.c
kernel/src/swarm/swarm_emotion.c
kernel/src/swarm/swarm_budget.c
kernel/src/web4/web4_json.c
kernel/src/web4/web4_util.c
kernel/src/mlkem/keccak.c
kernel/src/robin_debanks/sha256.c
EOF
}

incs() {
    cat <<'EOF'
kernel/arch/hosted
macos
kernel/src/zui
kernel/src/zui/views
kernel/src/apps
kernel/include
kernel/src/font
kernel/src/web4
kernel/src/swarm
kernel/src/lpres
kernel/src/modbind
kernel/src/e8
kernel/src/event_space
kernel/src/surplus
kernel/src/mlkem
kernel/src/robin_debanks
EOF
}

if [ "${1:-}" = --list ]; then
    case "${2:-}" in
        srcs) srcs ;;
        incs) incs ;;
        *) echo "usage: $0 --list srcs|incs" >&2; exit 2 ;;
    esac
    exit 0
fi
[ $# -eq 1 ] || { echo "usage: $0 OUT  |  $0 --list srcs|incs" >&2; exit 2; }
OUT="$1"
CC="${CC:-cc}"
SRC="$(srcs | sed "s|^|$R/|")"
INC="$(incs | sed "s|^|-I$R/|")"
FONTDIR="-DZXV_FONT_DIR=\"$R/assets/fonts/ascw-golden\""
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror ${CFLAGS:-}"

case "$(uname -s)" in
    Darwin)
        # shellcheck disable=SC2086
        $CC $CFLAGS -fobjc-arc $INC "$FONTDIR" "$HERE/zxv_desktop_main.c" $SRC \
            "$HERE/zui_present_metal.m" \
            -framework Cocoa -framework Metal -framework QuartzCore -o "$OUT" || exit 1
        ;;
    *)
        if ! echo '#include <X11/Xlib.h>' | $CC -E - >/dev/null 2>&1; then
            echo "  no X11 headers (libx11-dev, libxext-dev): zxv-desktop is not built" >&2
            exit 3
        fi
        PRESENT="$HERE/zui_present_x11.c $HERE/zui_x11_window.c"
        LIBS="-lX11 -lXext -lm"
        DEFS=""
        if echo '#include <vulkan/vulkan.h>' | $CC -E - >/dev/null 2>&1; then
            PRESENT="$PRESENT $HERE/zui_present_vulkan.c"
            LIBS="$LIBS -ldl"
        else
            DEFS="-DZUI_HAVE_VULKAN=0" # X11 only
        fi
        # shellcheck disable=SC2086
        $CC $CFLAGS $DEFS $INC "$FONTDIR" "$HERE/zxv_desktop_main.c" $SRC $PRESENT $LIBS \
            -o "$OUT" || exit 1
        ;;
esac
echo "  built $OUT"
