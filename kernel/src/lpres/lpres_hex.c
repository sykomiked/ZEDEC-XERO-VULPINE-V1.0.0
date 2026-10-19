/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lpres_hex.c — the 64-state hexagram tag. See lpres_hex.h (X1-X6). */
#include "lpres_hex.h"
#include "../robin_debanks/sha256.h"

#define A_P LPX_ACT_PROCEED
#define A_W LPX_ACT_WAIT
#define A_R LPX_ACT_REFUSE
#define A_E LPX_ACT_ESCALATE
#define A_X LPX_ACT_ROLLBACK

/* X2: index = the tag. Rows by base (False, Neither, Both, True); in each
 * row four entries per polarity (entropic, neutral, forbidden,
 * negentropic), one per phase. */
static const uint8_t ACTION[LPX_STATES] = {
    A_R, A_R, A_R, A_R, A_R, A_R, A_R, A_R, A_X, A_X, A_X, A_X, A_R, A_R, A_R, A_R,
    A_W, A_W, A_W, A_W, A_W, A_W, A_W, A_W, A_X, A_X, A_X, A_X, A_W, A_W, A_W, A_W,
    A_E, A_E, A_E, A_E, A_E, A_E, A_E, A_E, A_X, A_X, A_X, A_X, A_W, A_W, A_W, A_W,
    A_W, A_W, A_W, A_W, A_P, A_P, A_P, A_P, A_X, A_X, A_X, A_X, A_P, A_P, A_P, A_P,
};

lpx_action_t lpx_action(lpx_t t)
{
    return (lpx_action_t) ACTION[t & (LPX_STATES - 1u)];
}

uint8_t lpx_base_from_lpres(lpres_state_t s)
{
    switch (s) {
    case LPRES_STATE_TRUE:
        return LPX_B_TRUE;
    case LPRES_STATE_FALSE:
        return LPX_B_FALSE;
    case LPRES_STATE_BOTH:
        return LPX_B_BOTH;
    default:
        return LPX_B_NEITHER;
    }
}

lpres_state_t lpx_base_to_lpres(uint8_t base)
{
    static const lpres_state_t m[4] = {LPRES_STATE_FALSE, LPRES_STATE_NEITHER, LPRES_STATE_BOTH,
                                       LPRES_STATE_TRUE};
    return m[base & 3u];
}

/* X3: potential 00 -> emerging 01 -> manifest 11 -> dissolving 10 -> 00 */
uint8_t lpx_phase_next(uint8_t phase)
{
    static const uint8_t next[4] = {LPX_PH_EMERGING, LPX_PH_MANIFEST, LPX_PH_POTENTIAL,
                                    LPX_PH_DISSOLVING};
    return next[phase & 3u];
}

bool lpx_advance(lpx_t *t, uint8_t phase)
{
    if (!t || phase > 3u) return false;
    uint8_t cur = lpx_phase(*t);
    if (phase != cur && phase != lpx_phase_next(cur)) return false;
    *t = lpx_make(lpx_base(*t), lpx_pol(*t), phase);
    return true;
}

lpx_t lpx_set(lpx_t t, uint8_t base, uint8_t pol)
{
    if (lpx_forbidden(t)) pol = LPX_P_FORBIDDEN; /* X4 */
    return lpx_make(base, pol, lpx_phase(t));
}

uint8_t lpx_polarity(uint32_t pos0, uint32_t neg0, uint32_t pos1, uint32_t neg1, bool stale)
{
    if (stale) return LPX_P_ENTROPIC;
    uint32_t m0 = pos0 > neg0 ? pos0 - neg0 : neg0 - pos0;
    uint32_t m1 = pos1 > neg1 ? pos1 - neg1 : neg1 - pos1;
    if (m1 > m0) return LPX_P_NEGENTROPIC;
    if (m1 < m0) return LPX_P_ENTROPIC;
    return LPX_P_NEUTRAL;
}

/* X5: rank by badness, forbidden worst */
static uint8_t pol_rank(uint8_t p)
{
    static const uint8_t r[4] = {2u /* entropic */, 1u /* neutral */, 3u /* forbidden */,
                                 0u /* negentropic */};
    return r[p & 3u];
}

static uint8_t worse_pol(uint8_t a, uint8_t b)
{
    return pol_rank(a) >= pol_rank(b) ? a : b;
}

/* least advanced phase on the cycle's order */
static uint8_t phase_rank(uint8_t p)
{
    static const uint8_t r[4] = {0u /* potential */, 1u /* emerging */, 3u /* dissolving */,
                                 2u /* manifest */};
    return r[p & 3u];
}

static uint8_t earlier_phase(uint8_t a, uint8_t b)
{
    return phase_rank(a) <= phase_rank(b) ? a : b;
}

lpx_t lpx_and(lpx_t a, lpx_t b)
{
    lpres_state_t s = lpres_conjoin(lpx_base_to_lpres(lpx_base(a)), lpx_base_to_lpres(lpx_base(b)));
    return lpx_make(lpx_base_from_lpres(s), worse_pol(lpx_pol(a), lpx_pol(b)),
                    earlier_phase(lpx_phase(a), lpx_phase(b)));
}

lpx_t lpx_or(lpx_t a, lpx_t b)
{
    lpres_state_t s = lpres_disjoin(lpx_base_to_lpres(lpx_base(a)), lpx_base_to_lpres(lpx_base(b)));
    return lpx_make(lpx_base_from_lpres(s), worse_pol(lpx_pol(a), lpx_pol(b)),
                    earlier_phase(lpx_phase(a), lpx_phase(b)));
}

lpx_t lpx_not(lpx_t a)
{
    lpres_state_t s = lpres_negate(lpx_base_to_lpres(lpx_base(a)));
    return lpx_make(lpx_base_from_lpres(s), lpx_pol(a), lpx_phase(a));
}

const char *lpx_base_name(uint8_t base)
{
    static const char *const n[4] = {"false", "neither", "both", "true"};
    return n[base & 3u];
}

const char *lpx_pol_name(uint8_t pol)
{
    static const char *const n[4] = {"entropic", "neutral", "forbidden", "negentropic"};
    return n[pol & 3u];
}

const char *lpx_phase_name(uint8_t phase)
{
    static const char *const n[4] = {"potential", "emerging", "dissolving", "manifest"};
    return n[phase & 3u];
}

const char *lpx_action_name(lpx_action_t a)
{
    static const char *const n[5] = {"proceed", "wait", "refuse", "escalate", "rollback"};
    return (unsigned) a < 5u ? n[a] : "?";
}

/* ---- X6 ---- */
void lpx_qlog_init(lpx_qlog_t *q, lpx_qrec_t *storage, uint32_t cap)
{
    if (!q) return;
    q->rec = storage;
    q->cap = storage ? cap : 0;
    q->n = 0;
    for (uint32_t i = 0; i < LPX_QLOG_HASH; i++) q->head[i] = 0;
}

/* SHA-256 over "zxv/lpx-qlog/v1" || seq || tag || reason || submitter ||
 * subject || prev */
static void rec_hash(const lpx_qrec_t *r, uint8_t out[LPX_QLOG_HASH])
{
    static const char tag[] = "zxv/lpx-qlog/v1";
    uint8_t b[sizeof tag - 1 + 8 + 1 + 1 + 4 + 32 + LPX_QLOG_HASH];
    uint32_t k = 0;
    for (uint32_t i = 0; i < sizeof tag - 1; i++) b[k++] = (uint8_t) tag[i];
    for (uint32_t i = 0; i < 8; i++) b[k++] = (uint8_t) (r->seq >> (8 * i));
    b[k++] = r->tag;
    b[k++] = r->reason;
    for (uint32_t i = 0; i < 4; i++) b[k++] = (uint8_t) (r->submitter >> (8 * i));
    for (uint32_t i = 0; i < 32; i++) b[k++] = r->subject[i];
    for (uint32_t i = 0; i < LPX_QLOG_HASH; i++) b[k++] = r->prev[i];
    sha256(b, k, out);
}

bool lpx_qlog_append(lpx_qlog_t *q, lpx_t tag, uint8_t reason, uint32_t submitter,
                     const uint8_t subject[32])
{
    if (!q || !subject || !lpx_forbidden(tag) || tag >= LPX_STATES || q->n >= q->cap) return false;
    lpx_qrec_t *r = &q->rec[q->n];
    r->seq = q->n;
    r->tag = tag;
    r->reason = reason;
    r->submitter = submitter;
    for (uint32_t i = 0; i < 32; i++) r->subject[i] = subject[i];
    for (uint32_t i = 0; i < LPX_QLOG_HASH; i++) r->prev[i] = q->head[i];
    rec_hash(r, r->hash);
    for (uint32_t i = 0; i < LPX_QLOG_HASH; i++) q->head[i] = r->hash[i];
    q->n++;
    return true;
}

bool lpx_qlog_verify(const lpx_qlog_t *q)
{
    if (!q) return false;
    uint8_t prev[LPX_QLOG_HASH] = {0}, h[LPX_QLOG_HASH];
    for (uint32_t k = 0; k < q->n; k++) {
        const lpx_qrec_t *r = &q->rec[k];
        if (r->seq != k || !lpx_forbidden(r->tag) || r->tag >= LPX_STATES) return false;
        for (uint32_t i = 0; i < LPX_QLOG_HASH; i++)
            if (r->prev[i] != prev[i]) return false;
        rec_hash(r, h);
        for (uint32_t i = 0; i < LPX_QLOG_HASH; i++) {
            if (h[i] != r->hash[i]) return false;
            prev[i] = h[i];
        }
    }
    for (uint32_t i = 0; i < LPX_QLOG_HASH; i++)
        if (q->head[i] != prev[i]) return false;
    return true;
}
