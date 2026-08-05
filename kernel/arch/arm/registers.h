/* registers.h — ARM register frame for interrupt/exception handlers
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARM_REGISTERS_H
#define ARM_REGISTERS_H

#include <stdint.h>

/* ARM register frame — saved by exception entry assembly.
 * Mirrors the x86 registers_t layout so shared code can use
 * the same fields (int_no, err_code) for dispatch. */
typedef struct arm_registers {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11, r12;
    uint32_t sp, lr, pc, cpsr;
    uint32_t int_no;    /* Exception/fault number */
    uint32_t err_code;  /* FSR or 0 */
} arm_registers_t;

/* For shared code compatibility, registers_t maps to arm_registers_t on ARM */
typedef arm_registers_t registers_t;

#endif /* ARM_REGISTERS_H */
