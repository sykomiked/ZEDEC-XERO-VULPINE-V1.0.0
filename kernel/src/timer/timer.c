/* timer.c — PIT timer implementation
 * Fires at configurable Hz, drives scheduler tick.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "timer.h"
#include "../pic/pic.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_hz = 0;
static timer_callback_t callback = 0;

void timer_init(uint32_t hz) {
    timer_hz = hz;
    uint32_t divisor = PIT_FREQUENCY / hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor == 0) divisor = 1;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    pic_unmask(IRQ_TIMER);
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
    if (callback) callback(regs);
}
