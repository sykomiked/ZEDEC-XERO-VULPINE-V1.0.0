#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_ci_boot_check.sh — host test of build_system/ci_boot_check.sh, the
# fail-closed boot gate every CI boot job uses. Each case is a fake Makefile
# whose `run` target prints a serial log (and then hangs like a kernel, or
# exits like a broken QEMU); the gate must accept only a log with [BOOT_OK]
# and no fault marker that was stopped by the timeout.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CHECK="$HERE/ci_boot_check.sh"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/zxv_bootcheck.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

pass=0
fail=0
# case <name> <expected: 0 pass / 1 fail> <run recipe (shell)>
case_() {
    local name=$1 want=$2 recipe=$3 rc
    printf 'run:\n\t@%s\n' "$recipe" > "Makefile.$name"
    bash "$CHECK" "$name" "Makefile.$name" 2 > "out.$name" 2>&1
    rc=$?
    [ "$rc" -ne 0 ] && rc=1
    if [ "$rc" -eq "$want" ]; then
        echo "[PASS] $name (exit $rc)"
        pass=$((pass + 1))
    else
        echo "[FAIL] $name: expected exit $want, got $rc"
        cat "out.$name"
        fail=$((fail + 1))
    fi
}

HANG='sleep 30'
case_ healthy 0 "printf 'boot\n[BOOT_OK] up\ntick\n'; $HANG"
case_ contained_demo 0 "printf '[FAULT CONTAINED] demo ok\n[BOOT_OK] up\n'; $HANG"
case_ no_boot_ok 1 "printf 'boot\nstill booting\n'; $HANG"
case_ silent 1 "$HANG"
case_ fault_after_ok 1 "printf '[BOOT_OK] up\n[FAULT] ARM32 exception: data abort\n'; $HANG"
case_ el0_fault 1 "printf '[BOOT_OK] up\n[EL0 FAULT] esr=1\n'; $HANG"
case_ x86_exception 1 "printf '[BOOT_OK] up\nCPU exception vector=14\n'; $HANG"
case_ panic 1 "printf '[BOOT_OK] up\nKernel panic: oops\n'; $HANG"
case_ qemu_exit_error 1 "printf '[BOOT_OK] up\n'; exit 1"
case_ qemu_exit_clean_ok 0 "printf '[BOOT_OK] up\n'; exit 0"
case_ binary_bytes 0 "printf '\\001\\377junk\n[BOOT_OK] up\n'; $HANG"

echo "ci_boot_check: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
