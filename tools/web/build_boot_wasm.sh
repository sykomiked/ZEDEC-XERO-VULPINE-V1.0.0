#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_boot_wasm.sh — build the browser's boot screen (web/boot/zxv_boot.wasm)
# from the same C as every other target: zui_boot, the vector renderer, the
# launch screen and the boot sounds.
#
#   build_boot_wasm.sh             build and write web/boot/zxv_boot.wasm
#   build_boot_wasm.sh --check     fail when the committed .wasm is not what
#                                  the source builds to
#   build_boot_wasm.sh --out FILE  build to FILE (any clang; for trying it out)
#
# The .wasm is a sealed file with its own CID, so it must rebuild byte for
# byte. Different clang releases emit different code from the same source, so
# the committed file is built with exactly one clang release, pinned below.
# Writing or checking the committed file refuses any other release; set CLANG
# to the pinned compiler if it is not first on PATH (e.g. CLANG=clang-23).
#
# Needs clang with the wasm32 target and wasm-ld.
set -eu
PIN=23.1.0 # the clang release web/boot/zxv_boot.wasm is built with
CLANG=${CLANG:-clang}
R=$(cd "$(dirname "$0")/../.." && pwd)
K="$R/kernel"
OUT="$R/web/boot/zxv_boot.wasm"

mode=write
case "${1:-}" in
"") ;;
--check) mode=check ;;
--out)
    [ -n "${2:-}" ] || { echo "usage: $0 [--check | --out FILE]" >&2; exit 2; }
    mode=out
    OUT=$2
    ;;
*) echo "usage: $0 [--check | --out FILE]" >&2; exit 2 ;;
esac

have=$("$CLANG" --version 2>/dev/null | sed -n '1s/.*clang version \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
if [ "$have" != "$PIN" ]; then
    if [ "$mode" != out ]; then
        echo "$CLANG is clang ${have:-(unknown)}; web/boot/zxv_boot.wasm is built with clang $PIN." >&2
        echo "Another release builds different bytes, so a different CID. Install clang $PIN" >&2
        echo "and set CLANG to it, or use --out FILE to build a copy for trying out." >&2
        exit 1
    fi
    echo "note: clang ${have:-(unknown)}, not the pinned $PIN; $OUT will not match the sealed file" >&2
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
# The source path is never in the output: no debug info, and --strip-all
# drops the name and producers sections.
"$CLANG" --target=wasm32 -mcpu=generic -std=c11 -O2 -ffreestanding -nostdlib \
    -Wall -Wextra -Werror \
    -I"$K/include" -I"$K/src/zui" -I"$K/src/font" -I"$K/src/launch" -I"$K/src/emu" \
    -I"$K/src/vinea" -I"$K/src/lpres" -I"$K/src/event_space" -I"$K/src/mlkem" -I"$K/src/modbind" \
    -I"$K/src" \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=1048576 \
    "$R/web/boot/zxv_boot_wasm.c" "$K/src/zui/zui_boot.c" "$K/src/zui/zui_vec.c" \
    "$K/src/zui/zui_progress.c" "$K/src/zui/zui_canvas.c" "$K/src/zui/zui_layout.c" \
    "$K/src/zui/zui_login.c" "$K/src/zui/zui_depth.c" \
    "$K/src/zui/zui_desk.c" "$K/src/zui/zui_wm.c" "$K/src/zui/zui_dock.c" \
    "$K/src/zui/zui_motion.c" "$K/src/zui/zui_palette.c" "$K/src/zui/zui_launch_theme.c" \
    "$K/src/zui/zui_input.c" \
    "$K/src/font/truetype.c" "$K/src/launch/zxl_launch.c" "$K/src/launch/zxl_synth.c" \
    "$K/src/vinea/vna_delta.c" "$K/src/vinea/vna_common.c" \
    "$K/src/lpres/lpres.c" "$K/src/mlkem/keccak.c" \
    "$K/src/pqsec/pq_mldsa87.c" "$K/src/pqsec/mldsa/fips202.c" \
    "$K/src/robin_debanks/sha256.c" \
    -o "$tmp"
if [ "$mode" = check ]; then
    if ! cmp -s "$tmp" "$OUT"; then
        echo "DIFFERS: $OUT is not what its source builds to with clang $PIN (run tools/web/build_boot_wasm.sh)" >&2
        exit 1
    fi
    echo "web/boot/zxv_boot.wasm matches its source (clang $PIN)"
else
    cp "$tmp" "$OUT"
    echo "wrote $OUT ($(wc -c < "$OUT") bytes, clang ${have:-unknown})"
fi
