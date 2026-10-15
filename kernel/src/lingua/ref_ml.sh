# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_ml.sh - ML and OCaml reference checks, sourced by ref_check.sh (run
# from kernel/; uses its d, G, T, need, fail, ran and diffs). Every golden
# .sml program runs through zxlang and MLton, every golden .ml program
# through zxlang and the ocaml toplevel, and the outputs must agree byte
# for byte with each other and with the stored .out. Then N programs from
# gen_ml.c in each dialect: the OCaml ones run one by one; the SML ones
# are compiled by MLton in one batch (each program wrapped in its own
# structure, a marker line printed before it) because one MLton build
# takes seconds, and the batch output is cut back into one file per seed.
# Reference stderr (warnings) is ignored; every generated program must
# also pass zxlang check (the twin round trip).
N=${LINGUA_DIFF_N:-100}
if need mlton; then
    for f in $G/*.sml; do
        b=$(basename "$f" .sml)
        mlton -output "$d/$b.mlton" "$f" > "$d/$b.mlton.log" 2>&1 || { cat "$d/$b.mlton.log"; echo "[FAIL] mlton $f"; fail=1; continue; }
        "$d/$b.mlton" > "$d/$b.sml.ref" 2>/dev/null
        "$d/zxlang" run "$f" > "$d/$b.sml.zl" 2>&1
        if cmp -s "$d/$b.sml.ref" "$d/$b.sml.zl" && cmp -s "$d/$b.sml.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, mlton and $f.out differ"; diff "$d/$b.sml.ref" "$d/$b.sml.zl" | head -20; fail=1; fi
    done
fi
if need ocaml; then
    for f in $G/*.ml; do
        b=$(basename "$f" .ml)
        ocaml "$f" > "$d/$b.ml.ref" 2>"$d/$b.ocaml.log" || { cat "$d/$b.ocaml.log"; echo "[FAIL] ocaml $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.ml.zl" 2>&1
        if cmp -s "$d/$b.ml.ref" "$d/$b.ml.zl" && cmp -s "$d/$b.ml.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, ocaml and $f.out differ"; diff "$d/$b.ml.ref" "$d/$b.ml.zl" | head -20; fail=1; fi
    done
fi
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_ml.c -o "$d/gen_ml" || { echo "[FAIL] gen_ml build"; fail=1; }
if [ -x "$d/gen_ml" ] && command -v mlton >/dev/null 2>&1; then
    : > "$d/gml_all.sml"
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_ml" sml $s > "$d/gml_$s.sml"
        { printf 'val _ = print "@@%d\\n"\nstructure S%d = struct\n' $s $s; cat "$d/gml_$s.sml"; printf 'end\n'; } >> "$d/gml_all.sml"
        s=$((s + 1))
    done
    if mlton -output "$d/gml_all" "$d/gml_all.sml" > "$d/gml_all.log" 2>&1 && "$d/gml_all" > "$d/gml_all.out" 2>&1; then
        awk -v d="$d" '/^@@[0-9]+$/ { f = d "/gml_" substr($0, 3) ".ref"; printf "" > f; next } { print > f }' "$d/gml_all.out"
        s=1
        while [ $s -le "$N" ]; do
            "$d/zxlang" run "$d/gml_$s.sml" > "$d/gml_$s.zl" 2>&1
            cmp -s "$d/gml_$s.ref" "$d/gml_$s.zl" || { echo "[FAIL] ML seed $s differs from mlton"; diff "$d/gml_$s.ref" "$d/gml_$s.zl" | head; cp "$d/gml_$s.sml" "$T/lingua_diff_fail.sml"; fail=1; break; }
            "$d/zxlang" check "$d/gml_$s.sml" > /dev/null || { echo "[FAIL] twin round trip of ML seed $s"; cp "$d/gml_$s.sml" "$T/lingua_diff_fail.sml"; fail=1; break; }
            diffs=$((diffs + 1)); s=$((s + 1))
        done
    else
        head -30 "$d/gml_all.log"; echo "[FAIL] mlton rejects or stops in the generated ML batch"; cp "$d/gml_all.sml" "$T/lingua_diff_fail.sml"; fail=1
    fi
fi
if [ -x "$d/gen_ml" ] && command -v ocaml >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_ml" ocaml $s > "$d/g.ml"
        ocaml "$d/g.ml" > "$d/g.ml.ref" 2>"$d/g.ml.log" || { cat "$d/g.ml.log"; echo "[FAIL] ocaml rejects generated seed $s"; cp "$d/g.ml" "$T/lingua_diff_fail.ml"; fail=1; break; }
        "$d/zxlang" run "$d/g.ml" > "$d/g.ml.zl" 2>&1
        cmp -s "$d/g.ml.ref" "$d/g.ml.zl" || { echo "[FAIL] OCaml seed $s differs from ocaml"; diff "$d/g.ml.ref" "$d/g.ml.zl" | head; cp "$d/g.ml" "$T/lingua_diff_fail.ml"; fail=1; break; }
        "$d/zxlang" check "$d/g.ml" > /dev/null || { echo "[FAIL] twin round trip of OCaml seed $s"; cp "$d/g.ml" "$T/lingua_diff_fail.ml"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
