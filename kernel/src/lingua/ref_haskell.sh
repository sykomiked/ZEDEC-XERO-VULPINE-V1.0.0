# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_haskell.sh - Haskell reference check, sourced by ref_check.sh (run
# from kernel/; uses its d, G, T, need, fail, ran and diffs). Every golden
# .hs program runs through zxlang and runghc (GHC), and the outputs must
# agree byte for byte with each other and with the stored .out. Then N
# programs from gen_haskell.c: GHC compiles them in one batch (each program
# made module G<seed>, a driver printing a marker line before calling each
# main) because one GHC start takes seconds, and the batch output is cut
# back into one file per seed. Reference stderr (warnings) is ignored;
# every generated program must also pass zxlang check (the twin round
# trip).
N=${LINGUA_DIFF_N:-100}
if need runghc; then
    for f in $G/*.hs; do
        b=$(basename "$f" .hs)
        runghc "$f" > "$d/$b.hs.ref" 2>"$d/$b.ghc.log" || { cat "$d/$b.ghc.log"; echo "[FAIL] runghc $f"; fail=1; continue; }
        "$d/zxlang" run "$f" > "$d/$b.hs.zl" 2>&1
        if cmp -s "$d/$b.hs.ref" "$d/$b.hs.zl" && cmp -s "$d/$b.hs.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, runghc and $f.out differ"; diff "$d/$b.hs.ref" "$d/$b.hs.zl" | head -20; fail=1; fi
    done
fi
gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_haskell.c -o "$d/gen_haskell" || { echo "[FAIL] gen_haskell build"; fail=1; }
if [ -x "$d/gen_haskell" ] && need ghc; then
    mkdir -p "$d/ghs"
    { printf 'module Main (main) where\n'; s=1; while [ $s -le "$N" ]; do printf 'import qualified G%d\n' $s; s=$((s + 1)); done
      printf 'main :: IO ()\nmain = do\n'; s=1; while [ $s -le "$N" ]; do printf '  putStrLn "@@%d"\n  G%d.main\n' $s $s; s=$((s + 1)); done; } > "$d/ghs/Main.hs"
    s=1
    while [ $s -le "$N" ]; do
        "$d/gen_haskell" $s > "$d/ghs_$s.hs"
        { printf 'module G%d (main) where\n' $s; cat "$d/ghs_$s.hs"; } > "$d/ghs/G$s.hs"
        s=$((s + 1))
    done
    if ghc -O0 -v0 -i"$d/ghs" -outputdir "$d/ghs/o" -o "$d/ghs/all" "$d/ghs/Main.hs" > "$d/ghs.log" 2>&1 && "$d/ghs/all" > "$d/ghs.out" 2>&1; then
        awk -v d="$d" '/^@@[0-9]+$/ { f = d "/ghs_" substr($0, 3) ".ref"; printf "" > f; next } { print > f }' "$d/ghs.out"
        s=1
        while [ $s -le "$N" ]; do
            "$d/zxlang" run "$d/ghs_$s.hs" > "$d/ghs_$s.zl" 2>&1
            cmp -s "$d/ghs_$s.ref" "$d/ghs_$s.zl" || { echo "[FAIL] Haskell seed $s differs from ghc"; diff "$d/ghs_$s.ref" "$d/ghs_$s.zl" | head; cp "$d/ghs_$s.hs" "$T/lingua_diff_fail.hs"; fail=1; break; }
            "$d/zxlang" check "$d/ghs_$s.hs" > /dev/null || { echo "[FAIL] twin round trip of Haskell seed $s"; cp "$d/ghs_$s.hs" "$T/lingua_diff_fail.hs"; fail=1; break; }
            diffs=$((diffs + 1)); s=$((s + 1))
        done
    else
        head -30 "$d/ghs.log"; echo "[FAIL] ghc rejects or stops in the generated Haskell batch"; fail=1
    fi
fi
