/* timer_arm.h — ARM Generic Timer for QEMU virt machine
 * Replaces x86 PIT timer.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARM_TIMER_H
#define ARM_TIMER_H

#include <stdint.h>
#include "registers.h"

#define TIMER_DEFAULT_HZ 100

typedef void (*timer_callback_t)(registers_t *regs);

void timer_init(uint32_t hz);
void timer_register_callback(timer_callback_t cb);
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_ms(void);
void timer_interrupt_handler(registers_t *regs);

#endif /* ARM_TIMER_H */
