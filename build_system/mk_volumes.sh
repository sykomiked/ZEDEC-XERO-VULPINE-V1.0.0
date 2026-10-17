#!/bin/sh
# mk_volumes.sh — the multi-volume disk set: the core (the bootable ZXV
# disc), the 407 userland apps with their manifests, and the testbench
# (the wasm engine + the 7:1 suites). ISO 9660's 4 GB ceiling and the
# DVD-5 limit never bind because each volume is its own image, and UDF
# is used when xorriso offers it.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh build_system/mk_volumes.sh [release-dir]
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REL="${1:-$ROOT/build/volumes}"
STAGE="$ROOT/build/staging"
mkdir -p "$REL" "$STAGE/core" "$STAGE/userland" "$STAGE/testbench"

echo "== staging volume 1: the core (the bootable ZXV disc) =="
# the golden disc is the bootable core; it rides volume 1 whole
for f in "$ROOT"/dist/discs/*.img; do
    [ -f "$f" ] && cp -f "$f" "$STAGE/core/" 2>/dev/null || true
done
[ -f "$ROOT/kernel_x86_64.elf" ] && cp -f "$ROOT/kernel_x86_64.elf" "$STAGE/core/"
[ -f "$ROOT/kernel_arm64.elf" ] && cp -f "$ROOT/kernel_arm64.elf" "$STAGE/core/"

echo "== staging volume 2: the 407 userland apps =="
# the generated manifests + the exhaustive specs + the icons
cp -f "$ROOT"/tools/server-tasks/manifests/*.md "$STAGE/userland/" 2>/dev/null || true
cp -f "$ROOT"/tools/server-tasks/exhaustive/*.md "$STAGE/userland/" 2>/dev/null || true
mkdir -p "$STAGE/userland/icons" "$STAGE/userland/apps"
cp -f "$ROOT"/zxv-brand/app-icons/zxvg/*.zxvg "$STAGE/userland/icons/" 2>/dev/null || true
cp -f "$ROOT"/tools/agent_loop/results/app_*.txt "$STAGE/userland/apps/" 2>/dev/null || true

echo "== staging volume 3: the polyglot testbench =="
[ -f "$ROOT/web/boot/zxv_app.wasm" ] && cp -f "$ROOT/web/boot/zxv_app.wasm" "$STAGE/testbench/"
cp -f "$ROOT"/tools/server-tasks/tests/*.zxapp "$STAGE/testbench/" 2>/dev/null || true
cp -f "$ROOT"/tools/agent_loop/patterns.py "$STAGE/testbench/" 2>/dev/null || true
cp -f "$ROOT"/tools/agent_loop/decoding/*.md "$STAGE/testbench/" 2>/dev/null || true

pack() {
    name="$1"; label="$2"; dir="$3"
    if command -v xorriso >/dev/null 2>&1; then
        xorriso -as mkisofs -r -V "$label" -o "$REL/$name.iso" "$dir" >/dev/null 2>&1 \
            && echo "   $name.iso ($(du -sh "$REL/$name.iso" | cut -f1))" \
            || echo "   $name.iso FAILED"
    elif command -v genisoimage >/dev/null 2>&1; then
        genisoimage -r -V "$label" -o "$REL/$name.iso" "$dir" >/dev/null 2>&1 \
            && echo "   $name.iso ($(du -sh "$REL/$name.iso" | cut -f1))" \
            || echo "   $name.iso FAILED"
    else
        # no ISO tool: a tar of the volume is the portable equivalent
        tar czf "$REL/$name.tar.gz" -C "$(dirname "$dir")" "$(basename "$dir")" \
            && echo "   $name.tar.gz ($(du -sh "$REL/$name.tar.gz" | cut -f1)) (no xorriso)"
    fi
}

echo "== packing the volumes =="
pack zxv_core_m5 ZXV_CORE_M5 "$STAGE/core"
pack zxv_userland_407 ZXV_APPS_407 "$STAGE/userland"
pack zxv_polyglot_testbench ZXV_TESTBENCH "$STAGE/testbench"
echo "the multi-volume set is in $REL"
