/* surplus.h — Interaction Surplus Framework (ISF) engine
 * 
 * Implements the axiomatic surplus functional from Papers A-E:
 *   f(u) = ln(1 + (N-1)u)
 * where u = 1 - (x·y)² ∈ [0,1] is the orientation-based interaction parameter.
 *
 * Includes:
 *   - Core surplus computation (Theorem 2.1)
 *   - Two-source decomposition (Theorem 4.1): crossing + divergence
 *   - Sharp Lipschitz stability (Theorem 4.2)
 *   - Extremality conditions (Theorem 4.3)
 *   - Diagonal surplus (Theorem 4.4)
 *   - Open-system dynamics (Paper E): Q_{t+1} = (1-δ)Q_t + ηS_t - C_t
 *   - Information-theoretic bridge (Paper D): Bernoulli moment, entropy relation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef SURPLUS_H
#define SURPLUS_H

#include <stdint.h>
#include <stdbool.h>

/* Fixed-point arithmetic for freestanding kernel.
 * We use Q32.32 fixed-point (32-bit integer, 32-bit fraction) represented
 * as int64_t. The natural log is computed via Taylor series + range reduction.
 * For test-host builds, double-precision is used directly. */

#ifdef TEST_HOST
#include <math.h>

typedef double surplus_real_t;

#define SR_FROM_INT(x)   ((double)(x))
#define SR_FROM_FLOAT(x) ((double)(x))
#define SR_ZERO          (0.0)
#define SR_ONE           (1.0)
#define SR_ADD(a,b)      ((a)+(b))
#define SR_SUB(a,b)      ((a)-(b))
#define SR_MUL(a,b)      ((a)*(b))
#define SR_DIV(a,b)      ((a)/(b))
#define SR_LN(x)         (log(x))
#define SR_SQRT(x)       (sqrt(x))
#define SR_SIN(x)        (sin(x))
#define SR_COS(x)        (cos(x))
#define SR_CMP(a,b)      ((a)<(b)?-1:(a)>(b)?1:0)
#define surplus_real_t_max (1e300)

#else
/* Q32.32 fixed-point */
typedef int64_t surplus_real_t;

#define SR_SHIFT       32
#define SR_ONE         ((int64_t)1 << SR_SHIFT)
#define SR_ZERO        ((int64_t)0)
/* Multiply, not left-shift: `(int64_t)x << 32` is UB when x is negative,
 * which sr_ln() hits for every argument < 1 (red-team). Multiply by SR_ONE
 * is defined for negatives and identical for non-negatives. */
#define SR_FROM_INT(x) ((int64_t)(x) * SR_ONE)
#define SR_FROM_FLOAT(x) ((int64_t)((x) * (double)SR_ONE))
#define SR_ADD(a,b)    ((a)+(b))
#define SR_SUB(a,b)    ((a)-(b))
/* Q32.32 fixed-point multiply: (a * b) >> 32.
 *
 * Uses a 128-bit intermediate where the compiler provides one (every
 * 64-bit target this kernel builds for: AArch64, x86-64, RV64). The
 * previous hand-split 32-bit version was written for a -m32 host and
 * is kept below as a fallback for genuine 32-bit builds.
 */
#if defined(__SIZEOF_INT128__)
static inline int64_t sr_mul_impl(int64_t a, int64_t b) {
    return (int64_t)(((__int128)a * (__int128)b) >> 32);
}

/* Q32.32 fixed-point divide: (a << 32) / b.
 *
 * CORRECTNESS NOTE (bug fixed 2026-08-04): the previous 32-bit-split
 * implementation computed `((r % b) << 32) / b` on the r >= 2^31 path.
 * Since r = a % b is always < b, `r % b == r`, so that expression
 * shifted a value >= 2^31 left by 32 bits and overflowed int64 —
 * signed overflow, in practice returning 0. Every divide whose
 * remainder reached 2^31 silently produced 0: SR_DIV(1,3), SR_DIV(1,2)
 * and SR_DIV(2,3) all returned 0.0. That corrupted the t=(m-1)/(m+1)
 * term inside sr_ln() for any mantissa >= 1.5, which in turn VIOLATED
 * the ISF normalization axiom S4 (f(1,N) = ln N): f(1,12) evaluated to
 * 1.674 instead of ln 12 = 2.485. A 128-bit numerator makes the
 * division exact and restores the axiom.
 */
static inline int64_t sr_div_impl(int64_t a, int64_t b) {
    if (b == 0) return 0;
    /* Scale by SR_ONE with a MULTIPLY, not a left-shift: shifting a signed
     * __int128 that is negative is UB, and this is the divide primitive that
     * runs on the real AArch64 target (red-team). Multiply is well-defined
     * for negative operands and produces the identical value for positives. */
    return (int64_t)(((__int128)a * ((__int128)1 << 32)) / (__int128)b);
}
#else
/* Fallback for true 32-bit targets without __int128. */
static inline int64_t sr_mul_impl(int64_t a, int64_t b) {
    /* Negate through unsigned so INT64_MIN does not trigger signed-overflow
     * UB (red-team). Magnitudes are computed as uint64_t. */
    int neg = (a < 0) ^ (b < 0);
    uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
    uint32_t al = (uint32_t)ua;
    uint32_t ah = (uint32_t)(ua >> 32);
    uint32_t bl = (uint32_t)ub;
    uint32_t bh = (uint32_t)(ub >> 32);
    uint64_t result = (uint64_t)ah * bl + (uint64_t)al * bh + ((uint64_t)al * bl >> 32);
    result += (uint64_t)ah * bh << 32;
    return neg ? -(int64_t)result : (int64_t)result;
}

static inline int64_t sr_div_impl(int64_t a, int64_t b) {
    if (b == 0) return 0;
    int neg = (a < 0) ^ (b < 0);
    uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
    /* Restoring long division on unsigned magnitudes, 32 fractional bits at
     * a time — never shifts a signed value that can overflow. */
    uint64_t q = ua / ub;
    uint64_t r = ua % ub;
    uint64_t frac = 0;
    for (int i = 0; i < 32; i++) {
        r <<= 1;                 /* r < ub <= 2^63, so r<<1 stays in range */
        frac <<= 1;
        if (r >= ub) { r -= ub; frac |= 1; }
    }
    int64_t result = (int64_t)((q << 32) + frac);
    return neg ? -result : result;
}
#endif

#define SR_MUL(a,b)    sr_mul_impl((a), (b))
#define SR_DIV(a,b)    sr_div_impl((a), (b))
#define SR_CMP(a,b)    ((a)<(b)?-1:(a)>(b)?1:0)
#define surplus_real_t_max ((int64_t)0x7FFFFFFFFFFFFFFFLL)

surplus_real_t sr_ln(surplus_real_t x);
surplus_real_t sr_sqrt(surplus_real_t x);
#define SR_LN(x)   sr_ln(x)
#define SR_SQRT(x) sr_sqrt(x)
/* sin/cos not needed for surplus core — used only in test host */
#endif

/* ===== Core Surplus Functional (Paper A, Theorem 2.1) ===== */

/* Compute u(x,y) = 1 - (x·y)² for unit vectors
 * x, y are arrays of length dim
 * Returns value in [0, 1] */
surplus_real_t surplus_u(const surplus_real_t *x, const surplus_real_t *y, uint32_t dim);

/* Compute f(u) = ln(1 + (N-1)u) — the surplus functional
 * N = number of blocks, u ∈ [0,1] */
surplus_real_t surplus_f(surplus_real_t u, uint32_t N);

/* Compute full surplus F(x,y) = f(u(x,y)) */
surplus_real_t surplus_F(const surplus_real_t *x, const surplus_real_t *y,
                          uint32_t dim, uint32_t N);

/* ===== Derived Properties (Paper A, §3) ===== */

/* Monotonicity: f'(u) = (N-1) / (1 + (N-1)u) > 0 */
surplus_real_t surplus_deriv(surplus_real_t u, uint32_t N);

/* Concavity: f''(u) = -(N-1)² / (1 + (N-1)u)² < 0 */
surplus_real_t surplus_second_deriv(surplus_real_t u, uint32_t N);

/* Subadditivity check: f(a+b) ≤ f(a) + f(b) for a+b ≤ 1 */
bool surplus_subadditive(surplus_real_t a, surplus_real_t b, uint32_t N);

/* ===== Two-Source Decomposition (Paper A, Theorem 4.1) ===== */

typedef struct {
    surplus_real_t u_cross;  /* β² — crossing (between-block) component */
    surplus_real_t u_div;    /* α²sin²γ — divergence (within-block) component */
    surplus_real_t u_total;  /* u = u_cross + u_div */
    surplus_real_t f_cross;  /* f(u_cross) */
    surplus_real_t f_div;    /* f(u_div) */
    surplus_real_t f_total;  /* f(u) */
    surplus_real_t f_lower;  /* max(f_cross, f_div) */
    surplus_real_t f_upper;  /* f_cross + f_div */
} surplus_decomp_t;

/* Compute decomposition for pure block vector x ∈ H_i vs arbitrary y */
void surplus_decompose(surplus_decomp_t *d,
                        surplus_real_t alpha,    /* ‖y_‖‖ — projection onto H_i */
                        surplus_real_t beta,     /* ‖y_⊥‖ — perpendicular to H_i */
                        surplus_real_t sin_gamma, /* sin of angle within H_i */
                        uint32_t N);

/* ===== Lipschitz Stability (Paper A, Theorem 4.2) ===== */

/* |f(u1) - f(u2)| ≤ (N-1)|u1 - u2| */
surplus_real_t surplus_lipschitz_bound(uint32_t N);

/* Check Lipschitz condition for specific pair */
bool surplus_lipschitz_check(surplus_real_t u1, surplus_real_t u2, uint32_t N);

/* ===== Extremality (Paper A, Theorem 4.3) ===== */

/* F = 0 iff x = ±y (u = 0) */
bool surplus_is_zero(surplus_real_t u);

/* F = ln(N) iff x ⊥ y (u = 1) */
bool surplus_is_maximal(surplus_real_t u, uint32_t N);

/* Total pairwise surplus for k mutually orthogonal vectors from k blocks */
surplus_real_t surplus_orthogonal_total(uint32_t k, uint32_t N);

/* ===== Diagonal Surplus (Paper A, Theorem 4.4) ===== */

/* Surplus between pure block vector x and diagonal ê_X */
surplus_real_t surplus_diagonal(surplus_real_t x_dot_e1, uint32_t N);

/* ===== Information-Theoretic Bridge (Paper D) ===== */

/* Effective count: g(u) = 1 + (N-1)u = e^{f(u)} */
surplus_real_t surplus_effective_count(surplus_real_t u, uint32_t N);

/* Canonical N-state distribution entropy:
 * H(P_u) = f(u) - [(N-1)u / g(u)] * ln(u)
 * H(P_u) ≥ f(u) on [0,1] */
surplus_real_t surplus_canonical_entropy(surplus_real_t u, uint32_t N);

/* Jensen upper bound: E[F(U)] ≤ F(E[U]) = ln(1 + (N-1)E[U]) */
surplus_real_t surplus_jensen_upper(surplus_real_t E_U, uint32_t N);

/* ===== Open-System Dynamics (Paper E) ===== */

typedef struct {
    surplus_real_t Q;       /* Stored order / capacity */
    surplus_real_t delta;   /* Decay/dissipation rate ∈ [0,1] */
    surplus_real_t eta;     /* Conversion efficiency ≥ 0 */
    uint32_t N;             /* Block count for surplus computation */
    uint32_t tick;          /* Time step */
} surplus_dynamics_t;

/* Initialize dynamics state */
void surplus_dynamics_init(surplus_dynamics_t *dyn, surplus_real_t Q0,
                            surplus_real_t delta, surplus_real_t eta, uint32_t N);

/* One-step update: Q_{t+1} = (1-δ)Q_t + η·S_t - C_t
 * where S_t = f(U_t) = ln(1 + (N-1)U_t)
 * Returns Q_{t+1} */
surplus_real_t surplus_dynamics_step(surplus_dynamics_t *dyn,
                                      surplus_real_t U_t,
                                      surplus_real_t C_t);

/* Sustainability ceiling: Q ≤ max(Q0, η·ln(N)/δ) */
surplus_real_t surplus_sustainability_ceiling(surplus_dynamics_t *dyn);

/* One-step growth criterion: Q_{t+1} > Q_t iff η·S_t > δ·Q_t + C_t */
bool surplus_can_grow(surplus_dynamics_t *dyn, surplus_real_t U_t, surplus_real_t C_t);

/* Maximum achievable one-step increment: r_max = η·ln(N) - δ·Q_t - C_t */
surplus_real_t surplus_max_increment(surplus_dynamics_t *dyn, surplus_real_t C_t);

/* Steady state under constant forcing: Q_∞ = (η·f(u*) - c*)/δ */
surplus_real_t surplus_steady_state(surplus_real_t u_star, surplus_real_t c_star,
                                     surplus_real_t delta, surplus_real_t eta, uint32_t N);

#endif /* SURPLUS_H */
