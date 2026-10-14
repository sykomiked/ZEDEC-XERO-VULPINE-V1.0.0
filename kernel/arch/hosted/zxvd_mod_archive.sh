#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# zxvd_mod_archive.sh — build (or reuse) the archive of zxvd's module methods
# (zxvd_rpc_modules.c and the kernel sources they serve) and print its path.
# Every test that links zxvd_rpc.c needs it, because zxvd_rpc.c falls back to
# the module methods (test_zxvd_rpc.sh, test_zxvd_aipi.sh). CC as the caller's.
# Exits 3 when the build failed, like the test scripts that call it.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-mod.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
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
set --
for f in src/web4/web4_json.c src/web4/web4_util.c src/mlkem/keccak.c src/robin_debanks/sha256.c \
         $(sh "$HERE/app_sources.sh" modules); do
    set -- "$@" "$K/$f"
done
SUM="$({ "$CC" --version 2>&1; cat "$@" "$HERE"/*.h "$K"/include/*.h "$K"/src/*/*.h \
    "$K"/src/pqsec/mldsa/*.h "$R"/sdk/libzxv/include/*.h; } | cksum | cut -d' ' -f1)"
A="${ZXV_TMP:-${TMPDIR:-/tmp}}/zxvd-rpc-modules.$SUM.a"
if [ ! -f "$A" ]; then
    mkdir "$T/o"
    i=0
    for f in "$@"; do
        i=$((i + 1))
        # keccak.c at -O2: gcc 11 reports a false maybe-uninitialized in
        # keccak_sponge at -O1
        o=""
        case "$f" in */keccak.c) o=-O2 ;; esac
        cc_ ${o:+"$o"} -c "$f" -o "$T/o/$i.o" >&2 || exit 3
    done
    ar rcs "$T/m.a" "$T"/o/*.o || exit 3
    mv "$T/m.a" "$A.$$" && mv "$A.$$" "$A" # a whole archive or none
fi
echo "$A"
