#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxapp.sh — build libzxv and the apl-kron and ada-eve pillars and the Z-Wasm kit (incrementally) and run
# test_zxapp.c: app manifests, and KRON, Eve and WebAssembly app logic. Exits 3 when only the
# build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-zxapp.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
make -s -C "$R/sdk/pillars/apl-kron" CC="$CC" build/libzkron.a >"$T/kron.log" 2>&1 || { tail -20 "$T/kron.log"; exit 3; }
make -s -C "$R/sdk/pillars/ada-eve" CC="$CC" build/libzeve.a >"$T/eve.log" 2>&1 || { tail -20 "$T/eve.log"; exit 3; }
make -s -C "$R/sdk/kits/z-wasm" CC="$CC" build/libzwasm.a >"$T/wasm.log" 2>&1 || { tail -20 "$T/wasm.log"; exit 3; }
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$HERE" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/pillars/ada-eve/include" -I"$R/sdk/kits/z-wasm/include" -I"$R/sdk/kits/z-wasm/test" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxapp.c" "$HERE/zxapp.c" "$HERE/zxapp_kron.c" "$HERE/zxapp_eve.c" "$HERE/zxapp_wasm.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" "$R/sdk/pillars/ada-eve/build/libzeve.a" "$R/sdk/kits/z-wasm/build/libzwasm.a" -L"$R/sdk/libzxv/build" -lzxv -Wl,-rpath,"$R/sdk/libzxv/build" -lpthread \
    -o "$T/test_zxapp" || exit 3
"$T/test_zxapp"
