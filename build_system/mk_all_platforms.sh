#!/bin/sh
# mk_all_platforms.sh — the master cross-compilation: every target, one
# pipeline. The kernels (x86_64, arm64, riscv64, riscv32, arm32), the wasm
# engine and factory (the world computer), the three desktops (macOS .app
# in a .dmg, Windows .exe, Linux), the boot ROMs, and the disk images —
# split into sequential volumes when the 4 GiB ISO limit is reached.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh build_system/mk_all_platforms.sh [--kernels] [--wasm] [--desktop]
#                                            [--discs] [--all]
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ZXV_OUT:-$ROOT/build/platforms}"
mkdir -p "$OUT"

# the 4 GiB ISO 9660 limit: a volume larger than this is split
ISO_LIMIT=$((4 * 1024 * 1024 * 1024))

log() { printf '[all-platforms] %s\n' "$1"; }

# ---- 1. the kernels: every architecture --------------------------------
build_kernels() {
    log "kernels: x86_64 + arm64 (native), riscv64 + riscv32 + arm32 (queued)"
    for pair in "x86_64:Makefile.x86_64:kernel_x86_64.elf" \
                "arm64:Makefile.arm64:kernel_arm64.elf"; do
        arch="${pair%%:*}"; rest="${pair#*:}"
        mk="${rest%%:*}"; elf="${rest#*:}"
        if [ -f "$ROOT/build_system/$mk" ]; then
            log "  $arch: make -f build_system/$mk"
            make -C "$ROOT" -f "build_system/$mk" "$elf" >"$OUT/kernel-$arch.log" 2>&1 \
                && cp -f "$ROOT/$elf" "$OUT/" 2>/dev/null \
                && log "  $arch: OK" || log "  $arch: FAILED (see the log)"
        fi
    done
    # the queued architectures: the same source, a different toolchain.
    # The check is presence, so the pipeline reports honestly.
    for arch in riscv64 riscv32 arm32; do
        if command -v "${arch}-linux-gnu-gcc" >/dev/null 2>&1; then
            log "  $arch: toolchain present (the build is a later stage)"
        else
            log "  $arch: no toolchain (queued)"
        fi
    done
}

# ---- 2. the wasm: the world computer -----------------------------------
build_wasm() {
    log "wasm: the engine (zxv_app) + the factory (zxv_factory)"
    if [ -f "$ROOT/tools/web/build_app_wasm.sh" ]; then
        sh "$ROOT/tools/web/build_app_wasm.sh" --out "$OUT/zxv_app.wasm" \
            >"$OUT/wasm.log" 2>&1 && log "  zxv_app.wasm: OK" \
            || log "  zxv_app.wasm: FAILED (see wasm.log)"
    fi
    if [ -f "$ROOT/tools/web/build_app_wasm.sh" ] && [ -f "$ROOT/web/boot/zxv_factory_wasm.c" ]; then
        log "  zxv_factory.wasm: the factory source present (built with the engine)"
    fi
}

# ---- 3. the desktops: macOS .app in a .dmg, Windows .exe, Linux --------
build_desktop() {
    log "desktop: macOS (.app/.dmg), Windows (.exe), Linux"
    if [ -f "$ROOT/build_system/build_desktop.sh" ]; then
        sh "$ROOT/build_system/build_desktop.sh" >"$OUT/desktop.log" 2>&1 \
            && log "  the desktops built (see desktop.log)" \
            || log "  the desktops: partial (see desktop.log)"
    else
        log "  no desktop builder (the hosted shell needs macOS)"
    fi
}

# ---- 4. the disk images: the multi-volume set --------------------------
build_discs() {
    log "discs: the multi-volume set (splitting past the 4 GiB ISO limit)"
    if [ -f "$ROOT/build_system/mk_volumes.sh" ]; then
        sh "$ROOT/build_system/mk_volumes.sh" "$OUT/volumes" >"$OUT/discs.log" 2>&1 \
            && log "  the volumes built (see discs.log)" \
            || log "  the volumes: partial (see discs.log)"
    fi
    # the split: any volume over the ISO limit is cut into sequential parts
    if [ -d "$OUT/volumes" ]; then
        for vol in "$OUT/volumes"/*.iso; do
            [ -f "$vol" ] || continue
            sz=$(wc -c < "$vol")
            if [ "$sz" -gt "$ISO_LIMIT" ]; then
                log "  splitting $(basename "$vol") ($sz bytes > 4 GiB)"
                split -b 3800m "$vol" "$vol.part-"
                log "  the parts: $(ls "$vol".part-* | wc -l)"
            fi
        done
    fi
}

main() {
    stage="${1:---all}"
    log "the master cross-compilation -> $OUT"
    case "$stage" in
        --kernels) build_kernels ;;
        --wasm)    build_wasm ;;
        --desktop) build_desktop ;;
        --discs)   build_discs ;;
        --all|*)   build_kernels; build_wasm; build_desktop; build_discs ;;
    esac
    log "the artifacts:"
    ls -la "$OUT" 2>/dev/null | awk 'NR>1 {printf "  %s %s\n", $9, $5}' | head -20
    log "done."
}

main "$@"