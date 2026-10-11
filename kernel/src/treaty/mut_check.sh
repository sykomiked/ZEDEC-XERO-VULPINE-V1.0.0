#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# mut_check.sh - what the mutation harness runs per treaty mutant (run from
# kernel/): the two unit suites under ASan/UBSan and a short fuzz. Exits 3
# when only a build failed (the harness then drops the mutant as not valid),
# 1 when a test failed, 0 when everything passed.
T=${ZXV_TMP:-/tmp}
INC="-Iinclude -Isrc/treaty -Isrc/engine -Isrc/pay -Isrc/abacus -Isrc/rational -Isrc/swarm -Isrc/tensor -Isrc/mlkem -Isrc/zcapital -Isrc/modbind"
RUN="src/treaty/zxt_exec.c src/treaty/zxt_cobol.c src/engine/zxv_engine.c src/abacus/abacus.c src/rational/rational.c src/pay/pay_ledger.c src/pay/pay_util.c src/pay/pay_assure.c src/swarm/swarm_market.c src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/tensor/zt.c src/mlkem/keccak.c"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
W="-std=c11 -Wall -Werror -Wextra -DTEST_HOST"
gcc $W $SAN -Isrc/treaty src/treaty/test_zxt.c src/treaty/zxt.c -o "$T/test_zxt" 2>/dev/null || exit 3
gcc $W $SAN $INC src/treaty/test_zxt_exec.c src/treaty/zxt.c $RUN -o "$T/test_zxt_exec" 2>/dev/null || exit 3
gcc $W -O1 -Isrc/treaty src/treaty/fuzz_zxt.c src/treaty/zxt.c -o "$T/fuzz_zxt" 2>/dev/null || exit 3
"$T/test_zxt" > "$T/test_zxt.log" 2>&1 || { tail -5 "$T/test_zxt.log"; exit 1; }
"$T/test_zxt_exec" > "$T/test_zxt_exec.log" 2>&1 || { tail -5 "$T/test_zxt_exec.log"; exit 1; }
"$T/fuzz_zxt" 40000 1 > "$T/fuzz_zxt.log" 2>&1 || { tail -5 "$T/fuzz_zxt.log"; exit 1; }
echo "[PASS] treaty mutation check"
