#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
#
# build_desktop.sh — build the ZXV Swarm desktop apps for every platform from
# one machine, check them, and package them.
#
#   macOS    ZXV-Swarm.app (one universal binary, Intel + Apple Silicon) in a .dmg
#   Windows  zxv-swarm.exe in a .zip
#   Linux    x86_64 and aarch64 static binaries with a .desktop entry, .tar.gz
#
# Needs: zig 0.13 (or `pip install ziglang==0.13.0`), python3, and for the
# universal Mac binary `lipo` (Xcode) or `llvm-lipo`. On a Mac the .dmg is
# made with hdiutil; elsewhere with pycdlib (`pip install pycdlib`).
# Or run it all in Docker:  docker build -f build_system/Dockerfile.desktop .
#
# Usage: build_system/build_desktop.sh [--skip-tests] [--out DIR]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOSTED="$ROOT/kernel/arch/hosted"
SWARM="$ROOT/kernel/src/swarm"
OUT="$ROOT/dist"
SKIP_TESTS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-tests) SKIP_TESTS=1 ;;
        --out) OUT="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

VERSION="$(sed -n 's/^#define ZXV_VERSION *"\(.*\)"/\1/p' "$HOSTED/zxv_host.c")"
NAME="ZXV-Swarm-$VERSION"
WORK="$OUT/.work"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"

if command -v zig >/dev/null 2>&1; then ZIG=(zig)
elif python3 -c 'import ziglang' 2>/dev/null; then ZIG=(python3 -m ziglang)
else echo "zig not found: install zig 0.13 or 'pip install ziglang==0.13.0'" >&2; exit 1
fi
LIPO=""
for l in lipo llvm-lipo llvm-lipo-18 llvm-lipo-17; do
    if command -v "$l" >/dev/null 2>&1; then LIPO="$l"; break; fi
done

step() { printf '\n== %s\n' "$*"; }

# ---- 1. quality control on the engine itself
if [ "$SKIP_TESTS" = 0 ]; then
    step "Swarm tests"
    make -C "$ROOT/kernel" test-swarm
fi

# ---- 2. the window, compiled into the binary
step "Embedding the window"
python3 "$HOSTED/gen_ui.py" "$HOSTED/zxv_ui.html" "$WORK/zxv_ui.c"

SRCS=("$HOSTED/zxv_host.c" "$WORK/zxv_ui.c")
for m in budget emotion market ledger reserve harmonic overlap quality hk governor dna; do
    SRCS+=("$SWARM/swarm_$m.c")
done
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -I"$SWARM" -I"$ROOT/kernel/src/zcapital" -I"$ROOT/kernel/src/surplus")

build() {   # target output [libs...]
    local target="$1" out="$2"; shift 2
    echo "  $target"
    "${ZIG[@]}" cc -target "$target" "${CFLAGS[@]}" -s "${SRCS[@]}" "$@" -o "$out"
}

step "Compiling"
build x86_64-macos        "$WORK/zxv-mac-x86_64"
build aarch64-macos       "$WORK/zxv-mac-arm64"
build x86_64-windows-gnu  "$WORK/zxv-swarm.exe" -lws2_32 -lshell32
build x86_64-linux-musl   "$WORK/zxv-linux-x86_64"
build aarch64-linux-musl  "$WORK/zxv-linux-aarch64"
rm -f "$WORK"/*.pdb

if [ -n "$LIPO" ]; then
    "$LIPO" -create "$WORK/zxv-mac-x86_64" "$WORK/zxv-mac-arm64" -output "$WORK/zxv-mac"
    MAC_ARCHS="universal (Intel + Apple Silicon)"
else
    echo "  no lipo found: shipping the Apple Silicon binary only" >&2
    cp "$WORK/zxv-mac-arm64" "$WORK/zxv-mac"
    MAC_ARCHS="Apple Silicon only"
fi

# ---- 3. smoke test whichever binary runs here
step "Smoke test"
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)          NATIVE="$WORK/zxv-linux-x86_64" ;;
    Linux-aarch64)         NATIVE="$WORK/zxv-linux-aarch64" ;;
    Darwin-*)              NATIVE="$WORK/zxv-mac" ;;
    *)                     NATIVE="" ;;
esac
if [ -n "$NATIVE" ]; then
    python3 "$ROOT/build_system/smoke_test.py" "$NATIVE"
else
    echo "  no native binary for this machine; skipped"
fi

# ---- 4. packages
README_TEXT="ZXV Swarm $VERSION (preview)
Registered to 36N9 Genetics, LLC, Michael Laurence Curzi.
Provided as is, without warranty of any kind.

Start it and your browser opens the swarm's window (it listens on
127.0.0.1:8722 only, never on the network). Press Quit in the window to stop.

Server mode:   zxv-swarm --server     (then, from your own computer:
               ssh -N -L 8722:127.0.0.1:8722 you@server  and open
               http://127.0.0.1:8722/)

This preview runs the real swarm engine (Fibonacci budget, market, emotions,
witness, ledger, DNA, harmonic cycles). Its agents are stand-ins until the AI
model packs are installed, so it does not write real answers yet."

step "macOS app ($MAC_ARCHS)"
APP="$WORK/mac/ZXV Swarm.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$WORK/zxv-mac" "$APP/Contents/MacOS/zxv-swarm"
chmod 755 "$APP/Contents/MacOS/zxv-swarm"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>ZXV Swarm</string>
  <key>CFBundleDisplayName</key><string>ZXV Swarm</string>
  <key>CFBundleIdentifier</key><string>com.36n9genetics.zxv-swarm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleExecutable</key><string>zxv-swarm</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>10.15</string>
  <key>LSUIElement</key><true/>
  <key>NSHumanReadableCopyright</key><string>Copyright 2024-2026 36N9 Genetics, LLC, Michael Laurence Curzi. Provided without warranty.</string>
</dict>
</plist>
EOF
printf 'APPL????' > "$APP/Contents/PkgInfo"
printf '%s\n' "$README_TEXT" > "$WORK/mac/README.txt"
cat >> "$WORK/mac/README.txt" <<'EOF'

macOS: this preview is not notarised by Apple. The first time, right-click
ZXV Swarm.app and choose Open, then Open again.
EOF
ln -s /Applications "$WORK/mac/Applications" 2>/dev/null || true

(cd "$WORK/mac" && python3 "$ROOT/build_system/zip_tree.py" "$OUT/$NAME-macos.app.zip" "ZXV Swarm.app" README.txt)
if command -v hdiutil >/dev/null 2>&1; then
    rm -f "$OUT/$NAME-macos.dmg"
    hdiutil create -volname "ZXV Swarm" -srcfolder "$WORK/mac" -ov -format UDZO "$OUT/$NAME-macos.dmg" >/dev/null
    echo "  $NAME-macos.dmg (hdiutil)"
elif python3 -c 'import pycdlib' 2>/dev/null; then
    rm -f "$WORK/mac/Applications"
    python3 "$ROOT/build_system/make_dmg.py" "$WORK/mac" "ZXV Swarm" "$OUT/$NAME-macos.dmg"
    echo "  $NAME-macos.dmg (ISO 9660 + Rock Ridge; opens on any Mac)"
else
    echo "  no hdiutil or pycdlib: .dmg skipped, the .app.zip is ready" >&2
fi

step "Windows"
mkdir -p "$WORK/win"
cp "$WORK/zxv-swarm.exe" "$WORK/win/"
printf '%s\n\nWindows: SmartScreen may warn that the app is unrecognised. Choose\nMore info, then Run anyway.\n' "$README_TEXT" | sed 's/$/\r/' > "$WORK/win/README.txt"
(cd "$WORK/win" && python3 "$ROOT/build_system/zip_tree.py" "$OUT/$NAME-windows-x86_64.zip" zxv-swarm.exe README.txt)

step "Linux"
for arch in x86_64 aarch64; do
    D="$WORK/linux-$arch/zxv-swarm-$VERSION"
    mkdir -p "$D"
    cp "$WORK/zxv-linux-$arch" "$D/zxv-swarm"
    chmod 755 "$D/zxv-swarm"
    printf '%s\n' "$README_TEXT" > "$D/README.txt"
    cat > "$D/zxv-swarm.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=ZXV Swarm
Comment=Local AI swarm on the ZXV kernel
Exec=zxv-swarm
Terminal=false
Categories=Utility;
EOF
    cat > "$D/install.sh" <<'EOF'
#!/bin/sh
# Installs ZXV Swarm for this user only.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
cp "$here/zxv-swarm" "$HOME/.local/bin/zxv-swarm"
sed "s|^Exec=.*|Exec=$HOME/.local/bin/zxv-swarm|" "$here/zxv-swarm.desktop" \
    > "$HOME/.local/share/applications/zxv-swarm.desktop"
echo "Installed. Start ZXV Swarm from your applications menu or run ~/.local/bin/zxv-swarm"
EOF
    chmod 755 "$D/install.sh"
    tar -C "$WORK/linux-$arch" -czf "$OUT/$NAME-linux-$arch.tar.gz" "zxv-swarm-$VERSION"
done

# ---- 5. checksums and a final check of every package
step "Checking packages"
python3 "$ROOT/build_system/check_dist.py" "$OUT" "$VERSION"
(cd "$OUT" && sha256sum "$NAME"-* 2>/dev/null > "$NAME-SHA256SUMS.txt" || shasum -a 256 "$NAME"-* > "$NAME-SHA256SUMS.txt")
rm -rf "$WORK"
step "Done: $OUT"
ls -l "$OUT"
