/* gicv3.c — GICv3 Interrupt Controller driver for ARM64
 *
 * Hardware-as-code: implements the GICv3 as a virtual device
 * with distributor and redistributor register maps.
 *
 * Distributor/redistributor BASE addresses come from board_profile.h
 * (per-board data), not compile-time constants -- GICv3 itself (the
 * register layout/protocol this file speaks) is architecturally
 * identical across QEMU virt and every MediaTek Tank board; only the
 * physical base addresses differ. See board_profile.h for exactly
 * which of those addresses are verified vs. pending real hardware.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm64_arch.h"
#include "board_profile.h"

typedef void (*irq_handler_t)(void);

static irq_handler_t irq_handlers[256];

void gic_enable_irq(uint32_t irq);

void gic_init(void) {
    const board_profile_t *bp = board_get_profile();
    uint64_t gicd = bp->gicd_base;
    uint64_t gicr = bp->gicr_base;

    int i;
    for (i = 0; i < 256; i++) irq_handlers[i] = 0;

    /* Enable GIC distributor. The previous code wrote just `1`
     * (EnableGrp0 only) -- every interrupt this driver configures is
     * Group 1 (see IGROUPR writes below), so EnableGrp1 must also be
     * set or the distributor drops them before they ever reach a CPU
     * interface, regardless of their individual enable/priority bits.
     * ARE is set too since GICv3's native SPI affinity targeting
     * depends on it (this driver has no GICD_ITARGETSR fallback). */
    mmio_write(gicd + GICD_CTLR,
               GICD_CTLR_ARE | GICD_CTLR_ENABLE_G1 | GICD_CTLR_ENABLE_G0);

    /* Configure all SPIs (Shared Peripheral Interrupts) as group 0 (secure)
     * or group 1 (non-secure). We use group 1. */
    for (i = 32; i < 256; i += 32) {
        mmio_write(gicd + GICD_IGROUPR(i / 32), 0xFFFFFFFF);
    }

    /* Set priority for all interrupts to 0xA0 (mid priority).
     * GICD_IPRIORITYR is a byte-per-interrupt register array; writing
     * it one byte at a time via a 32-bit mmio_write() hits unaligned
     * addresses for every n not a multiple of 4 (e.g. n=1 -> 0x...401)
     * and traps as an alignment fault. Pack 4 interrupts per word and
     * write on 4-byte-aligned boundaries instead. */
    for (i = 0; i < 256; i += 4) {
        mmio_write(gicd + GICD_IPRIORITYR(i), 0xA0A0A0A0);
    }

    /* Wake up the redistributor. GICR_CTLR has no self-setting "ready"
     * bit -- the architected wake sequence is: clear GICR_WAKER's
     * ProcessorSleep (bit 1), then poll ChildrenAsleep (bit 2) until
     * the redistributor clears it. The previous code polled the wrong
     * register/bit entirely, spinning forever. */
    uint32_t waker = mmio_read(gicr + GICR_WAKER);
    waker &= ~(1u << 1);
    mmio_write(gicr + GICR_WAKER, waker);
    while (mmio_read(gicr + GICR_WAKER) & (1u << 2)) { }

    /* PPIs/SGIs (IRQ 0-31 -- this includes IRQ_TIMER=30, the generic
     * timer's PPI) live in the redistributor's SGI_base frame, not the
     * distributor. Without this, GICD_IGROUPR/ISENABLER above (which
     * only ever touched SPIs, i >= 32) leave every PPI in its
     * power-on-reset Group 0 state with its enable bit clear, so the
     * timer interrupt can never reach the Group-1 IAR/EOI path used by
     * gic_handle_irq() -- the CPU wakes from WFI never, and the whole
     * event-cycle loop hangs silently forever after boot. Route PPIs
     * into Group 1 and give them the same mid priority as SPIs here. */
    uint64_t sgi_base = gicr + GIC_REDIST_SGI_OFFSET;
    mmio_write(sgi_base + GICR_IGROUPR0, 0xFFFFFFFF);
    for (i = 0; i < 32; i += 4) {
        mmio_write(sgi_base + GICR_IPRIORITYR0 + i, 0xA0A0A0A0);
    }

    /* Set priority mask to allow all priorities */
    __asm__ __volatile__("msr S3_0_C4_C6_0, %0" :: "r"(0xFF));

    /* Enable group 1 interrupts at CPU interface (ICC_IGRPEN1_EL1 =
     * S3_0_C12_C12_7, a read-write register). The previous code used
     * op2=0, which is ICC_IAR1_EL1 -- a read-only register -- and
     * trapped as an undefined instruction the moment it was written. */
    uint64_t ctlr;
    __asm__ __volatile__("mrs %0, S3_0_C12_C12_7" : "=r"(ctlr));
    ctlr |= 1; /* Enable GIC */
    __asm__ __volatile__("msr S3_0_C12_C12_7, %0" :: "r"(ctlr));
}

void gic_register_handler(uint32_t irq, irq_handler_t handler) {
    if (irq < 256) irq_handlers[irq] = handler;
    gic_enable_irq(irq);
}

void gic_enable_irq(uint32_t irq) {
    const board_profile_t *bp = board_get_profile();
    if (irq < 32) {
        /* PPI/SGI: per-CPU enable lives in the redistributor's
         * SGI_base frame (see GIC_REDIST_SGI_OFFSET), not the
         * distributor -- GICD_ISENABLER only ever reaches SPIs. */
        mmio_write(bp->gicr_base + GIC_REDIST_SGI_OFFSET + GICR_ISENABLER0,
                   1u << irq);
        return;
    }
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    mmio_write(bp->gicd_base + GICD_ISENABLER(reg), bit);
}

void gic_disable_irq(uint32_t irq) {
    const board_profile_t *bp = board_get_profile();
    if (irq < 32) {
        mmio_write(bp->gicr_base + GIC_REDIST_SGI_OFFSET + GICR_ICENABLER0,
                   1u << irq);
        return;
    }
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    mmio_write(bp->gicd_base + GICD_ICENABLER(reg), bit);
}

void gic_handle_irq(void) {
    uint64_t iar;
    /* ICC_IAR1_EL1 (S3_0_C12_C12_0) -- Group 1 ack, matching how SPIs
     * were configured in gic_init(). The previous code read
     * S3_0_C12_C8_0 (ICC_IAR0_EL1), the Group 0 register, which never
     * reflects Group 1 interrupts. */
    __asm__ __volatile__("mrs %0, S3_0_C12_C12_0" : "=r"(iar));

    uint32_t irq = (uint32_t)(iar & 0x3FF);

    if (irq < 1023 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    /* End of interrupt: ICC_EOIR1_EL1 (S3_0_C12_C12_1). The previous
     * code wrote S3_0_C12_C9_0 (ICC_AP1R0_EL1), an unrelated register. */
    __asm__ __volatile__("msr S3_0_C12_C12_1, %0" :: "r"(iar));
}

void gic_eoi(uint32_t irq) {
    __asm__ __volatile__("msr S3_0_C12_C12_1, %0" :: "r"((uint64_t)irq));
}
