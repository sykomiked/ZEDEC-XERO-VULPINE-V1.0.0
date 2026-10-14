#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_web4.sh — build libzxv (incrementally) and run test_zxvd_web4.c:
# zxvd_web4.c behind zxvd_rpc.c on the real web4_agent envelopes and ML-DSA-65.
# The mutation harness's test command for zxvd_web4.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
# Everything but zxvd_web4.c and the test is compiled once into an archive
# kept under ZXV_TMP (or TMPDIR), named by a checksum of its sources, the
# headers and CC, so each mutant compiles two files.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
# what zxvd_rpc.c links beyond web4, keccak and sha256 (app_sources.sh rpcdeps)
RPCDEPS="$(sh "$K/arch/hosted/app_sources.sh" rpcdeps | sed "s|^|$K/|")"
RPCINC="$(sh "$K/arch/hosted/app_sources.sh" rpcincs | sed "s|^|-I$K/|")"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
TMPB="${ZXV_TMP:-${TMPDIR:-/tmp}}"
T="$(mktemp -d "$TMPB/zxvd-web4.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
INCS="$RPCINC -I$HERE -I$K/src/web4 -I$K/src/freight -I$K/src/pqsec -I$K/src/lpres -I$K/src/surplus
      -I$K/src/edp_risk -I$K/src/robin_debanks -I$K/src/mlkem -I$K/include -I$K/src/modbind
      -I$K/src/e8 -I$K/src/event_space -I$R/sdk/libzxv/include"
FLAGS="-std=c11 -O1 -Wall -Wextra"
# -Werror for this group and its test only: the engines and the vendored
# crypto are not changed here (gcc-11 warns in keccak.c)
WERR="-Werror"
set -- "$HERE/zxvd_rpc.c" $RPCDEPS "$HERE/zxv_http_guard.c" "$K/src/web4/web4_json.c" \
    "$K/src/web4/web4_util.c" "$K/src/mlkem/keccak.c" "$K/src/robin_debanks/sha256.c"
for f in pq_mldsa65.c mldsa/ntt.c mldsa/packing.c mldsa/poly.c mldsa/polyvec.c mldsa/reduce.c \
         mldsa/rounding.c mldsa/sign.c mldsa/symmetric-shake.c mldsa/fips202.c; do
    set -- "$@" "$K/src/pqsec/$f"
done
set -- "$@" "$K/src/web4/web4_agent.c" "$K/src/freight/freight.c" "$K/src/freight/freight_ops.c"
KEY="$({ echo "$CC $FLAGS"; cat "$@" "$HERE"/*.h "$K"/src/freight/*.h "$K"/src/pqsec/*.h \
    "$K"/src/pqsec/mldsa/*.h "$K"/src/web4/*.h; } | cksum | cut -d' ' -f1)"
LIB="$TMPB/zxvd-web4-objs-$KEY"
if [ ! -f "$LIB/objs.a" ]; then
    mkdir -p "$T/objs"
    i=0
    for f in "$@"; do
        i=$((i + 1))
        # shellcheck disable=SC2086 # CC may carry flags; INCS is a list
        $CC $FLAGS $INCS -c "$f" -o "$T/objs/$i.o" 2>>"$T/cc.log" ||
            { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
    done
    ar rcs "$T/objs/objs.a" "$T"/objs/*.o
    mv "$T/objs" "$LIB" 2>/dev/null || true # another run may have made it first
    [ -f "$LIB/objs.a" ] || LIB="$T/objs"
fi
# shellcheck disable=SC2086
$CC $FLAGS $WERR $INCS "$HERE/test_zxvd_web4.c" "$HERE/zxvd_web4.c" "$LIB/objs.a" \
    -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_web4" 2>"$T/cc.log" ||
    { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
"$T/test_zxvd_web4"
