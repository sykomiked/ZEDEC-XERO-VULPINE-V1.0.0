#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Cross-builds libzxv.so for Android ABIs with the NDK, using sdk/libzxv's
# own Makefile (no second list of sources), into
#   sdk/mobile/kotlin/build/libzxv-android/<abi>/libzxv.so
# which the :zxv-android module packages into the AAR. Flutter apps can use
# the same output (see sdk/mobile/dart/README.md).
#
#   ANDROID_NDK_HOME=/path/to/ndk scripts/build-libzxv-android.sh [abi ...]
#   (default ABIs: arm64-v8a x86_64; also armeabi-v7a, x86)
#
# Two workarounds for libzxv's Makefile on the NDK, kept here so the library
# itself is untouched:
#  - OSDEFS=-DZXV_HAVE_ARC4RANDOM=1: zxv_random otherwise calls getrandom(),
#    which bionic only declares from API 28; arc4random_buf exists at every
#    API level and draws from the kernel CSPRNG.
#  - an empty libpthread.a on the link path: the Makefile links -lpthread,
#    which the NDK no longer ships (pthreads live in bionic's libc).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$here/../../.." && pwd)"
api="${ZXV_ANDROID_API:-24}"
out="${ZXV_ANDROID_OUT:-$here/build/libzxv-android}"
ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [ -z "$ndk" ] && [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME/ndk" ]; then
  ndk="$ANDROID_HOME/ndk/$(ls "$ANDROID_HOME/ndk" | sort -V | tail -1)"
fi
[ -d "$ndk" ] || { echo "set ANDROID_NDK_HOME to an Android NDK (r26 or later)" >&2; exit 2; }

case "$(uname -s)" in
  Linux) host=linux-x86_64 ;;
  Darwin) host=darwin-x86_64 ;; # the NDK ships universal binaries under this name
  *) echo "unsupported build host $(uname -s)" >&2; exit 2 ;;
esac
tc="$ndk/toolchains/llvm/prebuilt/$host/bin"

abis=("$@")
[ ${#abis[@]} -gt 0 ] || abis=(arm64-v8a x86_64)

stub="$out/.stub"
mkdir -p "$stub"
"$tc/llvm-ar" rc "$stub/libpthread.a"

for abi in "${abis[@]}"; do
  case "$abi" in
    arm64-v8a) triple=aarch64-linux-android ;;
    x86_64) triple=x86_64-linux-android ;;
    armeabi-v7a) triple=armv7a-linux-androideabi ;;
    x86) triple=i686-linux-android ;;
    *) echo "unknown ABI $abi" >&2; exit 2 ;;
  esac
  obj="$out/$abi/obj"
  echo "== libzxv for $abi (API $api)"
  make -s -C "$repo/sdk/libzxv" \
    CC="$tc/$triple$api-clang -Wno-unused-command-line-argument -L$stub -Wl,-z,max-page-size=16384" \
    AR="$tc/llvm-ar" OUT="$obj" OSDEFS=-DZXV_HAVE_ARC4RANDOM=1 all
  "$tc/llvm-strip" --strip-debug -o "$out/$abi/libzxv.so" "$obj/libzxv.so"
  "$tc/llvm-readelf" -d "$out/$abi/libzxv.so" | grep -q 'SONAME.*libzxv.so'
done
echo "done: $out"
