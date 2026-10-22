# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_lisp.sh - sourced by ref_check.sh (uses its d, G, T, fail, ran, diffs
# and need). Runs the LISP goldens through sbcl and the Scheme goldens
# through guile, requiring zxlang, the reference and the .out file to agree
# byte for byte, then a differential run of gen_lisp.c programs in both
# dialects. Reference stderr (style warnings, guile's banner) is ignored.
# A golden's .in goes to the reference on stdin and its integers to
# zxlang as inputs (ref_in and ref_args, from ref_check.sh).
if need sbcl; then
    for f in $G/*.lisp; do
        b=$(basename "$f" .lisp)
        sbcl --script "$f" < "$(ref_in "$f")" > "$d/$b.ref" 2>"$d/$b.sbcl.log" || { cat "$d/$b.sbcl.log"; echo "[FAIL] sbcl $f"; fail=1; continue; }
        "$d/zxlang" run "$f" $(ref_args "$f") > "$d/$b.zl" 2>&1
        if cmp -s "$d/$b.ref" "$d/$b.zl" && cmp -s "$d/$b.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, sbcl and $f.out differ"; diff "$d/$b.ref" "$d/$b.zl" | head -20; fail=1; fi
    done
fi
if need guile; then
    for f in $G/*.scm; do
        b=$(basename "$f" .scm)
        guile --no-auto-compile "$f" < "$(ref_in "$f")" > "$d/$b.ref" 2>"$d/$b.guile.log" || { cat "$d/$b.guile.log"; echo "[FAIL] guile $f"; fail=1; continue; }
        "$d/zxlang" run "$f" $(ref_args "$f") > "$d/$b.zl" 2>&1
        if cmp -s "$d/$b.ref" "$d/$b.zl" && cmp -s "$d/$b.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, guile and $f.out differ"; diff "$d/$b.ref" "$d/$b.zl" | head -20; fail=1; fi
    done
fi
# Differential: random well-defined programs (gen_lisp.c) in both dialects.
N=${LINGUA_DIFF_N:-100}
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_lisp.c -o "$d/gen_lisp" || { echo "[FAIL] gen_lisp build"; exit 1; }
if command -v sbcl >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lisp" lisp $s > "$d/g.lisp"
        sbcl --script "$d/g.lisp" > "$d/g.ref" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] sbcl rejects generated seed $s"; cp "$d/g.lisp" "$T/lingua_diff_fail.lisp"; fail=1; break; }
        "$d/zxlang" run "$d/g.lisp" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] LISP seed $s differs from sbcl"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.lisp" "$T/lingua_diff_fail.lisp"; fail=1; break; }
        "$d/zxlang" check "$d/g.lisp" > /dev/null || { echo "[FAIL] twin round trip of LISP seed $s"; cp "$d/g.lisp" "$T/lingua_diff_fail.lisp"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
if command -v guile >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lisp" scm $s > "$d/g.scm"
        guile --no-auto-compile "$d/g.scm" > "$d/g.ref" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] guile rejects generated seed $s"; cp "$d/g.scm" "$T/lingua_diff_fail.scm"; fail=1; break; }
        "$d/zxlang" run "$d/g.scm" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Scheme seed $s differs from guile"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.scm" "$T/lingua_diff_fail.scm"; fail=1; break; }
        "$d/zxlang" check "$d/g.scm" > /dev/null || { echo "[FAIL] twin round trip of Scheme seed $s"; cp "$d/g.scm" "$T/lingua_diff_fail.scm"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
