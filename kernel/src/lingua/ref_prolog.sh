# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_prolog.sh - sourced by ref_check.sh (uses its d, G, T, fail, ran,
# diffs and need). Runs the Prolog goldens through SWI-Prolog (swipl -q,
# halting after the initialization goals), requiring zxlang, swipl and the
# .out file to agree byte for byte, then a differential run of gen_prolog.c
# programs. swipl's stderr (singleton warnings) is ignored, but a program
# swipl reports an error for is a failure of the generator.
if need swipl; then
    for f in $G/*.pro; do
        b=$(basename "$f" .pro)
        swipl -q -t halt "$f" > "$d/$b.ref" 2>"$d/$b.swipl.log" || { cat "$d/$b.swipl.log"; echo "[FAIL] swipl $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.zl" 2>&1
        if cmp -s "$d/$b.ref" "$d/$b.zl" && cmp -s "$d/$b.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, swipl and $f.out differ"; diff "$d/$b.ref" "$d/$b.zl" | head -20; fail=1; fi
    done
fi
# Differential: random well-defined programs (gen_prolog.c).
N=${LINGUA_DIFF_N:-100}
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_prolog.c -o "$d/gen_prolog" || { echo "[FAIL] gen_prolog build"; exit 1; }
if command -v swipl >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_prolog" $s > "$d/g.pro"
        swipl -q -t halt "$d/g.pro" > "$d/g.ref" 2>"$d/g.log"
        if [ $? -ne 0 ] || grep -q '^ERROR' "$d/g.log"; then cat "$d/g.log"; echo "[FAIL] swipl rejects generated seed $s"; cp "$d/g.pro" "$T/lingua_diff_fail.pro"; fail=1; break; fi
        "$d/zxlang" run "$d/g.pro" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Prolog seed $s differs from swipl"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.pro" "$T/lingua_diff_fail.pro"; fail=1; break; }
        "$d/zxlang" check "$d/g.pro" > /dev/null || { echo "[FAIL] twin round trip of Prolog seed $s"; cp "$d/g.pro" "$T/lingua_diff_fail.pro"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
