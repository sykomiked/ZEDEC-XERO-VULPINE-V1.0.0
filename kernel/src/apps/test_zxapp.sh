#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxapp.sh — build libzxv and the apl-kron pillar (incrementally) and run
# test_zxapp.c: app manifests and KRON app logic. Exits 3 when only the
# build failed.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxv-zxapp.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
make -s -C "$R/sdk/libzxv" CC="$CC" >"$T/lib.log" 2>&1 || { tail -20 "$T/lib.log"; exit 3; }
make -s -C "$R/sdk/pillars/apl-kron" CC="$CC" build/libzkron.a >"$T/kron.log" 2>&1 || { tail -20 "$T/kron.log"; exit 3; }
"$CC" -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$HERE" -I"$R/sdk/pillars/apl-kron/include" -I"$R/sdk/libzxv/include" \
    "$HERE/test_zxapp.c" "$HERE/zxapp.c" "$HERE/zxapp_kron.c" \
    "$R/sdk/pillars/apl-kron/build/libzkron.a" -L"$R/sdk/libzxv/build" -lzxv -Wl,-rpath,"$R/sdk/libzxv/build" -lpthread \
    -o "$T/test_zxapp" || exit 3
"$T/test_zxapp"
