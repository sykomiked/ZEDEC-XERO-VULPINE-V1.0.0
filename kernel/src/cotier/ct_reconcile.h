/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_reconcile.h — co-processor tier 3: exact reconciliation of what an
 * accelerator (tier 2: CUDA, Metal, Vulkan, a neural engine) computed in
 * FP16 / BF16 / FP32 back into the integer core's Q16.16. See
 * docs/COPROCESSOR_TIERS.md.
 *
 *   R1  INGEST.  Float bit patterns are decoded with integer bit operations
 *       only (sign, exponent, mantissa; subnormals included) and rounded to
 *       the nearest Q16.16, ties to even. Out of range values saturate to
 *       INT32_MAX / INT32_MIN (Q16.16 covers [-32768, 32768)). Infinities
 *       saturate the same way, NaN becomes 0. Every case is counted
 *       (ct_ingest_stats_t) and can be flagged per element, so nothing is
 *       silently clamped and nothing traps.
 *   R2  TWO SOURCES.  Coordinates are partitioned into categories (cat_of[i]
 *       in 0..n_cat-1). For a trusted reference r and an accelerator output
 *       a (unit x = r/|r|, y = a/|a|), let p_c = |x_c| and q_c = |y_c| be
 *       the two category energy profiles (unit vectors in R^n_cat) and
 *       alpha = p.q, beta^2 = 1 - alpha^2, cos(gamma) = (x.y) / alpha. Then
 *         u = 1 - (x.y)^2 = beta^2 + alpha^2 sin^2(gamma)
 *           = u_cross     + u_div
 *       u_cross = 1 - (p.q)^2: energy moved ACROSS categories (the profile
 *       changed); u_div = (p.q)^2 - (x.y)^2: rotation WITHIN categories with
 *       the profile held. When x is a pure block vector (one category) this
 *       is exactly surplus.h's Theorem 4.1 (alpha = |y_par|, beta =
 *       |y_perp|). u_div >= 0 is Cauchy-Schwarz per category
 *       (|x.y| <= p.q); computed values that break it beyond rounding, or
 *       break max(f_cross, f_div) <= f(u) <= f_cross + f_div, mark the
 *       sample inconsistent.
 *   R3  DRIFT.  f is Lipschitz with L = N - 1 (Theorem 4.2) and f(0) = 0, so
 *       a term t can move any downstream F by at most L * t. A sample
 *       drifts across categories when L * u_cross > tau_cross, inside one
 *       when L * u_div > tau_div; the magnitude channel compares |a| with
 *       |r| (relative tolerance tol_mag), which u cannot see.
 *   R4  PARACONSISTENT VERDICT (swarm_hk.h order and values):
 *         UNKNOWN  r or a is all zeros and the row had no NaN / Inf: no
 *                  evidence either way (with faults it is FALSE: nothing
 *                  valid came back).
 *         PARADOX  the arithmetic contradicts the theorems (the sum or the
 *                  bounds fail beyond rounding), or the row had NaN / Inf
 *                  and its finite part still agrees: escalate, don't trust.
 *         TRUE     direction and magnitude agree, no ingest fault.
 *         FALSE    direction and magnitude both disagree, or a faulted row
 *                  that also disagrees.
 *         GLUT     one channel agrees and the other does not: the
 *                  contradiction is held and counted, not resolved.
 *       Nothing here asserts or panics; every input gives a verdict.
 * Freestanding: no libc, no malloc, no floating point, no 64-bit division.
 */
#ifndef CT_RECONCILE_H
#define CT_RECONCILE_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

enum { CT_FMT_F16 = 0, CT_FMT_BF16 = 1, CT_FMT_F32 = 2 };

/* Per-element ingest flags. */
#define CT_ING_NAN     1u
#define CT_ING_INF     2u
#define CT_ING_SAT     4u  /* finite but outside Q16.16 */
#define CT_ING_SUB     8u  /* a subnormal input */
#define CT_ING_FLUSH   16u /* nonzero finite input that rounded to 0 */
#define CT_ING_INEXACT 32u
#define CT_ING_FAULT   (CT_ING_NAN | CT_ING_INF)

typedef struct {
    uint64_t n, nan, pos_inf, neg_inf, sat, subnormal, flushed, inexact;
} ct_ingest_stats_t;

/* One value; flags (may be null) receives CT_ING_* bits. */
zt_fx ct_f16_to_q16(uint16_t bits, uint32_t *flags);
zt_fx ct_bf16_to_q16(uint16_t bits, uint32_t *flags);
zt_fx ct_f32_to_q16(uint32_t bits, uint32_t *flags);

/* Arrays. fmt CT_FMT_F16 / BF16 read src16, CT_FMT_F32 reads src32 (the
 * other may be null). flags (may be null) gets n bytes of CT_ING_* bits.
 * st (may be null) is ADDED to. Returns the number of faulted (NaN / Inf)
 * elements, or -1 for a bad format or missing source. */
int64_t ct_ingest(uint32_t fmt, const uint16_t *src16, const uint32_t *src32, uint64_t n,
                  zt_fx *out, uint8_t *flags, ct_ingest_stats_t *st);

/* ---- two-source decomposition (R2) ---- */

#define CT_MAX_CATEGORIES 64u

typedef struct {
    uint32_t category;             /* where r has the most energy (informational) */
    zt_fx alpha;                   /* p.q, Q16 */
    zt_fx u_cross, u_div, u_total; /* Q16, each 0..1 */
    zt_fx f_cross, f_div, f_total; /* Q16 */
    bool consistent;               /* sum and f bounds hold within rounding */
} ct_decomp_t;

/* Returns 0, 1 when r or a is all zeros (d is zeroed), or -1 for bad
 * arguments (n_cat 0 or > CT_MAX_CATEGORIES, a category id out of range,
 * n > 2^24, N < 2). N is the ISF block count (L = N - 1). */
int32_t ct_decompose(const zt_fx *ref, const zt_fx *acc, uint32_t n, const uint8_t *cat_of,
                     uint32_t n_cat, uint32_t N, ct_decomp_t *d);

/* |a| / |r| in Q16, saturating at INT32_MAX; 0 when r is zero. */
zt_fx ct_norm_ratio(const zt_fx *ref, const zt_fx *acc, uint32_t n);

/* ---- drift check and verdict (R3, R4) ---- */

enum { CT_TRUE = 0, CT_FALSE, CT_GLUT, CT_NEUTRAL, CT_PARADOX, CT_UNKNOWN, CT_N_TRUTH };

typedef struct {
    uint32_t N;      /* ISF block count, L = N - 1 */
    zt_fx tau_cross; /* bound on L * u_cross, Q16 F units */
    zt_fx tau_div;   /* bound on L * u_div */
    zt_fx tol_mag;   /* | |a|/|r| - 1 | <= tol_mag, Q16 */
} ct_drift_cfg_t;

typedef struct {
    uint64_t samples;
    uint64_t truth[CT_N_TRUTH];
    uint64_t cross_drift, div_drift, mag_drift, faulted, inconsistent;
    zt_fx worst_cross, worst_div; /* largest L * u seen, Q16 (saturating) */
} ct_drift_stats_t;

/* One sample: ref and acc (n values, already in Q16), with `faults` the
 * number of NaN / Inf elements ct_ingest reported for acc's row. d (may be
 * null) receives the decomposition. st (may be null) is added to. Returns
 * the verdict CT_TRUE..CT_UNKNOWN; never fails (bad arguments: PARADOX). */
uint32_t ct_drift_check(const ct_drift_cfg_t *cfg, const zt_fx *ref, const zt_fx *acc, uint32_t n,
                        const uint8_t *cat_of, uint32_t n_cat, uint64_t faults,
                        ct_drift_stats_t *st, ct_decomp_t *d);

/* Deterministic sampling of rows to re-check on the integer engine:
 * true for about rate/65536 of the indices. */
bool ct_sample(uint64_t seed, uint64_t index, uint32_t rate_q16);

#endif /* CT_RECONCILE_H */
