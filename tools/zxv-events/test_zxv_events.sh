#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxv_events.sh [tmpdir] — build zxv_events and gen_events (ASan/UBSan),
# make a two-boot log from a real chg_abi session, check the viewer passes
# it, then that every kind of tampering fails it at the right line.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../../kernel" && pwd)"
T="$(mktemp -d "${1:-${TMPDIR:-/tmp}}/zxv-events.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
CC="${CC:-gcc}"
FL="-std=c11 -O1 -g -Wall -Werror -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all"
INC="-I$K/include -I$K/src/chiglet -I$K/src/lpres -I$K/src/pqsec -I$K/src/robin_debanks -I$K/src/modbind
     -I$K/src/event_space -I$K/src/surplus -I$K/src/edp_risk"
LIB="$K/src/chiglet/chg_abi.c $K/src/lpres/lpres.c $K/src/robin_debanks/sha256.c $K/src/pqsec/pq_mldsa65.c"
LIB="$LIB $(ls "$K"/src/pqsec/mldsa/*.c | sort | tr '\n' ' ')"
# shellcheck disable=SC2086 # flag and file lists
$CC $FL $INC "$HERE/zxv_events.c" $LIB -o "$T/zxv_events"
# shellcheck disable=SC2086
$CC $FL $INC "$HERE/gen_events.c" $LIB -o "$T/gen_events"
"$T/gen_events" > "$T/good.log"

pass=0 failed=0
ok() { pass=$((pass + 1)); }
bad() { failed=$((failed + 1)); echo "FAIL: $1"; }

# expect <log> <exit> <text the output must hold> <what>
expect() {
    rc=0
    "$T/zxv_events" "$1" > "$T/out" 2>&1 || rc=$?
    if [ "$rc" = "$2" ] && grep -qF -- "$3" "$T/out"; then ok; else bad "$4 (exit $rc)"; cat "$T/out"; fi
}

expect "$T/good.log" 0 "ok: 9 record(s) over 2 boot(s), every chain whole, 1 signature(s) verified" \
    "the untouched log passes"
expect "$T/good.log" 0 "#4 model_exec (chiglet) failed, gate true, code -5  args 6918188751797616640 1" \
    "a record reads back in words"
expect "$T/good.log" 0 "#2 storage_dma_read (chiglet) refused, gate false  args 0 9" "a refusal reads back"
rc=0
"$T/zxv_events" < "$T/good.log" > "$T/out" 2>&1 || rc=$?
if [ "$rc" = 0 ]; then ok; else bad "reading standard input (exit $rc)"; fi

# the n-th [EVT] record line (not head/key/sig), by file line number
evt_line() { grep -nE '^\[EVT\] [0-9]' "$T/good.log" | sed -n "${1}p" | cut -d: -f1; }
L2=$(evt_line 2)
L3=$(evt_line 3)
L4=$(evt_line 4)

# one record byte changed (its outcome: refused -> ran)
awk -v l="$L2" 'NR == l { $3 = substr($3, 1, 20) "00" substr($3, 23) } { print }' "$T/good.log" > "$T/t1.log"
expect "$T/t1.log" 1 "FAIL line $L2: the chain breaks here" "a changed record breaks the chain"

# a record removed
awk -v l="$L2" 'NR != l' "$T/good.log" > "$T/t2.log"
expect "$T/t2.log" 1 "the chain breaks here" "a removed record breaks the chain"

# two records swapped
awk -v a="$L3" -v b="$L4" 'NR == a { x = $0; next } NR == b { print; print x; next } { print }' \
    "$T/good.log" > "$T/t3.log"
expect "$T/t3.log" 1 "the chain breaks here" "reordered records break the chain"

# a head changed
awk -v l="$L2" 'NR == l { $4 = "ff" substr($4, 3) } { print }' "$T/good.log" > "$T/t4.log"
expect "$T/t4.log" 1 "FAIL line $L2: the chain breaks here" "a changed head breaks the chain"

# a record duplicated (a replayed line)
awk -v l="$L2" '{ print } NR == l { print }' "$T/good.log" > "$T/t5.log"
expect "$T/t5.log" 1 "the chain breaks here" "a repeated record breaks the chain"

# the second boot's marker removed: its records no longer start from zero
L=$(grep -n 'Chiglet governor' "$T/good.log" | sed -n 2p | cut -d: -f1)
awk -v l="$L" 'NR != l' "$T/good.log" > "$T/t6.log"
expect "$T/t6.log" 1 "the chain breaks here" "a boot's records spliced onto another boot"

# the signature changed
awk '/^\[EVT\] sig / { $3 = (substr($3, 1, 1) == "0" ? "1" : "0") substr($3, 2) } { print }' \
    "$T/good.log" > "$T/t7.log"
expect "$T/t7.log" 1 "the signature does not verify" "a changed signature fails"

# the key changed
awk '/^\[EVT\] key / { $3 = (substr($3, 1, 1) == "0" ? "1" : "0") substr($3, 2) } { print }' \
    "$T/good.log" > "$T/t8.log"
expect "$T/t8.log" 1 "the signature does not verify" "a changed key fails"

# the signed count is not the records above it
awk '/^\[EVT\] head / { $3 = $3 + 1 } { print }' "$T/good.log" > "$T/t9.log"
expect "$T/t9.log" 1 "the signed head is not the head of the records above it" "a wrong count fails"

# a record after the signature does not change what was signed, but a
# signature block cut short does
grep -v '^\[EVT\] sig ' "$T/good.log" > "$T/t10.log"
expect "$T/t10.log" 1 "the log ends inside a signature block" "a missing signature fails"

# a field no call can have (op 0x3f), with its head made to match
awk -v l="$L3" 'NR == l { $3 = substr($3, 1, 16) "3f" substr($3, 19) } { print }' "$T/good.log" > "$T/t11.log"
expect "$T/t11.log" 1 "a field no call can have" "an impossible record fails before the chain"

# a malformed event line
printf '[EVT] 1 zz\n' > "$T/t12.log"
expect "$T/t12.log" 1 "not an event line" "a malformed line fails"

# no events at all
printf 'boot\nnothing here\n' > "$T/t13.log"
expect "$T/t13.log" 2 "no events in this log" "a log with no events is not a pass"

echo "$pass passed, $failed failed"
[ "$failed" = 0 ]
