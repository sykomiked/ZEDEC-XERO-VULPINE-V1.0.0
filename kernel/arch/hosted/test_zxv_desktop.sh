#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxv_desktop.sh — the hosted ZXV desktop (zxv_desktop.h).
#
#   1. test_zxv_desktop.c, headless (no display, no X): the real desktop,
#      every app window opened by clicking its tile, Spaces, close and
#      reopen, a resize and a 2x panel, with zxvd not running.
#   2. The same with zxvd running, when ZXVD names a zxvd binary
#      (kernel/arch/hosted/build_zxvd.sh builds one; ZXVD=build builds it
#      here, which takes a few minutes).
#   3. zxv-desktop itself (build_zxv_desktop.sh) on the X11 backend under
#      xvfb-run, every app launched, its surface written and checked.
#
# Screens are saved as PNG in $ZXV_SHOTS (default /tmp/claude-0/shots) as
# hosted-*.png when python3 has PIL, else left as PPM. Prints [PASS],
# [SKIP] (a tool is missing; says which) or [FAIL] per part; exits 1 on any
# failure, 3 when only a build failed.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
SHOTS="${ZXV_SHOTS:-/tmp/claude-0/shots}"
FONT="$R/assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf"
T="$(mktemp -d "${TMPDIR:-/tmp}/zxv-desktop.XXXXXX")"
ZPID=""
cleanup() {
    [ -n "$ZPID" ] && kill "$ZPID" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT INT TERM
mkdir -p "$SHOTS" "$T/ppm"
FAILED=0
pass() { echo "[PASS] $1"; }
skip() { echo "[SKIP] $1"; }
fail() {
    echo "[FAIL] $1"
    FAILED=1
}

SRC="$(sh "$HERE/build_zxv_desktop.sh" --list srcs | sed "s|^|$R/|")"
INC="$(sh "$HERE/build_zxv_desktop.sh" --list incs | sed "s|^|-I$R/|")"

# ---- 1. headless
# shellcheck disable=SC2086
if ! "$CC" -std=c11 -O1 -Wall -Wextra -Werror -DZUI_HAVE_VULKAN=0 -DZUI_HAVE_X11=0 $INC \
    "-DZXV_APPS_DIR=\"$R/assets/icons/apps\"" "$HERE/test_zxv_desktop.c" $SRC -lm -o "$T/test_zxv_desktop" >"$T/build.log" 2>&1; then
    grep -v "Assembler messages\|incorrect section attributes" "$T/build.log" | tail -20
    echo "[FAIL] test_zxv_desktop: build"
    exit 3
fi
if "$T/test_zxv_desktop" "$FONT" "$T/ppm" >"$T/headless.txt" 2>&1; then
    pass "headless desktop: $(tail -1 "$T/headless.txt")"
else
    cat "$T/headless.txt"
    fail "headless desktop"
fi
"$T/test_zxv_desktop" none "$T" >"$T/nofont.txt" 2>&1 &&
    pass "headless desktop with no font file: $(tail -1 "$T/nofont.txt")" ||
    { cat "$T/nofont.txt"; fail "headless desktop with no font file"; }

# ---- 2. with zxvd running
ZB="${ZXVD:-}"
if [ "$ZB" = build ]; then
    if sh "$HERE/build_zxvd.sh" "$T/zxvd" >"$T/zxvd-build.log" 2>&1; then
        ZB="$T/zxvd/zxvd"
    else
        tail -5 "$T/zxvd-build.log"
        fail "zxvd build"
        ZB=""
    fi
fi
if [ -n "$ZB" ] && [ -x "$ZB" ]; then
    H="$T/home"
    mkdir -p "$H/models" "$H/files"
    printf 'hello\n' >"$H/files/notes.txt"
    SOCK="$T/zxvd.sock"
    HOME="$H" XDG_DATA_HOME="$H/xdg" ZXV_FILES_ROOT="$H/files" \
        "$ZB" --rpc "$SOCK" --models-dir "$H/models" >"$T/zxvd.log" 2>&1 &
    ZPID=$!
    i=0
    while [ ! -S "$SOCK" ] && [ $i -lt 100 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    mkdir -p "$T/ppm-live"
    if [ -S "$SOCK" ] && "$T/test_zxv_desktop" "$FONT" "$T/ppm-live" "$SOCK" >"$T/live.txt" 2>&1; then
        pass "headless desktop on a running zxvd: $(tail -1 "$T/live.txt")"
        for f in "$T/ppm-live"/hosted-*.ppm; do
            b="$(basename "$f" .ppm)"
            mv "$f" "$T/ppm/${b%%-*}-live-${b#hosted-}.ppm"
        done
    else
        cat "$T/live.txt" 2>/dev/null
        tail -5 "$T/zxvd.log"
        fail "headless desktop on a running zxvd"
    fi
    kill "$ZPID" 2>/dev/null
    wait "$ZPID" 2>/dev/null
    ZPID=""
else
    skip "zxvd running: set ZXVD=path/to/zxvd (or ZXVD=build) to run the connected checks"
fi

# ---- 3. the program on X11 under Xvfb
if ! command -v xvfb-run >/dev/null 2>&1; then
    skip "zxv-desktop on X11: no xvfb-run (apt-get install xvfb)"
else
    CC="$CC" sh "$HERE/build_zxv_desktop.sh" "$T/zxv-desktop" >"$T/xbuild.log" 2>&1
    rc=$?
    if [ $rc = 3 ]; then
        skip "zxv-desktop on X11: $(tail -1 "$T/xbuild.log")"
    elif [ $rc != 0 ]; then
        grep -v "Assembler messages\|incorrect section attributes" "$T/xbuild.log" | tail -20
        fail "zxv-desktop build"
    else
        timeout 120 xvfb-run -a -s "-screen 0 1400x900x24" "$T/zxv-desktop" --backend x11 \
            --rpc "" --font "$FONT" --launch all --frames 90 --screenshot "$T/ppm/hosted-x11.ppm" \
            >"$T/x11.txt" 2>&1
        rc=$?
        if [ $rc = 0 ] && grep -q "^desktop x11" "$T/x11.txt" &&
            [ "$(grep -c '^launch .* window [1-9]' "$T/x11.txt")" = 5 ] &&
            python3 - "$T/ppm/hosted-x11.ppm" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
parts = d.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
def at(x, y):
    i = 3 * (y * w + x)
    return px[i] << 16 | px[i + 1] << 8 | px[i + 2]
ground = sum(at(x, y) == 0x002911 for y in range(60, h - 60, 7) for x in range(100, w, 7))
gold = sum(at(x, y) == 0xF2C230 for y in range(0, 40) for x in range(0, 48))
white = sum(at(x, y) == 0xFFFFFF for y in range(0, h, 3) for x in range(0, w, 3))
ok = ground > 100 and gold > 20 and white == 0
print("  x11 surface %dx%d: ground %d, gold %d, white %d" % (w, h, ground, gold, white))
sys.exit(0 if ok else 1)
EOF
        then
            pass "zxv-desktop on X11 under Xvfb: $(head -1 "$T/x11.txt"), 5 windows"
        else
            cat "$T/x11.txt"
            fail "zxv-desktop on X11 under Xvfb (rc $rc)"
        fi
    fi
fi

# ---- the pictures
if python3 -c 'import PIL' 2>/dev/null; then
    python3 - "$T/ppm" "$SHOTS" <<'EOF'
import os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
n = 0
for f in sorted(os.listdir(src)):
    if f.endswith('.ppm'):
        Image.open(os.path.join(src, f)).save(os.path.join(dst, f[:-4] + '.png'))
        n += 1
print("  %d screens in %s/hosted-*.png" % (n, dst))
EOF
else
    cp "$T/ppm"/*.ppm "$SHOTS/" 2>/dev/null
    echo "  no PIL: screens left as PPM in $SHOTS"
fi
[ $FAILED = 0 ] && echo "[PASS] test_zxv_desktop" || echo "[FAIL] test_zxv_desktop"
exit $FAILED
