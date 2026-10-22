#!/bin/sh
# proj_gforth.sh DIR — run every Forth projection test_zl_proj wrote under
# DIR/forth on gforth and compare what it prints with what zl_run printed.
# Skips (and says so) when gforth is not installed.
dir=$1/forth
if ! command -v gforth >/dev/null 2>&1; then
    echo "[SKIP] gforth is not installed"
    exit 0
fi
n=0
for fs in "$dir"/*.fs; do
    [ -e "$fs" ] || continue
    want=${fs%.fs}.out
    got=$(mktemp)
    timeout 20 gforth "$fs" >"$got" 2>/dev/null </dev/null
    if ! cmp -s "$got" "$want"; then
        echo "[FAIL] gforth: $fs"
        rm -f "$got"
        exit 1
    fi
    rm -f "$got"
    n=$((n + 1))
done
echo "[PASS] Forth projections on gforth ($n programs)"
