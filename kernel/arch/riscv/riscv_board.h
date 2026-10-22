/* riscv_board.h — RISC-V board profiles (same discipline as
 * kernel/arch/arm64/board_profile.h: a board is DATA, one boot path).
 *
 * SHARED BY BOTH RISC-V BUILDS (rv64 and rv32). Selected at build time with
 * -DRV_TARGET_BOARD=<id> (build_system/Makefile.riscv BOARD=<id>); the
 * default is QEMU virt, the board CI has always booted.
 *
 * What a profile carries is what differs between RISC-V boards that all run
 * the kernel in S-mode under OpenSBI with DRAM at 0x80000000:
 *   - the console UART (base, register stride, whether the firmware has
 *     already set its baud rate),
 *   - the timebase of the `time` CSR (OpenSBI prints it: "aclint-mtimer @"),
 *   - the PLIC's S-mode context for a hart. On QEMU virt every hart has an
 *     M and an S context (S = 2 * hart + 1). On PolarFire SoC hart 0 is the
 *     E51 monitor core with an M context only, so U54 hart h has M = 2h - 1
 *     and S = 2h.
 *   - whether QEMU's fw_cfg (and so the ramfb scanout) is there. On a board
 *     without it the address is unmapped and a probe is an access fault.
 *
 * Every address below is cited from the mainline device tree of the same
 * silicon and was booted in QEMU's model of the board
 * (build_system/boards_boot_check.sh). No RISC-V profile has run on a
 * physical board yet.
 *
 * SpacemiT K3 (the Courier RISC-V board) has no profile: its device tree is
 * not public as of October 2026, and the K1's addresses (arch/riscv/boot/dts/
 * spacemit/k1.dtsi) are a different chip. It gets a profile when a K3 tree
 * is published.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RISCV_BOARD_H
#define RISCV_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define RV_BOARD_QEMU_VIRT 0 /* QEMU -M virt */
#define RV_BOARD_MPFS      1 /* Microchip PolarFire SoC (Icicle Kit): every ZXV arbiter */
#define RV_BOARD_MAX       2

#ifndef RV_TARGET_BOARD
#    define RV_TARGET_BOARD RV_BOARD_QEMU_VIRT
#endif

typedef struct {
    const char *board_name;
    const char *source; /* where the addresses come from */
    uintptr_t uart_base;
    uint32_t uart_reg_shift;   /* register stride = 1 << shift; shift 2 = 32-bit accesses */
    bool uart_keep_baud;       /* the boot firmware already set the line rate: leave the divisor */
    uint32_t timebase_hz;      /* `time` CSR frequency */
    uint32_t plic_hart0_has_s; /* 1: every hart has M + S contexts; 0: hart 0 is M-only (E51) */
    bool has_fw_cfg; /* QEMU fw_cfg at RAMFB_FWCFG_BASE (ramfb scanout); real boards: no */
} rv_board_t;

static inline const rv_board_t *rv_board(void)
{
    static const rv_board_t boards[RV_BOARD_MAX] = {
        [RV_BOARD_QEMU_VIRT] =
            {
                .board_name = "QEMU virt",
                .source = "qemu hw/riscv/virt.c: ns16550a @0x10000000, PLIC @0x0c000000, "
                          "aclint-mtimer @ 10 MHz (OpenSBI banner)",
                .uart_base = 0x10000000,
                .uart_reg_shift = 0,
                .uart_keep_baud = false,
                .timebase_hz = 10000000,
                .plic_hart0_has_s = 1,
                .has_fw_cfg = true,
            },
        [RV_BOARD_MPFS] =
            {
                .board_name = "Microchip PolarFire SoC Icicle Kit",
                .source = "linux arch/riscv/boot/dts/microchip/mpfs.dtsi: mmuart1 @0x20100000 "
                          "ns16550a reg-shift 2 reg-io-width 4 (stdout-path serial1 in "
                          "mpfs-icicle-kit.dts), PLIC @0x0c000000, timebase-frequency 1000000",
                .uart_base = 0x20100000,
                .uart_reg_shift = 2,
                .uart_keep_baud = true,
                .timebase_hz = 1000000,
                .plic_hart0_has_s = 0,
                .has_fw_cfg = false,
            },
    };
    return &boards[RV_TARGET_BOARD];
}

/* The PLIC context that delivers S-mode external interrupts to `hart`. */
static inline uint32_t rv_plic_s_context(const rv_board_t *b, uint32_t hart)
{
    return b->plic_hart0_has_s ? 2u * hart + 1u : 2u * hart;
}

#endif /* RISCV_BOARD_H */
