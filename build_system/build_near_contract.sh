#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_near_contract.sh — build the ZXV kernel's economic core as a NEAR
# smart contract (wasm32, NEAR "env" host ABI, no WASI, no libc).
#
#   build_near_contract.sh             build dist/near/zxv_near_contract.wasm
#   build_near_contract.sh --check     fail when the committed .wasm differs
#   build_near_contract.sh --out FILE  build to FILE
#
# The contract is the same C the fleet kernels link — triple_ledger, vino,
# surplus (Q32.32), scp144, sha256 — plus kernel/arch/near/near_contract.c,
# which exports the on-chain methods and round-trips the ledger state
# through env.storage_*. Table bounds are shrunk for the on-chain build
# (the fleet defaults serialize to ~48 MB); the code and arithmetic are
# unchanged.
#
# Like the boot wasm, the contract is a sealed file: build with the pinned
# clang release (below) so the bytes (and the deployment CID) are
# reproducible.
set -eu
PIN=23.1.0
CLANG=${CLANG:-clang}
R=$(cd "$(dirname "$0")/.." && pwd)
K="$R/kernel"
A="$K/arch/near"
OUT="$R/dist/near/zxv_near_contract.wasm"

mode=write
case "${1:-}" in
"") ;;
--check) mode=check ;;
--out)
    [ -n "${2:-}" ] || { echo "usage: $0 [--check | --out FILE]" >&2; exit 2; }
    mode=out
    OUT=$2
    ;;
*) echo "usage: $0 [--check | --out FILE]" >&2; exit 2 ;;
esac

have=$("$CLANG" --version 2>/dev/null | sed -n '1s/.*clang version \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
if [ "$have" != "$PIN" ]; then
    if [ "$mode" != out ]; then
        echo "$CLANG is clang ${have:-(unknown)}; the contract is built with clang $PIN." >&2
        echo "Install clang $PIN and set CLANG to it, or use --out FILE to try it out." >&2
        exit 1
    fi
    echo "note: clang ${have:-(unknown)}, not the pinned $PIN" >&2
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
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
    "$A/near_contract.c" "$A/near_rt.c" "$K/src/finance/triple_ledger.c" \
    "$K/src/vino/vino.c" "$K/src/surplus/surplus.c" \
    "$K/src/rmag/rmag_core.c" "$K/src/lpres/lpres_core.c" "$K/src/lpres/lpres.c" \
    "$K/src/tribute/tribute.c" \
    "$K/src/scp144/scp144.c" "$K/src/pqsec/mldsa/fips202.c" \
    -o "$tmp"

if [ "$mode" = check ]; then
    if ! cmp -s "$tmp" "$OUT"; then
        echo "DIFFERS: $OUT is not what its source builds to with clang $PIN (run build_system/build_near_contract.sh)" >&2
        exit 1
    fi
    echo "dist/near/zxv_near_contract.wasm matches its source (clang $PIN)"
else
    cp "$tmp" "$OUT"
    echo "wrote $OUT ($(wc -c < "$OUT") bytes, clang ${have:-unknown})"
fi