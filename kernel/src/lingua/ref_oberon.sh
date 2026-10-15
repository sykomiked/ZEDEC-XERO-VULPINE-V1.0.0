# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_oberon.sh - Oberon reference check, sourced by ref_check.sh (which
# sets d, G, T, N and need, and reads back ran, diffs and fail; it defines
# m2_build in ref_modula2.sh, which sorts before this file). No Oberon-07
# compiler is packaged for CI, so Oberon is checked through gm2, the
# Modula-2 reference, as ALGOL 60 is through fpc:
#   - zxlang's output for every golden/*.ob must equal its .ob.out, and the
#     integers it prints must equal, in order, those printed under gm2 by
#     the equivalent Modula-2 program src/lingua/xref/<name>.mod;
#   - N random programs from gen_pascal.c's oberon mode must print, under
#     zxlang, exactly what gm2 prints for the same program written as
#     Modula-2 (gen_pascal.c's oberon-m2 mode), byte for byte.
# Where OBNC (an Oberon-07 compiler, https://miasap.se/obnc/) is installed,
# the goldens and the N programs must also print exactly the same under it.
ob_build() { # source, directory: builds <directory>/p with obnc
    rm -rf "$2" && mkdir -p "$2" || return 1
    ob_mod=$(sed -n 's/^MODULE \([A-Za-z0-9]*\);.*/\1/p' "$1" | head -1)
    [ -n "$ob_mod" ] || { echo "no MODULE line in $1" > "$2.log"; return 1; }
    cp "$1" "$2/$ob_mod.Mod" && (cd "$2" && timeout 120 obnc "$ob_mod.Mod" && mv "$ob_mod" p) > "$2.log" 2>&1
}
ob_obnc=0
if command -v obnc > /dev/null 2>&1; then ob_obnc=1; else echo "[SKIP] obnc not installed: Oberon is checked through gm2 only"; fi
ob_gm2=0
if need gm2; then ob_gm2=1; fi
for f in $G/*.ob; do
    [ -f "$f" ] || continue
    b=$(basename "$f" .ob)
    "$d/zxlang" run "$f" > "$d/$b.ob.zl" 2>&1
    if ! cmp -s "$d/$b.ob.zl" "$f.out"; then
        echo "[FAIL] $f: zxlang and $f.out differ"; diff "$f.out" "$d/$b.ob.zl" | head -20; fail=1; continue
    fi
    x=src/lingua/xref/$b.mod
    if [ ! -f "$x" ]; then echo "[FAIL] $f: no cross-reference program $x"; fail=1; continue; fi
    if [ $ob_obnc = 1 ]; then
        ob_build "$f" "$d/ob_$b" || { cat "$d/ob_$b.log"; echo "[FAIL] obnc $f"; fail=1; continue; }
        "$d/ob_$b/p" > "$d/$b.ob.ref" || { echo "[FAIL] $f stops with an error under obnc"; fail=1; continue; }
        cmp -s "$d/$b.ob.ref" "$f.out" || { echo "[FAIL] $f: obnc and $f.out differ"; diff "$d/$b.ob.ref" "$f.out" | head -20; fail=1; continue; }
    fi
    [ $ob_gm2 = 1 ] || continue
    m2_build "$x" "$d/obx_$b" || { cat "$d/obx_$b.log"; echo "[FAIL] gm2 $x"; fail=1; continue; }
    "$d/obx_$b/p" > "$d/$b.obx.ref" || { echo "[FAIL] $x stops with an error under gm2"; fail=1; continue; }
    grep -oE -- '-?[0-9]+' "$d/$b.obx.ref" > "$d/$b.obx.num"
    grep -oE -- '-?[0-9]+' "$d/$b.ob.zl" > "$d/$b.ob.num"
    if cmp -s "$d/$b.obx.num" "$d/$b.ob.num"; then ran=$((ran + 1)); else
        echo "[FAIL] $f: printed numbers differ from gm2 $x"; diff "$d/$b.obx.num" "$d/$b.ob.num" | head -20; fail=1; fi
done
if [ $ob_gm2 = 1 ]; then
    [ -x "$d/gen_pascal" ] || gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_pascal.c -o "$d/gen_pascal" || { echo "[FAIL] gen_pascal build"; fail=1; }
    s=1
    while [ -x "$d/gen_pascal" ] && [ $s -le "$N" ]; do
        "$d/gen_pascal" oberon $s > "$d/g.ob" && "$d/gen_pascal" oberon-m2 $s > "$d/go.mod" || { echo "[FAIL] gen_pascal oberon seed $s"; fail=1; break; }
        m2_build "$d/go.mod" "$d/go" || { cat "$d/go.log"; echo "[FAIL] gm2 rejects generated Oberon-as-Modula-2 seed $s"; cp "$d/go.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        "$d/go/p" > "$d/go.ref" || { echo "[FAIL] generated Oberon-as-Modula-2 seed $s stops with an error under gm2"; cp "$d/go.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        "$d/zxlang" run "$d/g.ob" > "$d/go.zl" 2>&1
        cmp -s "$d/go.ref" "$d/go.zl" || { echo "[FAIL] Oberon seed $s differs from gm2 on its Modula-2 form"; diff "$d/go.ref" "$d/go.zl" | head; cp "$d/g.ob" "$T/lingua_diff_fail.ob"; fail=1; break; }
        if [ $ob_obnc = 1 ]; then
            ob_build "$d/g.ob" "$d/gob" || { cat "$d/gob.log"; echo "[FAIL] obnc rejects generated seed $s"; cp "$d/g.ob" "$T/lingua_diff_fail.ob"; fail=1; break; }
            "$d/gob/p" > "$d/gob.ref" || { echo "[FAIL] generated Oberon seed $s stops with an error under obnc"; cp "$d/g.ob" "$T/lingua_diff_fail.ob"; fail=1; break; }
            cmp -s "$d/gob.ref" "$d/go.zl" || { echo "[FAIL] Oberon seed $s differs from obnc"; diff "$d/gob.ref" "$d/go.zl" | head; cp "$d/g.ob" "$T/lingua_diff_fail.ob"; fail=1; break; }
        fi
        "$d/zxlang" check "$d/g.ob" > /dev/null || { echo "[FAIL] Oberon seed $s: twin round trip"; cp "$d/g.ob" "$T/lingua_diff_fail.ob"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
