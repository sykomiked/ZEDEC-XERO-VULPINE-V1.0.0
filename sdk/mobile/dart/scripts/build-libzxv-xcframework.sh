#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Builds Zxv.xcframework (static libzxv for iOS devices and the simulator)
# from sdk/libzxv's own Makefile, for Flutter iOS apps (macOS + Xcode only):
#
#   sdk/mobile/dart/scripts/build-libzxv-xcframework.sh
#   -> sdk/mobile/dart/build/Zxv.xcframework
#
# Slices: ios-arm64 (devices) and ios-arm64_x86_64-simulator. The Dart side
# finds the symbols with DynamicLibrary.process(), so the app must keep them:
# see "Shipping libzxv" in ../README.md (-force_load, Strip Style).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${ZXV_IOS_OUT:-$here/build/ios}"
min="${ZXV_IOS_MIN:-13.0}"
[ "$(uname -s)" = Darwin ] || { echo "needs macOS with Xcode" >&2; exit 2; }

build() { # <name> <sdk> <arch> <target-flag>
  local o="$out/$1"
  echo "== libzxv.a for $1"
  make -s -C "$repo/sdk/libzxv" \
    CC="xcrun -sdk $2 clang -arch $3 $4" AR="xcrun -sdk $2 ar" OUT="$o" "$o/libzxv.a"
}
rm -rf "$out" "$here/build/Zxv.xcframework"
build ios-arm64 iphoneos arm64 "-miphoneos-version-min=$min"
build sim-arm64 iphonesimulator arm64 "-mios-simulator-version-min=$min"
build sim-x86_64 iphonesimulator x86_64 "-mios-simulator-version-min=$min"
mkdir -p "$out/sim"
xcrun lipo -create "$out/sim-arm64/libzxv.a" "$out/sim-x86_64/libzxv.a" -output "$out/sim/libzxv.a"
mkdir -p "$out/headers"
cp "$repo/sdk/libzxv/include/zxv_api.h" "$out/headers/"
xcodebuild -create-xcframework \
  -library "$out/ios-arm64/libzxv.a" -headers "$out/headers" \
  -library "$out/sim/libzxv.a" -headers "$out/headers" \
  -output "$here/build/Zxv.xcframework"
echo "done: $here/build/Zxv.xcframework"
