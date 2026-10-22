# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_racket.sh - sourced by ref_check.sh (uses its d, G, T, fail, ran,
# diffs and need). Runs the Racket goldens (.rkt) through racket, requiring
# zxlang, racket and the .out file to agree byte for byte, then a
# differential run of gen_lisp.c programs in its rkt mode (which leaves
# some values bare at the top level for racket to print itself).
# A golden's .in goes to the reference on stdin and its integers to
# zxlang as inputs (ref_in and ref_args, from ref_check.sh).
if need racket; then
    for f in $G/*.rkt; do
        b=$(basename "$f" .rkt)
        racket "$f" < "$(ref_in "$f")" > "$d/$b.rkt.ref" 2>"$d/$b.rkt.log" || { cat "$d/$b.rkt.log"; echo "[FAIL] racket $f"; fail=1; continue; }
        "$d/zxlang" run "$f" $(ref_args "$f") > "$d/$b.rkt.zl" 2>&1
        if cmp -s "$d/$b.rkt.ref" "$d/$b.rkt.zl" && cmp -s "$d/$b.rkt.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, racket and $f.out differ"; diff "$d/$b.rkt.ref" "$d/$b.rkt.zl" | head -20; fail=1; fi
    done
    N=${LINGUA_DIFF_N:-100}
    gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_lisp.c -o "$d/gen_lisp" || { echo "[FAIL] gen_lisp build"; exit 1; }
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lisp" rkt $s > "$d/g.rkt"
        racket "$d/g.rkt" > "$d/g.ref" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] racket rejects generated seed $s"; cp "$d/g.rkt" "$T/lingua_diff_fail.rkt"; fail=1; break; }
        "$d/zxlang" run "$d/g.rkt" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Racket seed $s differs from racket"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.rkt" "$T/lingua_diff_fail.rkt"; fail=1; break; }
        "$d/zxlang" check "$d/g.rkt" > /dev/null || { echo "[FAIL] twin round trip of Racket seed $s"; cp "$d/g.rkt" "$T/lingua_diff_fail.rkt"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
