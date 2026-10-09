/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_phi.h — powers of phi in Q16.16 from integer Fibonacci numbers, and
 * snapping a scale to the nearest power. No floating point.
 *
 *   P1  phi^n = F(n) * phi + F(n - 1) for every integer n, with the
 *       negafibonacci extension F(-n) = (-1)^(n+1) F(n). Evaluated in int64
 *       with phi in Q46 (113859019825107), then rounded to Q16: error at most
 *       one unit in the last place. Range n = -24 .. 21 (phi^21 = 24476.99 is
 *       the largest that fits Q16.16; phi^-24 rounds to 1 LSB).
 *   P2  zt_phi_snap_q16 picks the n whose phi^n is nearest the scale (linear
 *       distance, ties to the smaller n). Adjacent powers differ by 61.8%, so
 *       snapping moves a scale by up to about 24%: it is a lossy choice of the
 *       spec, not a refinement.
 *   The tensor engine has its own zt_phi_pow (tensor/zt.h T3, a different
 *   derivation); test_residual_tap.c checks the two agree to 1 LSB.
 */
#ifndef ZT_PHI_H
#define ZT_PHI_H

#include <stdint.h>

#define ZT_PHI_MIN_EXP (-24)
#define ZT_PHI_MAX_EXP 21

int32_t zt_fib(int32_t n);         /* F(n) for -46 <= n <= 46, negafibonacci for n < 0 */
int32_t zt_phi_pow_q16(int32_t n); /* P1; 0 outside the range */
int32_t zt_phi_snap_q16(int32_t scale_q16, int32_t *n_out); /* P2; scale <= 0 gives 0 */

#endif /* ZT_PHI_H */
