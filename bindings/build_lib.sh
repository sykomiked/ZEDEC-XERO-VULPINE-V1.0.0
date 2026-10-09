#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# build_lib.sh — build libzxlegacy (static + shared) from the ABI wrapper and
# the freestanding bridge modules, then build and run the C ABI self-test and
# the GnuCOBOL and gfortran examples when those compilers are present.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
LEG="$ROOT/kernel/src/legacy"
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"

CC=${CC:-gcc}
CFLAGS="-std=c11 -O2 -Wall -Wextra -fPIC -I$HERE -I$LEG"
MODS="sip sdp gsm_encode ss7 tcap isup_map dtmf ebcdic cobol fortran"

echo "== compiling bridge modules =="
OBJS=""
for m in $MODS; do
    $CC $CFLAGS -c "$LEG/$m.c" -o "$OUT/$m.o"
    OBJS="$OBJS $OUT/$m.o"
done
$CC $CFLAGS -c "$HERE/zx_legacy_api.c" -o "$OUT/zx_legacy_api.o"
OBJS="$OBJS $OUT/zx_legacy_api.o"

echo "== archiving libzxlegacy.a and libzxlegacy.so =="
ar rcs "$OUT/libzxlegacy.a" $OBJS
$CC -shared -o "$OUT/libzxlegacy.so" $OBJS

echo "== C ABI self-test =="
$CC $CFLAGS "$HERE/test_abi.c" "$OUT/libzxlegacy.a" -o "$OUT/test_abi"
"$OUT/test_abi"

if command -v cobc >/dev/null 2>&1; then
    echo "== GnuCOBOL example =="
    cobc -x -std=default -I"$HERE/cobol" "$HERE/cobol/payvalidate.cob" -o "$OUT/payvalidate"
    #  GnuCOBOL resolves CALLed names at runtime; preload the bridge library so
    #  its symbols are visible to cob_resolve (no DT_NEEDED is recorded because
    #  the program references the symbols only through dynamic CALL).
    LD_LIBRARY_PATH="$OUT" LD_PRELOAD="$OUT/libzxlegacy.so" "$OUT/payvalidate"
else
    echo "== GnuCOBOL (cobc) not installed: skipping COBOL example =="
fi

if command -v gfortran >/dev/null 2>&1; then
    echo "== gfortran example =="
    gfortran -J"$OUT" "$HERE/fortran/zx_legacy_bind.f90" "$HERE/fortran/hfp_demo.f90" \
        -L"$OUT" -lzxlegacy -o "$OUT/hfp_demo"
    LD_LIBRARY_PATH="$OUT" "$OUT/hfp_demo"
else
    echo "== gfortran not installed: skipping Fortran example =="
fi

if command -v fpc >/dev/null 2>&1; then
    echo "== Free Pascal example =="
    ( cd "$OUT" && fpc -Fl"$OUT" "$HERE/pascal/zxdemo.pas" -o"$OUT/zxdemo" >/dev/null )
    LD_LIBRARY_PATH="$OUT" "$OUT/zxdemo"
else
    echo "== fpc not installed: skipping Pascal example =="
fi

if command -v gnatmake >/dev/null 2>&1; then
    echo "== Ada example =="
    ( cd "$OUT" && gnatmake -I"$HERE/ada" "$HERE/ada/zx_demo.adb" -o "$OUT/zx_demo" \
        -largs -L"$OUT" -lzxlegacy >/dev/null )
    LD_LIBRARY_PATH="$OUT" "$OUT/zx_demo"
else
    echo "== gnatmake not installed: skipping Ada example =="
fi

echo "== all binding builds done =="
