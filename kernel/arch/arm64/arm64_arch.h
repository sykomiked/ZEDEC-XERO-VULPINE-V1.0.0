/* arm64_arch.h — ARM64 Architecture Abstraction
 *
 * Provides ARM64-specific definitions and inline functions
 * replacing x86-specific code (GDT, IDT, PIC, port I/O).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ARM64_ARCH_H
#define ARM64_ARCH_H

#include <stdint.h>
#include <stdbool.h>

/* ===== QEMU virt platform addresses ===== */

#define UART0_BASE          0x09000000  /* PL011 UART */
/* REFERENCE VALUES ONLY -- the QEMU virt map, kept as documentation of
 * where these blocks sit on the one machine we boot-test daily. No ARM64
 * C code reads them (verified: zero consumers); the live addresses come
 * from board_profile.h, and which of them even exist is decided at
 * runtime by the GICD_PIDR2 probe in gicv3.c (a GICv2 machine has a
 * GICC and NO redistributor; a GICv3 machine is the exact opposite). */
#define GIC_DIST_BASE       0x08000000  /* Distributor (same base on v2 and v3) */
#define GIC_REDIST_BASE     0x080A0000  /* Redistributor -- GICv3 ONLY, unmapped on v2 */
#define GIC_CPU_BASE        0x08010000  /* GICC MMIO CPU interface -- GICv2 ONLY, unmapped on v3 */
#define TIMER_BASE          0x09050000  /* Generic timer (via system regs) */
#define FRAMEBUFFER_BASE    0x3C000000  /* SimpleFB if available */

/* UART PL011 registers */
#define UART_DR             0x00        /* Data register */
#define UART_FR             0x18        /* Flag register */
#define UART_IBRD           0x24
#define UART_FBRD           0x28
#define UART_LCRH           0x2C
#define UART_CR             0x30

/* ===== GIC registers =====
 *
 * The DISTRIBUTOR block below (CTLR/TYPER/ISENABLER/ICENABLER/
 * IPRIORITYR/ITARGETSR/ICFGR) is common to GICv2 and GICv3 -- same
 * offsets, same meaning. What actually differs between the two, and
 * therefore what gicv3.c branches on at RUNTIME after probing
 * GICD_PIDR2, is:
 *   (a) frame size -- v1/v2 distributors are 4KB with the ID block at
 *       0x0FD0-0x0FFC; v3+ widened the frame to 64KB and moved the ID
 *       block to 0xFFD0-0xFFFC;
 *   (b) SPI targeting -- v2 uses GICD_ITARGETSR, v3 uses affinity
 *       routing gated by GICD_CTLR.ARE (which is RES0 on a v2);
 *   (c) PPI/SGI state -- v2 keeps it in the (per-CPU banked)
 *       distributor, v3 moved it to a per-CPU redistributor;
 *   (d) the CPU interface -- v2 is MMIO (GICC_* below), v3 is CPU
 *       system registers (ICC_*_EL1, written as inline asm in gicv3.c).
 */
#define GICD_CTLR           0x000
/* GICD_CTLR bits when GICD_CTLR.DS=1 (single security state -- the
 * normal case for a non-secure-only boot with no EL3/TrustZone
 * firmware, as used here). Bit0/1 gate Group 0 / Group 1 interrupts
 * at the distributor: even an interrupt with IGROUPR=1 (Group 1),
 * ISENABLER=1, and a valid priority is still dropped at the
 * distributor if EnableGrp1 is clear. ARE (Affinity Routing Enable)
 * is required for GICv3-native affinity-based SPI targeting. */
#define GICD_CTLR_ENABLE_G0 (1u << 0)
#define GICD_CTLR_ENABLE_G1 (1u << 1)
#define GICD_CTLR_ARE       (1u << 4)
#define GICD_TYPER          0x004
#define GICD_ISENABLER(n)   (0x100 + (n) * 4)
#define GICD_ICENABLER(n)   (0x180 + (n) * 4)
#define GICD_IPRIORITYR(n)  (0x400 + (n))
#define GICD_ITARGETSR(n)   (0x800 + (n))   /* byte per INTID; RO for INTIDs 0-31 */
#define GICD_IGROUPR(n)     (0x080 + (n) * 4)
#define GICD_ICFGR(n)       (0xC00 + (n) * 4)   /* 2 bits per INTID -> 16 INTIDs per word */

/* GICD_TYPER.ITLinesNumber (bits [4:0]): the number of implemented
 * INTIDs is (ITLinesNumber + 1) * 32, capped by the architecture at
 * 1020 usable INTIDs (1020-1023 are special). Deriving the SPI count
 * this way is what replaces the old hardcoded "< 256" loop bounds --
 * ask the hardware how many lines it has, do not assume. */
#define GICD_TYPER_ITLINES(v)   (((v) & 0x1F) + 1)
#define GIC_MAX_INTIDS          1020

/* GICD_PIDR2 architecture revision lives in bits [7:4]: 1 = GICv1,
 * 2 = GICv2, 3 = GICv3, 4 = GICv4. There are TWO offsets and the order
 * they are read in is load-bearing:
 *   - 0x0FE8 is inside the 4KB frame every GICv1/v2 implements, and is
 *     RES0 (reads 0x00000000) inside a v3 64KB frame. It therefore
 *     DECODES ON BOTH layouts and must be read FIRST.
 *   - 0xFFE8 only exists on v3+. Reading it on a GICv2 is a synchronous
 *     external abort -- the precise fault this probe exists to remove.
 * Measured, GICD_BASE 0x08000000 on both machines (QEMU 6.2.0 on the
 * build box; the same values were seen on QEMU 11.0.2 during the
 * investigation, so this is not a QEMU-version artefact):
 *   -M virt               -> 0x0FE8 = 0x0000002B (rev 2), 0xFFE8 aborts
 *   -M virt,gic-version=3 -> 0x0FE8 = 0x00000000 (RES0), 0xFFE8 = 0x0000003B (rev 3)
 * and the two machines report DIFFERENT line counts from the same
 * binary (GICD_TYPER: 288 INTIDs on v2, 256 on v3), which is why the
 * configuration loops below are bounded by GICD_TYPER and not by a
 * constant. */
#define GICD_PIDR2_V2       0x0FE8
#define GICD_PIDR2_V3       0xFFE8
#define GICD_PIDR2_ARCH(v)  (((v) >> 4) & 0xF)

/* Probed GIC architecture version, as returned by gic_get_version().
 * UNKNOWN is not a code to shrug at: in that state the driver touches
 * no GIC register at all rather than guess a register map. */
#define GIC_VERSION_UNKNOWN 0
#define GIC_VERSION_V2      2
#define GIC_VERSION_V3      3

/* ID_AA64PFR0_EL1.GIC, bits [27:24]: 0 = no system-register CPU
 * interface (so ICC_*_EL1 are UNDEFINED and only a GICv2-style MMIO
 * CPU interface can be driven), 1 = GICv3/v4.0 sysreg interface,
 * 3 = GICv4.1. A system-register read can never external-abort, so
 * this is free corroborating evidence for the probe. */
#define ID_AA64PFR0_GIC(v)  (((v) >> 24) & 0xF)

#define GICR_CTLR           0x000
#define GICR_WAKER          0x014
#define GICR_TYPER          0x008
/* GICR_TYPER: bit 1 = VLPIS (GICv4 virtual LPIs -> the per-CPU frame is
 * twice as large), bit 4 = Last (this is the final redistributor in the
 * region -- the architected stop condition for the discovery walk),
 * bits [63:32] = Affinity_Value, matched against MPIDR_EL1[31:0] to find
 * THIS CPU's frame instead of assuming CPU 0 is at gicr_base. */
#define GICR_TYPER_VLPIS        (1u << 1)
#define GICR_TYPER_LAST         (1u << 4)
#define GIC_REDIST_STRIDE       0x20000   /* RD_base + SGI_base (GICv3) */
#define GIC_REDIST_STRIDE_VLPIS 0x40000   /* + VLPI_base + reserved (GICv4) */

/* GICv3 redistributors are two adjacent 64KB frames: RD_base (control,
 * offset 0) and SGI_base (PPI/SGI config, offset +0x10000). GICR_CTLR/
 * WAKER/TYPER above live in RD_base; IGROUPR0/ISENABLER0/ICENABLER0/
 * IPRIORITYR0 below are SGI_base-relative offsets and MUST be added to
 * (GIC_REDIST_BASE + GIC_REDIST_SGI_OFFSET), never GIC_REDIST_BASE
 * directly -- PPIs/SGIs (IRQ 0-31, including the generic timer PPI 30)
 * are per-CPU and are NOT covered by the distributor's GICD_IGROUPR/
 * ISENABLER/IPRIORITYR, which only affect SPIs (IRQ >= 32). */
#define GIC_REDIST_SGI_OFFSET 0x10000
#define GICR_IGROUPR0       0x080
#define GICR_ISENABLER0     0x100
#define GICR_ICENABLER0     0x180
#define GICR_IPRIORITYR0    0x400

/* GICv2 MMIO CPU interface (GICC_*), reached at board_profile_t.gicc_base.
 * These replace the ICC_PMR/ICC_IAR/ICC_EOIR macros that used to sit here
 * with offsets 0xFF0/0xFF8/0xFF4: those were neither the GICv3
 * system-register encodings (S3_0_C4_C6_0 / S3_0_C12_C12_0 /
 * S3_0_C12_C12_1, emitted as inline asm in gicv3.c) nor the correct GICC
 * MMIO offsets, and they had zero consumers -- a live trap for anyone
 * wiring up the v2 path from this header. */
#define GICC_CTLR            0x000
#define GICC_CTLR_ENABLE     (1u << 0)  /* on a GICv2 with the security extensions
                                          * absent (the QEMU virt case) bit 0 is
                                          * simply "Enable" for the one group */
#define GICC_PMR             0x004      /* priority mask: only priorities NUMERICALLY
                                          * LOWER than this value are signalled */
#define GICC_BPR             0x008
#define GICC_IAR             0x00C      /* acknowledge; full word must be echoed to EOIR */
#define GICC_EOIR            0x010
#define GICC_IIDR            0x0FC      /* bits [19:16] = architecture version (cross-check) */
#define GICC_IIDR_ARCH(v)    (((v) >> 16) & 0xF)

#define GIC_INTID_MASK       0x3FF      /* GICC_IAR / ICC_IAR1_EL1 INTID field */
#define GIC_INTID_SPURIOUS   1023       /* "no pending interrupt" -- must NOT be EOI'd */

/* IRQ numbers for QEMU virt */
#define IRQ_TIMER           30  /* Generic timer physical timer */
#define IRQ_UART            33  /* UART0 */
#define IRQ_KEYBOARD        34  /* (not used on ARM) */

/* ===== Inline I/O (MMIO for ARM) ===== */

static inline void mmio_write(uint64_t addr, uint32_t val) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    *ptr = val;
}

static inline uint32_t mmio_read(uint64_t addr) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    return *ptr;
}

static inline void mmio_write64(uint64_t addr, uint64_t val) {
    volatile uint64_t *ptr = (volatile uint64_t *)addr;
    *ptr = val;
}

static inline uint64_t mmio_read64(uint64_t addr) {
    volatile uint64_t *ptr = (volatile uint64_t *)addr;
    return *ptr;
}

/* ===== CPU control ===== */

static inline void enable_irq(void) {
    __asm__ __volatile__("msr daifclr, #2");
}

static inline void disable_irq(void) {
    __asm__ __volatile__("msr daifset, #2");
}

static inline void enable_fiq(void) {
    __asm__ __volatile__("msr daifclr, #1");
}

static inline void halt(void) {
    __asm__ __volatile__("wfi");
}

static inline uint64_t get_current_el(void) {
    uint64_t el;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(el));
    return el >> 2;
}

static inline uint64_t get_cntfrq(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/* Timer delay helper (defined in arm64_timer.c) */
void arm64_timer_delay_ms(uint64_t ms);

static inline uint64_t get_cntpct(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline void set_cntp_tval(uint64_t val) {
    __asm__ __volatile__("msr cntp_tval_el0, %0" :: "r"(val));
}

static inline void enable_cntp(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(val));
    val |= 1;              /* ENABLE */
    val &= ~(uint64_t)2;   /* clear IMASK -- must be 0 or the timer's own
                             * local mask suppresses CNTPNSIRQ regardless
                             * of how the GIC side is configured */
    __asm__ __volatile__("msr cntp_ctl_el0, %0" :: "r"(val));
}

/* ===== Cache operations ===== */

static inline void flush_dcache_all(void) {
    __asm__ __volatile__("dsb sy");
}

static inline void invalidate_icache_all(void) {
    __asm__ __volatile__("ic iallu");
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");
}

/* ===== TLB operations ===== */

static inline void tlb_invalidate_all(void) {
    /* ALLE1IS is EL2+-only and traps as undefined at EL1; VMALLE1IS is
     * the correct EL1-invocable op (current VMID, EL1&0 regime). */
    __asm__ __volatile__("tlbi vmalle1is");
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");
}

/* ===== Exception level transitions ===== */

static inline uint64_t get_vbar_el1(void) {
    uint64_t val;
    __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(val));
    return val;
}

static inline void set_vbar_el1(uint64_t val) {
    __asm__ __volatile__("msr vbar_el1, %0" :: "r"(val));
}

/* ===== Architecture detection ===== */

#define ARCH_ARM64 1
#define ARCH_NAME "AArch64 (ARM64)"

#endif /* ARM64_ARCH_H */
