#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxapp.sh — build libzxv, the apl-kron, ada-eve, cobol-sutra, fortran-trebago and mumps-myra
# pillars and the Z-Wasm kit (incrementally) and run test_zxapp.c: app manifests, and KRON, Eve,
# Sutra, Trebago, MYRA, WebAssembly and legacy (every Lingua language) app logic. Exits 3 when only the build failed.
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
# one test file per app (tests/<id>_zxapp.inc): the index that includes them all
ROWS=""
: >"$T/app_tests.inc"
for f in "$HERE"/tests/*_zxapp.inc; do
    [ -e "$f" ] || continue
    id="$(basename "$f" _zxapp.inc)"
    echo "#include \"tests/${id}_zxapp.inc\"" >>"$T/app_tests.inc"
    ROWS="$ROWS APP_TEST_ROW($id)"
done
echo "#define APP_TESTS_ROWS$ROWS" >>"$T/app_tests.inc"
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -DAPP_TESTS_INDEX="\"$T/app_tests.inc\"" -I"$HERE" -I"$R/kernel/include" -I"$R/kernel/src/modbind" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/pillars/ada-eve/include" -I"$R/sdk/pillars/cobol-sutra/include" -I"$R/sdk/pillars/fortran-trebago/include" -I"$R/sdk/pillars/mumps-myra/include" -I"$R/sdk/kits/z-wasm/include" -I"$R/sdk/kits/z-wasm/test" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxapp.c" "$HERE/zxapp.c" "$HERE/../chiglet/chg_vocab.c" "$HERE/zxwire.c" "$HERE/zxboard.c" "$HERE/zxapp_kron.c" "$HERE/zxapp_eve.c" "$HERE/zxapp_wasm.c" "$HERE/zxapp_sutra.c" "$HERE/zxapp_trebago.c" "$HERE/zxapp_myra.c" "$HERE/zxapp_lingua.c" "$HERE"/../lingua/zl_*.c "$HERE/../robin_debanks/sha256.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" "$R/sdk/pillars/ada-eve/build/libzeve.a" "$R/sdk/kits/z-wasm/build/libzwasm.a" \
    "$R/sdk/pillars/cobol-sutra/build/libzsutra.a" "$R/sdk/pillars/fortran-trebago/build/libztrebago.a" "$R/sdk/pillars/mumps-myra/build/libzmyra.a" -L"$R/sdk/libzxv/build" -lzxv -Wl,-rpath,"$R/sdk/libzxv/build" -lpthread \
    -o "$T/test_zxapp" || exit 3
"$T/test_zxapp" "$R/kernel/src"
# the languages the apps use, as docs/APP_LANGUAGE_COVERAGE.md says (LANGUAGE_FIT.tsv)
python3 "$R/tools/apps/lang_coverage.py" --check
# integers only: no float or double in an app's model or blocks (comments aside)
F="$(find "$HERE" -maxdepth 1 -name '*_model.[ch]'; find "$HERE/blocks" -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.cc' -o -name '*.rs' -o -name '*.zig')"
# shellcheck disable=SC2086 # F is a list of paths without spaces
if [ -n "$F" ] && sed -e 's://.*$::' -e 's:/\*.*\*/::g' $F | grep -nwE 'float|double|f32|f64|fabs|sqrt|sqrtf|powf' >/dev/null; then
    # shellcheck disable=SC2086
    grep -nwE 'float|double|f32|f64|fabs|sqrt|sqrtf|powf' $F | grep -vE '^[^:]*:[0-9]+:\s*(//|\*|/\*)'
    echo "[FAIL] floating point in an app's model or blocks: integers only (kernel/src/rational for fractions)"
    exit 1
fi
echo "[PASS] app models and blocks are integer-only"
# every app is really built and tested: its files exist and are used, test_zxapp.c loads it,
# test_apps.c tests its model, and the model calls only daemon methods that exist
python3 "$R/tools/apps/app_gate.py"
# one owner for every shared idea (CONCEPTS.tsv), as docs/APP_BLOCKS.md says
python3 "$R/tools/apps/concepts.py" --check
# the apps as one graph: what each unlocks, the build order, the weighted completion
python3 "$R/tools/apps/app_graph.py" --check
# the data packs: every source's licence class fits its pack, and packs build the same every time
python3 "$R/tools/apps/datapacks/datapack.py" check
python3 "$R/tools/apps/datapacks/test_datapack.py"
# every Lingua block is committed in its AI twin's canonical spelling (zl_twin)
L="$R/kernel/src/lingua"
"$CC" -std=c11 -O1 -I"$L" -I"$R/kernel/include" "$L/zxlang.c" "$L"/zl_*.c \
    -o "$T/zxlang" || exit 3
find "$HERE/blocks" -name '*.f' -o -name '*.bas' -o -name '*.fs' | sort | while read -r f; do
    "$T/zxlang" check --twin "$f" >/dev/null || { echo "[FAIL] $f is not in its twin's canonical form (zxlang twin)"; exit 1; }
done
echo "[PASS] Lingua blocks are in twin form"
