#!/bin/sh
# build.sh — build the zxv-fab tool with the app runtimes its unpack-app
# gate runs (the pillars, Z-Wasm and libzxv, built incrementally as
# kernel/src/apps/test_zxapp.sh builds them). Exits 3 when a build fails.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: build.sh OUT [extra compiler flags...]
set -eu
OUT=$1
shift
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../.." && pwd)"
K="$R/kernel/src"
CC="${CC:-cc}"
L="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-fab-build.XXXXXX")"
trap 'rm -rf "$L"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" > "$L/lib.log" 2>&1 || { tail -20 "$L/lib.log"; exit 3; }
for p in apl-kron:libzkron ada-eve:libzeve cobol-sutra:libzsutra mumps-myra:libzmyra; do
    make -s -C "$R/sdk/pillars/${p%%:*}" CC="$CC" "build/${p#*:}.a" > "$L/p.log" 2>&1 || { tail -20 "$L/p.log"; exit 3; }
done
make -s -C "$R/sdk/pillars/fortran-trebago" CC="$CC" FORTRAN=0 build/libztrebago.a > "$L/p.log" 2>&1 || { tail -20 "$L/p.log"; exit 3; }
make -s -C "$R/sdk/kits/z-wasm" CC="$CC" build/libzwasm.a > "$L/p.log" 2>&1 || { tail -20 "$L/p.log"; exit 3; }
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$@" \
    -I"$R/kernel/include" -I"$K/lpres" -I"$K/edp_risk" -I"$K/event_space" -I"$K/modbind" -I"$K/surplus" -I"$K/e8" \
    -I"$K/apps" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/pillars/ada-eve/include" \
    -I"$R/sdk/pillars/cobol-sutra/include" -I"$R/sdk/pillars/fortran-trebago/include" \
    -I"$R/sdk/pillars/mumps-myra/include" -I"$R/sdk/kits/z-wasm/include" -I"$R/sdk/libzxv/include" \
    "$HERE/zxv_fab.c" "$HERE/zxv_fab_app.c" \
    "$K/fab/fab.c" "$K/fab/fab_send.c" "$K/ipfs_node/ipfsn_multiformats.c" "$K/ipfs_node/ipfsn_unixfs.c" \
    "$K/robin_debanks/sha256.c" "$K/tls/aead.c" "$K/tls/hkdf.c" "$K/mlkem/keccak.c" \
    "$K/pqsec/pq_mlkem1024.c" "$K/pqsec/pq_mldsa87.c" "$K/pqsec/mldsa/fips202.c" \
    "$K/appfab/appfab.c" "$K/apps/zxapp.c" "$K/apps/zxapp_kron.c" "$K/apps/zxapp_eve.c" \
    "$K/apps/zxapp_sutra.c" "$K/apps/zxapp_trebago.c" "$K/apps/zxapp_myra.c" "$K/apps/zxapp_wasm.c" \
    "$K/apps/zxapp_lingua.c" "$K/apps/zxapp_pce.c" \
    "$K/lingua/zl_ir.c" "$K/lingua/zl_basic.c" "$K/lingua/zl_fortran.c" "$K/lingua/zl_forth.c" "$K/lingua/zl_pack.c" \
    "$K/pce/pce.c" "$K/pce/pce_prob.c" "$K/pce/pce_tac.c" "$K/pce/pce_strat.c" "$K/pce/pce_fin.c" "$K/pce/pce_entry.c" \
    "$K/paradox/paradox.c" "$K/lpres/lpres_hex.c" "$K/lpres/lpres.c" "$K/rational/rational.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" "$R/sdk/pillars/ada-eve/build/libzeve.a" \
    "$R/sdk/kits/z-wasm/build/libzwasm.a" "$R/sdk/pillars/cobol-sutra/build/libzsutra.a" \
    "$R/sdk/pillars/fortran-trebago/build/libztrebago.a" "$R/sdk/pillars/mumps-myra/build/libzmyra.a" \
    -L"$R/sdk/libzxv/build" -lzxv -Wl,-rpath,"$R/sdk/libzxv/build" -lpthread \
    -o "$OUT" > "$L/cc.log" 2>&1 || { grep -v "Assembler messages\|section attributes" "$L/cc.log"; exit 3; }
