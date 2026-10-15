#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_boot_fail_closed.sh — boot the REFUSAL path of one kernel (roadmap 2.14).
#
#   build_system/test_boot_fail_closed.sh <name> <makefile> [seconds]
#
# For each injectable boot check (1 = declaration gate, 2 = MegaROM; see
# ZXV_BOOT_INJECT in the arch main), build the kernel with that check forced to
# fail, boot it under QEMU through ci_boot_check.sh, and require all three:
#   - ci_boot_check.sh REJECTS the boot (exit non-zero);
#   - the log names the refusal with [BOOT_FAIL];
#   - the log never printed [BOOT_OK] (the milestone is not printed over a
#     failed check, which is what "fail closed" means here).
# Finally the shipping image (ZXV_BOOT_INJECT=0) is rebuilt, so a later boot
# or artifact upload in the same job never picks up an injected kernel.
# Covers riscv64, riscv32 and arm32; riscv32 needs ZXV_RV32_SBI as for its boot.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=${1:?usage: test_boot_fail_closed.sh <name> <makefile> [seconds]}
MK=${2:?usage: test_boot_fail_closed.sh <name> <makefile> [seconds]}
SECS=${3:-20}
JOBS=$(nproc 2>/dev/null || echo 2)
# Build and check logs go to a scratch dir; the boot log stays where
# ci_boot_check.sh writes it (boot_<name>.log, git-ignored).
L=$(mktemp -d "${TMPDIR:-/tmp}/zxv_failclosed.XXXXXX")

fail=0
for n in 1 2; do
    tag="${NAME}_inject${n}"
    if ! ZXV_BOOT_INJECT=$n make -j"$JOBS" -f "$MK" all > "$L/build_${tag}.log" 2>&1; then
        tail -20 "$L/build_${tag}.log"
        echo "::error::$tag: build with ZXV_BOOT_INJECT=$n failed"
        fail=1
        continue
    fi
    if ZXV_BOOT_INJECT=$n bash "$HERE/ci_boot_check.sh" "$tag" "$MK" "$SECS" > "$L/check_${tag}.log" 2>&1; then
        echo "::error::$tag: the boot check ACCEPTED a kernel whose check $n was forced to fail"
        fail=1
    fi
    if ! grep -aq '\[BOOT_FAIL\]' "boot_${tag}.log"; then
        echo "::error::$tag: no [BOOT_FAIL] line; the refusal was not reported"
        tail -20 "boot_${tag}.log"
        fail=1
    fi
    if grep -aq '\[BOOT_OK\]' "boot_${tag}.log"; then
        echo "::error::$tag: [BOOT_OK] printed although check $n failed (fail-open)"
        fail=1
    fi
    [ "$fail" -eq 0 ] && echo "[PASS] $tag: refused with $(grep -a -m1 '\[BOOT_FAIL\]' "boot_${tag}.log")"
done

if ! ZXV_BOOT_INJECT=0 make -j"$JOBS" -f "$MK" all > "$L/build_${NAME}_restore.log" 2>&1; then
    tail -20 "$L/build_${NAME}_restore.log"
    echo "::error::$NAME: rebuilding the shipping image (ZXV_BOOT_INJECT=0) failed"
    fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "[boot-fail-closed] $NAME: every injected boot check stopped the boot before [BOOT_OK]"
