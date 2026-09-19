/* timer.c — PIT timer implementation
 * Fires at configurable Hz, drives scheduler tick.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "timer.h"
#include "../pic/pic.h"

/* ---- MODBIND DECLARATION — L4 devices -------------------------------------
 * Comment, not code, pending the ZXV_PROVIDES mechanism -- see the fuller note
 * in kernel/src/pic/pic.c and PROVENANCE/X86_REHOME.md.
 *
 *   ZXV_PROVIDES(timer_tick_ready)
 *   ZXV_REQUIRES(irq_ctrl_ready)
 *   ZXV_BRINGUP(timer_init)
 *
 * The requirement is real and visible: timer_init ends in pic_unmask(IRQ_TIMER),
 * and pic_unmask is this object's ONLY undefined symbol. Programming the 8254
 * divisor without an unmasked IRQ 0 yields a counter nothing ever hears, so the
 * capability genuinely is not provided until the PIC is up.
 *
 * NOT declared: any requirement on idt. timer_interrupt_handler is an entry
 * point this module EXPORTS for a dispatcher to route; this module never calls
 * idt_register_handler, so a requirement on it would be invented rather than
 * observed. Until something routes vector 32 here, the tick is armed at the
 * PIC and unrouted at the CPU.
 */

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
