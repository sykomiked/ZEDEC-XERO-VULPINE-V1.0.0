/* riscv_timer.c — CLINT-based timer driver for RISC-V
 *
 * Uses the CLINT mtime register for kernel tick.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "riscv_arch.h"

typedef void (*timer_callback_t)(void);
static timer_callback_t timer_cb = 0;
static uint64_t timer_interval = 0;
static volatile uint64_t timer_ticks = 0;

/* S-mode (OpenSBI) timer: read the `time` CSR and program the next tick via the
 * SBI set_timer call instead of poking the CLINT mtimecmp directly, which a
 * supervisor-mode kernel under OpenSBI is not permitted to do.
 *
 * The 64-bit time read now has exactly ONE implementation, in riscv_arch.h
 * (get_time), and it is XLEN-aware for the reason this file already knew: on
 * rv32 `rdtime` writes only the low half, so the halves must be stitched with
 * rdtimeh and re-read on carry. This file used to carry a private correct copy
 * while the shared header carried a broken one — that split is exactly what let
 * riscv_timer_delay_us ship with an uninitialised high word. */
static inline uint64_t rd_time(void) { return get_time(); }

#if __riscv_xlen == 32
static inline void sbi_set_timer(uint64_t next) {
    register unsigned long a0 asm("a0") = (unsigned long)(next & 0xFFFFFFFFu);
    register unsigned long a1 asm("a1") = (unsigned long)(next >> 32);
    register unsigned long a7 asm("a7") = 0;   /* SBI legacy set_timer (EID 0) */
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
}
#else
static inline void sbi_set_timer(uint64_t next) {
    register unsigned long a0 asm("a0") = (unsigned long)next;
    register unsigned long a7 asm("a7") = 0;   /* SBI legacy set_timer (EID 0) */
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}
#endif

/* Timebase of the `time` CSR, in Hz.
 * REMAINING FIXED VALUE, STATED PLAINLY: 10 MHz is the QEMU virt / SiFive CLINT
 * default, and OpenSBI confirms it on this host ("aclint-mtimer @ 10000000Hz").
 * It is NOT universal — the authoritative answer is /cpus/timebase-frequency in
 * the device tree, which is why _start now captures the DTB pointer into
 * riscv_dtb_addr. Reading it needs an FDT parser this file does not have; until
 * that lands, a board with a different timebase gets a proportionally wrong tick
 * rate (it still boots and still ticks — the rate is off, nothing faults). */
#define RISCV_TIMEBASE_HZ 10000000UL

void riscv_timer_init(void) {
    /* 100 Hz tick. */
    timer_interval = RISCV_TIMEBASE_HZ / 100;
    sbi_set_timer(rd_time() + timer_interval);
    /* Enable the SUPERVISOR timer interrupt (sie.STIE, bit 5). XLEN-width
     * operand so `csrs` gets one register on both rv32 and rv64. */
    __asm__ __volatile__("csrs sie, %0" :: "r"((unsigned long)0x20));
}

void riscv_timer_set_callback(timer_callback_t cb) {
    timer_cb = cb;
}

void riscv_timer_set_frequency(uint32_t hz) {
    if (hz == 0) return;                       /* never divide by zero here */
    timer_interval = RISCV_TIMEBASE_HZ / hz;
    sbi_set_timer(rd_time() + timer_interval);
}

uint64_t riscv_timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t riscv_timer_get_uptime_ms(void) {
    return (timer_ticks * 1000) / 100; /* 100 Hz → ms */
}

void riscv_timer_handler(void) {
    timer_ticks++;
    sbi_set_timer(rd_time() + timer_interval);   /* schedule the next tick */
    if (timer_cb) timer_cb();
}

void riscv_timer_delay_us(uint64_t us) {
    /* 10 ticks per us at the QEMU virt 10 MHz time base.
     * The 64-bit multiply lowers to mul/mulhu on rv32 and mul on rv64 — both
     * native under the M extension, so this needs no libgcc helper. */
    uint64_t target = rd_time() + (us * 10);
    while (rd_time() < target) { }
}

void riscv_timer_delay_ms(uint64_t ms) {
    riscv_timer_delay_us(ms * 1000);
}
