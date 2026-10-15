/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xrisk.h — exact loss distributions for discrete credit/insurance portfolios
 *
 * WHAT IT ACTUALLY DOES
 * ---------------------
 * A portfolio has up to XRISK_MAX_OBLIGORS independent exposures. Exposure i
 * defaults with an exact rational probability p_i = num/den and then loses an
 * integer number of "loss units" L_i (the caller picks the unit, e.g. 1000
 * currency units). The total loss L = sum of L_i over defaulted exposures.
 *
 * xrisk computes the WHOLE distribution of L exactly, by dynamic programming
 * (one convolution per exposure) over loss units 0..sum(L_i), with exact
 * big-integer arithmetic. There is no sampling and no rounding: every
 * P(L = k) is an exact fraction mass[k] / den with one shared denominator.
 * From it come, all exact: P(L = k), P(L <= k), P(L >= k), expected loss,
 * VaR at a rational confidence level and expected shortfall.
 *
 * Correlation is modelled exactly as a finite mixture ("conditional
 * independence", one-factor style): up to XRISK_MAX_SCEN scenarios, each with
 * an exact rational weight (weights must sum to exactly 1) and its own p_i.
 * Within a scenario exposures are independent; the result is the weighted
 * mixture of the scenario distributions.
 *
 * EXACT REPRESENTATION (the design choice)
 *   Every input fraction is first reduced to lowest terms. For exposure i,
 *   m_i = lcm of its denominators over all scenarios (int64, overflow refused),
 *   and every p_{i,s} is rescaled to a numerator over m_i. Scenario weights are
 *   rescaled to one denominator B = lcm of the weight denominators. Then the
 *   whole distribution shares ONE denominator  den = B * prod_i m_i, held in a
 *   fixed-width unsigned big integer (XRISK_BITS payload bits, default 4096,
 *   plus XRISK_HEAD_BITS of headroom for metric arithmetic). den is computed
 *   first, with exact overflow detection; if it does not fit, the computation
 *   is REFUSED (XRISK_E_OVERFLOW). Every numerator is <= den, so the DP itself
 *   cannot overflow once den fits. Nothing is ever rounded.
 *
 * PARACONSISTENT INPUT FUSION
 *   An exposure may have several probability sources (rating agency, internal
 *   model, market-implied). xrisk_fuse() classifies them with the four values
 *   of lpres (same numbers, own enum, see below):
 *     NEITHER  no source           -> refused; a value is required
 *     TRUE     all agree within tol -> use the most conservative (highest) p
 *     BOTH     they conflict        -> the exposure is flagged and carries a
 *                                      low p and a high p; the caller computes
 *                                      the portfolio twice (XRISK_SIDE_LOW and
 *                                      XRISK_SIDE_HIGH) and reports BOTH as
 *                                      bounds. Never averaged.
 *   Because every L_i >= 0, raising any p_i can only shift loss upwards
 *   (stochastic order), so the LOW and HIGH runs bracket every metric for any
 *   choice of p inside the conflicting range. The tests check the bracketing.
 *
 * HONEST LIMITS
 *   - Discrete models only: integer loss units, fixed loss-given-default per
 *     exposure, a finite scenario mixture. Continuous models (lognormal
 *     severities, Gaussian copulas with continuous factors, path-dependent
 *     market risk) are NOT computed exactly here; Monte Carlo is the more
 *     general tool for them. Discretising a continuous model into units is the
 *     caller's modelling choice and its error is not measured by this module.
 *   - Cost: about scenarios x exposures x total_units x limbs(den) word
 *     operations. With 256 exposures, 4096 units and 4-digit probabilities it
 *     is well under a second on a desktop CPU; much larger books are refused.
 *   - Exact is exact relative to the INPUTS. Garbage probabilities give an
 *     exactly computed garbage distribution. Model risk is not removed.
 *   - The denominator grows with every exposure (prod of m_i). Probabilities
 *     with many digits (or co-prime denominators across many exposures) hit
 *     XRISK_BITS sooner; then the run is refused, never approximated.
 *   - This is a computation tool. It is not investment, credit or insurance
 *     advice and is not a regulatory capital model. Nothing quantum here.
 *
 * Freestanding: integer only, no allocation (the caller owns every struct; an
 * xrisk_dist_t is several megabytes, so put it in static storage or allocate
 * it), no libc. All external symbols start with xrisk_.
 */
#ifndef ZXV_XRISK_H
#define ZXV_XRISK_H

#include <stdint.h>
#include <stddef.h>

#ifndef XRISK_MAX_OBLIGORS
#    define XRISK_MAX_OBLIGORS 256
#endif
#ifndef XRISK_MAX_UNITS
#    define XRISK_MAX_UNITS 4096 /* cap on sum of L_i */
#endif
#ifndef XRISK_MAX_SCEN
#    define XRISK_MAX_SCEN 16
#endif
#ifndef XRISK_BITS
#    define XRISK_BITS 4096 /* payload bits allowed for the shared denominator */
#endif
#define XRISK_HEAD_BITS   128 /* headroom for metric arithmetic (k * mass, alpha) */
#define XRISK_LIMBS       ((XRISK_BITS + XRISK_HEAD_BITS + 31) / 32)
#define XRISK_MAX_SOURCES 8

/* Status codes. */
typedef enum {
    XRISK_OK = 0,
    XRISK_E_NULL = -1,     /* NULL pointer argument */
    XRISK_E_RANGE = -2,    /* index / count / units out of range */
    XRISK_E_PROB = -3,     /* den <= 0, num < 0 or num > den */
    XRISK_E_WEIGHT = -4,   /* scenario weights invalid or do not sum to 1 */
    XRISK_E_OVERFLOW = -5, /* exact value does not fit: refused, not rounded */
    XRISK_E_UNSET = -6,    /* probability or weight never set */
    XRISK_E_NEITHER = -7,  /* fusion: no probability source */
    XRISK_E_ALPHA = -8,    /* confidence level not in (0,1) */
    XRISK_E_DIVZERO = -9,  /* rational with zero denominator */
    XRISK_E_BUF = -10      /* output buffer too small */
} xrisk_status_t;

/* Four-valued fusion verdict. Same numeric values as lpres_state_t in
 * kernel/src/lpres/lpres.h (NEITHER=0 TRUE=1 FALSE=2 BOTH=3); defined here
 * so this module does not pull in lpres's headers. FALSE is never returned by
 * xrisk_fuse (a probability source cannot attest "no probability"); it exists
 * so the values line up with lpres. */
typedef enum { XRISK_NEITHER = 0, XRISK_TRUE = 1, XRISK_FALSE = 2, XRISK_BOTH = 3 } xrisk_state_t;

/* Which side of a flagged (BOTH) exposure to use. */
typedef enum { XRISK_SIDE_LOW = 0, XRISK_SIDE_HIGH = 1 } xrisk_side_t;

/* Little-endian fixed-width unsigned big integer, 32-bit limbs. */
typedef struct {
    uint32_t l[XRISK_LIMBS];
} xrisk_big_t;

/* An exact non-negative rational num/den (den > 0). */
typedef struct {
    xrisk_big_t num;
    xrisk_big_t den;
} xrisk_rat_t;

/* An input fraction. */
typedef struct {
    int64_t num;
    int64_t den;
} xrisk_q_t;

typedef struct {
    uint32_t n_obl;  /* exposures */
    uint32_t n_scen; /* scenarios (1 = plain independent model) */
    uint32_t loss[XRISK_MAX_OBLIGORS];
    uint8_t loss_set[XRISK_MAX_OBLIGORS];
    uint8_t flag[XRISK_MAX_OBLIGORS]; /* xrisk_state_t of the last fusion (TRUE if set directly) */
    xrisk_q_t w[XRISK_MAX_SCEN];
    xrisk_q_t p_lo[XRISK_MAX_SCEN][XRISK_MAX_OBLIGORS];
    xrisk_q_t p_hi[XRISK_MAX_SCEN][XRISK_MAX_OBLIGORS];
} xrisk_model_t;

typedef struct {
    uint32_t max_loss; /* support is 0..max_loss */
    uint32_t len;      /* limbs actually in use (<= XRISK_LIMBS) */
    xrisk_big_t den;   /* shared denominator */
    xrisk_big_t mass[XRISK_MAX_UNITS + 1];
    xrisk_big_t work[XRISK_MAX_UNITS + 1]; /* per-scenario scratch */
} xrisk_dist_t;

/* ---- big integer helpers (exposed for reports and tests) ---- */
void xrisk_big_zero(xrisk_big_t *a);
void xrisk_big_from_u64(xrisk_big_t *a, uint64_t v);
void xrisk_big_copy(xrisk_big_t *dst, const xrisk_big_t *src);
int xrisk_big_is_zero(const xrisk_big_t *a);
int xrisk_big_cmp(const xrisk_big_t *a, const xrisk_big_t *b); /* -1, 0, 1 */
int xrisk_big_add(xrisk_big_t *dst, const xrisk_big_t *a, const xrisk_big_t *b);
int xrisk_big_sub(xrisk_big_t *dst, const xrisk_big_t *a, const xrisk_big_t *b); /* a >= b */
int xrisk_big_mul_u64(xrisk_big_t *dst, const xrisk_big_t *a, uint64_t m);
/* q = a / d, r = a % d (either may be NULL). d != 0. */
int xrisk_big_divmod(xrisk_big_t *q, xrisk_big_t *r, const xrisk_big_t *a, const xrisk_big_t *d);
uint32_t xrisk_big_bits(const xrisk_big_t *a);
/* Value if it fits in 64 bits, else XRISK_E_OVERFLOW. */
int xrisk_big_to_u64(const xrisk_big_t *a, uint64_t *out);

/* ---- rationals ---- */
int xrisk_rat_reduce(xrisk_rat_t *r); /* to lowest terms */
/* *out = sign(a - b) by exact cross multiplication (double-width internally). */
int xrisk_rat_cmp(const xrisk_rat_t *a, const xrisk_rat_t *b, int *out);
/* Decimal digits of num/den TRUNCATED (not rounded) to `places` fractional
 * digits, NUL-terminated, e.g. "0.0123". No floats. */
int xrisk_rat_to_dec(const xrisk_rat_t *r, uint32_t places, char *buf, size_t cap);

/* ---- fusion ---- */
/* Classify n sources (each a valid probability) with absolute tolerance tol
 * (a probability-sized rational >= 0): agree iff max - min <= tol.
 * TRUE: *lo = *hi = max. BOTH: *lo = min, *hi = max. n == 0: returns
 * XRISK_E_NEITHER and *state = XRISK_NEITHER. Fractions are returned reduced. */
int xrisk_fuse(const xrisk_q_t *src, uint32_t n, xrisk_q_t tol, xrisk_q_t *lo, xrisk_q_t *hi,
               xrisk_state_t *state);

/* ---- model ---- */
int xrisk_model_init(xrisk_model_t *m, uint32_t n_obl, uint32_t n_scen);
int xrisk_model_set_loss(xrisk_model_t *m, uint32_t i, uint32_t units);
int xrisk_model_set_weight(xrisk_model_t *m, uint32_t s, int64_t num, int64_t den);
int xrisk_model_set_p(xrisk_model_t *m, uint32_t s, uint32_t i, int64_t num, int64_t den);
/* Fuse sources for exposure i in scenario s and store lo/hi. On NEITHER the
 * exposure stays unset (compute will refuse) and XRISK_E_NEITHER is returned. */
int xrisk_model_set_sources(xrisk_model_t *m, uint32_t s, uint32_t i, const xrisk_q_t *src,
                            uint32_t n, xrisk_q_t tol, xrisk_state_t *state);
/* Number of exposures flagged BOTH (conflicting sources) in any scenario. */
uint32_t xrisk_model_conflicts(const xrisk_model_t *m);

/* ---- exact distribution and metrics ---- */
int xrisk_compute(const xrisk_model_t *m, xrisk_side_t side, xrisk_dist_t *d);
int xrisk_pmf(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out);  /* P(L = k) */
int xrisk_cdf(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out);  /* P(L <= k) */
int xrisk_tail(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out); /* P(L >= k) */
int xrisk_expected_loss(const xrisk_dist_t *d, xrisk_rat_t *out);    /* E[L], in units */
/* VaR_alpha = smallest k with P(L <= k) >= alpha, alpha = an/ad in (0,1). */
int xrisk_var(const xrisk_dist_t *d, int64_t an, int64_t ad, uint32_t *k);
/* Expected shortfall (Acerbi-Tasche):
 *   ES = ( E[L 1{L > v}] + v (P(L <= v) - alpha) ) / (1 - alpha),  v = VaR_alpha.
 * Coherent; equals E[L | L >= v] when P(L <= v) = alpha exactly. */
int xrisk_es(const xrisk_dist_t *d, int64_t an, int64_t ad, xrisk_rat_t *out);

#endif /* ZXV_XRISK_H */
