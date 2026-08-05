/* arm64_compat.h — ARM64 Compatibility Layer
 *
 * Provides x86-compatible function signatures that map to ARM64
 * implementations, allowing shared kernel code to compile on both.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ARM64_COMPAT_H
#define ARM64_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include "arm64_arch.h"

/* ===== Port I/O compatibility (x86 → ARM64 MMIO) ===== */
/* On ARM64, "port I/O" maps to MMIO addresses.
 * For the framebuffer and UART, we use direct MMIO. */

static inline void outb(uint16_t port, uint8_t val) {
    /* On ARM64, port I/O is not available — use MMIO instead.
     * This is a no-op stub; actual I/O goes through mmio_write. */
    (void)port;
    (void)val;
}

static inline uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}

static inline void outw(uint16_t port, uint16_t val) {
    (void)port;
    (void)val;
}

static inline uint16_t inw(uint16_t port) {
    (void)port;
    return 0;
}

static inline void outl(uint16_t port, uint32_t val) {
    (void)port;
    (void)val;
}

static inline uint32_t inl(uint16_t port) {
    (void)port;
    return 0;
}

/* ===== Interrupt control compatibility ===== */

static inline void cli(void) {
    disable_irq();
}

static inline void sti(void) {
    enable_irq();
}

static inline void hlt(void) {
    halt();
}

/* ===== GDT/IDT/PIC stubs (not applicable on ARM64) ===== */
/* These are no-ops on ARM64; GIC handles interrupts. */

#define idt_register_handler(irq, handler) gic_register_handler(irq, handler)
#define pic_init()                        ((void)0)
#define pic_mask(irq)                     gic_disable_irq(irq)
#define pic_unmask(irq)                   gic_enable_irq(irq)

/* ===== Timer compatibility ===== */
/* x86 timer_interrupt_handler → ARM64 timer handler */

/* ===== Framebuffer compatibility ===== */
/* On ARM64, framebuffer is at a different address or uses UART output.
 * The kernel_main will detect platform and use appropriate output. */

/* ===== Architecture detection macro ===== */
#ifndef __x86_64__
#ifndef __i386__
#define TARGET_ARM64 1
#endif
#endif

#endif /* ARM64_COMPAT_H */
