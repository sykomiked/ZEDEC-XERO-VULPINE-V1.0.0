#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_test.sh OUT — build test_hwkey_host (the machine's own security chip
# end to end, hwkey_host.h) for this platform:
#   Linux    CC (default cc), TPM over /dev/tpmrm0 or ZXV_TPM=mssim:HOST:PORT
#   macOS    clang for the C, swiftc for CryptoKit and the link
#   Windows  CC=x86_64-w64-mingw32-gcc (cross), NCrypt / Platform Crypto Provider
# SANITIZE=1 adds ASan/UBSan (Linux only).
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUT" >&2; exit 2; }
OUT="$1"
H="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$H/../../.." && pwd)"
CC="${CC:-cc}"
INC="-I$K/include -I$K/src/pay -I$K/src/mlkem -I$K/src/lpres -I$K/src/event_space -I$K/src/modbind
     -I$K/src/surplus -I$K/src/tensor -I$K/src/swarm -I$K/src/zcapital -I$K/src/edp_risk
     -I$K/src/trispace"
SRC="$H/test_hwkey_host.c $H/hwkey_host.c $K/src/outbox/outbox.c $K/src/hwkey/hwkey.c
     $K/src/hwkey/hwkey_soft.c $K/src/pqsec/pq_mldsa87.c $K/src/pqsec/mldsa/fips202.c
     $K/src/pay/pay_ledger.c $K/src/pay/pay_assure.c $K/src/pay/pay_util.c
     $K/src/swarm/swarm_market.c $K/src/swarm/swarm_budget.c $K/src/swarm/swarm_emotion.c
     $K/src/robin_debanks/sha256.c $K/src/mlkem/keccak.c $K/src/tensor/zt.c"
FLAGS="-std=c11 -O1 -g -Wall -Werror -Wextra -DTEST_HOST"
if [ "${SANITIZE:-}" = 1 ]; then FLAGS="$FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all"; fi

case "$CC" in
*mingw*)
    # shellcheck disable=SC2086
    $CC $FLAGS $INC $SRC "$H/hwkey_win.c" -lncrypt -lbcrypt -o "$OUT"
    ;;
*)
    if [ "$(uname -s)" = Darwin ]; then
        T="$(mktemp -d)"
        FLAGS="-std=c11 -O1 -g -Wall -Werror -Wextra -DTEST_HOST"
        for f in $SRC "$H/hwkey_se.c"; do
            # shellcheck disable=SC2086
            xcrun clang $FLAGS -mmacosx-version-min=10.15 $INC -c "$f" -o "$T/$(basename "$f" .c).o"
        done
        xcrun swiftc -parse-as-library -O -target "$(uname -m)-apple-macos10.15" "$H/hwkey_se.swift" "$T"/*.o \
            -o "$OUT"
        rm -rf "$T"
    else
        # shellcheck disable=SC2086
        $CC $FLAGS $INC $SRC "$H/hwkey_tpm.c" -o "$OUT"
    fi
    ;;
esac
echo "built $OUT"
