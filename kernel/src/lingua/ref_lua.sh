# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_lua.sh - Lua reference checks, sourced by ref_check.sh (run from
# kernel/; uses its d, G, T, need, fail, ran and diffs). Every golden .lua
# program runs through zxlang and lua (5.4 or later: 5.3 has different
# integers), and the outputs must agree byte for byte with each other and
# with the stored .out. Then N programs from gen_lua.c run through both.
N=${LINGUA_DIFF_N:-100}
if need lua; then
    for f in $G/*.lua; do
        b=$(basename "$f" .lua)
        lua "$f" > "$d/$b.lua.ref" 2>"$d/$b.lua.log" || { cat "$d/$b.lua.log"; echo "[FAIL] lua $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.lua.zl" 2>&1
        if cmp -s "$d/$b.lua.ref" "$d/$b.lua.zl" && cmp -s "$d/$b.lua.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, lua and $f.out differ"; diff "$d/$b.lua.ref" "$d/$b.lua.zl" | head -20; fail=1; fi
    done
fi
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_lua.c -o "$d/gen_lua" || { echo "[FAIL] gen_lua build"; fail=1; }
if [ -x "$d/gen_lua" ] && command -v lua >/dev/null 2>&1; then
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_lua" $s > "$d/g.lua"
        lua "$d/g.lua" > "$d/g.lua.ref" 2>"$d/g.lua.log" || { cat "$d/g.lua.log"; echo "[FAIL] lua rejects generated seed $s"; cp "$d/g.lua" "$T/lingua_diff_fail.lua"; fail=1; break; }
        "$d/zxlang" run "$d/g.lua" > "$d/g.lua.zl" 2>&1
        cmp -s "$d/g.lua.ref" "$d/g.lua.zl" || { echo "[FAIL] Lua seed $s differs from lua"; diff "$d/g.lua.ref" "$d/g.lua.zl" | head; cp "$d/g.lua" "$T/lingua_diff_fail.lua"; fail=1; break; }
        "$d/zxlang" check "$d/g.lua" > /dev/null || { echo "[FAIL] twin round trip of Lua seed $s"; cp "$d/g.lua" "$T/lingua_diff_fail.lua"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi