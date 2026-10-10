/* kernel_main_arm64.c — VOVINA SHAKINA ARM64 boot driver
 *
 * Bare-metal entry point. Called by boot_arm64.s with a simple convention:
 *   x0 = boot flags / 0 for now
 *   x1 = device-tree pointer or 0
 *   x2 = boot data or 0
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include <stdint.h>
#include "m5_types.h"
#include "arch/cpu.h"
#include "freestanding.h"

/* Subsystem headers */
#include "../src/oseq/oseq_core.h"
#include "../src/rmag/rmag_core.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/telemetry/telemetry_core.h"
#include "../src/axiom_matrix/axiom_matrix_core.h"
#include "../src/crit168/crit_168_word.h"
#include "../src/surplus/surplus.h"
#include "../src/edp_risk/edp_risk.h"

/* Simple console UART for QEMU virt: non-secure PL011 @ 0x09000000 */
#define UART_BASE ((uintptr_t)0x09000000)

#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCRH  0x2C
#define PL011_CR    0x30
#define PL011_IMSC  0x38
#define PL011_ICR   0x44

#define pl011_reg(off) (*(volatile uint32_t *)(UART_BASE + (uintptr_t)(off)))

static void pl011_init(void) {
    pl011_reg(PL011_CR) = 0;
    pl011_reg(PL011_IMSC) = 0;
    pl011_reg(PL011_ICR)  = 0x7FF;
    pl011_reg(PL011_LCRH) = (3U << 5) | (1U << 4);
    pl011_reg(PL011_IBRD) = 13;
    pl011_reg(PL011_FBRD) = 1;
    pl011_reg(PL011_CR) = (1U << 0) | (1U << 8) | (1U << 9);
}

static void aarch64_uart_putc(char c) {
    while (pl011_reg(PL011_FR) & (1U << 5)) { /* wait for TXFF clear */ }
    pl011_reg(PL011_DR) = (uint32_t)(uint8_t)c;
}

static void aarch64_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') aarch64_uart_putc('\r');
        aarch64_uart_putc(*s++);
    }
}

static void itoa_local(uint64_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char tmp[20];
    int i = 0;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = 0;
}

void kernel_main(uint64_t boot_flags, void *dtb, void *boot_data) {
    (void)boot_flags;
    (void)dtb;
    (void)boot_data;

    pl011_init();
    aarch64_uart_puts("\nZEDEC pqOS — M5 Axiomatic Kernel (ARM64)\n");
    aarch64_uart_puts("=============================================\n\n");
    aarch64_uart_puts("License: Apache-2.0\n");
    aarch64_uart_puts("Author: H.M. Michael-Laurence: Curzi (c)\n\n");

    /* M5 Kernel subsystems */
    aarch64_uart_puts("[BOOT] M5 Kernel subsystems...\n");
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);
    aarch64_uart_puts("  [OK] Phase Coordinator (EXEC_DC)\n");
    rmag_init(256);
    aarch64_uart_puts("  [OK] RMAG (256 slots)\n");
    lpres_init();
    aarch64_uart_puts("  [OK] LPRES\n");
    iphase_init();
    aarch64_uart_puts("  [OK] IPHASE\n");
    choice_handoff();
    aarch64_uart_puts("  [OK] CHOICE\n");
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    aarch64_uart_puts("  [OK] OSEQ\n");

    for (int i = 0; i < 5; i++) {
        phase_coordinator_tick(&tick);
        aarch64_uart_puts("  cycle ");
        char buf[20];
        itoa_local((uint64_t)(i + 1), buf);
        aarch64_uart_puts(buf);
        aarch64_uart_puts(": omega=");
        itoa_local(tick.omega, buf);
        aarch64_uart_puts(buf);
        aarch64_uart_puts("\n");
    }

    /* M5 coverage check */
    m5_coords_t coverage;
    coverage.omega = 1;
    coverage.r = SR_FROM_INT(1);
    coverage.ell = SR_FROM_INT(2);
    coverage.phi = SR_ZERO;
    coverage.chi = 0;
    bool satisfied = edp_coverage_satisfied(&coverage);
    if (satisfied)
        aarch64_uart_puts("\n[OK] M5 coverage satisfied\n");
    else
        aarch64_uart_puts("\n[WARN] M5 coverage below floor\n");

    aarch64_uart_puts("\n[BOOT] Entering event cycle...\n");

    arch_interrupts_enable();

    uint64_t cycle = 0;
    while (1) {
        arch_halt();
        phase_coordinator_tick(&tick);
        (void)cycle;
    }
}
