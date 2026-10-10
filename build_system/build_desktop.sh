#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_desktop.sh — build the ZXV desktop apps, test them, and package them.
#
#   macOS    ZXV.app: the native shell (macos/ZXVApp.m, AppKit + WKWebView)
#            and the engine (kernel/arch/hosted), both universal (Intel +
#            Apple Silicon), the window, the first-run model fetcher and its
#            manifest; in a .dmg and a .app.zip. Signed and notarised when
#            the signing variables are set (see build_signed_app.sh).
#   Windows  zxv-swarm.exe in a .zip
#   Linux    x86_64 and aarch64 static binaries with a .desktop entry, .tar.gz
#
# The native shell can only be compiled on a Mac. Built elsewhere, ZXV.app
# holds the engine alone, which opens the window in the default browser
# (no Dock icon or menus). The macOS CI job builds the full app.
#
# Needs: python3; zig 0.13 (or `pip install ziglang==0.13.0`) for the
# Windows and Linux builds and for the Mac engine off a Mac; `lipo` or
# `llvm-lipo` for a universal engine off a Mac; Pillow for the app icon
# (skipped without it). On a Mac the .dmg is made with hdiutil; elsewhere
# with pycdlib. Or run it all in Docker: build_system/Dockerfile.desktop.
#
# Environment:
#   ZXV_BUNDLE_MODEL=FILE.gguf  bundle this model in the app (no download
#                               needed on first run; mind its licence)
#   ZXV_SIGN_IDENTITY, ZXV_TEAM_ID, ZXV_NOTARY_PROFILE: see build_signed_app.sh
#
# Usage: build_system/build_desktop.sh [--skip-tests] [--mac-only] [--out DIR]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOSTED="$ROOT/kernel/arch/hosted"
TENSOR="$ROOT/kernel/src/tensor"
MACOS="$ROOT/macos"
OUT="$ROOT/dist"
SKIP_TESTS=0
MAC_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-tests) SKIP_TESTS=1 ;;
        --mac-only) MAC_ONLY=1 ;;
        --out) OUT="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

VERSION="$(sed -n 's/^#define ZXV_VERSION *"\(.*\)"/\1/p' "$HOSTED/zxv_host.c")"
NAME="ZXV-$VERSION"
WORK="$OUT/.work"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"

ON_MAC=0
if [ "$(uname -s)" = Darwin ] && xcrun --find clang >/dev/null 2>&1; then ON_MAC=1; fi

ZIG=()
if command -v zig >/dev/null 2>&1; then ZIG=(zig)
elif python3 -c 'import ziglang' 2>/dev/null; then ZIG=(python3 -m ziglang)
fi
if [ ${#ZIG[@]} -eq 0 ] && { [ "$MAC_ONLY" = 0 ] || [ "$ON_MAC" = 0 ]; }; then
    echo "zig not found: install zig 0.13 or 'pip install ziglang==0.13.0'" >&2
    exit 1
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
    step "Hosted app tests (API guard, budget gate, net host, end-to-end API, peers, updates)"
    sh "$HOSTED/test_hosted.sh"
fi

# ---- 2. the sources: one list for every platform
step "Embedding the window"
python3 "$HOSTED/gen_ui.py" "$HOSTED/zxv_ui.html" "$WORK/zxv_ui.c"

# One list for the app and its tests: kernel/arch/hosted/app_sources.sh. It
# names every swarm module (so one added to the tests cannot be left out of
# the app), every tensor-engine source, the budget gate, the Vinea node and
# its UDP glue (zxv_net_host.c; networking is OFF unless the user turns it
# on), the update checker with ipfs_node, and the notification bus with its
# OS bridge. Windows uses winsock for the UDP socket; its update check
# reports "not wired" (no curl spawn there yet).
SRCS=("$WORK/zxv_ui.c")
while IFS= read -r f; do SRCS+=("$ROOT/kernel/$f"); done < <(sh "$HOSTED/app_sources.sh" srcs)
INCS=()
while IFS= read -r d; do INCS+=(-I"$ROOT/kernel/$d"); done < <(sh "$HOSTED/app_sources.sh" incs)
echo "  sources: ${#SRCS[@]} files (swarm, tensor, budget gate, vinea + UDP, updates, notifications)"
DEFS=()
if [ -f "$TENSOR/zt_model.c" ] && [ -f "$HOSTED/zxv_zt_glue.c" ]; then
    SRCS+=("$HOSTED/zxv_zt_glue.c")
    DEFS+=(-DZXV_HAVE_ZT_GLUE)
    echo "  forward pass: compiled in (zt_model + zxv_zt_glue)"
elif [ -f "$TENSOR/zt_model.c" ]; then
    echo "  forward pass: zt_model compiled, but no zxv_zt_glue.c yet: the app cannot generate"
else
    echo "  forward pass: not in the tree yet; the model slot maps and tokenizes only"
fi
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror "${INCS[@]}" ${DEFS[@]+"${DEFS[@]}"})

build() {   # target output [libs...]
    local target="$1" out="$2"; shift 2
    echo "  $target"
    "${ZIG[@]}" cc -target "$target" "${CFLAGS[@]}" -s "${SRCS[@]}" "$@" -o "$out"
}

step "Compiling the engine"
if [ "$ON_MAC" = 1 ]; then
    echo "  macOS universal (Apple clang)"
    xcrun clang -arch arm64 -arch x86_64 -mmacosx-version-min=10.15 "${CFLAGS[@]}" \
        "${SRCS[@]}" -o "$WORK/zxv-mac"
    strip -x "$WORK/zxv-mac"
    MAC_ARCHS="universal (Intel + Apple Silicon)"
else
    build x86_64-macos  "$WORK/zxv-mac-x86_64"
    build aarch64-macos "$WORK/zxv-mac-arm64"
    if [ -n "$LIPO" ]; then
        "$LIPO" -create "$WORK/zxv-mac-x86_64" "$WORK/zxv-mac-arm64" -output "$WORK/zxv-mac"
        MAC_ARCHS="universal (Intel + Apple Silicon)"
    else
        echo "  no lipo found: shipping the Apple Silicon engine only" >&2
        cp "$WORK/zxv-mac-arm64" "$WORK/zxv-mac"
        MAC_ARCHS="Apple Silicon only"
    fi
fi
if [ "$MAC_ONLY" = 0 ]; then
    build x86_64-windows-gnu  "$WORK/zxv-swarm.exe" -lws2_32 -lshell32 -lbcrypt
    build x86_64-linux-musl   "$WORK/zxv-linux-x86_64"
    build aarch64-linux-musl  "$WORK/zxv-linux-aarch64"
    rm -f "$WORK"/*.pdb
fi

# ---- 3. end-to-end test of whichever engine runs here
step "End-to-end test of the shipped engine"
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)  NATIVE="$WORK/zxv-linux-x86_64" ;;
    Linux-aarch64) NATIVE="$WORK/zxv-linux-aarch64" ;;
    Darwin-*)      NATIVE="$WORK/zxv-mac" ;;
    *)             NATIVE="" ;;
esac
if [ -n "$NATIVE" ] && [ -x "$NATIVE" ]; then
    cc -std=c11 -I"$TENSOR" "$HOSTED/test_write_gguf.c" -o "$WORK/write_gguf"
    "$WORK/write_gguf" "$WORK/tiny-test.gguf"
    python3 "$HOSTED/test_host_api.py" "$NATIVE" "$WORK/tiny-test.gguf"
    python3 "$HOSTED/test_host_net.py" "$NATIVE"
    rm -f "$WORK/write_gguf" "$WORK/tiny-test.gguf"
else
    echo "  no native binary for this machine; skipped"
fi

# ---- 4. packages
README_TEXT="ZXV $VERSION (preview)
Registered to 36N9 Genetics, LLC, Michael Laurence Curzi.
Licensed under the Apache License 2.0. Provided as is, without warranty.

The swarm's window is served on 127.0.0.1 only, never on the network, and
every request must carry a token that changes at each launch.

Server mode:   zxv-swarm --server     (then, from your own computer:
               ssh -N -L 8722:127.0.0.1:8722 you@server  and open the
               ZXV-URL it prints, including the #token part)

This preview runs the real swarm engine (Fibonacci budget, market, emotions,
witness, ledger, DNA, harmonic cycles) and reads GGUF models with the kernel
tensor engine. When the forward pass is built in (the build log says so), a
Q8_0 GGUF model answers greedily on one CPU thread; it is slow and has been
tested only on tiny test models."

step "macOS app ($MAC_ARCHS)"
APP="$WORK/mac/ZXV.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/models"
cp "$WORK/zxv-mac" "$APP/Contents/MacOS/zxv-engine"
chmod 755 "$APP/Contents/MacOS/zxv-engine"
set +e
sh "$MACOS/build_macos_shell.sh" "$APP/Contents/MacOS/ZXV"
SHELL_RC=$?
set -e
case "$SHELL_RC" in
    0) EXEC=ZXV; UIEL=false; MAC_KIND="native app (Dock, menus, WKWebView window)" ;;
    3) EXEC=zxv-engine; UIEL=true; MAC_KIND="engine only (opens the default browser)" ;;
    *) echo "the native shell failed to compile" >&2; exit 1 ;;
esac
sed -e "s/@VERSION@/$VERSION/g" -e "s/@EXECUTABLE@/$EXEC/g" -e "s/@UIELEMENT@/$UIEL/g" \
    "$MACOS/Info.plist.in" > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
cp "$MACOS/models.manifest" "$APP/Contents/Resources/models.manifest"
cp "$MACOS/zxv-model-fetch.sh" "$APP/Contents/Resources/zxv-model-fetch.sh"
chmod 755 "$APP/Contents/Resources/zxv-model-fetch.sh"
cp "$ROOT/docs/MAC_APP.md" "$APP/Contents/Resources/MAC_APP.md"
if python3 "$MACOS/make_icns.py" "$ROOT/kernel/boot/zede_logo.png" \
        "$APP/Contents/Resources/AppIcon.icns" 2>/dev/null; then
    echo "  icon: AppIcon.icns"
else
    echo "  icon skipped (pip install pillow to make it)" >&2
fi
if [ -n "${ZXV_BUNDLE_MODEL:-}" ]; then
    if [ "$(head -c 4 "$ZXV_BUNDLE_MODEL")" != GGUF ]; then
        echo "ZXV_BUNDLE_MODEL is not a GGUF file: $ZXV_BUNDLE_MODEL" >&2
        exit 1
    fi
    cp "$ZXV_BUNDLE_MODEL" "$APP/Contents/Resources/models/"
    echo "  bundled model: $(basename "$ZXV_BUNDLE_MODEL")"
fi
echo "  $MAC_KIND"

step "Signing (skipped unless the signing variables are set)"
"$ROOT/build_system/build_signed_app.sh" --mac-app "$APP"

printf '%s\n' "$README_TEXT" > "$WORK/mac/README.txt"
cat >> "$WORK/mac/README.txt" <<'EOF'

macOS: drag ZXV to Applications and open it. If this copy is not notarised,
the first time right-click ZXV and choose Open, then Open again.
EOF
ln -s /Applications "$WORK/mac/Applications" 2>/dev/null || true

(cd "$WORK/mac" && python3 "$ROOT/build_system/zip_tree.py" "$OUT/$NAME-macos.app.zip" "ZXV.app" README.txt)
rm -f "$OUT/$NAME-macos.dmg"
if command -v hdiutil >/dev/null 2>&1; then
    hdiutil create -volname "ZXV" -srcfolder "$WORK/mac" -ov -format UDZO "$OUT/$NAME-macos.dmg" >/dev/null
    echo "  $NAME-macos.dmg (hdiutil)"
    "$ROOT/build_system/build_signed_app.sh" --mac-dmg "$OUT/$NAME-macos.dmg"
elif python3 -c 'import pycdlib' 2>/dev/null; then
    rm -f "$WORK/mac/Applications"
    python3 "$ROOT/build_system/make_dmg.py" "$WORK/mac" "ZXV" "$OUT/$NAME-macos.dmg"
    echo "  $NAME-macos.dmg (ISO 9660 + Rock Ridge; opens on any Mac)"
else
    echo "  no hdiutil or pycdlib: .dmg skipped, the .app.zip is ready" >&2
fi

if [ "$MAC_ONLY" = 0 ]; then
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
        cp "$MACOS/zxv-model-fetch.sh" "$MACOS/models.manifest" "$D/"
        printf '%s\n\nModels: put a GGUF file in ~/.local/share/zxv/models, or run\n  ./zxv-model-fetch.sh models.manifest ~/.local/share/zxv/models\n' "$README_TEXT" > "$D/README.txt"
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
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/zxv/models"
cp "$here/zxv-swarm" "$HOME/.local/bin/zxv-swarm"
sed "s|^Exec=.*|Exec=$HOME/.local/bin/zxv-swarm|" "$here/zxv-swarm.desktop" \
    > "$HOME/.local/share/applications/zxv-swarm.desktop"
echo "Installed. Start ZXV Swarm from your applications menu or run ~/.local/bin/zxv-swarm"
EOF
        chmod 755 "$D/install.sh"
        tar -C "$WORK/linux-$arch" -czf "$OUT/$NAME-linux-$arch.tar.gz" "zxv-swarm-$VERSION"
    done
fi

# ---- 5. checksums and a final check of every package
step "Checking packages"
CHECK_ARGS=("$OUT" "$VERSION")
[ "$MAC_ONLY" = 1 ] && CHECK_ARGS+=(--mac-only)
python3 "$MACOS/check_packages.py" "${CHECK_ARGS[@]}"
rm -f "$OUT/$NAME-SHA256SUMS.txt"
(cd "$OUT" && { sha256sum "$NAME"-* 2>/dev/null || shasum -a 256 "$NAME"-*; } > "$WORK/sums" &&
    mv "$WORK/sums" "$NAME-SHA256SUMS.txt")
rm -rf "$WORK"
step "Done: $OUT"
ls -l "$OUT"
