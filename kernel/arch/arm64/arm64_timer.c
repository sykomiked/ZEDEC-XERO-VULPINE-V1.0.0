/* arm64_timer.c — Generic Timer driver for ARM64
 *
 * Uses the ARMv8 generic timer (CNT* registers) for kernel tick.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include "arm64_arch.h"

typedef void (*timer_callback_t)(void);
static timer_callback_t timer_cb = 0;
static uint64_t timer_freq = 0;
static uint64_t timer_interval = 0;
static volatile uint64_t timer_ticks = 0;

void arm64_timer_init(void) {
    timer_freq = get_cntfrq();
    /* Default: 100 Hz tick (10ms interval) */
    timer_interval = timer_freq / 100;
    set_cntp_tval(timer_interval);
    enable_cntp();
}

void arm64_timer_set_callback(timer_callback_t cb) {
    timer_cb = cb;
}

void arm64_timer_set_frequency(uint32_t hz) {
    timer_interval = timer_freq / hz;
    set_cntp_tval(timer_interval);
}

uint64_t arm64_timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t arm64_timer_get_uptime_ms(void) {
    if (timer_freq == 0) return 0;
    return (timer_ticks * 1000) / (timer_freq / timer_interval);
}

void arm64_timer_handler(void) {
    timer_ticks++;
    set_cntp_tval(timer_interval); /* Reload */

    if (timer_cb) timer_cb();
}

void arm64_timer_delay_us(uint64_t us) {
    uint64_t freq = get_cntfrq();
    uint64_t target = get_cntpct() + (freq * us) / 1000000;
    while (get_cntpct() < target) { }
}

void arm64_timer_delay_ms(uint64_t ms) {
    arm64_timer_delay_us(ms * 1000);
}
