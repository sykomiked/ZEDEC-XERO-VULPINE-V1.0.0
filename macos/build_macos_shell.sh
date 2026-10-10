#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_macos_shell.sh OUT — compile the native shell (ZXVApp.m) as one
# universal binary (arm64 + x86_64), macOS 10.15 and later. Mac only: it
# needs Xcode or the Command Line Tools (xcrun clang and the macOS SDK).
# Exit 0 and write OUT, or exit 3 when this is not a Mac (callers then ship
# the engine-only fallback), or 1 when the compile fails.
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUT" >&2; exit 2; }
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ "$(uname -s)" != Darwin ] || ! xcrun --find clang >/dev/null 2>&1; then
    echo "  not a Mac with Xcode tools: the native shell is not built" >&2
    exit 3
fi
ARCHS="${ZXV_MAC_ARCHS:-arm64 x86_64}"
FLAGS=""
for a in $ARCHS; do FLAGS="$FLAGS -arch $a"; done
# shellcheck disable=SC2086
xcrun clang $FLAGS -mmacosx-version-min=10.15 -fobjc-arc -O2 -Wall -Wextra \
    -Wno-unused-parameter -Werror=objc-method-access -Werror=incompatible-pointer-types \
    -Werror=implicit-function-declaration -Werror=unguarded-availability-new \
    "$HERE/ZXVApp.m" \
    -framework Cocoa -framework WebKit -framework Security -framework UserNotifications \
    -weak_framework UniformTypeIdentifiers \
    -o "$1"
lipo -info "$1"
