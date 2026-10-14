#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Builds libzxv and emu-state from this repository and copies what the
# Unreal module links into Source/ThirdParty/Zxv (git-ignored):
#
#   include/            zxv_api.h, zxv_emu.h
#   lib/<Linux|Mac>/    libzxv.so or libzxv.dylib, libzxv_emu.a
#
# usage: tools/stage_thirdparty.sh            (from anywhere)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="$here/../.."
case "$(uname -s)" in
  Linux)  plat=Linux; shared=libzxv.so ;;
  Darwin) plat=Mac;   shared=libzxv.dylib ;;
  *) echo "stage_thirdparty: libzxv has no build for $(uname -s) in this repository yet" >&2; exit 1 ;;
esac

make -C "$sdk/libzxv" all
make -C "$sdk/engines/emu-state" dist

dest="$here/Source/ThirdParty/Zxv"
mkdir -p "$dest/include" "$dest/lib/$plat"
cp "$sdk/engines/emu-state/build/dist/include/zxv_api.h" \
   "$sdk/engines/emu-state/build/dist/include/zxv_emu.h" "$dest/include/"
cp "$sdk/libzxv/build/$shared" "$sdk/engines/emu-state/build/libzxv_emu.a" "$dest/lib/$plat/"
echo "staged into $dest:"
find "$dest" -type f | sort
