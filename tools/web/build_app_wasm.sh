#!/bin/sh
# build_app_wasm.sh — the browser's app engine: the same pillar runtimes
# the bare metal uses (Eve, KRON, MYRA, Sutra, Trebago) plus the zxapp
# loader, compiled to wasm32. The browser loads a .zxapp and runs it with
# the identical semantics the node enforces — no server round trip, no
# re-implementation. The tribute module rides along so the browser's
# ledger applies the same four public destinations.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/web/build_app_wasm.sh [--out FILE]
set -eu

CLANG=${CLANG:-clang}
R=$(cd "$(dirname "$0")/../.." && pwd)
K="$R/kernel"
P="$R/sdk/pillars"
OUT="$R/web/boot/zxv_app.wasm"
[ "${1:-}" = "--out" ] && OUT="${2:?--out needs a file}"

"$CLANG" --target=wasm32 -mcpu=generic -std=c11 -O2 -ffreestanding -nostdlib \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=2097152 \
    -Wl,--export=zxw_load -Wl,--export=zxw_check -Wl,--export=zxw_call \
    -Wl,--export=zxw_tribute_address -Wl,--export=zxw_tribute_split \
    -Wl,--export=zxw_free -Wl,--export=zxw_error \
    -Wl,--export=malloc -Wl,--export=free \
    -Dmemset=fs_memset -Dmemcpy=fs_memcpy -Dstrlen=fs_strlen -Dstrcmp=fs_strcmp \
    -Dstrcpy=fs_strcpy -Dmalloc=fs_malloc -Dcalloc=fs_calloc -Dmemcmp=fs_memcmp -Dmemchr=fs_memchr -Drealloc=fs_realloc -Dmemmove=fs_memmove \
    -include "$K/include/freestanding.h" \
    -I"$K/include/freestanding_stubs" -I"$K/include" -I"$K/src" -I"$K/src/apps" -I"$K/src/lpres" -I"$K/src/rational" \
    -I"$K/src/event_space" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/surplus" \
    -I"$K/src/mlkem" -I"$K/src/tribute" \
    -I"$P/apl-kron/src" -I"$P/ada-eve/src" -I"$P/cobol-sutra/src" \
    -I"$P/fortran-trebago/src" -I"$P/mumps-myra/src" \
    -I"$P/apl-kron/include" -I"$P/ada-eve/include" -I"$P/cobol-sutra/include" \
    -I"$P/fortran-trebago/include" -I"$P/mumps-myra/include" -I"$R/sdk/libzxv/include" \
    "$R/web/boot/zxv_app_wasm.c" "$K/freestanding.c" \
    "$K/src/apps/zxapp.c" "$K/src/apps/zxapp_eve.c" "$K/src/apps/zxapp_kron.c" \
    "$K/src/apps/zxapp_myra.c" "$K/src/apps/zxapp_sutra.c" \
    "$K/src/apps/zxapp_trebago.c" \
    "$K/src/tribute/tribute.c" "$K/src/robin_debanks/sha256.c" "$K/src/rational/rational.c" "$K/src/mlkem/keccak.c" "$R/web/boot/zxv_rat_shim.c" "$R/web/boot/zxv_ti3_shim.c" "$R/web/boot/zxv_lp_shim.c" "$K/src/lpres/lpres.c" \
    "$P"/apl-kron/src/*.c "$P"/ada-eve/src/*.c "$P"/cobol-sutra/src/*.c \
    "$P"/fortran-trebago/src/*.c "$P"/mumps-myra/src/*.c \
    -o "$OUT"
ls -la "$OUT" | awk '{printf "app engine: %s (%.1f KB)\n", $9, $5/1024}'
