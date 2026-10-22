#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_money.sh — build libzxv (incrementally) and run test_zxvd_money.c:
# mint.*, qnote.*, fx.*, vhub.*, capmkt.*, market.*, royalty.*, cred.* and
# dispute.* (zxvd_money.c, zxvd_sandbox.c) behind zxvd_rpc.c, on the real
# mint, qnote, fxepoch, vhub, capmkt, market, royalty, cred, dispute and vrf
# engines and pay_ledger. CFLAGS is appended (e.g. -fsanitize=address,undefined
# -fno-sanitize-recover=all). Exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps;
# it brings lpres_hex.c, which vhub's quarantine log uses)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c): their
# archive goes last on the link line, after every object (zxvd_mod_archive.sh)
MODA="$(sh "$K/arch/hosted/zxvd_mod_archive.sh")" || exit 3
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:-}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-money.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
BASEINC="-I$K/include -I$K/src/modbind -I$K/src/e8 -I$K/src/event_space -I$K/src/surplus"
# keccak.c on its own, without -Werror: gcc 11 warns about it (maybe-uninitialized)
"$CC" -std=c11 -O1 $CFLAGS -c -I"$K/src/mlkem" $BASEINC "$K/src/mlkem/keccak.c" -o "$T/keccak.o" \
    2>"$T/k.log" || { tail -20 "$T/k.log"; exit 3; }
# vrf.c, ed25519_verify.c and test_signer.c include the vendored ref10 field
# code, which left-shifts negative limbs: UBSan's shift-base check is off for
# those objects only (kernel/Makefile VRF_UB); every other check stays on
for f in vrf/vrf robin_debanks/ed25519_verify provenance/test_signer; do
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $CFLAGS -fno-sanitize=shift-base $BASEINC -c "$K/src/$f.c" \
        -o "$T/${f##*/}.o" 2>"$T/v.log" || { tail -20 "$T/v.log"; exit 3; }
done
set --
for f in mint/mint qnote/qnote svault/svault fxepoch/fxepoch vhub/vhub capmkt/capmkt \
         market/market market/mk_order market/mk_extra market/mk_pay royalty/royalty cred/cred \
         sicily/sicily dispute/dispute pay/pay_ledger pay/pay_util pay/pay_assure \
         robin_debanks/sha256 pqsec/pq_mldsa87 pqsec/mldsa/fips202 \
         tensor/zt swarm/swarm_market swarm/swarm_budget \
         swarm/swarm_emotion web4/web4_json web4/web4_util; do
    set -- "$@" "$K/src/$f.c"
done
"$CC" -std=c11 -O1 -Wall -Wextra -Werror $CFLAGS -I"$HERE" -I"$K/src/web4" \
    -I"$K/src/robin_debanks" -I"$K/src/mlkem" $BASEINC -I"$K/src/tensor" -I"$K/src/swarm" \
    -I"$K/src/zcapital" -I"$K/src/pay" -I"$K/src/lpres" -I"$K/src/mint" -I"$K/src/qnote" \
    -I"$K/src/svault" -I"$K/src/fxepoch" -I"$K/src/vhub" -I"$K/src/capmkt" -I"$K/src/market" \
    -I"$K/src/royalty" -I"$K/src/cred" -I"$K/src/sicily" -I"$K/src/dispute" -I"$K/src/vrf" \
    -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxvd_money.c" "$HERE/zxvd_money.c" "$HERE/zxvd_sandbox.c" "$HERE/zxvd_rpc.c" \
    $RPCINC $RPCDEPS "$@" "$T/keccak.o" "$T/vrf.o" "$T/ed25519_verify.o" "$T/test_signer.o" \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_money" 2>"$T/cc.log" ||
    { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -40; exit 3; }
"$T/test_zxvd_money"
