/* timer.h — Programmable Interval Timer (PIT 8253/8254)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "../idt/idt.h"

#define PIT_FREQUENCY 1193182
#define PIT_CHANNEL0  0x40
#define PIT_COMMAND   0x43
#define TIMER_DEFAULT_HZ 100

typedef void (*timer_callback_t)(registers_t *regs);

void timer_init(uint32_t hz);
void timer_register_callback(timer_callback_t cb);
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_ms(void);
void timer_interrupt_handler(registers_t *regs);

#endif
