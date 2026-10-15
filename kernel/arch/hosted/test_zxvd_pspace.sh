#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_pspace.sh — build libzxv (incrementally) and run
# test_zxvd_pspace.c: zxvd_pspace.c (pspace.*, hall.*) behind zxvd_rpc.c on
# two real Vinea net hosts on 127.0.0.1. The mutation harness's test command
# for zxvd_pspace.c (tests/harness/mutator/targets.json): exits 3 when only
# the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-pspace.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
INCS="$(sh "$HERE/app_sources.sh" incs | sed "s|^|-I$K/|") $(sh "$HERE/app_sources.sh" rpcincs |
    sed "s|^|-I$K/|") -I$K/src/web4 -I$R/sdk/libzxv/include"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c): their
# archive, built once and reused (zxvd_mod_archive.sh)
MODA="$(sh "$K/arch/hosted/zxvd_mod_archive.sh")" || exit 3
# what zxvd_rpc.c links (app_sources.sh rpcdeps), the net host and Vinea
# (app_sources.sh vinea), once each; keccak.c apart (below)
{
    sh "$HERE/app_sources.sh" rpcdeps
    sh "$HERE/app_sources.sh" vinea
    printf '%s\n' arch/hosted/zxv_net_host.c arch/hosted/zxv_http_guard.c src/web4/web4_json.c \
        src/web4/web4_util.c src/swarm/swarm_budget.c src/swarm/swarm_emotion.c \
        src/swarm/swarm_market.c src/swarm/swarm_hk.c
} | grep -v '^src/mlkem/keccak.c$' | sort -u | sed "s|^|$K/|" >"$T/srcs"
# keccak.c without -Werror: gcc 11 at -O1 reports a false maybe-uninitialized
# in it (test_zxvd_rpc.sh has the same failure with gcc 11)
# shellcheck disable=SC2086 # CC may carry flags; INCS is a list
$CC -std=c11 -O1 -Wall -Wextra $INCS -c "$K/src/mlkem/keccak.c" -o "$T/keccak.o" 2>/dev/null || exit 3
# shellcheck disable=SC2086,SC2046
$CC -std=c11 -O1 -Wall -Wextra -Werror $INCS "$HERE/test_zxvd_pspace.c" \
    "$HERE/zxvd_pspace.c" "$HERE/zxvd_rpc.c" $(cat "$T/srcs") "$T/keccak.o" \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lpthread -lm -o "$T/test_zxvd_pspace" || exit 3
"$T/test_zxvd_pspace"
