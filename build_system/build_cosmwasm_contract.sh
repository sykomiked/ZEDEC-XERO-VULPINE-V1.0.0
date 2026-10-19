#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_cosmwasm_contract.sh — build the ZXV kernel core as a CosmWasm
# contract (wasm32, module "env" host ABI: db_read/db_write/db_scan...).
# The SAME core sources as the NEAR build; only the ABI shim differs.
#
#   build_cosmwasm_contract.sh [--out FILE]   build dist/near/zxv_cosmwasm.wasm
#
# STATUS: compile-proven. The db_* signatures must be verified against the
# target cosmwasm-vm version before a real deploy — the ABI has drifted
# across 0.x -> 1.x -> 2.x, and unlike NEAR we have no live chain here to
# bind against yet.
set -eu
PIN=23.1.0
CLANG=${CLANG:-clang}
R=$(cd "$(dirname "$0")/.." && pwd)
K="$R/kernel"
A="$K/arch/cosmwasm"
OUT="${2:-$R/dist/near/zxv_cosmwasm.wasm}"
if [ "${1:-}" = "--out" ]; then OUT=$2; fi

mkdir -p "$(dirname "$OUT")"

"$CLANG" --target=wasm32 -mcpu=generic -std=c11 -O2 -ffreestanding -nostdlib \
    -Wall -Wextra -Werror \
    -Dmemset=fs_memset -Dmemcpy=fs_memcpy -Dstrlen=fs_strlen -Dstrcmp=fs_strcmp \
    -Dstrcpy=fs_strcpy -Dmalloc=fs_malloc -Dcalloc=fs_calloc -Dmemcmp=fs_memcmp \
    -Dpow=fs_pow -Dlog=fs_log \
    -include "$K/include/freestanding.h" \
    -I"$K/include" -I"$K/include/freestanding_stubs" \
    -I"$K/src" -I"$K/src/finance" -I"$K/src/vino" \
    -I"$K/src/edp_risk" -I"$K/src/surplus" -I"$K/src/zcapital" -I"$K/src/scp144" \
    -I"$K/src/robin_debanks" -I"$K/src/rmag" -I"$K/src/lpres" \
    -I"$K/src/event_space" -I"$K/src/tribute" \
    -I"$K/src/pqsec" -I"$K/src/pqsec/mldsa" \
    -I"$K/src/modbind" -I"$A" \
    -DZXV_TL_MAX_ACCOUNTS=64 -DZXV_TL_MAX_ENTRIES=16 -DZXV_TL_MAX_VOUCHERS=8 \
    -DVINO_MAX_ACCOUNTS=128 -DVINO_MAX_TXNS=1024 -DVINO_MAX_ASSETS=16 \
    -DVINO_MAX_PEERS=8 -DVINO_MAX_MESSAGES=8 \
    -Wl,--no-entry -Wl,--strip-all -Wl,--export-memory \
    -Wl,--initial-memory=8388608 -Wl,-z,stack-size=1048576 \
    "$A/cosmwasm_contract.c" "$K/src/finance/triple_ledger.c" \
    "$K/src/vino/vino.c" "$K/src/surplus/surplus.c" \
    "$K/src/rmag/rmag_core.c" "$K/src/lpres/lpres_core.c" "$K/src/lpres/lpres.c" \
    "$K/src/tribute/tribute.c" \
    "$K/src/scp144/scp144.c" "$K/src/pqsec/mldsa/fips202.c" \
    -o "$OUT"

echo "wrote $OUT ($(wc -c < "$OUT") bytes, clang ${CLANG##*/})"