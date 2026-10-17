#!/bin/sh
# migrate_filebase.sh — the one-shot Filebase migration: bring the sealed
# release bucket from the build host, wipe the stale objects, publish the
# new tree (every file its own CID), and write the pin manifest.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/vps/migrate_filebase.sh [--dry-run]
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${ZXV_BUILD_HOST:-ionet@209.137.198.181}"
KEY="${ZXV_BUILD_KEY:-$HOME/.ssh/keys/key1}"
BUCKET_REMOTE="/home/ionet/zxv/src-int/build/ipfs-dist/bucket"
BUCKET_LOCAL="$ROOT/build/ipfs-dist/bucket"
PY=/usr/bin/python3   # the S3 listing parser needs expat: the system python

DRY=""
[ "${1:-}" = "--dry-run" ] && DRY="--dry-run"

echo "== 1/4 bringing the sealed bucket from $HOST =="
mkdir -p "$ROOT/build/ipfs-dist"
if [ -z "$DRY" ]; then
    rsync -a -e "ssh -i $KEY -o BatchMode=yes" "$HOST:$BUCKET_REMOTE" "$ROOT/build/ipfs-dist/"
fi
echo "   files: $(find "$BUCKET_LOCAL" -type f 2>/dev/null | wc -l)"

echo "== 2/4 wiping the stale objects =="
$PY "$ROOT/tools/vps/wipe_bucket.py" $DRY

echo "== 3/4 publishing the new tree (per-file CIDs) =="
if [ -z "$DRY" ]; then
    $PY "$ROOT/tools/vps/publish_release.py" "$BUCKET_LOCAL" --prefix ""
else
    $PY "$ROOT/tools/vps/publish_release.py" "$BUCKET_LOCAL" --dry-run
fi

echo "== 4/4 the pin manifest =="
ls -la "$ROOT/build/ipfs-dist/pin-manifest.json" 2>/dev/null || echo "   (written by the publish)"
echo "done. the gateway root now serves the new index.html; the S3 root needs"
echo "the Filebase console toggle for the same."
