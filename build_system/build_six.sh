#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_six.sh — cross-compile the kernel for all six platforms in one run.
# Each architecture uses its own curated Makefile and toolchain, so the six
# never share a stale object and x86-only drivers never leak into the arm64,
# riscv or arm32 images. The SCP-144 144-dimensional lattice ROM and the
# fractal seed are generated at parse time into every ELF and verified by
# the kernel before [BOOT_OK] (scp144_embed.c).
#
#   x86_64  build_system/Makefile.x86_64    -> kernel_x86_64.elf
#   arm64   build_system/Makefile.arm64     -> kernel_arm64.elf
#   riscv64 build_system/Makefile.riscv     -> kernel_riscv.elf
#   riscv32 build_system/Makefile.riscv32   -> kernel_riscv32.elf
#   arm32   build_system/Makefile.arm32     -> zedec_xero_arm32.elf
#   wasm32  tools/web/build_boot_wasm.sh    -> web/boot/zxv_boot.wasm
#
# The universal Makefile (Makefile.universal, ARCH=...) is the single
# description of the common source set; its one-list-for-every-arch claim
# is not yet true (x86-only drivers in zxv_sources.mk fail on arm64/riscv),
# so the per-arch Makefiles are the real builders here.
#
# Before the first run on a fresh server:
#     sudo sh build_system/six_toolchains.sh
#
# usage: sh build_system/build_six.sh [--jobs N] [--clean]
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS=1
case "$(uname -s)" in
Linux) JOBS=$(nproc) ;;
Darwin) JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1) ;;
esac
CLEAN=0
while [ $# -gt 0 ]; do
    case "$1" in
    --jobs) JOBS=$2; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    *) echo "usage: build_six.sh [--jobs N] [--clean]" >&2; exit 2 ;;
    esac
done

OUT="$ROOT/build/six"
mkdir -p "$OUT"
echo "[build_six] jobs=$JOBS out=$OUT"

build_one() {
    arch=$1
    mk=$2
    target=$3
    log="$OUT/$arch.log"
    echo "=== $arch ($target) ==="
    if make -C "$ROOT" -f "build_system/$mk" "$target" -j"$JOBS" >"$log" 2>&1; then
        ls -la "$ROOT/$target" 2>/dev/null | awk '{print "  OK   "$5" bytes "$9}'
    else
        echo "  FAIL (tail of $log):"
        tail -8 "$log" | sed 's/^/    /'
        fail=1
    fi
}

fail=0
build_one x86_64 Makefile.x86_64 kernel_x86_64.elf
build_one arm64 Makefile.arm64 kernel_arm64.elf
build_one riscv64 Makefile.riscv kernel_riscv.elf
build_one riscv32 Makefile.riscv32 kernel_riscv32.elf
build_one arm32 Makefile.arm32 zedec_xero_arm32.elf

echo "=== wasm32 (web/boot engine) ==="
log="$OUT/wasm32.log"
if sh "$ROOT/tools/web/build_boot_wasm.sh" --out "$ROOT/build/six/zxv_boot.wasm" >"$log" 2>&1; then
    echo "  OK   $(ls -la "$ROOT/build/six/zxv_boot.wasm" 2>/dev/null | awk '{print $5}') bytes"
else
    echo "  FAIL (tail of $log):"
    tail -8 "$log" | sed 's/^/    /'
    fail=1
fi

echo
echo "=== summary ==="
for f in kernel_x86_64.elf kernel_arm64.elf kernel_riscv.elf kernel_riscv32.elf zedec_xero_arm32.elf build/six/zxv_boot.wasm; do
    if [ -f "$ROOT/$f" ]; then printf '  %-28s OK\n' "$f"; else printf '  %-28s MISSING\n' "$f"; fi
done
[ "$fail" = 0 ] && echo "[build_six] all six platforms built" || { echo "[build_six] one or more platforms failed"; exit 1; }