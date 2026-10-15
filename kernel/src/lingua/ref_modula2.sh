# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_modula2.sh - Modula-2 reference check, sourced by ref_check.sh (which
# sets d, G, T, N and need, and reads back ran, diffs and fail). GNU
# Modula-2 in the PIM4 dialect with its run-time checks on is the
# reference: gm2 -fpim4 -fsoft-check-all -fno-wholevalue (gm2 13's
# whole-value check aborts every DIV of a negative dividend, so it is off;
# overflow then wraps there and traps here, and generated programs never
# overflow). Every golden/*.mod must print exactly its .mod.out under both
# gm2 and zxlang, and so must N random programs from gen_pascal.c in its
# m2 mode. gm2 wants a program module in a file named after it, so each is
# copied to <Module>.mod in a directory of its own.
m2_build() { # source, directory: builds <directory>/p
    rm -rf "$2" && mkdir -p "$2" || return 1
    m2_mod=$(sed -n 's/^MODULE \([A-Za-z0-9]*\);.*/\1/p' "$1" | head -1)
    [ -n "$m2_mod" ] || { echo "no MODULE line in $1" > "$2.log"; return 1; }
    cp "$1" "$2/$m2_mod.mod" && (cd "$2" && gm2 -fpim4 -fsoft-check-all -fno-wholevalue "$m2_mod.mod" -o p) > "$2.log" 2>&1
}
if need gm2; then
    for f in $G/*.mod; do
        [ -f "$f" ] || continue
        b=$(basename "$f" .mod)
        m2_build "$f" "$d/m2_$b" || { cat "$d/m2_$b.log"; echo "[FAIL] gm2 $f"; fail=1; continue; }
        "$d/m2_$b/p" > "$d/$b.mod.ref" || { echo "[FAIL] $f stops with an error under gm2"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.mod.zl" 2>&1
        if cmp -s "$d/$b.mod.ref" "$d/$b.mod.zl" && cmp -s "$d/$b.mod.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, gm2 and $f.out differ"; diff "$d/$b.mod.ref" "$d/$b.mod.zl" | head -20; fail=1; fi
    done
    [ -x "$d/gen_pascal" ] || gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_pascal.c -o "$d/gen_pascal" || { echo "[FAIL] gen_pascal build"; fail=1; }
    s=1
    while [ -x "$d/gen_pascal" ] && [ $s -le "$N" ]; do
        "$d/gen_pascal" m2 $s > "$d/g.mod" || { echo "[FAIL] gen_pascal m2 seed $s"; fail=1; break; }
        m2_build "$d/g.mod" "$d/gm" || { cat "$d/gm.log"; echo "[FAIL] gm2 rejects generated seed $s"; cp "$d/g.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        "$d/gm/p" > "$d/gm.ref" || { echo "[FAIL] generated Modula-2 seed $s stops with an error under gm2"; cp "$d/g.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        "$d/zxlang" run "$d/g.mod" > "$d/gm.zl" 2>&1
        cmp -s "$d/gm.ref" "$d/gm.zl" || { echo "[FAIL] Modula-2 seed $s differs from gm2"; diff "$d/gm.ref" "$d/gm.zl" | head; cp "$d/g.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        "$d/zxlang" check "$d/g.mod" > /dev/null || { echo "[FAIL] Modula-2 seed $s: twin round trip"; cp "$d/g.mod" "$T/lingua_diff_fail.mod"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
