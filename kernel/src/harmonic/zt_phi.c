/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_phi.c — see zt_phi.h. */
#include "zt_phi.h"

#define PHI_Q46 113859019825107LL

int32_t zt_fib(int32_t n)
{
    int32_t m = n < 0 ? -n : n;
    if (m > 46) return 0;
    int32_t a = 0, b = 1; /* F(0), F(1) */
    for (int32_t i = 0; i < m; i++) {
        int32_t t = a + b;
        a = b;
        b = t;
    }
    /* a = F(m); F(-m) = (-1)^(m+1) F(m) */
    return (n < 0 && (m & 1) == 0) ? -a : a;
}

int32_t zt_phi_pow_q16(int32_t n)
{
    if (n < ZT_PHI_MIN_EXP || n > ZT_PHI_MAX_EXP) return 0;
    int64_t v = (int64_t) zt_fib(n) * PHI_Q46 + (int64_t) zt_fib(n - 1) * (1LL << 46);
    return (int32_t) ((v + (1LL << 29)) >> 30); /* v > 0: phi^n is positive */
}

int32_t zt_phi_snap_q16(int32_t scale_q16, int32_t *n_out)
{
    if (scale_q16 <= 0) {
        if (n_out) *n_out = 0;
        return 0;
    }
    int32_t best = ZT_PHI_MIN_EXP;
    int64_t bd = -1;
    for (int32_t n = ZT_PHI_MIN_EXP; n <= ZT_PHI_MAX_EXP; n++) {
        int64_t d = (int64_t) zt_phi_pow_q16(n) - scale_q16;
        if (d < 0) d = -d;
        if (bd < 0 || d < bd) {
            bd = d;
            best = n;
        }
    }
    if (n_out) *n_out = best;
    return zt_phi_pow_q16(best);
}
