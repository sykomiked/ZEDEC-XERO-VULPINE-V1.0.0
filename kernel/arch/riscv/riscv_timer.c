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

void riscv_timer_init(void) {
    /* Default: 100 Hz tick (10ms interval)
     * QEMU virt CLINT mtime runs at 10MHz */
    timer_interval = 100000; /* 10MHz / 100 = 100000 */
    
    /* Set mtimecmp */
    uint64_t current = mmio_read64(CLINT_MTIME);
    mmio_write64(CLINT_MTIMECMP(0), current + timer_interval);
    
    /* Enable machine timer interrupt */
    uint64_t mtie = 0x80;
    __asm__ __volatile__("csrs mie, %0" :: "r"(mtie));  /* MTIE bit */
}

void riscv_timer_set_callback(timer_callback_t cb) {
    timer_cb = cb;
}

void riscv_timer_set_frequency(uint32_t hz) {
    timer_interval = 10000000 / hz; /* 10MHz / hz */
    uint64_t current = mmio_read64(CLINT_MTIME);
    mmio_write64(CLINT_MTIMECMP(0), current + timer_interval);
}

uint64_t riscv_timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t riscv_timer_get_uptime_ms(void) {
    return (timer_ticks * 1000) / 100; /* 100 Hz → ms */
}

void riscv_timer_handler(void) {
    timer_ticks++;
    
    /* Reload mtimecmp */
    uint64_t current = mmio_read64(CLINT_MTIME);
    mmio_write64(CLINT_MTIMECMP(0), current + timer_interval);
    
    if (timer_cb) timer_cb();
}

void riscv_timer_delay_us(uint64_t us) {
    uint64_t target = get_time() + (us * 10); /* 10 ticks per us at 10MHz */
    while (get_time() < target) { }
}

void riscv_timer_delay_ms(uint64_t ms) {
    riscv_timer_delay_us(ms * 1000);
}
