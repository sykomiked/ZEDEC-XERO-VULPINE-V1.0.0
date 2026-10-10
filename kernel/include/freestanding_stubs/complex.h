/* freestanding complex.h -- deliberately unusable in kernel images.
 *
 * Kernel images are integer-only (-mgeneral-regs-only / integer riscv ABI), so
 * `double complex` cannot be compiled there. Use zxv_cq16_t and the cq16_*
 * helpers from zxv_fixed.h (Q16.16 real and imaginary parts) instead.
 */
#ifndef FREESTANDING_COMPLEX_H
#define FREESTANDING_COMPLEX_H
#error "complex.h is not available in freestanding kernel code; use zxv_cq16_t from zxv_fixed.h"
#endif
