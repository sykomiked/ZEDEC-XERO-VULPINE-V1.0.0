#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# boards_boot_check.sh — build each SoC board profile and boot it in QEMU's
# model of that board, failing closed unless the console reached [BOOT_OK]
# (and [AI_OK] on arm64) with no fault marker.
#
#   build_system/boards_boot_check.sh [seconds]
#
# Boards (the arbiters and handheld SoCs of the device line, see
# kernel/src/devprof/devprof.c):
#   arm64 BOARD=6  Zynq UltraScale+ (Sovereign arbiter)  -M xlnx-zcu102
#   arm64 BOARD=7  Versal (Citadel arbiter)              -M xlnx-versal-virt
#   riscv BOARD=1  PolarFire SoC (every other arbiter)   -M microchip-icicle-kit
# arm64 BOARD=5 (RK3588, the handhelds) is built but not booted: QEMU has no
# model of it.
#
# The Icicle Kit model needs a device tree and QEMU does not make one, so the
# mainline Icicle Kit sources are fetched at a pinned Linux tag into build/
# (never committed) and compiled with cpp + dtc. ZXV_MPFS_DTB=<file> uses a
# ready .dtb instead. The board console is MMUART1, QEMU's second serial port.
#
# Needs: aarch64-linux-gnu-gcc, riscv64-linux-gnu-gcc, qemu-system-aarch64,
# qemu-system-riscv64 with /usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin,
# dtc and curl.
set -u

SECS=${1:-30}
LINUX_TAG=v7.2
DTB_DIR=build/mpfs-dtb
OPENSBI=/usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
FAULT_RE='\[BOOT_FAIL\]|\[FAULT\]|\[EL0 FAULT\]|PANIC|[Kk]ernel panic'
fail=0

# check <name> <log> <marker...>: [BOOT_OK], every marker, no fault marker.
check() {
    local name=$1 log=$2 m bad=0
    shift 2
    for m in '[BOOT_OK]' "$@"; do
        if ! grep -aqF -- "$m" "$log"; then
            echo "::error::$name did not print $m"
            bad=1
        fi
    done
    if grep -aqE "$FAULT_RE" "$log"; then
        echo "::error::$name printed a fault marker:"
        grep -aE "$FAULT_RE" "$log" | head -5
        bad=1
    fi
    if [ "$bad" -eq 0 ]; then
        echo "[boards] $name: [BOOT_OK] reached, no fault marker"
    else
        fail=1
    fi
}

# boot <name> <log> <qemu args...>: run QEMU for SECS; only a timeout is normal.
boot() {
    local name=$1 log=$2 rc=0
    shift 2
    timeout "$SECS" "$@" < /dev/null > "${log%.log}.qemu.log" 2>&1 || rc=$?
    if [ "$rc" -ne 124 ]; then
        echo "::error::$name: QEMU exited $rc before the timeout"
        cat "${log%.log}.qemu.log"
        fail=1
        return 1
    fi
    return 0
}

build() {
    local name=$1
    shift
    if ! make -j"$(nproc)" "$@" all > "boot_$name.build.log" 2>&1; then
        echo "::error::$name: build failed"
        tail -30 "boot_$name.build.log"
        fail=1
        return 1
    fi
    return 0
}

# ---- arm64 ------------------------------------------------------------------
build arm64-board5 -f build_system/Makefile.arm64 BOARD=5 &&
    echo "[boards] arm64-board5 (RK3588): built; QEMU has no RK3588 model to boot it"

if build arm64-board6 -f build_system/Makefile.arm64 BOARD=6; then
    rm -f boot_arm64-board6.log
    boot arm64-board6 boot_arm64-board6.log qemu-system-aarch64 -M xlnx-zcu102 -m 4G \
        -kernel kernel_arm64_board6.elf -display none -monitor none \
        -serial file:boot_arm64-board6.log &&
        check "arm64-board6 (Zynq UltraScale+)" boot_arm64-board6.log '[AI_OK]'
fi

if build arm64-board7 -f build_system/Makefile.arm64 BOARD=7; then
    rm -f boot_arm64-board7.log
    boot arm64-board7 boot_arm64-board7.log qemu-system-aarch64 -M xlnx-versal-virt -m 4G \
        -kernel kernel_arm64_board7.elf -display none -monitor none \
        -serial file:boot_arm64-board7.log &&
        check "arm64-board7 (Versal)" boot_arm64-board7.log '[AI_OK]'
fi

# ---- riscv ------------------------------------------------------------------
mpfs_dtb() {
    if [ -n "${ZXV_MPFS_DTB:-}" ]; then
        echo "$ZXV_MPFS_DTB"
        return 0
    fi
    local src="https://raw.githubusercontent.com/torvalds/linux/$LINUX_TAG"
    local f
    mkdir -p "$DTB_DIR/include/dt-bindings/clock" "$DTB_DIR/include/dt-bindings/gpio" \
        "$DTB_DIR/include/dt-bindings/leds" || return 1
    for f in mpfs-icicle-kit.dts mpfs-icicle-kit-common.dtsi mpfs-icicle-kit-fabric.dtsi \
        mpfs.dtsi mpfs-pinctrl.dtsi; do
        curl -sSf -o "$DTB_DIR/$f" "$src/arch/riscv/boot/dts/microchip/$f" || return 1
    done
    for f in clock/microchip,mpfs-clock.h gpio/gpio.h leds/common.h; do
        curl -sSf -o "$DTB_DIR/include/dt-bindings/$f" "$src/include/dt-bindings/$f" || return 1
    done
    cpp -nostdinc -undef -x assembler-with-cpp -I "$DTB_DIR" -I "$DTB_DIR/include" \
        "$DTB_DIR/mpfs-icicle-kit.dts" -o "$DTB_DIR/icicle.pre" || return 1
    dtc -q -I dts -O dtb -o "$DTB_DIR/icicle.dtb" "$DTB_DIR/icicle.pre" || return 1
    echo "$DTB_DIR/icicle.dtb"
}

if build riscv-board1 -f build_system/Makefile.riscv BOARD=1; then
    if ! DTB=$(mpfs_dtb); then
        echo "::error::riscv-board1: could not fetch or compile the Icicle Kit device tree ($LINUX_TAG)"
        fail=1
    else
        rm -f boot_riscv-board1.sbi.log boot_riscv-board1.log
        boot riscv-board1 boot_riscv-board1.log qemu-system-riscv64 -M microchip-icicle-kit \
            -m 2G -smp 5 -bios "$OPENSBI" -dtb "$DTB" -kernel kernel_riscv_board1.elf \
            -display none -monitor none \
            -serial file:boot_riscv-board1.sbi.log -serial file:boot_riscv-board1.log &&
            check "riscv-board1 (PolarFire SoC)" boot_riscv-board1.log
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "[boards] FAILED"
    exit 1
fi
echo "[boards] every board profile QEMU models reached [BOOT_OK]"
