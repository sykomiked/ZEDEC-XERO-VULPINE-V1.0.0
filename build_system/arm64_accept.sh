#!/usr/bin/env bash
# build_system/arm64_accept.sh — ARM64 QEMU acceptance smoke test
#
# Builds the ARM64 kernel and runs it under qemu-system-aarch64,
# checking that the serial output contains the expected [BOOT_OK]
# marker and that the EL0 / P-TERM prompt is reached.
#
# Usage:
#   build_system/arm64_accept.sh [preempt]
#
# If "preempt" is passed as the first argument, the kernel is built
# with ENABLE_PREEMPT_TEST=1 and the test additionally checks for
# interleaved 'A' and 'B' characters in the output.

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAKE="make -f build_system/Makefile.arm64"
LOG_DIR="/tmp"
ACCEPT_LOG="${LOG_DIR}/arm64_accept_$$.log"
QEMU_TIMEOUT=35

cd "$PROJECT_ROOT"

PREEMPT_ARG=""
if [ "$1" = "preempt" ]; then
    PREEMPT_ARG="CFLAGS_EXTRA='-DENABLE_PREEMPT_TEST=1'"
    QEMU_TIMEOUT=95
fi

echo "[ACCEPT] Building ARM64 kernel..."
$MAKE clean
$MAKE -j"$(nproc)" $PREEMPT_ARG 2>&1 | tee "${LOG_DIR}/arm64_accept_build_$$.log"

if [ ! -f "${PROJECT_ROOT}/kernel_arm64.elf" ]; then
    echo "[ACCEPT FAIL] kernel_arm64.elf not built"
    exit 1
fi

echo "[ACCEPT] Running QEMU (timeout ${QEMU_TIMEOUT}s)..."
timeout "$QEMU_TIMEOUT" qemu-system-aarch64 \
    -M virt,gic-version=3 \
    -cpu cortex-a53 \
    -m 256M \
    -kernel kernel_arm64.elf \
    -nographic -serial mon:stdio \
    > "$ACCEPT_LOG" 2>&1 || true

if ! grep -q "\[BOOT_OK\]" "$ACCEPT_LOG"; then
    echo "[ACCEPT FAIL] [BOOT_OK] not found in serial log"
    tail -30 "$ACCEPT_LOG"
    exit 1
fi

# Reject any fault, exception, or panic marker in the boot log.
if grep -qE '\[FAULT\]|\[PANIC\]|panic:|Exception:|Synchronous|SError|Data Abort|Undefined instruction|PC alignment' "$ACCEPT_LOG"; then
    echo "[ACCEPT FAIL] fault or exception marker found"
    grep -nE '\[FAULT\]|\[PANIC\]|panic:|Exception:|Synchronous|SError|Data Abort|Undefined instruction|PC alignment' "$ACCEPT_LOG" | tail -10
    exit 1
fi

# Non-preempt builds must reach the EL0 P-TERM prompt.
if [ "$1" != "preempt" ]; then
    if ! grep -q "P-TERM/EL0" "$ACCEPT_LOG"; then
        echo "[ACCEPT FAIL] EL0 P-TERM prompt not reached"
        tail -30 "$ACCEPT_LOG"
        exit 1
    fi
fi

if [ "$1" = "preempt" ]; then
    # The preempt build runs two EL0 processes that do not call SYS_YIELD.
    # Their output is the last line composed only of A/B characters.
    USER_OUT=$(grep -E '^[AB \t]+$' "$ACCEPT_LOG" | tail -1 | tr -d '[:space:]')
    if [ -z "$USER_OUT" ]; then
        echo "[ACCEPT FAIL] No preemption A/B output seen"
        tail -20 "$ACCEPT_LOG"
        exit 1
    fi

    A_COUNT=$(echo -n "$USER_OUT" | tr -cd 'A' | wc -c | tr -d ' ')
    B_COUNT=$(echo -n "$USER_OUT" | tr -cd 'B' | wc -c | tr -d ' ')
    TRANSITIONS=$(echo -n "$USER_OUT" | awk '{c=0; for(i=2;i<=length($0);i++) if(substr($0,i,1)!=substr($0,i-1,1)) c++; print c}')
    SYS_YIELD_COUNT=$(grep -c "SYS_YIELD" "$ACCEPT_LOG" || true)

    echo "[ACCEPT] Preemption output: ${USER_OUT}"
    echo "[ACCEPT] A=${A_COUNT} B=${B_COUNT} transitions=${TRANSITIONS} SYS_YIELD=${SYS_YIELD_COUNT}"

    if [ "$A_COUNT" -ne 20 ]; then
        echo "[ACCEPT FAIL] Expected 20 A characters, got ${A_COUNT}"
        exit 1
    fi

    if [ "$B_COUNT" -ne 20 ]; then
        echo "[ACCEPT FAIL] Expected 20 B characters, got ${B_COUNT}"
        exit 1
    fi

    if [ "$TRANSITIONS" -le "$SYS_YIELD_COUNT" ]; then
        echo "[ACCEPT FAIL] No A/B transition without SYS_YIELD observed (transitions=${TRANSITIONS}, SYS_YIELD=${SYS_YIELD_COUNT})"
        exit 1
    fi
fi

WARN_COUNT=$(grep -ci "warning:" "${LOG_DIR}/arm64_accept_build_$$.log" || true)
echo "[ACCEPT] Build warning count: ${WARN_COUNT}"

if [ "$1" = "preempt" ]; then
    echo "[ACCEPT PASS] ARM64 timer preemption verified: 20 A, 20 B, ${TRANSITIONS:-0} transitions, ${SYS_YIELD_COUNT:-0} SYS_YIELD, [BOOT_OK], no faults."
else
    echo "[ACCEPT PASS] ARM64 kernel booted to EL0 and [BOOT_OK] confirmed."
fi
