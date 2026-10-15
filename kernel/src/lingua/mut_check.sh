#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# mut_check.sh - what the mutation harness runs per Lingua mutant (run from
# kernel/): the unit suite under ASan/UBSan and a short fuzz. Exits 3 when
# only a build failed (the harness then drops the mutant as not valid), 1
# when a test failed, 0 when everything passed.
T=${ZXV_TMP:-/tmp}
SRC="src/lingua/zl_ir.c src/lingua/zl_basic.c src/lingua/zl_fortran.c src/lingua/zl_forth.c src/lingua/zl_pack.c"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
W="-std=c11 -Wall -Werror -Wextra -Isrc/lingua"
gcc $W $SAN src/lingua/test_zl.c $SRC -o "$T/test_zl_mut" 2>/dev/null || exit 3
gcc $W -O1 src/lingua/fuzz_zl.c $SRC -o "$T/fuzz_zl_mut" 2>/dev/null || exit 3
timeout 120 "$T/test_zl_mut" src/lingua/golden > "$T/test_zl_mut.log" 2>&1 || { tail -5 "$T/test_zl_mut.log"; exit 1; }
timeout 120 "$T/fuzz_zl_mut" 3000 1 > "$T/fuzz_zl_mut.log" 2>&1 || { tail -5 "$T/fuzz_zl_mut.log"; exit 1; }
echo "[PASS] lingua mutation check"
