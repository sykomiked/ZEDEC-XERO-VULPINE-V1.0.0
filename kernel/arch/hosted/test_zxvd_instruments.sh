#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_instruments.sh — build libzxv (incrementally) and run
# test_zxvd_instruments.c: bonds.* and derivatives.* (zxvd_instruments.c,
# zxvd_sandbox.c) behind zxvd_rpc.c, on the real vbond and vderiv engines. The
# mutation harness's test command for zxvd_instruments.c and zxvd_sandbox.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c): their
# archive, built once and reused (zxvd_mod_archive.sh)
MODA="$(sh "$K/arch/hosted/zxvd_mod_archive.sh")" || exit 3
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-inst.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
# keccak.c on its own, without -Werror: gcc 11 warns about it (maybe-uninitialized)
"$CC" -std=c11 -O1 -c -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" \
    -I"$K/src/event_space" -I"$K/src/surplus" "$K/src/mlkem/keccak.c" -o "$T/keccak.o" 2>"$T/k.log" ||
    { tail -20 "$T/k.log"; exit 3; }
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -I"$HERE" -I"$K/src/web4" -I"$K/src/robin_debanks" \
    -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" \
    -I"$K/src/surplus" -I"$K/src/tensor" -I"$K/src/swarm" -I"$K/src/zcapital" -I"$K/src/pay" \
    -I"$K/src/vbond" -I"$K/src/vderiv" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxvd_instruments.c" "$HERE/zxvd_instruments.c" "$HERE/zxvd_sandbox.c" \
    "$HERE/zxvd_rpc.c" $RPCINC $RPCDEPS "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" \
    "$K/src/vbond/vbond.c" "$K/src/vderiv/vderiv.c" "$K/src/pay/pay_ledger.c" \
    "$K/src/pay/pay_util.c" "$K/src/pay/pay_assure.c" "$T/keccak.o" \
    "$K/src/robin_debanks/sha256.c" "$K/src/robin_debanks/ed25519_verify.c" \
    "$K/src/provenance/test_signer.c" "$K/src/tensor/zt.c" "$K/src/swarm/swarm_market.c" \
    "$K/src/swarm/swarm_budget.c" "$K/src/swarm/swarm_emotion.c" \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_instruments" 2>"$T/cc.log" ||
    { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -30; exit 3; }
"$T/test_zxvd_instruments"
