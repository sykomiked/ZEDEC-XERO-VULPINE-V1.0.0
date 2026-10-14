#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_power.sh — build libzxv (incrementally) and run test_zxvd_power.c:
# zxvd_power.c (power.*) behind zxvd_rpc.c on the real power ledger, read
# from a Linux-style fixture. The mutation harness's test command for
# zxvd_power.c (tests/harness/mutator/targets.json): exits 3 when only the
# build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-power.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
INCS="-I$HERE -I$K/src/web4 -I$K/src/power -I$K/src/swarm -I$K/src/robin_debanks -I$K/src/mlkem
      -I$K/include -I$K/src/modbind -I$K/src/e8 -I$K/src/event_space -I$K/src/surplus
      -I$R/sdk/libzxv/include"
OSLIBS=""
case "$($CC -dumpmachine 2>/dev/null)" in
    *darwin*|*apple*) OSLIBS="-framework IOKit -framework CoreFoundation" ;;
esac
# keccak.c without -Werror: gcc 11 at -O1 reports a false maybe-uninitialized
# in it (test_zxvd_rpc.sh has the same failure with gcc 11)
# shellcheck disable=SC2086 # CC may carry flags; INCS is a list
$CC -std=c11 -O1 -Wall -Wextra $INCS -c "$K/src/mlkem/keccak.c" -o "$T/keccak.o" || exit 3
# the reader and the ledger are compiled once; zxvd_power.c, the mutation
# target, with the test
# shellcheck disable=SC2086
$CC -std=c11 -O1 -Wall -Wextra -Werror $INCS -c "$HERE/zxv_power_host.c" -o "$T/host.o" || exit 3
# shellcheck disable=SC2086
$CC -std=c11 -O1 -Wall -Wextra -Werror $INCS -c "$K/src/power/power_ledger.c" -o "$T/ledger.o" ||
    exit 3
# shellcheck disable=SC2086
$CC -std=c11 -O1 -Wall -Wextra -Werror $INCS "$HERE/test_zxvd_power.c" "$HERE/zxvd_power.c" \
    "$HERE/zxvd_rpc.c" "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" "$T/keccak.o" \
    "$K/src/robin_debanks/sha256.c" "$T/host.o" "$T/ledger.o" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm $OSLIBS -o "$T/test_zxvd_power" || exit 3
"$T/test_zxvd_power"
