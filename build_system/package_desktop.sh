#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# package_desktop.sh — the Linux and Windows desktop apps on zxvd.
#
# The app is zxvd (the swarm with its JSON-RPC control socket, linked to
# libzxv through its frozen ABI) started with --desktop: the socket goes in
# the per-user folder and the window opens in the default browser (the
# existing browser UI; the native SwiftUI app is Mac only). SDKs and
# scripts talk to the same socket. docs/DESKTOP_LINUX_WINDOWS.md is the
# user-facing page.
#
#   linux    zxvd + libzxv.so for this machine's architecture (x86_64 or
#            aarch64), as a .deb and as a portable .tar.gz; and as an
#            AppImage when ZXV_APPIMAGE_RUNTIME names the AppImage type-2
#            runtime for this architecture (its SHA-256 must be one of
#            those pinned below, so the shipped launcher is a known binary)
#   windows  zxvd.exe + libzxv.dll + ZXV.exe (a launcher without a console
#            window) in a .zip, built with MinGW-w64 (CC, default
#            x86_64-w64-mingw32-gcc) from Linux or from MSYS2
#
# Writes OUTDIR/ZXV-<version>-linux-<arch>.{deb,tar.gz,AppImage} or
# OUTDIR/ZXV-<version>-windows-x86_64.zip, and the stage folder
# OUTDIR/stage-<target>/ (tests run zxvd from there).
#
# Needs: python3, make, a C compiler; dpkg-deb for the .deb; mksquashfs
# (squashfs-tools) for the AppImage; Pillow for the Windows icon (skipped
# without it); windres for the icon (skipped without it).
#
# Usage: build_system/package_desktop.sh linux|windows [OUTDIR]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOSTED="$ROOT/kernel/arch/hosted"
TARGET="${1:?usage: package_desktop.sh linux|windows [OUTDIR]}"
OUT="${2:-$ROOT/dist}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
VERSION="$(sed -n 's/^#define ZXV_VERSION *"\(.*\)"/\1/p' "$HOSTED/zxv_host.c")"
NAME="ZXV-$VERSION"

# The AppImage type-2 runtime (github.com/AppImage/type2-runtime), pinned by
# hash: a new upstream build fails here until its hash is checked and added.
APPIMAGE_RUNTIME_SHA256="
156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074 x86_64
b4ff0030242d0c3bb12ce40541828303cf167493f4793456f0436edd6255c39d aarch64
"

README_TEXT="ZXV $VERSION (preview)
Registered to 36N9 Genetics, LLC, Michael Laurence Curzi.
Licensed under the Apache License 2.0. Provided as is, without warranty.

ZXV runs a local AI swarm on the ZXV kernel's integer engine. Its window
opens in your default browser, served on 127.0.0.1 only, never on the
network, and every request must carry a token that changes at each launch.
Start it again while it runs and it opens the running one's window.

Its control socket (JSON-RPC 2.0, one request per line; docs/ZXVD.md) is
for the SDKs and your own scripts, and only your user can open it."

step() { printf '\n== %s\n' "$*"; }

# ASCW Golden, the window's font, with its SIL OFL 1.1 licence
put_fonts() { # DIR
    mkdir -p "$1"
    cp "$ROOT"/assets/fonts/ascw-golden/*.ttf "$ROOT"/assets/fonts/ascw-golden/*.otf \
        "$ROOT/assets/fonts/ascw-golden/OFL.txt" "$ROOT/assets/fonts/ascw-golden/FONTLOG.md" "$1/"
}

build_engine() {   # STAGE_ENGINE_DIR (CC from the environment)
    step "zxvd + libzxv ($("${CC%% *}" -dumpmachine 2>/dev/null || echo "$CC"))"
    rm -rf "$OUT/.engine"
    sh "$HOSTED/build_zxvd.sh" "$OUT/.engine"
    mkdir -p "$1"
}

case "$TARGET" in
linux)
    CC="${CC:-cc}"
    export CC
    case "$(uname -m)" in
        x86_64|amd64) ARCH=x86_64; DEBARCH=amd64 ;;
        aarch64|arm64) ARCH=aarch64; DEBARCH=arm64 ;;
        *) echo "unsupported architecture $(uname -m)" >&2; exit 2 ;;
    esac
    STAGE="$OUT/stage-linux-$ARCH"
    rm -rf "$STAGE"
    build_engine "$STAGE/usr/lib/zxv"
    cp "$OUT/.engine/zxvd" "$OUT/.engine/libzxv.so" "$STAGE/usr/lib/zxv/"
    strip --strip-unneeded "$STAGE/usr/lib/zxv/zxvd" "$STAGE/usr/lib/zxv/libzxv.so" 2>/dev/null || true
    chmod 755 "$STAGE/usr/lib/zxv/zxvd"
    chmod 644 "$STAGE/usr/lib/zxv/libzxv.so"
    cp "$ROOT/macos/zxv-model-fetch.sh" "$ROOT/macos/models.manifest" "$STAGE/usr/lib/zxv/"
    chmod 755 "$STAGE/usr/lib/zxv/zxv-model-fetch.sh"
    put_fonts "$STAGE/usr/lib/zxv/fonts" # zxvd finds them beside itself

    mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications" "$STAGE/usr/share/pixmaps" \
        "$STAGE/usr/share/doc/zxv"
    cat > "$STAGE/usr/bin/zxv" <<'EOF'
#!/bin/sh
# ZXV: the swarm with its window in the default browser (zxvd --desktop).
# "zxv --help" lists the options; "zxv fetch-model" downloads the default model.
here="$(dirname "$(readlink -f "$0")")"
lib="$here/../lib/zxv"
if [ "${1:-}" = fetch-model ]; then
    exec "$lib/zxv-model-fetch.sh" "$lib/models.manifest" "${XDG_DATA_HOME:-$HOME/.local/share}/zxv/models"
fi
exec "$lib/zxvd" --desktop "$@"
EOF
    chmod 755 "$STAGE/usr/bin/zxv"
    cat > "$STAGE/usr/share/applications/zxv.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=ZXV Swarm
Comment=Local AI swarm on the ZXV kernel
Exec=zxv
Icon=zxv
Terminal=false
Categories=Utility;
EOF
    cp "$ROOT/kernel/boot/zede_logo.png" "$STAGE/usr/share/pixmaps/zxv.png"
    printf '%s\n\nModels: put a GGUF file in ~/.local/share/zxv/models, or run\n  zxv fetch-model\n' \
        "$README_TEXT" > "$STAGE/usr/share/doc/zxv/README.txt"
    cp "$ROOT/LICENSE" "$STAGE/usr/share/doc/zxv/copyright"

    # .deb: the oldest glibc the binaries need, read from their symbol versions
    if command -v dpkg-deb >/dev/null 2>&1; then
        step ".deb ($DEBARCH)"
        GLIBC="$(objdump -T "$STAGE/usr/lib/zxv/zxvd" "$STAGE/usr/lib/zxv/libzxv.so" |
                 sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
        DEB="$OUT/.deb"
        rm -rf "$DEB"
        cp -a "$STAGE" "$DEB"
        mkdir -p "$DEB/DEBIAN"
        cat > "$DEB/DEBIAN/control" <<EOF
Package: zxv
Version: ${VERSION//-/\~}
Architecture: $DEBARCH
Maintainer: 36N9 Genetics, LLC <noreply@36n9genetics.invalid>
Depends: libc6 (>= ${GLIBC:-2.31})
Recommends: xdg-utils, curl
Section: utils
Priority: optional
Homepage: https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0
Description: ZXV local AI swarm (preview)
 A local multi-model AI swarm on the ZXV kernel's integer engine. The window
 opens in the default browser on 127.0.0.1 only; a JSON-RPC control socket
 serves the SDKs. Apache-2.0, provided as is, without warranty.
EOF
        find "$DEB" -type d -exec chmod 755 {} +
        dpkg-deb --root-owner-group --build "$DEB" "$OUT/$NAME-linux-$ARCH.deb" >/dev/null
        rm -rf "$DEB"
        echo "  $NAME-linux-$ARCH.deb (needs glibc >= ${GLIBC:-?})"
    else
        echo "  no dpkg-deb: .deb skipped" >&2
    fi

    step "portable .tar.gz"
    T="$OUT/.tar/zxv-$VERSION"
    rm -rf "$OUT/.tar"
    mkdir -p "$T"
    cp -a "$STAGE/usr" "$T/"
    cp "$STAGE/usr/share/doc/zxv/README.txt" "$T/README.txt"
    cat > "$T/install.sh" <<'EOF'
#!/bin/sh
# Installs ZXV for this user only, under ~/.local.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/lib" "$HOME/.local/bin" "$HOME/.local/share/applications" \
    "$HOME/.local/share/pixmaps"
rm -rf "$HOME/.local/lib/zxv"
cp -a "$here/usr/lib/zxv" "$HOME/.local/lib/zxv"
cp "$here/usr/bin/zxv" "$HOME/.local/bin/zxv"
cp "$here/usr/share/pixmaps/zxv.png" "$HOME/.local/share/pixmaps/zxv.png"
sed "s|^Exec=.*|Exec=$HOME/.local/bin/zxv|" "$here/usr/share/applications/zxv.desktop" \
    > "$HOME/.local/share/applications/zxv.desktop"
echo "Installed. Start ZXV Swarm from your applications menu or run ~/.local/bin/zxv"
EOF
    chmod 755 "$T/install.sh"
    tar -C "$OUT/.tar" --owner=0 --group=0 -czf "$OUT/$NAME-linux-$ARCH.tar.gz" "zxv-$VERSION"
    rm -rf "$OUT/.tar"
    echo "  $NAME-linux-$ARCH.tar.gz"

    if [ -n "${ZXV_APPIMAGE_RUNTIME:-}" ]; then
        step "AppImage ($ARCH)"
        got="$(sha256sum "$ZXV_APPIMAGE_RUNTIME" | cut -d' ' -f1)"
        if ! printf '%s' "$APPIMAGE_RUNTIME_SHA256" | grep -qx "$got $ARCH"; then
            echo "the AppImage runtime $ZXV_APPIMAGE_RUNTIME ($got) is not a pinned $ARCH runtime" >&2
            exit 1
        fi
        command -v mksquashfs >/dev/null || { echo "mksquashfs (squashfs-tools) is needed" >&2; exit 1; }
        AD="$OUT/.AppDir"
        rm -rf "$AD"
        mkdir -p "$AD"
        cp -a "$STAGE/usr" "$AD/"
        cp "$STAGE/usr/share/applications/zxv.desktop" "$AD/zxv.desktop"
        cp "$STAGE/usr/share/pixmaps/zxv.png" "$AD/zxv.png"
        ln -s zxv.png "$AD/.DirIcon"
        cat > "$AD/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/zxv" "$@"
EOF
        chmod 755 "$AD/AppRun"
        rm -f "$OUT/.squashfs"
        mksquashfs "$AD" "$OUT/.squashfs" -root-owned -noappend -comp zstd -quiet >/dev/null
        cat "$ZXV_APPIMAGE_RUNTIME" "$OUT/.squashfs" > "$OUT/$NAME-linux-$ARCH.AppImage"
        chmod 755 "$OUT/$NAME-linux-$ARCH.AppImage"
        rm -rf "$AD" "$OUT/.squashfs"
        echo "  $NAME-linux-$ARCH.AppImage"
    else
        echo "  AppImage skipped (set ZXV_APPIMAGE_RUNTIME to a pinned type-2 runtime)"
    fi
    ;;

windows)
    CC="${CC:-x86_64-w64-mingw32-gcc}"
    export CC
    case "$($CC -dumpmachine 2>/dev/null)" in
        *mingw*|*windows*) ;;
        *) echo "CC=$CC does not target Windows (try x86_64-w64-mingw32-gcc)" >&2; exit 2 ;;
    esac
    STAGE="$OUT/stage-windows-x86_64/ZXV"
    rm -rf "$OUT/stage-windows-x86_64"
    build_engine "$STAGE"
    cp "$OUT/.engine/zxvd.exe" "$OUT/.engine/libzxv.dll" "$STAGE/"

    step "ZXV.exe (the launcher)"
    ICON_OBJ=()
    WINDRES="${WINDRES:-${CC%gcc}windres}"
    if command -v "$WINDRES" >/dev/null 2>&1 &&
        python3 -c "from PIL import Image; Image.open('$ROOT/kernel/boot/zede_logo.png').save('$OUT/.zxv.ico', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(250,250)])" 2>/dev/null; then
        printf '1 ICON "%s"\n' "$OUT/.zxv.ico" > "$OUT/.zxv.rc"
        "$WINDRES" "$OUT/.zxv.rc" -O coff -o "$OUT/.zxv-res.o"
        ICON_OBJ=("$OUT/.zxv-res.o")
        echo "  icon: from kernel/boot/zede_logo.png"
    else
        echo "  icon skipped (needs windres and Pillow)"
    fi
    $CC -std=c11 -O2 -Wall -Wextra -Werror -municode -mwindows "$HOSTED/zxv_launch_win.c" \
        ${ICON_OBJ[@]+"${ICON_OBJ[@]}"} -lshell32 -o "$STAGE/ZXV.exe"
    rm -f "$OUT/.zxv.ico" "$OUT/.zxv.rc" "$OUT/.zxv-res.o"
    STRIP="${CC%gcc}strip"
    command -v "$STRIP" >/dev/null 2>&1 && "$STRIP" "$STAGE"/*.exe "$STAGE/libzxv.dll"
    cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
    cp "$ROOT/macos/models.manifest" "$STAGE/models.manifest"
    put_fonts "$STAGE/fonts"
    printf '%s\n\nStart ZXV.exe (keep it with zxvd.exe and libzxv.dll in one folder).\nNeeds Windows 10 version 1803 or later, 64-bit.\n\nSmartScreen may warn that the app is unrecognised (it is not code-signed).\nChoose More info, then Run anyway.\n\nModels: put a GGUF file in %%APPDATA%%\\ZXV\\models (models.manifest lists the\ndefault model, its download address and its SHA-256).\nThe engine log is %%LOCALAPPDATA%%\\ZXV\\logs\\engine.log.\n' \
        "$README_TEXT" | sed 's/$/\r/' > "$STAGE/README.txt"
    (cd "$OUT/stage-windows-x86_64" &&
        python3 "$ROOT/build_system/zip_tree.py" "$OUT/$NAME-windows-x86_64.zip" ZXV)
    echo "  $NAME-windows-x86_64.zip"
    ;;
*)
    echo "usage: package_desktop.sh linux|windows [OUTDIR]" >&2
    exit 2
    ;;
esac
rm -rf "$OUT/.engine"
step "Done"
ls -l "$OUT"/"$NAME"-*
