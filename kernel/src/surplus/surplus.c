/* surplus.c — Interaction Surplus Framework (ISF) engine implementation
 *
 * Implements f(u) = ln(1 + (N-1)u) and all structural theorems from Papers A-E.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "surplus.h"

#ifndef TEST_HOST
/* ===== Fixed-point math for freestanding kernel ===== */

/* ln(x) via range reduction + Taylor series
 * ln(x) = ln(m * 2^e) = e*ln(2) + ln(m) where m ∈ [1, 2)
 * ln(m) = 2 * [t + t³/3 + t⁵/5 + ...] where t = (m-1)/(m+1) */
surplus_real_t sr_ln(surplus_real_t x) {
    if (x <= 0) return -0x7FFFFFFFFFFFFFFFLL; /* -∞ sentinel */
    
    /* Range reduce: find e such that x / 2^e ∈ [1, 2) */
    int e = 0;
    surplus_real_t m = x;
    while (m >= (SR_ONE << 1)) { m = m >> 1; e++; }
    while (m < SR_ONE) { m = m << 1; e--; }
    
    /* m ∈ [1, 2) in Q32.32. Compute t = (m-1)/(m+1) */
    surplus_real_t one = SR_ONE;
    surplus_real_t t = SR_DIV(SR_SUB(m, one), SR_ADD(m, one));
    surplus_real_t t2 = SR_MUL(t, t);
    
    /* ln(m) = 2 * (t + t³/3 + t⁵/5 + t⁷/7 + ...) */
    surplus_real_t sum = t;
    surplus_real_t term = t;
    for (int i = 3; i <= 21; i += 2) {
        term = SR_MUL(term, t2);
        surplus_real_t inc = SR_DIV(term, SR_FROM_INT(i));
        sum = SR_ADD(sum, inc);
    }
    sum = SR_ADD(sum, sum); /* *2 */
    
    /* Add e * ln(2) — ln(2) ≈ 0.69314718 in Q32.32 */
    surplus_real_t ln2 = (surplus_real_t)(0.6931471805599453 * (double)SR_ONE);
    return SR_ADD(sum, SR_MUL(SR_FROM_INT(e), ln2));
}

/* sqrt(x) via Newton's method */
surplus_real_t sr_sqrt(surplus_real_t x) {
    if (x <= 0) return 0;
    surplus_real_t guess = x >> 1;
    if (guess == 0) guess = SR_ONE;
    for (int i = 0; i < 20; i++) {
        surplus_real_t next = SR_ADD(guess, SR_DIV(x, guess)) >> 1;
        if (next == guess) break;
        guess = next;
    }
    return guess;
}
#endif /* TEST_HOST */

/* ===== Core Surplus Functional ===== */

surplus_real_t surplus_u(const surplus_real_t *x, const surplus_real_t *y, uint32_t dim) {
    surplus_real_t dot = SR_ZERO;
    for (uint32_t i = 0; i < dim; i++) {
        dot = SR_ADD(dot, SR_MUL(x[i], y[i]));
    }
    surplus_real_t dot_sq = SR_MUL(dot, dot);
    /* u = 1 - dot² */
    return SR_SUB(SR_ONE, dot_sq);
}

surplus_real_t surplus_f(surplus_real_t u, uint32_t N) {
    /* f(u) = ln(1 + (N-1)*u).  N is a block count: N<2 has zero surplus.
     * Guard first — N==0 made the unsigned (N-1) wrap to 4294967295 and
     * SR_FROM_INT of that overflowed to garbage (red-team). Also reject an
     * N so large that (N-1) in Q32.32 would overflow. */
    if (N < 2) return SR_ZERO;
    if (N - 1 > 0x7FFFFFFFu) return SR_ZERO;
    if (u < 0) u = SR_ZERO;
    if (u > SR_ONE) u = SR_ONE;
    surplus_real_t inner = SR_ADD(SR_ONE, SR_MUL(SR_FROM_INT((int64_t)N - 1), u));
    return SR_LN(inner);
}

surplus_real_t surplus_F(const surplus_real_t *x, const surplus_real_t *y,
                          uint32_t dim, uint32_t N) {
    surplus_real_t u = surplus_u(x, y, dim);
    return surplus_f(u, N);
}

/* ===== Derived Properties ===== */

surplus_real_t surplus_deriv(surplus_real_t u, uint32_t N) {
    /* f'(u) = (N-1) / (1 + (N-1)u) */
    surplus_real_t inner = SR_ADD(SR_ONE, SR_MUL(SR_FROM_INT(N - 1), u));
    return SR_DIV(SR_FROM_INT(N - 1), inner);
}

surplus_real_t surplus_second_deriv(surplus_real_t u, uint32_t N) {
    /* f''(u) = -(N-1)² / (1 + (N-1)u)² */
    surplus_real_t inner = SR_ADD(SR_ONE, SR_MUL(SR_FROM_INT(N - 1), u));
    surplus_real_t inner_sq = SR_MUL(inner, inner);
    surplus_real_t numer = SR_MUL(SR_FROM_INT(N - 1), SR_FROM_INT(N - 1));
    return -SR_DIV(numer, inner_sq); /* Note: negation needs care in fixed-point */
}

bool surplus_subadditive(surplus_real_t a, surplus_real_t b, uint32_t N) {
    surplus_real_t sum = SR_ADD(a, b);
    if (SR_CMP(sum, SR_ONE) > 0) return false; /* a+b > 1, undefined */
    surplus_real_t f_sum = surplus_f(sum, N);
    surplus_real_t f_a = surplus_f(a, N);
    surplus_real_t f_b = surplus_f(b, N);
    return SR_CMP(f_sum, SR_ADD(f_a, f_b)) <= 0;
}

/* ===== Two-Source Decomposition ===== */

void surplus_decompose(surplus_decomp_t *d,
                        surplus_real_t alpha,
                        surplus_real_t beta,
                        surplus_real_t sin_gamma,
                        uint32_t N) {
    /* u_cross = β² */
    d->u_cross = SR_MUL(beta, beta);
    /* u_div = α² * sin²(γ) */
    surplus_real_t sin_g_sq = SR_MUL(sin_gamma, sin_gamma);
    d->u_div = SR_MUL(SR_MUL(alpha, alpha), sin_g_sq);
    /* u = u_cross + u_div */
    d->u_total = SR_ADD(d->u_cross, d->u_div);
    /* f values */
    d->f_cross = surplus_f(d->u_cross, N);
    d->f_div = surplus_f(d->u_div, N);
    d->f_total = surplus_f(d->u_total, N);
    /* Bounds: max(f_cross, f_div) ≤ f(u) ≤ f_cross + f_div */
    d->f_lower = (SR_CMP(d->f_cross, d->f_div) > 0) ? d->f_cross : d->f_div;
    d->f_upper = SR_ADD(d->f_cross, d->f_div);
}

/* ===== Lipschitz Stability ===== */

surplus_real_t surplus_lipschitz_bound(uint32_t N) {
    return SR_FROM_INT(N - 1);
}

bool surplus_lipschitz_check(surplus_real_t u1, surplus_real_t u2, uint32_t N) {
    surplus_real_t diff_f = surplus_f(u1, N) - surplus_f(u2, N);
    if (diff_f < 0) diff_f = -diff_f;
    surplus_real_t diff_u = u1 - u2;
    if (diff_u < 0) diff_u = -diff_u;
    surplus_real_t bound = SR_MUL(SR_FROM_INT(N - 1), diff_u);
    return SR_CMP(diff_f, bound) <= 0;
}

/* ===== Extremality ===== */

bool surplus_is_zero(surplus_real_t u) {
    return SR_CMP(u, SR_ZERO) == 0;
}

bool surplus_is_maximal(surplus_real_t u, uint32_t N) {
    surplus_real_t f_val = surplus_f(u, N);
    surplus_real_t f_max = SR_LN(SR_FROM_INT(N));
    return SR_CMP(f_val, f_max) == 0;
}

surplus_real_t surplus_orthogonal_total(uint32_t k, uint32_t N) {
    /* k(k-1)/2 * ln(N) — but we compute k choose 2 * f(1) */
    surplus_real_t f_max = SR_LN(SR_FROM_INT(N));
    uint32_t pairs = k * (k - 1) / 2;
    return SR_MUL(SR_FROM_INT(pairs), f_max);
}

/* ===== Diagonal Surplus ===== */

surplus_real_t surplus_diagonal(surplus_real_t x_dot_e1, uint32_t N) {
    /* u = 1 - (x·e1)² / N */
    surplus_real_t dot_sq = SR_MUL(x_dot_e1, x_dot_e1);
    surplus_real_t u = SR_SUB(SR_ONE, SR_DIV(dot_sq, SR_FROM_INT(N)));
    return surplus_f(u, N);
}

/* ===== Information-Theoretic Bridge ===== */

surplus_real_t surplus_effective_count(surplus_real_t u, uint32_t N) {
    /* g(u) = 1 + (N-1)u */
    return SR_ADD(SR_ONE, SR_MUL(SR_FROM_INT(N - 1), u));
}

surplus_real_t surplus_canonical_entropy(surplus_real_t u, uint32_t N) {
    /* H(P_u) = f(u) - [(N-1)u / g(u)] * ln(u) */
    if (SR_CMP(u, SR_ZERO) == 0) return SR_ZERO; /* lim u→0 */
    surplus_real_t f_val = surplus_f(u, N);
    surplus_real_t g = surplus_effective_count(u, N);
    surplus_real_t coeff = SR_DIV(SR_MUL(SR_FROM_INT(N - 1), u), g);
    surplus_real_t ln_u = SR_LN(u);
    return SR_SUB(f_val, SR_MUL(coeff, ln_u));
}

surplus_real_t surplus_jensen_upper(surplus_real_t E_U, uint32_t N) {
    /* E[F(U)] ≤ F(E[U]) = ln(1 + (N-1)E[U]) */
    return surplus_f(E_U, N);
}

/* ===== Open-System Dynamics ===== */

void surplus_dynamics_init(surplus_dynamics_t *dyn, surplus_real_t Q0,
                            surplus_real_t delta, surplus_real_t eta, uint32_t N) {
    dyn->Q = Q0;
    dyn->delta = delta;
    dyn->eta = eta;
    dyn->N = N;
    dyn->tick = 0;
}

surplus_real_t surplus_dynamics_step(surplus_dynamics_t *dyn,
                                      surplus_real_t U_t,
                                      surplus_real_t C_t) {
    /* S_t = f(U_t) = ln(1 + (N-1)U_t) */
    surplus_real_t S_t = surplus_f(U_t, dyn->N);
    /* Q_{t+1} = (1-δ)Q_t + η·S_t - C_t */
    surplus_real_t decay = SR_MUL(SR_SUB(SR_ONE, dyn->delta), dyn->Q);
    surplus_real_t input = SR_MUL(dyn->eta, S_t);
    dyn->Q = SR_SUB(SR_ADD(decay, input), C_t);
    if (dyn->Q < 0) dyn->Q = SR_ZERO; /* Non-negative constraint */
    dyn->tick++;
    return dyn->Q;
}

surplus_real_t surplus_sustainability_ceiling(surplus_dynamics_t *dyn) {
    surplus_real_t ln_N = SR_LN(SR_FROM_INT(dyn->N));
    surplus_real_t ceiling = SR_DIV(SR_MUL(dyn->eta, ln_N), dyn->delta);
    if (SR_CMP(dyn->Q, ceiling) > 0) return dyn->Q;
    return ceiling;
}

bool surplus_can_grow(surplus_dynamics_t *dyn, surplus_real_t U_t, surplus_real_t C_t) {
    surplus_real_t S_t = surplus_f(U_t, dyn->N);
    surplus_real_t lhs = SR_MUL(dyn->eta, S_t);
    surplus_real_t rhs = SR_ADD(SR_MUL(dyn->delta, dyn->Q), C_t);
    return SR_CMP(lhs, rhs) > 0;
}

surplus_real_t surplus_max_increment(surplus_dynamics_t *dyn, surplus_real_t C_t) {
    surplus_real_t ln_N = SR_LN(SR_FROM_INT(dyn->N));
    surplus_real_t eta_ln_N = SR_MUL(dyn->eta, ln_N);
    surplus_real_t delta_Q = SR_MUL(dyn->delta, dyn->Q);
    surplus_real_t r_max = SR_SUB(eta_ln_N, SR_ADD(delta_Q, C_t));
    if (r_max < 0) r_max = SR_ZERO;
    return r_max;
}

surplus_real_t surplus_steady_state(surplus_real_t u_star, surplus_real_t c_star,
                                     surplus_real_t delta, surplus_real_t eta, uint32_t N) {
    surplus_real_t f_val = surplus_f(u_star, N);
    surplus_real_t numer = SR_SUB(SR_MUL(eta, f_val), c_star);
    surplus_real_t result = SR_DIV(numer, delta);
    if (result < 0) result = SR_ZERO;
    return result;
}
