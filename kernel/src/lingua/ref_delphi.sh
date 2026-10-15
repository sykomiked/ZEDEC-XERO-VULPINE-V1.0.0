# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_delphi.sh - Delphi reference check, sourced by ref_check.sh (which
# sets d, G, T, N and need, and reads back ran, diffs and fail). Free
# Pascal in Delphi mode (fpc -Mdelphi) with overflow and range checks on
# (-Co -Cr, Delphi's {$Q+} {$R+}) is the reference: every golden/*.dpr
# must print exactly its .dpr.out under both fpc and zxlang, and so must N
# random programs from gen_pascal.c in its delphi mode.
dpr_build() { # source, output
    fpc -Mdelphi -Co -Cr -v0 -FE"$d" -o"$2" "$1" > "$2.fpc.log" 2>&1
}
if need fpc; then
    for f in $G/*.dpr; do
        b=$(basename "$f" .dpr)
        dpr_build "$f" "$d/$b.dpr.bin" || { cat "$d/$b.dpr.bin.fpc.log"; echo "[FAIL] fpc -Mdelphi $f"; fail=1; continue; }
        "$d/$b.dpr.bin" > "$d/$b.dpr.ref" || { echo "[FAIL] $f stops with an error under fpc"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.dpr.zl" 2>&1
        if cmp -s "$d/$b.dpr.ref" "$d/$b.dpr.zl" && cmp -s "$d/$b.dpr.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, fpc -Mdelphi and $f.out differ"; diff "$d/$b.dpr.ref" "$d/$b.dpr.zl" | head -20; fail=1; fi
    done
    [ -x "$d/gen_pascal" ] || gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_pascal.c -o "$d/gen_pascal" || { echo "[FAIL] gen_pascal build"; fail=1; }
    s=1
    while [ -x "$d/gen_pascal" ] && [ $s -le "$N" ]; do
        "$d/gen_pascal" delphi $s > "$d/g.dpr" || { echo "[FAIL] gen_pascal delphi seed $s"; fail=1; break; }
        dpr_build "$d/g.dpr" "$d/gd" || { cat "$d/gd.fpc.log"; echo "[FAIL] fpc -Mdelphi rejects generated seed $s"; cp "$d/g.dpr" "$T/lingua_diff_fail.dpr"; fail=1; break; }
        "$d/gd" > "$d/gd.ref" || { echo "[FAIL] generated Delphi seed $s stops with an error under fpc"; cp "$d/g.dpr" "$T/lingua_diff_fail.dpr"; fail=1; break; }
        "$d/zxlang" run "$d/g.dpr" > "$d/gd.zl" 2>&1
        cmp -s "$d/gd.ref" "$d/gd.zl" || { echo "[FAIL] Delphi seed $s differs from fpc -Mdelphi"; diff "$d/gd.ref" "$d/gd.zl" | head; cp "$d/g.dpr" "$T/lingua_diff_fail.dpr"; fail=1; break; }
        "$d/zxlang" check "$d/g.dpr" > /dev/null || { echo "[FAIL] Delphi seed $s: twin round trip"; cp "$d/g.dpr" "$T/lingua_diff_fail.dpr"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
