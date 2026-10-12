#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_cli.sh ZXV - the zxv command end to end: list, new, run (exit codes
# for each verdict), test, prompt, single files, and refusals.
set -u
ZXV="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
T="$(mktemp -d "${TMPDIR:-/tmp}/zxv-cli.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
cd "$T"
pass=0
fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
expect_rc() { # want, description, command...
    want=$1
    what=$2
    shift 2
    "$@" >"$T/out" 2>&1
    rc=$?
    if [ "$rc" = "$want" ]; then ok; else bad "$what: exit $rc, want $want"; cat "$T/out"; fi
}

expect_rc 0 "list" "$ZXV" list
grep -q payment-reconciliation "$T/out" && ok || bad "list names the templates"
expect_rc 0 "version" "$ZXV" version
expect_rc 2 "usage" "$ZXV"
expect_rc 2 "unknown template" "$ZXV" new no-such-template

# A held template: BOTH is exit 3, and the gated step does not run.
expect_rc 0 "new" "$ZXV" new payment-reconciliation recon
[ -f recon/template.zxv ] && [ -f recon/expected.json ] && ok || bad "new writes the files and expected.json"
expect_rc 2 "new refuses a non-empty directory" "$ZXV" new payment-reconciliation recon
expect_rc 3 "run held" "$ZXV" run recon
grep -q 'HELD: P-1002' "$T/out" && ok || bad "run shows what is held"
grep -q 'not run' "$T/out" && ok || bad "the gated step is not run"
expect_rc 0 "test matches expected.json" "$ZXV" test recon

# Resolve the conflict: TRUE is exit 0, settlement runs, test sees the change.
sed 's/receiver = 1 -1 1 1/receiver = 1  1 1 1/' recon/inputs.txt >recon/x && mv recon/x recon/inputs.txt
expect_rc 0 "run clear" "$ZXV" run recon
grep -q 'fees = 755' "$T/out" && ok || bad "the treaty settles with the fee"
expect_rc 1 "test sees the change" "$ZXV" test recon
expect_rc 0 "test --update" "$ZXV" test recon --update
expect_rc 0 "test after update" "$ZXV" test recon

# Undecided: NEITHER is exit 4.
sed 's/^rtgs .*/rtgs     = 1 1 1 0/; s/^sender .*/sender   = 1 1 1 0/; s/^receiver .*/receiver = 1 1 1 0/' \
    recon/inputs.txt >recon/x && mv recon/x recon/inputs.txt
expect_rc 4 "run undecided" "$ZXV" run recon

# A refusal: FALSE is exit 1, with the line and column.
expect_rc 0 "new treaty" "$ZXV" new trade-corridor corridor
sed 's/PARTY BETA 3000000 3000 1\/7/PARTY BETA 3000000 3000001 1\/7/' corridor/corridor.treaty >corridor/x &&
    mv corridor/x corridor/corridor.treaty
expect_rc 1 "run refused" "$ZXV" run corridor
grep -q 'REFUSED at line 5, column 1' "$T/out" && ok || bad "the refusal names the line"
expect_rc 1 "run --json" "$ZXV" run corridor --json
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["verdict"]=="FALSE"' "$T/out" && ok ||
    bad "--json prints the result object"

# One file, language from the extension.
printf '+/1r3 1r6 1r2\n' >sum.k
expect_rc 0 "run one file" "$ZXV" run sum.k
grep -q 'run = 1' "$T/out" && ok || bad "one KRON file: 1r3+1r6+1r2 = 1"
printf 'x*2\n' >twice.k
printf 'x = 1 2 3\n' >in.txt
expect_rc 0 "run with --inputs" "$ZXV" run twice.k --inputs in.txt
grep -q 'run = (vec 2 4 6)' "$T/out" && ok || bad "inputs bind KRON names"
printf 'TREATY T USD NET\nPARTY A 10 0 1/2\nPARTY B 10 0 1/2\nOWE A B 5\nEND\n' >t.treaty
expect_rc 0 "run a treaty file" "$ZXV" run t.treaty
printf 'x\n' >what.txt
expect_rc 2 "unknown extension" "$ZXV" run what.txt
expect_rc 1 "--lang" "$ZXV" run what.txt --lang treaty

# The AI prompt (no model is called: it is printed for a model to read).
expect_rc 0 "prompt" "$ZXV" prompt corridor "Acme owes Beta 12,000 dollars"
grep -q 'untrusted' "$T/out" && grep -q 'Acme owes Beta' "$T/out" && ok || bad "prompt carries the rules and the request"
expect_rc 2 "prompt without a draft line" "$ZXV" prompt recon

# Every shipped template starts, runs and passes its own test.
for id in $("$ZXV" list | awk 'NF >= 3 && $1 ~ /^[a-z0-9-]+$/ && $2 ~ /^[a-z]+$/ {print $1}'); do
    "$ZXV" new "$id" "all-$id" >/dev/null 2>&1 || bad "new $id"
    "$ZXV" run "all-$id" >/dev/null 2>&1
    rc=$?
    [ "$rc" = 0 ] || [ "$rc" = 3 ] || [ "$rc" = 4 ] && ok || bad "$id: shipped run exit $rc"
    "$ZXV" test "all-$id" >/dev/null 2>&1 && ok || bad "$id: zxv test"
done

echo "test_cli: $pass passed, $fail failed"
[ "$fail" = 0 ]
