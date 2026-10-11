/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_bounds.c — is there any rational in a declared range that also
 * meets its den rule? Exact integer arithmetic only (R11): products of two
 * int64 values are taken in a 128-bit integer where the compiler has one;
 * without it the answer is "not determined". */
#include "zau_internal.h"

#define ZAU_DEN_SEARCH 4096 /* the bound for den<=N: denominators 1..4096 */

#if defined(__SIZEOF_INT128__)
typedef __int128 wide;

static wide floor_div(wide a, wide b) { /* b > 0 */
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static wide ceil_div(wide a, wide b) { return -floor_div(-a, b); }

/* Is there an integer p with lo <= p/q <= hi? */
static int fits(const zc_bounds *b, int64_t q) {
    wide pmin = ceil_div((wide) b->lo_num * q, (wide) b->lo_den);
    wide pmax = floor_div((wide) b->hi_num * q, (wide) b->hi_den);
    return pmin <= pmax;
}
#endif

int zau_rat_feasible(const zc_bounds *b) {
    if (b->type != ZC_T_RAT || !b->has_lo || !b->has_hi || b->den_mode == ZC_DEN_NONE) return 1;
    if (b->lo_den <= 0 || b->hi_den <= 0 || b->den_bound <= 0) return -1;
#if defined(__SIZEOF_INT128__)
    if (b->den_mode == ZC_DEN_DIVIDES) return fits(b, b->den_bound) ? 1 : 0;
    for (int64_t q = 1; q <= b->den_bound && q <= ZAU_DEN_SEARCH; q++)
        if (fits(b, q)) return 1;
    return b->den_bound <= ZAU_DEN_SEARCH ? 0 : -1;
#else
    return -1;
#endif
}
