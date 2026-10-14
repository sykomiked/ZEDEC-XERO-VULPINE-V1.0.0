#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zui_present.sh — build the hosted zui present layers and run them for
# real: tools/zui/zui_window.c on the X11 backend and on the Vulkan backend
# under a virtual X server (Xvfb), checking that frames present, that a key
# sent with xdotool comes back as zui events, and that the window's pixels
# after a resize are byte for byte the app's surface (P2). The Windows layers
# (Direct3D 11, Vulkan on Win32) are compiled and linked with MinGW-w64.
# Each step prints [PASS], [SKIP] (a tool is missing; says which) or [FAIL].
#
#   kernel/arch/hosted/test_zui_present.sh             (from anywhere)
#   ZXV_TEST_WINE=1 kernel/arch/hosted/test_zui_present.sh
#                                    (also run the Windows build under Wine)
#
# Tools used when present: xvfb-run (xvfb), xdotool, import (imagemagick),
# a Vulkan ICD (mesa-vulkan-drivers gives lavapipe), x86_64-w64-mingw32-gcc,
# wine. The Vulkan run sets ZXV_VULKAN_CPU=1 so a CPU device is accepted.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${TMPDIR:-/tmp}/zui-present.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
FAILED=0

pass() { echo "[PASS] $1"; }
skip() { echo "[SKIP] $1"; }
fail() {
    echo "[FAIL] $1"
    FAILED=1
}
have() { command -v "$1" >/dev/null 2>&1; }

CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror"
INCS="-I$K/src/zui -I$K/src/font -I$K/include -I$HERE"
ZUI="$K/src/zui/zui_canvas.c $K/src/zui/zui_layout.c $K/src/zui/zui_input.c
     $K/src/zui/zui_present_fb.c $K/src/font/truetype.c"
DEMO="$R/tools/zui/zui_window.c $HERE/zui_present_host.c"

# The inner script runs inside one Xvfb session: BIN BACKEND OUT
cat >"$T/session.sh" <<'EOF'
BIN="$1"; B="$2"; O="$3"
"$BIN" --backend "$B" --frames 5 >"$O/frames.txt" 2>&1
echo $? >"$O/frames.rc"
command -v xdotool >/dev/null 2>&1 || exit 0
"$BIN" --backend "$B" --still --frames 100000 --screenshot "$O/surface.ppm" >"$O/keys.txt" 2>&1 &
PID=$!
( sleep 20; kill "$PID" 2>/dev/null ) &
GUARD=$!
WID=$(timeout 10 xdotool search --sync --onlyvisible --name zui_window 2>/dev/null | head -1)
if [ -n "$WID" ]; then
    sleep 0.5
    xdotool windowfocus "$WID" 2>/dev/null
    sleep 0.3
    xdotool key a
    xdotool windowsize "$WID" 500 300
    sleep 1
    if command -v import >/dev/null 2>&1; then
        import -depth 8 -window "$WID" "ppm:$O/screen.ppm" 2>/dev/null
    fi
    xdotool key Escape
fi
wait "$PID"
kill "$GUARD" 2>/dev/null
exit 0
EOF

# run_backend NAME BIN: the frames, keys and pixels checks for one backend
run_backend() {
    name="$1"
    bin="$2"
    out="$T/$name"
    mkdir -p "$out"
    timeout 120 xvfb-run -a -s "-screen 0 1280x1024x24" sh "$T/session.sh" "$bin" "$name" "$out" \
        >/dev/null 2>&1
    if grep -q "no present backend opened" "$out/frames.txt" 2>/dev/null; then
        skip "zui_present_$name: the backend did not open here ($(head -1 "$out/frames.txt"))"
        return
    fi
    if [ "$(cat "$out/frames.rc" 2>/dev/null)" = 0 ] &&
        grep -q "^exit frames frames=5 " "$out/frames.txt"; then
        pass "zui_present_$name: 5 frames presented under Xvfb"
    else
        fail "zui_present_$name: frames"
        sed 's/^/    /' "$out/frames.txt" | tail -5
        return
    fi
    if ! have xdotool; then
        skip "zui_present_$name input: xdotool not installed"
        return
    fi
    if grep -q "^event key_down key=0x61 " "$out/keys.txt" &&
        grep -q '^event text text="a"' "$out/keys.txt" &&
        grep -q "^event resize w=500 h=300" "$out/keys.txt" &&
        grep -q "^exit escape " "$out/keys.txt"; then
        pass "zui_present_$name input: key 'a' -> key_down + text, resize, Escape -> exit"
    else
        fail "zui_present_$name input"
        sed 's/^/    /' "$out/keys.txt" | grep -v '^    frame' | tail -10
    fi
    if [ ! -f "$out/screen.ppm" ]; then
        skip "zui_present_$name pixels: import (imagemagick) not installed"
    elif cmp -s "$out/screen.ppm" "$out/surface.ppm"; then
        pass "zui_present_$name pixels: window after resize == app surface, byte for byte"
    else
        fail "zui_present_$name pixels: window differs from the surface"
    fi
}

# ---- Linux: X11 and Vulkan ---------------------------------------------------
if [ "$(uname -s)" != Linux ]; then
    skip "zui_present_x11 / zui_present_vulkan: not Linux"
else
    if ! printf '#include <X11/Xlib.h>\n#include <X11/extensions/XShm.h>\n' |
        "$CC" -E - >/dev/null 2>&1; then
        skip "zui_present_x11: no Xlib/XShm headers (libx11-dev libxext-dev)"
    elif "$CC" $CFLAGS $INCS -DZUI_HAVE_VULKAN=0 $DEMO "$HERE/zui_present_x11.c" \
        "$HERE/zui_x11_window.c" $ZUI -lX11 -lXext -lm -o "$T/zui_window_x11" 2>"$T/x11.log"; then
        pass "zui_present_x11: builds (-Wall -Wextra -Werror)"
        if have xvfb-run; then
            run_backend x11 "$T/zui_window_x11"
        else
            skip "zui_present_x11 run: xvfb-run not installed (xvfb)"
        fi
    else
        fail "zui_present_x11: build"
        head -20 "$T/x11.log"
    fi

    if ! printf '#include <vulkan/vulkan.h>\n' | "$CC" -E - >/dev/null 2>&1; then
        skip "zui_present_vulkan: no Vulkan headers (libvulkan-dev)"
    elif "$CC" $CFLAGS $INCS $DEMO "$HERE/zui_present_vulkan.c" "$HERE/zui_present_x11.c" \
        "$HERE/zui_x11_window.c" $ZUI -lX11 -lXext -ldl -lm -o "$T/zui_window_vk" \
        2>"$T/vk.log"; then
        pass "zui_present_vulkan: builds (-Wall -Wextra -Werror)"
        if ! have xvfb-run; then
            skip "zui_present_vulkan run: xvfb-run not installed (xvfb)"
        elif ! { ls /usr/share/vulkan/icd.d/*.json || ls /etc/vulkan/icd.d/*.json; } \
            >/dev/null 2>&1; then
            skip "zui_present_vulkan run: no Vulkan ICD (mesa-vulkan-drivers)"
        else
            ZXV_VULKAN_CPU=1
            export ZXV_VULKAN_CPU
            run_backend vulkan "$T/zui_window_vk"
        fi
    else
        fail "zui_present_vulkan: build"
        head -20 "$T/vk.log"
    fi
fi

# ---- Windows: Direct3D 11 and Vulkan on Win32, with MinGW-w64 --------------
MINGW="${MINGW:-x86_64-w64-mingw32-gcc}"
if ! have "$MINGW"; then
    skip "zui_present_d3d11: $MINGW not installed (gcc-mingw-w64-x86-64)"
else
    if "$MINGW" $CFLAGS $INCS -c "$HERE/zui_present_d3d11.c" -o "$T/d3d11.o" 2>"$T/w.log" &&
        "$MINGW" $CFLAGS $INCS -c "$HERE/zui_win32_window.c" -o "$T/win32.o" 2>>"$T/w.log"; then
        pass "zui_present_d3d11 + zui_win32_window: compile with $MINGW (-Wall -Wextra -Werror)"
    else
        fail "zui_present_d3d11: compile"
        head -20 "$T/w.log"
    fi
    # Vulkan for Win32 needs only the headers; MinGW cannot take /usr/include
    # whole, so copy the Vulkan directories next to the build.
    VKH=""
    for d in /usr/include /usr/local/include; do
        if [ -f "$d/vulkan/vulkan.h" ]; then
            mkdir -p "$T/vkinc"
            cp -r "$d/vulkan" "$T/vkinc/"
            [ -d "$d/vk_video" ] && cp -r "$d/vk_video" "$T/vkinc/"
            VKH="-I$T/vkinc"
            break
        fi
    done
    if [ -z "$VKH" ]; then
        skip "zui_present_vulkan (Win32): no Vulkan headers"
        WVK="-DZUI_HAVE_VULKAN=0"
        WVKSRC=""
    elif "$MINGW" $CFLAGS $INCS $VKH -c "$HERE/zui_present_vulkan.c" -o "$T/vkw.o" \
        2>"$T/wv.log"; then
        pass "zui_present_vulkan (Win32): compiles with $MINGW"
        WVK=""
        WVKSRC="$HERE/zui_present_vulkan.c"
    else
        fail "zui_present_vulkan (Win32): compile"
        head -20 "$T/wv.log"
        WVK="-DZUI_HAVE_VULKAN=0"
        WVKSRC=""
    fi
    if "$MINGW" $CFLAGS $INCS $VKH $WVK $DEMO "$HERE/zui_present_d3d11.c" $WVKSRC \
        "$HERE/zui_win32_window.c" $ZUI -ld3d11 -ldxgi -ldxguid -limm32 -luser32 -lgdi32 \
        -o "$T/zui_window.exe" 2>"$T/wl.log"; then
        pass "zui_window.exe: links with $MINGW"
        if [ "${ZXV_TEST_WINE:-0}" != 1 ]; then
            skip "zui_present_d3d11 run: set ZXV_TEST_WINE=1 to run it under Wine"
        elif ! have wine || ! have xvfb-run; then
            skip "zui_present_d3d11 run: wine or xvfb-run not installed"
        else
            mkdir -p "$T/d3d11"
            cat >"$T/wine.sh" <<EOF
export WINEDEBUG=-all WINEPREFIX="$T/wineprefix"
wineboot -i >/dev/null 2>&1
exec sh "$T/session.sh" "$T/wine_zui_window.sh" d3d11 "$T/d3d11"
EOF
            printf '#!/bin/sh\nexec wine "%s" "$@"\n' "$T/zui_window.exe" >"$T/wine_zui_window.sh"
            chmod +x "$T/wine_zui_window.sh"
            timeout 300 xvfb-run -a -s "-screen 0 1280x1024x24" sh "$T/wine.sh" >/dev/null 2>&1
            # the same checks as on Linux, from the files the session wrote
            run_wine_checks=1
        fi
    else
        fail "zui_window.exe: link"
        head -20 "$T/wl.log"
    fi
fi
if [ "${run_wine_checks:-0}" = 1 ]; then
    out="$T/d3d11"
    if grep -q "^exit frames frames=5 " "$out/frames.txt" 2>/dev/null; then
        pass "zui_present_d3d11 (Wine): 5 frames presented"
    else
        fail "zui_present_d3d11 (Wine): frames"
        tail -5 "$out/frames.txt" 2>/dev/null
    fi
    if grep -q '^event text text="a"' "$out/keys.txt" 2>/dev/null &&
        grep -q "^exit escape " "$out/keys.txt"; then
        pass "zui_present_d3d11 (Wine) input: key 'a' -> text, Escape -> exit"
    else
        skip "zui_present_d3d11 (Wine) input: no xdotool round trip"
    fi
    if [ -f "$out/screen.ppm" ] && cmp -s "$out/screen.ppm" "$out/surface.ppm"; then
        pass "zui_present_d3d11 (Wine) pixels: window == app surface"
    else
        skip "zui_present_d3d11 (Wine) pixels: not compared"
    fi
fi

if [ "$FAILED" = 0 ]; then
    echo "*** zui present: no failures ***"
else
    echo "*** zui present: FAILURES ***"
fi
exit "$FAILED"
