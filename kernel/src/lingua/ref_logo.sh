# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ref_logo.sh - Logo reference checks, sourced by ref_check.sh (run from
# kernel/; uses its d, G, T, need, fail, ran and diffs). Every golden .lg
# program and N programs from gen_logo.c run through zxlang and UCBLogo 6.2
# (ucblogo), and the outputs must agree byte for byte (goldens: with the
# stored .out too). ucblogo draws to a window and writes nothing useful to
# stdout, so it runs under xvfb-run with a driver that sends the program's
# output to a file with setwrite; an error is appended as an ERR: line. A
# golden's .in (integers, one per line) is ucblogo's read stream, opened by
# the driver with setread (ucblogo does not read stdin under xvfb-run), and
# its integers are zxlang's inputs (ref_args, from ref_check.sh).

# logo_ref prog.lg out: run prog.lg in ucblogo, its output into out.
logo_ref() {
    rm -f "$d/lg.out"
    rd=
    if [ -f "$1.in" ]; then rd=$(printf 'openread "%s\nsetread "%s' "$1.in" "$1.in"); fi
    printf '%s\nopenwrite "%s\nsetwrite "%s\ncatch "error [load "%s]\nmake "e error\nif not emptyp :e [(print "ERR: :e)]\nsetwrite []\nclose "%s\nbye\n' \
        "$rd" "$d/lg.out" "$d/lg.out" "$1" "$d/lg.out" > "$d/lg_drv.lg"
    timeout 60 xvfb-run -a ucblogo "$d/lg_drv.lg" < /dev/null > /dev/null 2>&1
    if [ -f "$d/lg.out" ]; then cat "$d/lg.out" > "$2"; else echo "(ucblogo wrote nothing)" > "$2"; fi
}

if need ucblogo && need xvfb-run; then
    for f in $G/*.lg; do
        b=$(basename "$f" .lg)
        logo_ref "$f" "$d/$b.lg.ref"
        "$d/zxlang" run "$f" $(ref_args "$f") > "$d/$b.lg.zl" 2>&1
        if cmp -s "$d/$b.lg.ref" "$d/$b.lg.zl" && cmp -s "$d/$b.lg.ref" "$f.out"; then ran=$((ran + 1)); else
            echo "[FAIL] $f: zxlang, ucblogo and $f.out differ"; diff "$d/$b.lg.ref" "$d/$b.lg.zl" | head -20; fail=1; fi
    done
    gcc -std=c11 -O2 -Wall -Werror -Wextra src/lingua/gen_logo.c -o "$d/gen_logo" || { echo "[FAIL] gen_logo build"; fail=1; }
    s=1
    while [ -x "$d/gen_logo" ] && [ $s -le "${LINGUA_DIFF_N:-100}" ]; do
        "$d/gen_logo" $s > "$d/g.lg" || { echo "[FAIL] gen_logo seed $s"; fail=1; break; }
        logo_ref "$d/g.lg" "$d/g.lg.ref"
        "$d/zxlang" run "$d/g.lg" > "$d/g.lg.zl" 2>&1
        cmp -s "$d/g.lg.ref" "$d/g.lg.zl" || { echo "[FAIL] Logo seed $s differs from ucblogo"; diff "$d/g.lg.ref" "$d/g.lg.zl" | head; cp "$d/g.lg" "$T/lingua_diff_fail.lg"; fail=1; break; }
        diffs=$((diffs + 1)); s=$((s + 1))
    done
fi
