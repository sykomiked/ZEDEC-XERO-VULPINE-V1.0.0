#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxapp.sh — build libzxv, the apl-kron, ada-eve, cobol-sutra, fortran-trebago and mumps-myra
# pillars and the Z-Wasm kit (incrementally) and run test_zxapp.c: app manifests, and KRON, Eve,
# Sutra, Trebago, MYRA, WebAssembly and legacy (Lingua: FORTRAN 77, BASIC, Forth) app logic. Exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-zxapp.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
make -s -C "$R/sdk/pillars/apl-kron" CC="$CC" build/libzkron.a >"$T/kron.log" 2>&1 || { tail -20 "$T/kron.log"; exit 3; }
make -s -C "$R/sdk/pillars/ada-eve" CC="$CC" build/libzeve.a >"$T/eve.log" 2>&1 || { tail -20 "$T/eve.log"; exit 3; }
make -s -C "$R/sdk/pillars/cobol-sutra" CC="$CC" build/libzsutra.a >"$T/sutra.log" 2>&1 || { tail -20 "$T/sutra.log"; exit 3; }
make -s -C "$R/sdk/pillars/fortran-trebago" CC="$CC" FORTRAN=0 build/libztrebago.a >"$T/trebago.log" 2>&1 || { tail -20 "$T/trebago.log"; exit 3; }
make -s -C "$R/sdk/pillars/mumps-myra" CC="$CC" build/libzmyra.a >"$T/myra.log" 2>&1 || { tail -20 "$T/myra.log"; exit 3; }
make -s -C "$R/sdk/kits/z-wasm" CC="$CC" build/libzwasm.a >"$T/wasm.log" 2>&1 || { tail -20 "$T/wasm.log"; exit 3; }
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$HERE" -I"$R/kernel/include" -I"$R/kernel/src/modbind" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/pillars/ada-eve/include" -I"$R/sdk/pillars/cobol-sutra/include" -I"$R/sdk/pillars/fortran-trebago/include" -I"$R/sdk/pillars/mumps-myra/include" -I"$R/sdk/kits/z-wasm/include" -I"$R/sdk/kits/z-wasm/test" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxapp.c" "$HERE/zxapp.c" "$HERE/zxwire.c" "$HERE/zxboard.c" "$HERE/zxapp_kron.c" "$HERE/zxapp_eve.c" "$HERE/zxapp_wasm.c" "$HERE/zxapp_sutra.c" "$HERE/zxapp_trebago.c" "$HERE/zxapp_myra.c" "$HERE/zxapp_lingua.c" "$HERE/../lingua/zl_ir.c" "$HERE/../lingua/zl_basic.c" "$HERE/../lingua/zl_fortran.c" "$HERE/../lingua/zl_forth.c" "$HERE/../lingua/zl_pack.c" "$HERE/../robin_debanks/sha256.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" "$R/sdk/pillars/ada-eve/build/libzeve.a" "$R/sdk/kits/z-wasm/build/libzwasm.a" \
    "$R/sdk/pillars/cobol-sutra/build/libzsutra.a" "$R/sdk/pillars/fortran-trebago/build/libztrebago.a" "$R/sdk/pillars/mumps-myra/build/libzmyra.a" -L"$R/sdk/libzxv/build" -lzxv -Wl,-rpath,"$R/sdk/libzxv/build" -lpthread \
    -o "$T/test_zxapp" || exit 3
"$T/test_zxapp" "$R/kernel/src"
# the languages the apps use, as docs/APP_LANGUAGE_COVERAGE.md says (LANGUAGE_FIT.tsv)
python3 "$R/tools/apps/lang_coverage.py" --check
# every Lingua block is committed in its AI twin's canonical spelling (zl_twin)
L="$R/kernel/src/lingua"
"$CC" -std=c11 -O1 -I"$L" -I"$R/kernel/include" "$L/zxlang.c" "$L/zl_ir.c" "$L/zl_basic.c" "$L/zl_fortran.c" \
    "$L/zl_forth.c" "$L/zl_pack.c" -o "$T/zxlang" || exit 3
find "$HERE/blocks" -name '*.f' -o -name '*.bas' -o -name '*.fs' | sort | while read -r f; do
    "$T/zxlang" check --twin "$f" >/dev/null || { echo "[FAIL] $f is not in its twin's canonical form (zxlang twin)"; exit 1; }
done
echo "[PASS] Lingua blocks are in twin form"
