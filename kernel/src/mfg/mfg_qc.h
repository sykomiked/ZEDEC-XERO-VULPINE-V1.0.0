/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_qc.h — F.4 four-valued quality inspection with guard bands.
 *
 * WHAT IT DOES
 *   A part has features (a bore diameter, a length ...). Each feature has a
 *   nominal value and a tolerance band [nominal - tol_minus, nominal +
 *   tol_plus], and is measured by one or more gauges (caliper, CMM, vision).
 *   Each gauge reports a value v and its measurement uncertainty u, in
 *   integer micrometres. The decision is guard-banded:
 *     gauge IN        (TRUE)  only if the whole interval [v-u, v+u] is
 *                             inside the closed band
 *     gauge OUT       (FALSE) only if [v-u, v+u] is entirely outside it
 *     gauge AMBIGUOUS (BOTH)  otherwise (the interval straddles a limit,
 *                             including touching it from outside)
 *     gauge invalid   (NEITHER) the caller marked the reading not valid
 *   Combining the valid gauges of one feature:
 *     all IN -> TRUE (pass); all OUT -> FALSE (reject);
 *     any disagreement or any ambiguous gauge -> BOTH (hold: re-measure or
 *     a person decides); no valid gauge -> NEITHER (hold).
 *   A part PASSES only if every feature is TRUE. Any FALSE feature rejects
 *   it; otherwise any BOTH or NEITHER holds it. A part is never passed by a
 *   guess, a vote or an average.
 *
 * HONEST LIMITS
 *   The decision is only as good as the uncertainty the caller supplies:
 *   this code does not estimate gauge uncertainty, calibration state or
 *   repeatability. It does not read gauges or cameras itself. It is
 *   designed to support an inspection process, not to certify a part.
 *
 * Freestanding, integer-only, bounded, no allocation, no libc.
 */
#ifndef ZXV_MFG_QC_H
#define ZXV_MFG_QC_H

#include "mfg_common.h"

#define MFG_QC_MAX_GAUGES   8u
#define MFG_QC_MAX_FEATURES 64u
#define MFG_QC_VALUE_MAX    1000000000000LL /* +-1e12 um = +-1000 km */

typedef struct {
    int64_t value;       /* measured, um */
    int64_t uncertainty; /* expanded uncertainty, um, >= 0 */
    uint8_t valid;       /* 0: reading not usable (gauge fault, no reading) */
} mfg_qc_gauge_t;

typedef struct {
    int64_t nominal;   /* um */
    int64_t tol_minus; /* >= 0 */
    int64_t tol_plus;  /* >= 0 */
    uint32_t ngauge;   /* <= MFG_QC_MAX_GAUGES */
    mfg_qc_gauge_t gauge[MFG_QC_MAX_GAUGES];
} mfg_qc_feature_t;

typedef struct {
    uint8_t state; /* mfg_tv_t for the feature */
    uint8_t n_in, n_out, n_ambiguous, n_invalid;
} mfg_qc_feature_result_t;

/* Decide one gauge against the band [lo, hi]. Returns MFG_OK and writes
 * *out (TRUE in, FALSE out, BOTH ambiguous, NEITHER if !g->valid). */
mfg_status_t mfg_qc_gauge(const mfg_qc_gauge_t *g, int64_t lo, int64_t hi, mfg_tv_t *out);

/* Decide one feature. */
mfg_status_t mfg_qc_feature(const mfg_qc_feature_t *f, mfg_qc_feature_result_t *out);

/* Decide a part from n features (1..MFG_QC_MAX_FEATURES). results may be
 * NULL; if not, it receives n entries. *part_state: TRUE pass, FALSE
 * reject, BOTH/NEITHER hold. *verdict: RELEASE_OK / REJECT / HOLD. */
mfg_status_t mfg_qc_part(const mfg_qc_feature_t *f, uint32_t n, mfg_qc_feature_result_t *results,
                         mfg_tv_t *part_state, mfg_verdict_t *verdict);

#endif /* ZXV_MFG_QC_H */
