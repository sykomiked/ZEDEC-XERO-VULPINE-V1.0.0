# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_commonlisp.sh - sourced by ref_check.sh (uses its d, G, T, fail, ran,
# diffs and need). Runs the Common Lisp goldens (.cl) through sbcl,
# requiring zxlang, sbcl and the .out file to agree byte for byte, then a
# differential run of gen_lisp.c programs in its cl mode. sbcl reads a .cl
# file as Lisp source; its stderr (style warnings) is ignored.
if need sbcl; then
    for f in $G/*.cl; do
        b=$(basename "$f" .cl)
        sbcl --script "$f" > "$d/$b.cl.ref" 2>"$d/$b.cl.log" || { cat "$d/$b.cl.log"; echo "[FAIL] sbcl $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.cl.zl" 2>&1
        if cmp -s "$d/$b.cl.ref" "$d/$b.cl.zl" && cmp -s "$d/$b.cl.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, sbcl and $f.out differ"; diff "$d/$b.cl.ref" "$d/$b.cl.zl" | head -20; fail=1; fi
    done
    N=${LINGUA_DIFF_N:-100}
    gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_lisp.c -o "$d/gen_lisp" || { echo "[FAIL] gen_lisp build"; exit 1; }
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lisp" cl $s > "$d/g.cl"
        sbcl --script "$d/g.cl" > "$d/g.ref" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] sbcl rejects generated Common Lisp seed $s"; cp "$d/g.cl" "$T/lingua_diff_fail.cl"; fail=1; break; }
        "$d/zxlang" run "$d/g.cl" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Common Lisp seed $s differs from sbcl"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.cl" "$T/lingua_diff_fail.cl"; fail=1; break; }
        "$d/zxlang" check "$d/g.cl" > /dev/null || { echo "[FAIL] twin round trip of Common Lisp seed $s"; cp "$d/g.cl" "$T/lingua_diff_fail.cl"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
