#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_studio_app.sh OUTDIR — build "ZXV Studio.app": the SwiftUI app
# (universal, macOS 13 and later) with zxvd and libzxv.dylib inside it, so it
# starts its own daemon when none is running. Ad-hoc signed only: there is
# no Developer ID yet, so the first launch needs right-click > Open.
#
#   OUTDIR/ZXV Studio.app
#   OUTDIR/ZXV-Studio-<version>-macos.zip
#
# Mac only (Xcode 15 or later). Exit 3 when this is not a Mac.
#   ZXV_MAC_ARCHS="arm64"   build one slice instead of arm64 + x86_64
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../.." && pwd)"
if [ "$(uname -s)" != Darwin ] || ! xcrun --find swift >/dev/null 2>&1; then
    echo "  not a Mac with Xcode: ZXV Studio is not built" >&2
    exit 3
fi
mkdir -p "$1"
OUT="$(cd "$1" && pwd)"
ARCHS="${ZXV_MAC_ARCHS:-arm64 x86_64}"
VERSION="$(sed -n 's/^#define ZXV_VERSION *"\(.*\)"/\1/p' "$R/kernel/arch/hosted/zxv_host.c")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/zxv-studio.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT INT TERM

# 1. the app binary
set --
for a in $ARCHS; do set -- "$@" --arch "$a"; done
swift build -c release --package-path "$HERE" --product ZXVStudio "$@"
BIN="$(swift build -c release --package-path "$HERE" --product ZXVStudio "$@" --show-bin-path)/ZXVStudio"

# 2. zxvd and libzxv, one slice per arch, joined with lipo
ZX=""
LIB=""
for a in $ARCHS; do
    CC="xcrun clang -arch $a -mmacosx-version-min=13.0" \
        sh "$R/kernel/arch/hosted/build_zxvd.sh" "$WORK/zxvd-$a" >"$WORK/zxvd-$a.log" 2>&1 ||
        { tail -30 "$WORK/zxvd-$a.log"; echo "zxvd ($a) failed to build" >&2; exit 1; }
    ZX="$ZX $WORK/zxvd-$a/zxvd"
    LIB="$LIB $WORK/zxvd-$a/libzxv.dylib"
done
# shellcheck disable=SC2086
lipo -create $ZX -output "$WORK/zxvd"
# shellcheck disable=SC2086
lipo -create $LIB -output "$WORK/libzxv.dylib"

# 3. the bundle
APP="$OUT/ZXV Studio.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/ZXVStudio"
cp "$WORK/zxvd" "$APP/Contents/MacOS/zxvd"
cp "$WORK/libzxv.dylib" "$APP/Contents/Frameworks/libzxv.dylib"
# build_zxvd.sh already links zxvd with @executable_path/../Frameworks as an rpath.
strip -x "$APP/Contents/MacOS/zxvd"
sed -e "s/@VERSION@/$VERSION/g" "$HERE/Info.plist.in" >"$APP/Contents/Info.plist"
if python3 -c 'import PIL' 2>/dev/null; then
    python3 "$R/macos/make_icns.py" "$R/kernel/boot/zede_logo.png" \
        "$APP/Contents/Resources/AppIcon.icns" || echo "  no icon (make_icns.py failed)" >&2
else
    echo "  no icon: Pillow is not installed" >&2
fi
plutil -lint "$APP/Contents/Info.plist" >/dev/null

# 4. ad-hoc signature, inside out (no Developer ID yet)
codesign --force --sign - "$APP/Contents/Frameworks/libzxv.dylib"
codesign --force --sign - "$APP/Contents/MacOS/zxvd"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
lipo -info "$APP/Contents/MacOS/ZXVStudio" "$APP/Contents/MacOS/zxvd"

( cd "$OUT" && rm -f "ZXV-Studio-$VERSION-macos.zip" &&
  ditto -c -k --keepParent "ZXV Studio.app" "ZXV-Studio-$VERSION-macos.zip" )
echo "[PASS] $APP"
