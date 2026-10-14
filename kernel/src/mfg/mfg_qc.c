/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_qc.c — F.4 four-valued, guard-banded inspection (see mfg_qc.h).
 *
 * WHAT IT DOES
 *   Turns gauge readings with uncertainty into a four-valued decision per
 *   feature and a pass / reject / hold verdict per part.
 *
 * HONEST LIMITS
 *   Trusts the caller's uncertainty figures; reads no instruments.
 */
#include "mfg_qc.h"

static int mfg_qc_in(int64_t v)
{
    return v >= -MFG_QC_VALUE_MAX && v <= MFG_QC_VALUE_MAX;
}

mfg_status_t mfg_qc_gauge(const mfg_qc_gauge_t *g, int64_t lo, int64_t hi, mfg_tv_t *out)
{
    int64_t a, b;
    if (!g || !out) return MFG_ERR_NULL;
    if (!mfg_qc_in(lo) || !mfg_qc_in(hi) || lo > hi) return MFG_ERR_RANGE;
    if (!g->valid) {
        *out = MFG_NEITHER;
        return MFG_OK;
    }
    if (!mfg_qc_in(g->value) || g->uncertainty < 0 || g->uncertainty > MFG_QC_VALUE_MAX)
        return MFG_ERR_RANGE;
    a = g->value - g->uncertainty; /* bounded: no overflow */
    b = g->value + g->uncertainty;
    if (a >= lo && b <= hi)
        *out = MFG_TRUE;
    else if (b < lo || a > hi)
        *out = MFG_FALSE;
    else
        *out = MFG_BOTH;
    return MFG_OK;
}

mfg_status_t mfg_qc_feature(const mfg_qc_feature_t *f, mfg_qc_feature_result_t *out)
{
    int64_t lo, hi;
    uint32_t i;
    if (!f || !out) return MFG_ERR_NULL;
    if (f->ngauge > MFG_QC_MAX_GAUGES) return MFG_ERR_CAPACITY;
    if (!mfg_qc_in(f->nominal) || f->tol_minus < 0 || f->tol_plus < 0 ||
        f->tol_minus > MFG_QC_VALUE_MAX || f->tol_plus > MFG_QC_VALUE_MAX)
        return MFG_ERR_RANGE;
    lo = f->nominal - f->tol_minus;
    hi = f->nominal + f->tol_plus;
    if (!mfg_qc_in(lo) || !mfg_qc_in(hi)) return MFG_ERR_RANGE;
    out->n_in = out->n_out = out->n_ambiguous = out->n_invalid = 0;
    for (i = 0; i < f->ngauge; i++) {
        mfg_tv_t t;
        mfg_status_t s = mfg_qc_gauge(&f->gauge[i], lo, hi, &t);
        if (s != MFG_OK) return s;
        if (t == MFG_TRUE)
            out->n_in++;
        else if (t == MFG_FALSE)
            out->n_out++;
        else if (t == MFG_BOTH)
            out->n_ambiguous++;
        else
            out->n_invalid++;
    }
    if (out->n_in + out->n_out + out->n_ambiguous == 0)
        out->state = MFG_NEITHER;
    else if (out->n_out == 0 && out->n_ambiguous == 0)
        out->state = MFG_TRUE;
    else if (out->n_in == 0 && out->n_ambiguous == 0)
        out->state = MFG_FALSE;
    else
        out->state = MFG_BOTH;
    return MFG_OK;
}

mfg_status_t mfg_qc_part(const mfg_qc_feature_t *f, uint32_t n, mfg_qc_feature_result_t *results,
                         mfg_tv_t *part_state, mfg_verdict_t *verdict)
{
    uint32_t i;
    int any_false = 0, any_both = 0, any_neither = 0;
    if (!f || !part_state || !verdict) return MFG_ERR_NULL;
    if (n == 0 || n > MFG_QC_MAX_FEATURES) return MFG_ERR_RANGE;
    for (i = 0; i < n; i++) {
        mfg_qc_feature_result_t r;
        mfg_status_t s = mfg_qc_feature(&f[i], &r);
        if (s != MFG_OK) return s;
        if (results) results[i] = r;
        if (r.state == MFG_FALSE)
            any_false = 1;
        else if (r.state == MFG_BOTH)
            any_both = 1;
        else if (r.state == MFG_NEITHER)
            any_neither = 1;
    }
    if (any_false) {
        *part_state = MFG_FALSE;
        *verdict = MFG_REJECT;
    } else if (any_both || any_neither) {
        /* BOTH if any conflict, NEITHER if only missing evidence */
        *part_state = any_both ? MFG_BOTH : MFG_NEITHER;
        *verdict = MFG_HOLD;
    } else {
        *part_state = MFG_TRUE;
        *verdict = MFG_RELEASE_OK;
    }
    return MFG_OK;
}
