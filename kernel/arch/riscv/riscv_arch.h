/* riscv_arch.h — RISC-V Architecture Abstraction
 *
 * Provides RISC-V-specific definitions for VOVINA SHAKINA.
 * Target: qemu-system-riscv64 -M virt
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef RISCV_ARCH_H
#define RISCV_ARCH_H

#include <stdint.h>
#include <stdbool.h>

/* ===== QEMU virt platform addresses ===== */

#define UART0_BASE          0x10000000  /* NS16550A UART */
#define CLINT_BASE          0x02000000  /* Core-local interruptor */
#define PLIC_BASE           0x0C000000  /* Platform-level interrupt controller */
#define FRAMEBUFFER_BASE    0x3C000000  /* SimpleFB if available */

/* UART NS16550A registers (byte-offset) */
#define UART_THR            0x00        /* Transmit holding register */
#define UART_RHR            0x00        /* Receive holding register */
#define UART_IER            0x01        /* Interrupt enable */
#define UART_FCR            0x02        /* FIFO control */
#define UART_LCR            0x03        /* Line control */
#define UART_LSR            0x05        /* Line status */

/* CLINT registers */
#define CLINT_MSIP(hart)    (CLINT_BASE + 0x0000 + (hart) * 4)
#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000 + (hart) * 8)
#define CLINT_MTIME         (CLINT_BASE + 0xBFF8)

/* PLIC registers */
#define PLIC_PRIORITY(irq)  (PLIC_BASE + 0x0000 + (irq) * 4)
#define PLIC_PENDING(irq)   (PLIC_BASE + 0x1000 + ((irq) / 32) * 4)
#define PLIC_ENABLE(hart, irq) (PLIC_BASE + 0x2000 + (hart) * 0x80 + ((irq) / 32) * 4)
#define PLIC_THRESHOLD(hart) (PLIC_BASE + 0x200000 + (hart) * 0x1000)
#define PLIC_CLAIM(hart)    (PLIC_BASE + 0x200004 + (hart) * 0x1000)

/* IRQ numbers */
#define IRQ_UART            10  /* UART0 interrupt */
#define IRQ_TIMER           7   /* Machine timer */

/* ===== Inline I/O (MMIO) ===== */

static inline void mmio_write(uint64_t addr, uint32_t val) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    *ptr = val;
}

static inline uint32_t mmio_read(uint64_t addr) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    return *ptr;
}

static inline void mmio_write8(uint64_t addr, uint8_t val) {
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
    *ptr = val;
}

static inline uint8_t mmio_read8(uint64_t addr) {
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
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

/* S-mode (booted by OpenSBI): use sstatus.SIE (bit 1), not mstatus.MIE. */
static inline void enable_irq(void) {
    __asm__ __volatile__("csrs sstatus, 0x2");  /* SIE bit */
}

static inline void disable_irq(void) {
    __asm__ __volatile__("csrc sstatus, 0x2");
}

static inline void halt(void) {
    __asm__ __volatile__("wfi");
}

static inline uint64_t get_time(void) {
    uint64_t val;
    __asm__ __volatile__("rdtime %0" : "=r"(val));
    return val;
}

static inline uint64_t get_cycle(void) {
    uint64_t val;
    __asm__ __volatile__("rdcycle %0" : "=r"(val));
    return val;
}

static inline uint64_t get_instret(void) {
    uint64_t val;
    __asm__ __volatile__("rdinstret %0" : "=r"(val));
    return val;
}

/* ===== CSR operations ===== */

static inline uint64_t csr_read(const char *csr) {
    uint64_t val;
    /* Simplified — actual CSR access requires inline asm per register */
    (void)csr;
    __asm__ __volatile__("csrr %0, mhartid" : "=r"(val));
    return val;
}

static inline void csr_write_mtvec(uint64_t val) {
    __asm__ __volatile__("csrw mtvec, %0" :: "r"(val));
}

static inline void csr_write_mepc(uint64_t val) {
    __asm__ __volatile__("csrw mepc, %0" :: "r"(val));
}

static inline uint64_t csr_read_mcause(void) {
    uint64_t val;
    __asm__ __volatile__("csrr %0, mcause" : "=r"(val));
    return val;
}

/* ===== Cache operations ===== */

static inline void flush_dcache_all(void) {
    __asm__ __volatile__("fence rw, rw");
}

static inline void invalidate_icache_all(void) {
    __asm__ __volatile__("fence.i");
}

/* ===== Architecture detection ===== */

#define ARCH_RISCV 1
#define ARCH_NAME "RISC-V 64-bit"

#endif /* RISCV_ARCH_H */
