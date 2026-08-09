/* zphi.c — ℤ[φ] exact arithmetic. See zphi.h for why this type exists.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include "zphi.h"

/* PHI_D is used ONLY by zphi_to_double, which is diagnostic. No decision in
 * this module reads it. */
#define ZPHI_GOLDEN_D 1.6180339887498948482

static inline zphi_t mk(int64_t a, int64_t b, bool ok) {
    zphi_t r; r.a = a; r.b = b; r.valid = ok; return r;
}

zphi_t zphi_invalid(void)    { return mk(0, 0, false); }
zphi_t zphi_make(int64_t a, int64_t b) { return mk(a, b, true); }
zphi_t zphi_int(int64_t n)   { return mk(n, 0, true); }
zphi_t zphi_zero(void)       { return mk(0, 0, true); }
zphi_t zphi_one(void)        { return mk(1, 0, true); }
zphi_t zphi_golden(void)     { return mk(0, 1, true); }   /* phi        */
zphi_t zphi_inv_golden(void) { return mk(-1, 1, true); }  /* 1/phi = phi-1 */

zphi_t zphi_add(zphi_t x, zphi_t y) {
    if (!x.valid || !y.valid) return zphi_invalid();
    int64_t a, b;
    if (__builtin_add_overflow(x.a, y.a, &a)) return zphi_invalid();
    if (__builtin_add_overflow(x.b, y.b, &b)) return zphi_invalid();
    return mk(a, b, true);
}

zphi_t zphi_sub(zphi_t x, zphi_t y) {
    if (!x.valid || !y.valid) return zphi_invalid();
    int64_t a, b;
    if (__builtin_sub_overflow(x.a, y.a, &a)) return zphi_invalid();
    if (__builtin_sub_overflow(x.b, y.b, &b)) return zphi_invalid();
    return mk(a, b, true);
}

zphi_t zphi_neg(zphi_t x) {
    if (!x.valid) return zphi_invalid();
    int64_t a, b;
    if (__builtin_sub_overflow((int64_t)0, x.a, &a)) return zphi_invalid();
    if (__builtin_sub_overflow((int64_t)0, x.b, &b)) return zphi_invalid();
    return mk(a, b, true);
}

/* (a + b*phi)(c + d*phi) = ac + (ad + bc)phi + bd*phi^2
 *                        = (ac + bd) + (ad + bc + bd)phi     since phi^2 = phi+1
 * THIS FUNCTION IS THE GENERATING RULE. Nothing else in the module needs to
 * know about x^2 = x + 1; closure comes entirely from these two lines. */
zphi_t zphi_mul(zphi_t x, zphi_t y) {
    if (!x.valid || !y.valid) return zphi_invalid();
    int64_t ac, bd, ad, bc, ra, t, rb;
    if (__builtin_mul_overflow(x.a, y.a, &ac)) return zphi_invalid();
    if (__builtin_mul_overflow(x.b, y.b, &bd)) return zphi_invalid();
    if (__builtin_mul_overflow(x.a, y.b, &ad)) return zphi_invalid();
    if (__builtin_mul_overflow(x.b, y.a, &bc)) return zphi_invalid();
    if (__builtin_add_overflow(ac, bd, &ra))   return zphi_invalid();
    if (__builtin_add_overflow(ad, bc, &t))    return zphi_invalid();
    if (__builtin_add_overflow(t,  bd, &rb))   return zphi_invalid();
    return mk(ra, rb, true);
}

zphi_t zphi_scale(zphi_t x, int64_t k) {
    if (!x.valid) return zphi_invalid();
    int64_t a, b;
    if (__builtin_mul_overflow(x.a, k, &a)) return zphi_invalid();
    if (__builtin_mul_overflow(x.b, k, &b)) return zphi_invalid();
    return mk(a, b, true);
}

/* phi -> psi = 1 - phi.  a + b*phi  ->  a + b(1-phi) = (a+b) - b*phi */
zphi_t zphi_conj(zphi_t x) {
    if (!x.valid) return zphi_invalid();
    int64_t a, b;
    if (__builtin_add_overflow(x.a, x.b, &a)) return zphi_invalid();
    if (__builtin_sub_overflow((int64_t)0, x.b, &b)) return zphi_invalid();
    return mk(a, b, true);
}

/* N(a + b*phi) = a^2 + ab - b^2, always an ordinary integer. */
int64_t zphi_norm(zphi_t x, bool *ok) {
    if (ok) *ok = false;
    if (!x.valid) return 0;
    int64_t aa, ab, bb, t, n;
    if (__builtin_mul_overflow(x.a, x.a, &aa)) return 0;
    if (__builtin_mul_overflow(x.a, x.b, &ab)) return 0;
    if (__builtin_mul_overflow(x.b, x.b, &bb)) return 0;
    if (__builtin_add_overflow(aa, ab, &t))    return 0;
    if (__builtin_sub_overflow(t, bb, &n))     return 0;
    if (ok) *ok = true;
    return n;
}

int64_t zphi_trace(zphi_t x, bool *ok) {
    if (ok) *ok = false;
    if (!x.valid) return 0;
    int64_t t, n;
    if (__builtin_mul_overflow(x.a, (int64_t)2, &t)) return 0;
    if (__builtin_add_overflow(t, x.b, &n))          return 0;
    if (ok) *ok = true;
    return n;
}

bool zphi_eq(zphi_t x, zphi_t y) {
    if (!x.valid || !y.valid) return false;
    return x.a == y.a && x.b == y.b;
}
bool zphi_is_zero(zphi_t x) { return x.valid && x.a == 0 && x.b == 0; }
bool zphi_is_int(zphi_t x)  { return x.valid && x.b == 0; }

double zphi_to_double(zphi_t x) {
    if (!x.valid) return 0.0;
    return (double)x.a + (double)x.b * ZPHI_GOLDEN_D;
}

uint32_t zphi_selfcheck(void) {
    uint32_t bad = 0;
    const zphi_t P = zphi_golden();

    /* THE generating rule: phi^2 == phi + 1. If this fails nothing else in the
     * exact-geometry stack means anything. */
    if (!zphi_eq(zphi_mul(P, P), zphi_add(P, zphi_one()))) bad++;

    /* 1/phi == phi - 1, i.e. phi * (phi-1) == 1 */
    if (!zphi_eq(zphi_mul(P, zphi_inv_golden()), zphi_one())) bad++;

    /* The two roots: phi + psi == 1 and phi * psi == -1. */
    zphi_t psi = zphi_conj(P);
    if (!zphi_eq(zphi_add(P, psi), zphi_one()))                bad++;
    if (!zphi_eq(zphi_mul(P, psi), zphi_int(-1)))              bad++;

    /* Norm is multiplicative: N(xy) == N(x)N(y). This is the property the
     * whole lattice layer leans on, so it is swept, not spot-checked. */
    for (int64_t a = -6; a <= 6; a++)
    for (int64_t b = -6; b <= 6; b++)
    for (int64_t c = -6; c <= 6; c++)
    for (int64_t d = -6; d <= 6; d++) {
        zphi_t x = zphi_make(a, b), y = zphi_make(c, d);
        bool o1 = false, o2 = false, o3 = false;
        int64_t nx = zphi_norm(x, &o1), ny = zphi_norm(y, &o2);
        int64_t nxy = zphi_norm(zphi_mul(x, y), &o3);
        if (!o1 || !o2 || !o3) { bad++; goto done_norm; }
        if (nxy != nx * ny) { bad++; goto done_norm; }
    }
done_norm:

    /* Fibonacci emerges from the rule, it is not supplied to it:
     * phi^k == F(k)*phi + F(k-1). Checked to k=20 against the recurrence. */
    {
        zphi_t pk = zphi_one();
        int64_t f_prev = 0, f_cur = 1;   /* F(0), F(1) */
        for (int k = 1; k <= 20; k++) {
            pk = zphi_mul(pk, P);
            if (!pk.valid) { bad++; break; }
            if (pk.a != f_prev || pk.b != f_cur) { bad++; break; }
            int64_t nx = f_prev + f_cur;
            f_prev = f_cur; f_cur = nx;
        }
    }

    /* Overflow must be REPORTED, not wrapped. */
    {
        zphi_t big = zphi_make(INT64_MAX, 0);
        if (zphi_add(big, zphi_one()).valid) bad++;
        if (zphi_mul(big, zphi_int(3)).valid) bad++;
        /* and invalidity propagates */
        if (zphi_add(zphi_invalid(), zphi_one()).valid) bad++;
        if (zphi_eq(zphi_invalid(), zphi_invalid())) bad++;  /* never equal */
    }
    return bad;
}
