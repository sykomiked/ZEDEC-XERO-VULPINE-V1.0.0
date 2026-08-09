/* riscv_timer.c — CLINT-based timer driver for RISC-V
 *
 * Uses the CLINT mtime register for kernel tick.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "riscv_arch.h"

typedef void (*timer_callback_t)(void);
static timer_callback_t timer_cb = 0;
static uint64_t timer_interval = 0;
static volatile uint64_t timer_ticks = 0;

/* S-mode (OpenSBI) timer: read the `time` CSR (rdtime) and program the next tick
 * via the SBI set_timer call instead of poking the CLINT mtimecmp directly (which
 * a supervisor-mode kernel under OpenSBI is not permitted to do). */
static inline uint64_t rd_time(void) {
    uint64_t t; __asm__ __volatile__("rdtime %0" : "=r"(t)); return t;
}
static inline void sbi_set_timer(uint64_t next) {
    register uint64_t a0 asm("a0") = next;
    register uint64_t a7 asm("a7") = 0;   /* SBI legacy set_timer (EID 0) */
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

void riscv_timer_init(void) {
    /* 100 Hz tick; the QEMU virt time CSR runs at 10 MHz. */
    timer_interval = 100000;
    sbi_set_timer(rd_time() + timer_interval);
    /* Enable the SUPERVISOR timer interrupt (sie.STIE, bit 5). */
    __asm__ __volatile__("csrs sie, %0" :: "r"((uint64_t)0x20));
}

void riscv_timer_set_callback(timer_callback_t cb) {
    timer_cb = cb;
}

void riscv_timer_set_frequency(uint32_t hz) {
    timer_interval = 10000000 / hz; /* 10MHz / hz */
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
    uint64_t target = get_time() + (us * 10); /* 10 ticks per us at 10MHz */
    while (get_time() < target) { }
}

void riscv_timer_delay_ms(uint64_t ms) {
    riscv_timer_delay_us(ms * 1000);
}
