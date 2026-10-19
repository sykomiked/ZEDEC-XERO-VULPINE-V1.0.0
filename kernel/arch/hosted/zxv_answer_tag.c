/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_answer_tag.c — see zxv_answer_tag.h (T1-T4). */
#include "zxv_answer_tag.h"

static uint32_t clamp32(uint64_t x)
{
    return x > UINT32_MAX ? UINT32_MAX : (uint32_t) x;
}

/* T3 */
static uint8_t trend(const zxv_zt_verdict_t *v)
{
    if (v->n_first == 0 || v->n_second == 0) return LPX_P_NEUTRAL;
    uint32_t m0 = clamp32(v->margin_first / v->n_first);
    uint32_t m1 = clamp32(v->margin_second / v->n_second);
    uint32_t d = m1 > m0 ? m1 - m0 : m0 - m1;
    if ((uint64_t) d * 8u <= m0) return LPX_P_NEUTRAL;
    return lpx_polarity(m0, 0, m1, 0, false);
}

lpx_t zxv_answer_tag(const zxv_zt_verdict_t *v, uint8_t *reason)
{
    uint8_t why = ZXV_TAG_R_NONE;
    lpx_t t = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    if (!v) {
        if (reason) *reason = ZXV_TAG_R_FORWARD;
        return lpx_forbid(t);
    }
    if (v->forward_error || v->n_sat) { /* T2 */
        why = v->forward_error ? ZXV_TAG_R_FORWARD : ZXV_TAG_R_SATURATED;
        lpx_advance(&t, LPX_PH_EMERGING);
        t = lpx_forbid(t);
    } else if (v->n_gen) { /* T4: potential -> emerging -> manifest */
        t = lpx_set(t, LPX_B_TRUE, trend(v));
        lpx_advance(&t, LPX_PH_EMERGING);
        lpx_advance(&t, LPX_PH_MANIFEST);
    }
    if (reason) *reason = why;
    return t;
}
