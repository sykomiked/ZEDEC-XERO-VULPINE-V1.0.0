#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# zxv-verify.sh - check that each file's name is its own CID. POSIX sh.
#
#   tools/zxv-verify.sh FILE...          e.g. bafybei....img, bafkrei....install
#
# Every file of a ZXV release is named <its CID>.<ext> (docs/BUCKET.md), so a
# flipped bit makes the name false. The CID is recomputed here with no IPFS
# node: sh cannot do the UnixFS chunking and SHA-256 itself, so this compiles
# the one-file C helper tools/zxv-cid.c (any C99 compiler, ~0.1 s) and runs
# it. The parameters are pinned there and match `ipfs add --only-hash
# --cid-version=1` with Kubo's defaults (256 KiB chunks, raw leaves, balanced,
# 174 links, sha2-256). For a <cid>.install, every file it lists is checked
# too, beside it in the bucket: present, the listed size and SHA-256, and a
# name that is its CID. Exit 0 only if everything holds.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ $# -gt 0 ] || { echo "usage: zxv-verify.sh FILE..." >&2; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT INT TERM
cc=${CC:-cc}
"$cc" -std=c99 -O2 -o "$tmp/zxv-cid" "$here/zxv-cid.c" || { echo "zxv-verify: no C compiler (set CC)" >&2; exit 2; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
bad=0
named() { # $1 file: is its name its CID?
  got=$("$tmp/zxv-cid" "$1" | cut -d' ' -f1)
  stem=$(basename -- "$1"); stem=${stem%%.*}
  if [ "$got" = "$stem" ]; then echo "ok    $1"; else echo "FALSE $1: its CID is $got"; bad=1; fi
}
for f in "$@"; do
  [ -f "$f" ] || { echo "FALSE $f: no such file"; bad=1; continue; }
  named "$f"
  case "$f" in *.install) ;; *) continue ;; esac
  head -n 1 "$f" | grep -qx '# zxv-install 1' || { echo "FALSE $f: not a zxv-install 1 file"; bad=1; continue; }
  dir=$(dirname -- "$f")
  while read -r kind arch disc group bytes sum cid path; do
    case "$kind" in core|layer|swarm|cartridge|doc) ;; *) continue ;; esac
    p="$dir/$path"
    case "$path" in */"$cid".*) ;; *) echo "FALSE $p: listed as $cid"; bad=1; continue ;; esac
    [ -f "$p" ] || { echo "FALSE $p: missing"; bad=1; continue; }
    [ "$(wc -c < "$p" | tr -d ' ')" = "$bytes" ] || { echo "FALSE $p: not $bytes bytes"; bad=1; continue; }
    [ "$(sha "$p")" = "$sum" ] || { echo "FALSE $p: sha256 differs"; bad=1; continue; }
    named "$p"
  done < "$f"
done
[ "$bad" = 0 ] && echo "all names are their CIDs" || exit 1
