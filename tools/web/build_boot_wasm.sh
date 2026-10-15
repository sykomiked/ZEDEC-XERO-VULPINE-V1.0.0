#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_boot_wasm.sh — build the browser's boot screen (web/boot/zxv_boot.wasm)
# from the same C as every other target: zui_boot, the vector renderer, the
# launch screen and the boot sounds. --check builds to a temporary file and
# fails when the committed .wasm is not what the source builds to.
#
# Needs clang with the wasm32 target and wasm-ld.
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
K="$R/kernel"
OUT="$R/web/boot/zxv_boot.wasm"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
clang --target=wasm32 -std=c11 -O2 -ffreestanding -nostdlib -Wall -Wextra -Werror \
    -I"$K/include" -I"$K/src/zui" -I"$K/src/font" -I"$K/src/launch" \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=1048576 \
    "$R/web/boot/zxv_boot_wasm.c" "$K/src/zui/zui_boot.c" "$K/src/zui/zui_vec.c" \
    "$K/src/zui/zui_progress.c" "$K/src/zui/zui_canvas.c" "$K/src/zui/zui_layout.c" \
    "$K/src/font/truetype.c" "$K/src/launch/zxl_launch.c" "$K/src/launch/zxl_synth.c" \
    -o "$tmp"
if [ "${1:-}" = "--check" ]; then
    if ! cmp -s "$tmp" "$OUT"; then
        echo "DIFFERS: $OUT is not what its source builds to (run tools/web/build_boot_wasm.sh)" >&2
        exit 1
    fi
    echo "web/boot/zxv_boot.wasm matches its source"
else
    cp "$tmp" "$OUT"
    echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
fi
