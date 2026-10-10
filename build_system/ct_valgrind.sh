#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ct_valgrind.sh -- secret-dependent branch/index check for ML-KEM and ML-DSA
# under valgrind memcheck (the "ctgrind" technique). See
# kernel/src/pqsec/test_ct_valgrind.c for what is marked secret and for what
# this does and does not show. It is NOT a proof of constant-time behaviour.
#
# Modes and their gate:
#   selftest                     must report (proves the check is live)
#   mlkem-decaps, mlkem-encaps   must report nothing (exit 1 otherwise)
#   mldsa-sign                   report only. Measured 2026-10 (gcc 11 and 13, -O2): all
#                                reports are in the vendored pq-crystals reference
#                                (poly_chknorm and the rejection gotos, make_hint,
#                                hint packing, challenge sampling from c~), i.e. on
#                                candidates that are rejected or on values that
#                                end up in the public signature. That is the
#                                reference design, not a proof that it is safe;
#                                every reported location is printed.
# Usage: build_system/ct_valgrind.sh   (needs valgrind and its headers)
#   CT_OUT=dir (default /tmp/zxv_ct_valgrind), CT_OPT=-O2
set -u
here=$(cd "$(dirname "$0")" && pwd)
K="$here/../kernel"
cd "$K" || exit 2
OUT=${CT_OUT:-/tmp/zxv_ct_valgrind}
OPT=${CT_OPT:--O2}
mkdir -p "$OUT"
command -v valgrind > /dev/null || {
    echo "[FAIL] ct-valgrind: valgrind is not installed"
    exit 1
}
export CPATH="$K/include:$K/src/modbind:$K/src/e8:$K/src/event_space:$K/src/surplus"

bin="$OUT/test_ct_valgrind"
# shellcheck disable=SC2046
gcc -std=c11 -Wall -Werror -Wextra $OPT -g -Iinclude -Isrc/pqsec -Isrc/mlkem -Isrc/lpres \
    -Isrc/surplus -Isrc/edp_risk src/pqsec/test_ct_valgrind.c src/mlkem/keccak.c \
    src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c src/mlkem/mlkem_sample.c src/mlkem/mlkem_ntt.c \
    src/mlkem/mlkem_kpe.c src/pqsec/pq_mldsa65.c $(ls src/pqsec/mldsa/*.c | sort) \
    -o "$bin" || {
    echo "[FAIL] ct-valgrind: does not build"
    exit 1
}

fail=0
for mode in selftest mlkem-decaps mlkem-encaps mldsa-sign; do
    log="$OUT/$mode.log"
    valgrind --tool=memcheck --error-exitcode=99 --track-origins=yes --num-callers=8 \
        "$bin" "$mode" > "$log" 2>&1
    rc=$?
    errs=$(sed -n 's/.*ERROR SUMMARY: \([0-9]*\) errors.*/\1/p' "$log" | tail -1)
    # Distinct innermost source locations of the reports.
    locs=$(awk '/depends on uninitialised|Use of uninitialised value/ {getline; print $NF}' \
        "$log" | sort | uniq -c | sort -rn)
    if [ "$mode" = selftest ]; then
        if [ "${errs:-0}" != 0 ]; then
            echo "[PASS] ct-valgrind selftest: the deliberate secret branch was reported"
        else
            echo "[FAIL] ct-valgrind selftest: memcheck did not report a secret branch"
            fail=1
        fi
    elif ! grep -q '^\[PASS\]' "$log"; then
        tail -20 "$log"
        echo "[FAIL] ct-valgrind $mode: the operation itself failed"
        fail=1
    elif [ "${errs:-0}" = 0 ] && [ $rc -eq 0 ]; then
        echo "[PASS] ct-valgrind $mode: no secret-dependent branch or index reported"
    elif [ "$mode" = mldsa-sign ]; then
        echo "[REPORT] ct-valgrind $mode: $errs reports, not a gate (see test_ct_valgrind.c)."
        echo "         Distinct report sites (count, innermost frame):"
        echo "$locs" | sed 's/^/           /'
    else
        echo "[FAIL] ct-valgrind $mode: $errs reports. Locations (count, innermost frame):"
        echo "$locs" | sed 's/^/           /'
        echo "         full log: $log"
        fail=1
    fi
done
[ $fail -eq 0 ] && echo "[PASS] ct-valgrind" || {
    echo "[FAIL] ct-valgrind"
    exit 1
}
