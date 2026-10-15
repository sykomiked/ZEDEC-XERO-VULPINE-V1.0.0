# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_algol.sh - ALGOL 60 reference check, sourced by ref_check.sh (which
# provides d, G, fail, ran and diffs). No ALGOL 60 implementation is
# packaged for CI, so each golden .alg program has an equivalent Pascal
# program in src/lingua/xref/ (kept out of golden/, whose files are all
# twin-checked). zxlang's output must equal the stored .alg.out byte for
# byte, and the integers it prints must equal, in order, the integers the
# Pascal program prints when compiled with fpc. Layout is not compared: the
# two languages pad numbers differently.
# The .alg.out comparison runs even when fpc is missing.
alg_fpc=0
if need fpc; then alg_fpc=1; fi
for f in $G/*.alg; do
    [ -f "$f" ] || continue
    b=$(basename "$f" .alg)
    "$d/zxlang" run "$f" > "$d/$b.alg.zl" 2>&1
    if ! cmp -s "$d/$b.alg.zl" "$f.out"; then
        echo "[FAIL] $f: zxlang and $f.out differ"; diff "$f.out" "$d/$b.alg.zl" | head -20; fail=1; continue
    fi
    x=src/lingua/xref/$b.pas
    if [ ! -f "$x" ]; then echo "[FAIL] $f: no cross-reference program $x"; fail=1; continue; fi
    [ $alg_fpc = 1 ] || continue
    mkdir -p "$d/fpc_$b"
    fpc -v0 -FE"$d/fpc_$b" -o"$d/fpc_$b/$b" "$x" > "$d/$b.fpc.log" 2>&1 || { cat "$d/$b.fpc.log"; echo "[FAIL] fpc $x"; fail=1; continue; }
    "$d/fpc_$b/$b" > "$d/$b.pas.ref"
    grep -oE -- '-?[0-9]+' "$d/$b.pas.ref" > "$d/$b.pas.num"
    grep -oE -- '-?[0-9]+' "$d/$b.alg.zl" > "$d/$b.alg.num"
    if cmp -s "$d/$b.pas.num" "$d/$b.alg.num"; then ran=$((ran + 1)); else
        echo "[FAIL] $f: printed numbers differ from fpc $x"; diff "$d/$b.pas.num" "$d/$b.alg.num" | head -20; fail=1; fi
done
