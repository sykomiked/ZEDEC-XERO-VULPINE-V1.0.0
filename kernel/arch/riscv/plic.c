/* plic.c — PLIC (Platform-Level Interrupt Controller) driver for RISC-V
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "riscv_arch.h"

typedef void (*irq_handler_t)(void);
static irq_handler_t plic_handlers[256];

void plic_init(void) {
    int i;
    for (i = 0; i < 256; i++) plic_handlers[i] = 0;

    /* Set priority for all interrupts to 1 (minimum enabled) */
    for (i = 1; i < 256; i++) {
        mmio_write(PLIC_PRIORITY(i), 1);
    }

    /* Set threshold to 0 (allow all priorities) for hart 0 M-mode */
    mmio_write(PLIC_THRESHOLD(0), 0);

    /* Disable all interrupts initially */
    for (i = 0; i < 8; i++) {
        mmio_write(PLIC_ENABLE(0, i * 32), 0);
    }
}

void plic_register_handler(uint32_t irq, irq_handler_t handler) {
    if (irq < 256) plic_handlers[irq] = handler;

    /* Enable interrupt for hart 0 M-mode */
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    uint32_t current = mmio_read(PLIC_ENABLE(0, reg * 32));
    mmio_write(PLIC_ENABLE(0, reg * 32), current | bit);
}

void plic_enable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    uint32_t current = mmio_read(PLIC_ENABLE(0, reg * 32));
    mmio_write(PLIC_ENABLE(0, reg * 32), current | bit);
}

void plic_disable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    uint32_t current = mmio_read(PLIC_ENABLE(0, reg * 32));
    mmio_write(PLIC_ENABLE(0, reg * 32), current & ~bit);
}

uint32_t plic_claim(void) {
    return mmio_read(PLIC_CLAIM(0));
}

void plic_complete(uint32_t irq) {
    mmio_write(PLIC_CLAIM(0), irq);
}

void plic_handle_irq(void) {
    uint32_t irq = plic_claim();
    if (irq > 0 && irq < 256 && plic_handlers[irq]) {
        plic_handlers[irq]();
    }
    plic_complete(irq);
}
