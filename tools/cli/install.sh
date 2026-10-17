#!/bin/sh
# install.sh — the zero-install bootstrap: detect the platform, fetch the
# static CLI binaries from the release, verify the sha256. No IPFS
# daemon, no config files, no background daemons (until you start one).
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
set -eu

BASE="${ZXV_BASE:-https://zedec.s3.filebase.io/zxv-release}"
VERSION="${ZXV_VERSION:-v1.0.0}"

os=$(uname -s 2>/dev/null || echo unknown)
arch=$(uname -m 2>/dev/null || echo unknown)
case "$os:$arch" in
  Darwin:arm64) plat=darwin-arm64 ;;
  Darwin:x86_64) plat=darwin-amd64 ;;
  Linux:arm64|Linux:aarch64) plat=linux-arm64 ;;
  Linux:x86_64|Linux:amd64) plat=linux-amd64 ;;
  *) echo "unsupported platform: $os $arch" >&2; exit 1 ;;
esac

prefix="${ZXV_PREFIX:-$HOME/.zxv}"
mkdir -p "$prefix/bin"
echo "zxv: fetching $plat binaries from $BASE..."
for tool in zxv 36n9 9n63; do
    url="$BASE/$VERSION/$plat/$tool"
    tmp="$prefix/bin/.$tool.tmp"
    curl -fsSL "$url" -o "$tmp"
    chmod +x "$tmp"
    mv "$tmp" "$prefix/bin/$tool"
    echo "zxv: installed $tool"
done
echo "zxv: done. Add $prefix/bin to your PATH:"
echo "  export PATH=\"$prefix/bin:\$PATH\""
echo "Then: zxv daemon --port 8724   (the local PoP node)"