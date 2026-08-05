/* arch.h — Architecture abstraction layer for ARM (ARMv7-A)
 * Provides inline equivalents of x86 operations used in shared kernel code.
 * Target: QEMU virt machine (Cortex-A15)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARCH_ARM_H
#define ARCH_ARM_H

#include <stdint.h>

/* ---- Interrupt enable/disable (replaces x86 sti/cli) ---- */
static inline void arch_enable_interrupts(void) {
    __asm__ __volatile__("cpsie i" : : : "memory");
}

static inline void arch_disable_interrupts(void) {
    __asm__ __volatile__("cpsid i" : : : "memory");
}

/* ---- Wait for interrupt (replaces x86 hlt) ---- */
static inline void arch_halt(void) {
    __asm__ __volatile__("wfi" : : : "memory");
}

/* ---- Memory barrier ---- */
static inline void arch_dsb(void) {
    __asm__ __volatile__("dsb" : : : "memory");
}

static inline void arch_isb(void) {
    __asm__ __volatile__("isb" : : : "memory");
}

/* ---- MMIO I/O (replaces x86 outb/inb for port I/O) ---- */
static inline void mmio_write(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write8(uint32_t addr, uint8_t val) {
    *(volatile uint8_t *)addr = val;
}

static inline uint8_t mmio_read8(uint32_t addr) {
    return *(volatile uint8_t *)addr;
}

/* ---- QEMU virt machine memory map ---- */
#define UART0_BASE      0x09000000  /* PL011 UART */
#define GIC_DIST_BASE   0x08000000  /* GIC distributor */
#define GIC_CPU_BASE    0x08010000  /* GIC CPU interface */
#define GTIMER_BASE     0x09040000  /* Generic timer */
#define VIRTIO_BASE     0x0A000000  /* VirtIO devices */
#define DRAM_BASE       0x40000000  /* RAM starts here */

/* ---- IRQ numbers for QEMU virt ---- */
#define IRQ_TIMER       27          /* Generic timer virtual IRQ */
#define IRQ_UART0       33          /* PL011 UART */
#define IRQ_VIRTIO      64          /* VirtIO transport */

#endif /* ARCH_ARM_H */
