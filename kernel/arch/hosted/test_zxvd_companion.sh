#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_companion.sh — build libzxv (incrementally) and run
# test_zxvd_companion.c: zxvd_companion.c behind zxvd_rpc.c on the real swarm
# DNA, emotion and evolution code and the chiglet's name (chg_name.c). The
# mutation harness's test command for zxvd_companion.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c): their
# archive goes last on the link line, after every object (zxvd_mod_archive.sh)
MODA="$(sh "$K/arch/hosted/zxvd_mod_archive.sh")" || exit 3
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-companion.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
INCS="$RPCINC -I$HERE -I$K/src/web4 -I$K/src/swarm -I$K/src/robin_debanks -I$K/src/mlkem -I$K/include
      -I$K/src/modbind -I$K/src/e8 -I$K/src/event_space -I$K/src/surplus -I$R/sdk/libzxv/include"
# keccak.c without -Werror: gcc 11 at -O1 reports a false maybe-uninitialized
# in it (test_zxvd_rpc.sh has the same failure with gcc 11)
# shellcheck disable=SC2086 # CC may carry flags; INCS is a list
$CC -std=c11 -O1 -Wall -Wextra $INCS -c "$K/src/mlkem/keccak.c" -o "$T/keccak.o" || exit 3
set --
for f in swarm_dna swarm_emotion swarm_budget swarm_evolve; do set -- "$@" "$K/src/swarm/$f.c"; done
# shellcheck disable=SC2086
$CC -std=c11 -O1 -Wall -Wextra -Werror $INCS "$HERE/test_zxvd_companion.c" "$HERE/zxvd_companion.c" \
    "$HERE/zxvd_rpc.c" $RPCDEPS "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" "$T/keccak.o" \
    "$K/src/robin_debanks/sha256.c" "$K/src/chiglet/chg_name.c" "$@" -L"$R/sdk/libzxv/build" -lzxv \
    "$RP" -lm \
    "$MODA" -o "$T/test_zxvd_companion" || exit 3
"$T/test_zxvd_companion"
