#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_check.sh - run every golden program through zxlang and through the
# language's reference implementation (gfortran for FORTRAN, gforth for
# Forth), and require all three of zxlang, the reference and the stored
# .out file to agree byte for byte. Run from kernel/. A missing reference
# tool is a SKIP unless ZXV_REQUIRE_LINGUA_REF=1. BASIC has no reference in
# CI (bwBASIC crashes in batch mode); its .out files are checked by test_zl.
T=${ZXV_TMP:-/tmp}
d=$(mktemp -d "$T/zl_ref.XXXXXX") || exit 1
G=src/lingua/golden
gcc -std=c11 -O2 -Wall -Werror -Wextra -Isrc/lingua src/lingua/zxlang.c src/lingua/zl_*.c -o "$d/zxlang" || { echo "[FAIL] zxlang build"; exit 1; }
fail=0
ran=0
need() {
    if command -v "$1" >/dev/null 2>&1; then return 0; fi
    if [ "$ZXV_REQUIRE_LINGUA_REF" = 1 ]; then echo "[FAIL] lingua reference: $1 not installed"; fail=1; return 1; fi
    echo "[SKIP] lingua reference: $1 not installed"
    return 1
}
if need gfortran; then
    for f in $G/*.f; do
        b=$(basename "$f" .f)
        gfortran -std=legacy -o "$d/$b" "$f" 2>"$d/$b.gf.log" || { cat "$d/$b.gf.log"; echo "[FAIL] gfortran $f"; fail=1; continue; }
        "$d/$b" > "$d/$b.ref"
        "$d/zxlang" run "$f" > "$d/$b.zl" 2>&1
        if cmp -s "$d/$b.ref" "$d/$b.zl" && cmp -s "$d/$b.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, gfortran and $f.out differ"; diff "$d/$b.ref" "$d/$b.zl" | head -20; fail=1; fi
    done
fi
if need gforth; then
    for f in $G/*.fs; do
        b=$(basename "$f" .fs)
        gforth "$f" -e bye > "$d/$b.ref" 2>"$d/$b.gforth.log" || { cat "$d/$b.gforth.log"; echo "[FAIL] gforth $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.zl" 2>&1
        if cmp -s "$d/$b.ref" "$d/$b.zl" && cmp -s "$d/$b.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, gforth and $f.out differ"; diff "$d/$b.ref" "$d/$b.zl" | head -20; fail=1; fi
    done
fi
# Differential: random well-defined programs (gen_zl.c) through both.
N=${LINGUA_DIFF_N:-100}
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_zl.c -o "$d/gen_zl" || { echo "[FAIL] gen_zl build"; exit 1; }
diffs=0
if command -v gfortran >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_zl" f $s > "$d/g.f"
        gfortran -std=legacy -o "$d/g" "$d/g.f" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] gfortran rejects generated seed $s"; fail=1; break; }
        "$d/g" > "$d/g.ref"
        "$d/zxlang" run "$d/g.f" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] FORTRAN seed $s differs from gfortran"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.f" "$T/lingua_diff_fail.f"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
if command -v gforth >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_zl" fs $s > "$d/g.fs"
        gforth "$d/g.fs" -e bye > "$d/g.ref" 2>&1
        "$d/zxlang" run "$d/g.fs" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Forth seed $s differs from gforth"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.fs" "$T/lingua_diff_fail.fs"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
# Per-language reference checks: each ref_<lang>.sh adds to ran, diffs, fail.
for x in src/lingua/ref_*.sh; do
    case "$x" in */ref_check.sh) continue ;; esac
    [ -f "$x" ] && . "$x"
done
for f in $G/*; do
    case "$f" in *.out) continue ;; esac
    "$d/zxlang" check "$f" > /dev/null || { echo "[FAIL] twin round trip $f"; fail=1; }
done
rm -rf "$d"
[ $fail = 0 ] && echo "[PASS] lingua reference check ($ran golden and $diffs generated programs byte-identical to gfortran/gforth/fpc; twins round-trip)"
exit $fail
