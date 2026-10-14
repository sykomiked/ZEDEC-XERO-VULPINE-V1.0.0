#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# mut_check.sh - what the mutation harness runs per policy-engine mutant
# (run from kernel/): the two unit suites under ASan/UBSan and a short fuzz.
# Exits 3 when only a build failed (the harness then drops the mutant as not
# valid), 1 when a test failed, 0 when everything passed.
T=${ZXV_TMP:-/tmp}
INC="-Iinclude -Isrc/policy -Isrc/lpres -Isrc/event_space -Isrc/modbind -Isrc/treaty -Isrc/engine -Isrc/pay -Isrc/abacus -Isrc/rational -Isrc/swarm -Isrc/tensor -Isrc/mlkem -Isrc/zcapital"
RUN="src/lpres/lpres.c src/treaty/zxt.c src/treaty/zxt_exec.c src/engine/zxv_engine.c src/abacus/abacus.c src/rational/rational.c src/pay/pay_ledger.c src/pay/pay_util.c src/pay/pay_assure.c src/swarm/swarm_market.c src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/tensor/zt.c src/mlkem/keccak.c"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
W="-std=c11 -Wall -Werror -Wextra -DTEST_HOST"
gcc $W $SAN -Isrc/policy src/policy/test_zxp.c src/policy/zxp.c src/policy/zxp_draft.c -o "$T/test_zxp" 2>/dev/null || exit 3
gcc $W $SAN $INC src/policy/test_policy.c src/policy/policy.c src/policy/policy_cobol.c src/policy/zxp.c $RUN -o "$T/test_policy" 2>/dev/null || exit 3
gcc $W -O1 -Isrc/policy src/policy/fuzz_zxp.c src/policy/zxp.c -o "$T/fuzz_zxp" 2>/dev/null || exit 3
"$T/test_zxp" > "$T/test_zxp.log" 2>&1 || { tail -5 "$T/test_zxp.log"; exit 1; }
"$T/test_policy" > "$T/test_policy.log" 2>&1 || { tail -5 "$T/test_policy.log"; exit 1; }
"$T/fuzz_zxp" 20000 1 > "$T/fuzz_zxp.log" 2>&1 || { tail -5 "$T/fuzz_zxp.log"; exit 1; }
echo "[PASS] policy mutation check"
