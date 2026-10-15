# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_clojure.sh - sourced by ref_check.sh (uses its d, G, T, fail, ran,
# diffs and need). Runs the Clojure goldens (.clj) through the clojure CLI
# ("clojure file.clj"), requiring zxlang, clojure and the .out file to
# agree byte for byte, then a differential run of gen_lisp.c programs in
# its clj mode. Each clojure run starts a JVM (about a second), so the
# default of 100 programs takes a minute or two.
if need clojure; then
    for f in $G/*.clj; do
        b=$(basename "$f" .clj)
        clojure "$f" > "$d/$b.clj.ref" 2>"$d/$b.clj.log" || { cat "$d/$b.clj.log"; echo "[FAIL] clojure $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.clj.zl" 2>&1
        if cmp -s "$d/$b.clj.ref" "$d/$b.clj.zl" && cmp -s "$d/$b.clj.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, clojure and $f.out differ"; diff "$d/$b.clj.ref" "$d/$b.clj.zl" | head -20; fail=1; fi
    done
    N=${LINGUA_DIFF_N:-100}
    gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_lisp.c -o "$d/gen_lisp" || { echo "[FAIL] gen_lisp build"; exit 1; }
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lisp" clj $s > "$d/g.clj"
        clojure "$d/g.clj" > "$d/g.ref" 2>"$d/g.log" || { cat "$d/g.log"; echo "[FAIL] clojure rejects generated seed $s"; cp "$d/g.clj" "$T/lingua_diff_fail.clj"; fail=1; break; }
        "$d/zxlang" run "$d/g.clj" > "$d/g.zl" 2>&1
        cmp -s "$d/g.ref" "$d/g.zl" || { echo "[FAIL] Clojure seed $s differs from clojure"; diff "$d/g.ref" "$d/g.zl" | head; cp "$d/g.clj" "$T/lingua_diff_fail.clj"; fail=1; break; }
        "$d/zxlang" check "$d/g.clj" > /dev/null || { echo "[FAIL] twin round trip of Clojure seed $s"; cp "$d/g.clj" "$T/lingua_diff_fail.clj"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
