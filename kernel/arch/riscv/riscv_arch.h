/* riscv_arch.h — RISC-V Architecture Abstraction
 *
 * Provides RISC-V-specific definitions for VOVINA SHAKINA.
 * SHARED BY BOTH RISC-V BUILDS: rv64 (lp64d, boot.s) and rv32 (ilp32d,
 * boot_rv32.s). Everything here must therefore be XLEN-agnostic — see the
 * counter helpers below for why that is not a formality.
 * Target: qemu-system-riscv64 | qemu-system-riscv32 -M virt
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

/* ===== Firmware handoff (published by boot.s / boot_rv32.s) ===== */

/* What the SBI firmware told us about THIS machine, captured at _start before
 * anything can overwrite a0/a1. riscv_dtb_addr is the device-tree blob: the
 * authoritative, per-machine answer to "where is the UART / PLIC / virtio", as
 * opposed to a compiled-in constant. Stored XLEN-wide so one `sw`/`sd` fits. */
extern unsigned long riscv_boot_hart;
extern unsigned long riscv_dtb_addr;

/* ===== Inline I/O (MMIO) ===== */

/* Addresses are uintptr_t, NOT uint64_t: on ilp32 a uint64_t address is silently
 * truncated by the cast to a pointer (31 -Wint-to-pointer-cast warnings before
 * this change). uintptr_t is 4 bytes on rv32 and 8 on rv64, so the ABI decides
 * the width instead of the header asserting it. */
static inline void mmio_write(uintptr_t addr, uint32_t val) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    *ptr = val;
}

static inline uint32_t mmio_read(uintptr_t addr) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    return *ptr;
}

static inline void mmio_write8(uintptr_t addr, uint8_t val) {
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
    *ptr = val;
}

static inline uint8_t mmio_read8(uintptr_t addr) {
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
    return *ptr;
}

/* 64-bit MMIO. On rv64 this is one ld/sd — a single bus transaction. rv32 has
 * no ld/sd, so it CANNOT be atomic there; the compiler would split it silently
 * and pick its own order. Make the split explicit and document the order
 * (low word first on write, high word first on read) so a caller can reason
 * about a register that latches on one half. If a device needs a genuinely
 * atomic 64-bit access, it cannot use this on rv32 — it needs its own protocol.
 * QEMU virt's CLINT mtimecmp is exactly this kind of register, which is why the
 * timer goes through SBI set_timer instead (see riscv_timer.c). */
static inline void mmio_write64(uintptr_t addr, uint64_t val) {
#if __riscv_xlen == 32
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    ptr[0] = (uint32_t)(val & 0xFFFFFFFFu);   /* low word first */
    ptr[1] = (uint32_t)(val >> 32);
#else
    volatile uint64_t *ptr = (volatile uint64_t *)addr;
    *ptr = val;
#endif
}

static inline uint64_t mmio_read64(uintptr_t addr) {
#if __riscv_xlen == 32
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    uint32_t hi, lo, hi2;
    /* Re-read the high word if it moved between the two halves. */
    do {
        hi  = ptr[1];
        lo  = ptr[0];
        hi2 = ptr[1];
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
#else
    volatile uint64_t *ptr = (volatile uint64_t *)addr;
    return *ptr;
#endif
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

/* ===== 64-bit performance counters =====
 *
 * These MUST be XLEN-aware. Writing
 *     uint64_t v; asm("rdtime %0" : "=r"(v));
 * looks portable and is a live bug on rv32: GCC allocates a REGISTER PAIR for
 * the 64-bit operand, `%0` names only the low half, and the high half keeps
 * whatever junk was in the paired register on entry. Measured in the shipped
 * rv32 image before this fix, riscv_timer_delay_us inlined get_time() twice and
 * got two DIFFERENT uninitialised high registers (a7 and a1), so
 * `while (get_time() < target)` either fell straight through or hung forever,
 * decided by garbage. rv32 has to stitch the halves with the *h counters and
 * re-read on carry; rv64 reads the whole thing in one register.
 *
 * No libgcc: the stitch is a constant shift and an or, which both ABIs lower to
 * native instructions. Nothing here can emit a libcall. */
#if __riscv_xlen == 32
#define RISCV_RD64(lo_insn, hi_insn)                                     \
    uint32_t lo, hi, hi2;                                                \
    do {                                                                 \
        __asm__ __volatile__(hi_insn " %0" : "=r"(hi));                  \
        __asm__ __volatile__(lo_insn " %0" : "=r"(lo));                  \
        __asm__ __volatile__(hi_insn " %0" : "=r"(hi2));                 \
    } while (hi != hi2);                                                 \
    return ((uint64_t)hi << 32) | lo;
#else
#define RISCV_RD64(lo_insn, hi_insn)                                     \
    unsigned long v;                                                     \
    __asm__ __volatile__(lo_insn " %0" : "=r"(v));                       \
    return (uint64_t)v;
#endif

static inline uint64_t get_time(void)    { RISCV_RD64("rdtime",    "rdtimeh")    }
static inline uint64_t get_cycle(void)   { RISCV_RD64("rdcycle",   "rdcycleh")   }
static inline uint64_t get_instret(void) { RISCV_RD64("rdinstret", "rdinstreth") }

/* ===== CSR operations =====
 *
 * All CSRs are XLEN-wide, so these take/return `unsigned long`, never uint64_t
 * — a uint64_t here is the same register-pair bug documented above.
 *
 * MACHINE-MODE ONLY, AND WE DO NOT RUN IN M-MODE. OpenSBI hands us the kernel
 * in SUPERVISOR mode, where mhartid/mtvec/mepc/mcause raise Illegal Instruction.
 * They are kept (staged building blocks for a future M-mode/bare-SBI port, and
 * the file is shared with any board that boots without an SBI), but nothing may
 * call them on the OpenSBI path — the hart id now arrives in riscv_boot_hart and
 * the trap vector is stvec. The S-mode reads below are the ones that are legal
 * here, and the trap reporter uses them. */

static inline unsigned long csr_read_mhartid_M(void) {
    unsigned long val;
    __asm__ __volatile__("csrr %0, mhartid" : "=r"(val));
    return val;
}

static inline void csr_write_mtvec_M(unsigned long val) {
    __asm__ __volatile__("csrw mtvec, %0" :: "r"(val));
}

static inline void csr_write_mepc_M(unsigned long val) {
    __asm__ __volatile__("csrw mepc, %0" :: "r"(val));
}

static inline unsigned long csr_read_mcause_M(void) {
    unsigned long val;
    __asm__ __volatile__("csrr %0, mcause" : "=r"(val));
    return val;
}

/* S-mode reads — legal under OpenSBI on both rv32 and rv64. */
static inline unsigned long csr_read_sepc(void) {
    unsigned long val;
    __asm__ __volatile__("csrr %0, sepc" : "=r"(val));
    return val;
}

static inline void csr_write_sepc(unsigned long val) {
    __asm__ __volatile__("csrw sepc, %0" :: "r"(val));
}

static inline unsigned long csr_read_stval(void) {
    unsigned long val;
    __asm__ __volatile__("csrr %0, stval" : "=r"(val));
    return val;
}

static inline unsigned long csr_read_sstatus(void) {
    unsigned long val;
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(val));
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
/* Ask the toolchain what width it actually built, do not assert one. */
#if __riscv_xlen == 32
#define ARCH_NAME "RISC-V 32-bit"
#define ARCH_XLEN 32
#else
#define ARCH_NAME "RISC-V 64-bit"
#define ARCH_XLEN 64
#endif

#endif /* RISCV_ARCH_H */
