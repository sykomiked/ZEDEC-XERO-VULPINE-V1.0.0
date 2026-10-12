#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_rpc.sh — build libzxv (incrementally) and run test_zxvd_rpc.c
# against zxvd_rpc.c, then test_zxvd_rpc_modules.c (the module methods). The
# mutation harness's test command for zxvd_rpc.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-rpc.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
# zxvd_rpc.c falls back to the module methods (zxvd_rpc_modules.c), so both
# tests link them and the kernel sources they serve. That side is compiled
# once into an archive kept under ZXV_TMP (or TMPDIR), named by a checksum of
# the compiler, every source and every header it can include, and reused:
# the mutation harness changes only zxvd_rpc.c, once per mutant.
cc_() {
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror -I"$HERE" -I"$K/src/web4" -I"$K/src/robin_debanks" \
        -I"$K/src/mlkem" -I"$K/include" -I"$K/src/modbind" -I"$K/src/e8" -I"$K/src/event_space" \
        -I"$K/src/surplus" -I"$K/src/freight" -I"$K/src/harmonic" -I"$K/src/stream" -I"$K/src/ehop" \
        -I"$K/src/ipfs_node" -I"$K/src/pqsec" -I"$K/src/lpres" -I"$K/src/edp_risk" \
        -I"$R/sdk/libzxv/include" "$@"
}
set -- "$HERE/zxvd_rpc_modules.c"
for f in web4/web4_json.c web4/web4_util.c mlkem/keccak.c robin_debanks/sha256.c freight/freight.c \
         freight/freight_ops.c harmonic/zt_harmonic_wire.c stream/stream_seg.c \
         ipfs_node/ipfsn_multiformats.c ehop/ehop_sched.c ehop/ehop_frame.c tls/aead.c \
         web4/web4_cid.c web4/web4_agent.c pqsec/pq_mldsa65.c; do
    set -- "$@" "$K/src/$f"
done
for f in "$K"/src/pqsec/mldsa/[a-z]*.c; do set -- "$@" "$f"; done
SUM="$({ "$CC" --version 2>&1; cat "$@" "$HERE"/*.h "$K"/include/*.h "$K"/src/*/*.h \
    "$K"/src/pqsec/mldsa/*.h "$R"/sdk/libzxv/include/*.h; } | cksum | cut -d' ' -f1)"
A="${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-rpc-modules.$SUM.a"
if [ ! -f "$A" ]; then
    mkdir "$T/o"
    i=0
    for f in "$@"; do
        i=$((i + 1))
        cc_ -c "$f" -o "$T/o/$i.o" || exit 3
    done
    ar rcs "$T/m.a" "$T"/o/*.o || exit 3
    mv "$T/m.a" "$A.$$" && mv "$A.$$" "$A" # a whole archive or none
fi
for t in test_zxvd_rpc test_zxvd_rpc_modules; do
    cc_ "$HERE/$t.c" "$HERE/zxvd_rpc.c" "$A" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/$t" ||
        exit 3
done
"$T/test_zxvd_rpc"
"$T/test_zxvd_rpc_modules"
