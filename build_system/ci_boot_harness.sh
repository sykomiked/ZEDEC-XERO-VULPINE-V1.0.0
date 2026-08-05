#!/bin/bash
# ci_boot_harness.sh — boot the ARM64 kernel N times under QEMU and
# require every boot to reach [BOOT_OK]. Fail-closed: a single missing
# BOOT_OK, timeout, or kernel fault fails the whole run.
#
#   build_system/ci_boot_harness.sh [N]     (default 100)
#
# Run from 05_KERNEL/. Expects kernel_arm64.elf already built.
set -u
N=${1:-100}
KERNEL=kernel_arm64.elf
QEMU=qemu-system-aarch64

if [ ! -f "$KERNEL" ]; then
    echo "[HARNESS] $KERNEL not found — build it first"; exit 1
fi

pass=0
for i in $(seq 1 "$N"); do
    # The kernel never exits, so stop reading as soon as BOOT_OK appears
    # (sed q). A hard `timeout` backstops a hung boot. Capture the full
    # pre-BOOT_OK log so we can also assert no fault occurred on the way.
    out=$(timeout 12 $QEMU -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
        -kernel "$KERNEL" -nographic -serial mon:stdio 2>&1 \
        | sed '/\[BOOT_OK\]/q')
    if echo "$out" | grep -q "\[BOOT_OK\]" && \
       ! echo "$out" | grep -qE "\[FAULT\]|\[EL0 FAULT\]"; then
        pass=$((pass+1))
    else
        echo "[HARNESS] boot $i FAILED (no BOOT_OK or a fault occurred)"
        echo "$out" | tail -5
    fi
done

echo "[HARNESS] $pass/$N boots reached BOOT_OK with no faults"
[ "$pass" -eq "$N" ] || { echo "*** BOOT HARNESS FAILED ***"; exit 1; }
echo "*** BOOT HARNESS PASSED ($N/$N) ***"
