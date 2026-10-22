# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_pascal.sh - Pascal reference check, sourced by ref_check.sh (which
# sets d, G, T, N and need, and reads back ran, diffs and fail). Free Pascal
# in ISO 7185 mode (fpc -Miso), with range and overflow checks on so a
# program that strays is an error there rather than a wrapped number, is
# the reference: every golden/*.pas must print exactly its .pas.out under
# both fpc and zxlang, and so must N random programs from gen_pascal.c.
fpc_build() { # source, output
    fpc -Miso -Cr -Co -v0 -FE"$d" -o"$2" "$1" > "$2.fpc.log" 2>&1
}
if need fpc; then
    for f in $G/*.pas; do
        b=$(basename "$f" .pas)
        fpc_build "$f" "$d/$b.pas.bin" || { cat "$d/$b.pas.bin.fpc.log"; echo "[FAIL] fpc $f"; fail=1; continue; }
        "$d/$b.pas.bin" > "$d/$b.pas.ref" || { echo "[FAIL] $f stops with an error under fpc"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.pas.zl" 2>&1
        if cmp -s "$d/$b.pas.ref" "$d/$b.pas.zl" && cmp -s "$d/$b.pas.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, fpc and $f.out differ"; diff "$d/$b.pas.ref" "$d/$b.pas.zl" | head -20; fail=1; fi
    done
    gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_pascal.c -o "$d/gen_pascal" || { echo "[FAIL] gen_pascal build"; fail=1; }
    s=1
    while [ -x "$d/gen_pascal" ] && [ $s -le "$N" ]; do
        "$d/gen_pascal" $s > "$d/g.pas" || { echo "[FAIL] gen_pascal seed $s"; fail=1; break; }
        fpc_build "$d/g.pas" "$d/gp" || { cat "$d/gp.fpc.log"; echo "[FAIL] fpc rejects generated seed $s"; cp "$d/g.pas" "$T/lingua_diff_fail.pas"; fail=1; break; }
        "$d/gp" > "$d/gp.ref" || { echo "[FAIL] generated seed $s stops with an error under fpc"; cp "$d/g.pas" "$T/lingua_diff_fail.pas"; fail=1; break; }
        "$d/zxlang" run "$d/g.pas" > "$d/gp.zl" 2>&1
        cmp -s "$d/gp.ref" "$d/gp.zl" || { echo "[FAIL] Pascal seed $s differs from fpc"; diff "$d/gp.ref" "$d/gp.zl" | head; cp "$d/g.pas" "$T/lingua_diff_fail.pas"; fail=1; break; }
        "$d/zxlang" check "$d/g.pas" > /dev/null || { echo "[FAIL] Pascal seed $s: twin round trip"; cp "$d/g.pas" "$T/lingua_diff_fail.pas"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
