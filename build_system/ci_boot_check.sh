#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ci_boot_check.sh — boot one architecture's kernel under QEMU once and fail
# closed unless the serial log reached [BOOT_OK] with no fault marker.
#
#   build_system/ci_boot_check.sh <name> <makefile> [seconds]
#
#   name      label for messages and the log file (boot_<name>.log)
#   makefile  e.g. build_system/Makefile.riscv; its `run` target boots QEMU
#   seconds   how long QEMU runs before timeout stops it (default 30)
#
# The kernels never exit, so `timeout` ending QEMU (exit 124) is the normal
# outcome. Any OTHER non-zero exit means make or QEMU failed before or while
# booting (missing firmware, bad -M, build error) and fails the check even if a
# partial log happens to contain the marker.
#
# Fault markers, one per place a kernel reports a fatal exception:
#   [FAULT]                  arm64 arm64_exceptions.c, riscv kernel_main_riscv.c,
#                            arm32 kernel_main_arm32.c (arm32_fault_report)
#   [EL0 FAULT]              arm64 user-mode fault
#   CPU exception vector=    x86_64 ring3.c x86_exc_report
#   PANIC / Kernel panic     generic, in case a later handler uses them
# "[FAULT CONTAINED]" is a healthy boot line (fault-containment demo) and is
# deliberately not matched: the bracket must close right after FAULT.
set -u

NAME=${1:?usage: ci_boot_check.sh <name> <makefile> [seconds]}
MK=${2:?usage: ci_boot_check.sh <name> <makefile> [seconds]}
SECS=${3:-30}
LOG="boot_${NAME}.log"
FAULT_RE='\[FAULT\]|\[EL0 FAULT\]|CPU exception vector=|PANIC|[Kk]ernel panic'

rc=0
timeout "$SECS" make -f "$MK" run < /dev/null > "$LOG" 2>&1 || rc=$?
cat "$LOG"
echo "[boot-check] $NAME: make run exited $rc after at most ${SECS}s"

fail=0
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "::error::$NAME boot: make run failed with exit $rc (not a timeout)"
    fail=1
fi
# -a: the serial log carries a few non-text bytes, and grep must not treat it
# as a binary file.
if ! grep -aq '\[BOOT_OK\]' "$LOG"; then
    echo "::error::$NAME boot did not reach [BOOT_OK]"
    fail=1
fi
if grep -aqE "$FAULT_RE" "$LOG"; then
    echo "::error::$NAME boot printed a fault marker:"
    grep -anE "$FAULT_RE" "$LOG" | head -5
    fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "[boot-check] $NAME: [BOOT_OK] reached, no fault marker"
