/* timer_arm.c — ARM Generic Timer driver for QEMU virt machine
 * Replaces x86 PIT (8253/8254) timer.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "timer_arm.h"
#include "arch.h"
#include "gic.h"

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_hz = 100;
static timer_callback_t callback = 0;

/* Read the ARM generic timer counter (CNTVT_C) */
static inline uint64_t read_cntvct(void) {
    uint64_t val;
    __asm__ __volatile__("mrrc p15, 1, %Q0, %R0, c14" : "=r"(val));
    return val;
}

/* Read the virtual timer control register */
static inline uint32_t read_cntv_ctl(void) {
    uint32_t val;
    __asm__ __volatile__("mrc p15, 0, %0, c14, c3, 1" : "=r"(val));
    return val;
}

/* Write the virtual timer control register */
static inline void write_cntv_ctl(uint32_t val) {
    __asm__ __volatile__("mcr p15, 0, %0, c14, c3, 1" : : "r"(val));
}

/* Write the virtual timer compare value (CNTV_CVAL) */
static inline void write_cntv_cval(uint64_t val) {
    __asm__ __volatile__("mcrr p15, 3, %Q0, %R0, c14" : : "r"(val));
}

/* Read the virtual timer frequency (CNTFRQ) */
static inline uint32_t read_cntfrq(void) {
    uint32_t val;
    __asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
    return val;
}

void timer_init(uint32_t hz) {
    timer_hz = hz;
    uint32_t freq = read_cntfrq();
    if (freq == 0) freq = 24000000; /* QEMU virt default: 24MHz */

    uint32_t interval = freq / hz;
    if (interval == 0) interval = 1;

    /* Disable timer */
    write_cntv_ctl(0);

    /* Set next compare value */
    uint64_t current = read_cntvct();
    write_cntv_cval(current + interval);

    /* Enable timer as virtual timer: IMASK=0, ENABLE=1 */
    write_cntv_ctl(1);

    /* Enable GIC IRQ for generic timer */
    gic_enable_irq(IRQ_TIMER);
}

void timer_register_callback(timer_callback_t cb) {
    callback = cb;
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t timer_get_uptime_ms(void) {
    if (timer_hz == 0) return 0;
    return (timer_ticks * 1000) / timer_hz;
}

void timer_interrupt_handler(registers_t *regs) {
    timer_ticks++;

    /* Re-arm the timer */
    uint32_t freq = read_cntfrq();
    if (freq == 0) freq = 24000000;
    uint32_t interval = freq / timer_hz;
    if (interval == 0) interval = 1;
    uint64_t current = read_cntvct();
    write_cntv_cval(current + interval);

    /* Clear the timer interrupt status */
    write_cntv_ctl(1);

    if (callback) callback(regs);
}
