/* arm32_arch.h — ARM32 (AArch32) Architecture Header for ZEDEC XERO pqOS
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC
 */
#ifndef ARM32_ARCH_H
#define ARM32_ARCH_H

#include <stdint.h>

/* ===== MMIO base addresses (versatilepb default) ===== */
#define ARM32_MMIO_BASE         0x10100000
#define ARM32_UART0_BASE        (ARM32_MMIO_BASE + 0x1000)
#define ARM32_UART0_DR          (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x000))
#define ARM32_UART0_FR          (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x018))
#define ARM32_UART0_IBRD        (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x024))
#define ARM32_UART0_FBRD        (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x028))
#define ARM32_UART0_LCRH        (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x02C))
#define ARM32_UART0_CR          (*(volatile uint32_t *)(ARM32_UART0_BASE + 0x030))

/* GIC (Cortex-A9 MPCore) */
#define ARM32_GIC_DIST_BASE     0x1E001000
#define ARM32_GIC_CPU_BASE      0x1E000100

/* Timer (private timer) */
#define ARM32_TIMER_BASE        0x1E000600
#define ARM32_TIMER_LOAD        (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x00))
#define ARM32_TIMER_VALUE       (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x04))
#define ARM32_TIMER_CTRL        (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x08))
#define ARM32_TIMER_INTCLR      (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x0C))
#define ARM32_TIMER_RIS         (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x10))
#define ARM32_TIMER_MIS         (*(volatile uint32_t *)(ARM32_TIMER_BASE + 0x14))

/* ===== Inline I/O ===== */
static inline void arm32_mmio_write(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t arm32_mmio_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

/* ===== CPU control ===== */
static inline void arm32_enable_irq(void) {
    __asm__ volatile ("cpsie i" ::: "memory");
}
static inline void arm32_disable_irq(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
}
static inline void arm32_enable_fiq(void) {
    __asm__ volatile ("cpsie f" ::: "memory");
}
static inline void arm32_disable_fiq(void) {
    __asm__ volatile ("cpsid f" ::: "memory");
}

/* ===== Cache operations ===== */
static inline void arm32_dsb(void) {
    __asm__ volatile ("dsb" ::: "memory");
}
static inline void arm32_isb(void) {
    __asm__ volatile ("isb" ::: "memory");
}
static inline void arm32_dmb(void) {
    __asm__ volatile ("dmb" ::: "memory");
}

static inline void arm32_flush_icache(void) {
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r"(0) : "memory");
}
static inline void arm32_flush_dcache(void) {
    __asm__ volatile ("mcr p15, 0, %0, c7, c6, 0" :: "r"(0) : "memory");
}
static inline void arm32_flush_tlb(void) {
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory");
}

/* ===== UART ===== */
static inline void arm32_uart_putc(char c) {
    while (ARM32_UART0_FR & (1 << 5)) { } /* Wait if TX FIFO full */
    ARM32_UART0_DR = (uint32_t)c;
}
static inline char arm32_uart_getc(void) {
    while (ARM32_UART0_FR & (1 << 4)) { } /* Wait if RX FIFO empty */
    return (char)(ARM32_UART0_DR & 0xFF);
}

/* ===== Architecture detection ===== */
#define ARCH_NAME "ARM32 (AArch32)"
#define ARCH_BITS 32
#define ARCH_WORD_SIZE 4

#endif /* ARM32_ARCH_H */
