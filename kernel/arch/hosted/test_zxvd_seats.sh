#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxvd_seats.sh — build libzxv (incrementally) and run test_zxvd_seats.c:
# zxvd_seats.c (seats.*) behind zxvd_rpc.c on the real seats module and
# ident's Argon2id. The mutation harness's test command for zxvd_seats.c
# (tests/harness/mutator/targets.json): exits 3 when only the build failed.
# Everything but zxvd_seats.c and the test is compiled once into an archive
# kept under ZXV_TMP (or TMPDIR), named by a checksum of its sources, the
# headers and CC, so each mutant compiles two files.
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
TMPB="${ZXV_TMP:-${TMPDIR:-/tmp}}"
T="$(mktemp -d "$TMPB/zxvd-seats.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
RP="-Wl,-rpath,$R/sdk/libzxv/build"
INCS="$RPCINC -I$HERE -I$K/src/web4 -I$K/src/seats -I$K/src/ident -I$K/src/pqsec -I$K/src/lpres
      -I$K/src/surplus -I$K/src/edp_risk -I$K/src/robin_debanks -I$K/src/mlkem -I$K/include
      -I$K/src/modbind -I$K/src/e8 -I$K/src/event_space -I$R/sdk/libzxv/include"
FLAGS="-std=c11 -O2 -Wall -Wextra"
WERR="-Werror"
# the seats module, ident's Argon2id and what id_core.c (its memory helpers)
# links against (app_sources.sh groups, build_zxvd.sh)
set -- "$HERE/zxvd_rpc.c" $RPCDEPS "$HERE/zxv_http_guard.c" "$K/src/web4/web4_json.c" \
    "$K/src/web4/web4_util.c" "$K/src/robin_debanks/sha256.c" "$K/src/tls/x25519.c" \
    "$K/src/seats/seats.c" "$K/src/ident/id_argon2.c" "$K/src/ident/id_core.c" "$K/src/ident/id_mem.c"
for f in pq_mldsa65 pq_mldsa87 pq_mlkem1024 pq_matrix pq_hqc5 pq_slh256s; do
    set -- "$@" "$K/src/pqsec/$f.c"
done
for f in ntt packing poly polyvec reduce rounding sign symmetric-shake fips202; do
    set -- "$@" "$K/src/pqsec/mldsa/$f.c"
done
for f in sha3_api sha3_f1600 slh_dsa slh_shake; do set -- "$@" "$K/src/pqsec/slhdsa/$f.c"; done
for f in keccak mlkem768 mlkem_encode mlkem_sample mlkem_ntt mlkem_kpe; do
    set -- "$@" "$K/src/mlkem/$f.c"
done
KEY="$({ echo "$CC $FLAGS"; cat "$@" "$HERE"/*.h "$K"/src/seats/*.h "$K"/src/ident/*.h \
    "$K"/src/pqsec/*.h "$K"/src/pqsec/*/*.h "$K"/src/pqsec/hqc5/*.c "$K"/src/mlkem/*.h \
    "$K"/src/web4/*.h; } | cksum | cut -d' ' -f1)"
LIB="$TMPB/zxvd-seats-objs-$KEY"
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
$CC $FLAGS $WERR $INCS "$HERE/test_zxvd_seats.c" "$HERE/zxvd_seats.c" "$LIB/objs.a" \
    "$MODA" -L"$R/sdk/libzxv/build" -lzxv "$RP" -lm -o "$T/test_zxvd_seats" 2>"$T/cc.log" ||
    { grep -v "Assembler messages\|incorrect section attributes" "$T/cc.log" | tail -20; exit 3; }
ZXV_TMP="$T" "$T/test_zxvd_seats"
