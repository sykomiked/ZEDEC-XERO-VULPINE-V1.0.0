/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vhub.c — V-Bill hub clearing. See vhub.h H1-H6. */
#include "vhub.h"
#include "../robin_debanks/sha256.h"

void vhub_init(vhub_t *h, pay_ledger_t *L, uint16_t vbill_asset, vhub_verify_fn verify, void *vctx,
               const uint8_t seed[32], lpx_qrec_t *qstore, uint32_t qcap)
{
    if (!h) return;
    pay_memset(h, 0, sizeof *h);
    h->L = L;
    h->vbill = vbill_asset;
    h->verify = verify;
    h->vctx = vctx;
    if (seed) pay_memcpy(h->seed, seed, 32);
    lpx_qlog_init(&h->qlog, qstore, qcap);
}

static vhub_ccy_t *slot(vhub_t *h, uint8_t c)
{
    return (h && c < VHUB_MAX_CCY && h->ccy[c].used) ? &h->ccy[c] : 0;
}

vhub_status_t vhub_add_ccy(vhub_t *h, uint8_t c, uint16_t asset, uint32_t issuer, uint32_t pool,
                           uint32_t vfloat)
{
    if (!h || c >= VHUB_MAX_CCY || h->ccy[c].used) return VHUB_ERR_ARG;
    vhub_ccy_t *x = &h->ccy[c];
    pay_memset(x, 0, sizeof *x);
    x->used = true;
    x->asset = asset;
    x->issuer = issuer;
    x->pool = pool;
    x->vfloat = vfloat;
    return VHUB_OK;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void vhub_peg_msg(uint8_t c, uint64_t num, uint64_t den, uint64_t epoch, uint8_t out[VHUB_PEG_MSG])
{
    static const char tag[] = "zxv/vhub-peg/v1";
    for (uint32_t i = 0; i < 15; i++) out[i] = (uint8_t) tag[i];
    out[15] = c;
    put64(out + 16, num);
    put64(out + 24, den);
    put64(out + 32, epoch);
}

vhub_status_t vhub_set_peg(vhub_t *h, uint8_t c, uint64_t num, uint64_t den, uint64_t epoch,
                           const uint8_t *sig, uint32_t sig_len)
{
    vhub_ccy_t *x = slot(h, c);
    if (!x || num == 0 || den == 0 || !sig || !h->verify) return VHUB_ERR_ARG;
    if (epoch <= x->peg_epoch) return VHUB_ERR_EPOCH;
    uint8_t m[VHUB_PEG_MSG];
    vhub_peg_msg(c, num, den, epoch, m);
    if (!h->verify(h->vctx, x->issuer, m, sizeof m, sig, sig_len)) return VHUB_ERR_SIG;
    x->num = num;
    x->den = den;
    x->peg_epoch = epoch;
    return VHUB_OK;
}

vhub_status_t vhub_add_rule(vhub_t *h, uint8_t c, vhub_rule_fn fn, vhub_commit_fn commit, void *ctx)
{
    vhub_ccy_t *x = slot(h, c);
    if (!x || !fn) return VHUB_ERR_ARG;
    if (x->n_rules >= VHUB_MAX_RULES) return VHUB_ERR_FULL;
    x->rule[x->n_rules++] = (vhub_rule_t){fn, commit, ctx};
    return VHUB_OK;
}

/* H2: v = a*denA/numA and b = v*numB/denB, both exact */
static bool convert(const vhub_ccy_t *A, const vhub_ccy_t *B, uint64_t a, uint64_t *v, uint64_t *b)
{
    uint64_t r1, r2;
    if (!pay_muldiv(a, A->den, A->num, v, &r1) || r1) return false;
    if (!pay_muldiv(*v, B->num, B->den, b, &r2) || r2) return false;
    return true;
}

static bool pegged(const vhub_ccy_t *x)
{
    return x && x->num && x->den;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

uint64_t vhub_quote(const vhub_t *h, uint8_t a, uint8_t b, uint64_t want)
{
    const vhub_ccy_t *A = slot((vhub_t *) h, a), *B = slot((vhub_t *) h, b);
    if (!pegged(A) || !pegged(B) || a == b) return 0;
    /* v = a*denA/numA is exact iff a is a multiple of s1 = numA/g, g =
     * gcd(numA, denA); then v = k*u with k = a/s1, u = denA/g, and
     * v*numB/denB is exact iff k is a multiple of s2 = denB with every
     * factor shared with u and numB divided out. */
    uint64_t g = gcd64(A->num, A->den), s1 = A->num / g, u = A->den / g;
    uint64_t s2 = B->den;
    s2 /= gcd64(s2, u);
    s2 /= gcd64(s2, B->num);
    uint64_t step, rem;
    if (!pay_muldiv(s1, s2, 1, &step, &rem) || step == 0) return 0;
    uint64_t q = want / step * step, v, out;
    return (q && convert(A, B, q, &v, &out)) ? q : 0;
}

/* P4-style identifiers: SHA-256(seed || tag || seq) */
static void derive(const vhub_t *h, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, h->seed, 32);
    in[32] = tag;
    put64(in + 33, h->seq);
    sha256(in, sizeof in, out);
}

/* forbidden > refused > escalate > wait */
static int severity(vhub_status_t st)
{
    switch (st) {
    case VHUB_FORBIDDEN:
        return 4;
    case VHUB_REFUSED:
        return 3;
    case VHUB_ESCALATE:
        return 2;
    case VHUB_WAIT:
        return 1;
    default:
        return 0;
    }
}

/* H5: worst action over every rule of both currencies */
static vhub_status_t judge(vhub_t *h, vhub_ccy_t *A, vhub_ccy_t *B, const vhub_swap_t *sw)
{
    vhub_status_t worst = VHUB_OK;
    h->last_tag = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    vhub_ccy_t *side[2] = {A, B};
    for (uint8_t s = 0; s < 2; s++) {
        uint32_t n = side[s]->n_rules, runs = n ? n : 1;
        for (uint32_t i = 0; i < runs; i++) {
            /* no rule for this currency: neither (wait), fail closed */
            lpx_t t = n ? side[s]->rule[i].fn(side[s]->rule[i].ctx, sw, s)
                        : lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
            vhub_status_t st;
            switch (lpx_action(t)) {
            case LPX_ACT_PROCEED:
                continue;
            case LPX_ACT_ROLLBACK:
                st = VHUB_FORBIDDEN;
                break;
            case LPX_ACT_ESCALATE:
                st = VHUB_ESCALATE;
                break;
            case LPX_ACT_REFUSE:
                st = VHUB_REFUSED;
                break;
            default:
                st = VHUB_WAIT;
                break;
            }
            if (severity(st) > severity(worst)) {
                worst = st;
                h->last_tag = t;
            }
        }
    }
    return worst;
}

vhub_status_t vhub_swap(vhub_t *h, vhub_swap_t *sw, uint64_t tick)
{
    if (!h || !sw || !h->L) return VHUB_ERR_ARG;
    if (h->qlog.cap && h->qlog.n >= h->qlog.cap) return VHUB_HALTED;
    vhub_ccy_t *A = slot(h, sw->from_ccy), *B = slot(h, sw->to_ccy);
    if (!pegged(A) || !pegged(B) || A == B || sw->amount == 0 || sw->amount >= PAY_BAL_MAX)
        return VHUB_ERR_ARG;
    if (!convert(A, B, sw->amount, &sw->vbills, &sw->out) || sw->vbills == 0 || sw->out == 0 ||
        sw->vbills >= PAY_BAL_MAX || sw->out >= PAY_BAL_MAX)
        return VHUB_ERR_INEXACT;
    sw->epoch = h->epoch;

    vhub_status_t j = judge(h, A, B, sw);
    if (j == VHUB_FORBIDDEN) {
        uint8_t subject[32], m[8 + 8 + 4 + 4 + 2];
        put64(m, sw->amount);
        put64(m + 8, sw->epoch);
        for (int i = 0; i < 4; i++) {
            m[16 + i] = (uint8_t) (sw->sender >> (8 * i));
            m[20 + i] = (uint8_t) (sw->receiver >> (8 * i));
        }
        m[24] = sw->from_ccy;
        m[25] = sw->to_ccy;
        sha256(m, sizeof m, subject);
        if (!lpx_qlog_append(&h->qlog, lpx_forbid(h->last_tag), 1, sw->sender, subject))
            return VHUB_HALTED;
        return VHUB_FORBIDDEN;
    }
    if (j != VHUB_OK) return j;

    /* H3 */
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    const pay_line_t ln[6] = {
        {sw->sender, -(int64_t) sw->amount, 0}, {A->pool, (int64_t) sw->amount, 0},
        {A->vfloat, -(int64_t) sw->vbills, 0},  {B->vfloat, (int64_t) sw->vbills, 0},
        {B->pool, -(int64_t) sw->out, 0},       {sw->receiver, (int64_t) sw->out, 0}};
    for (int i = 0; i < 6; i++) r.lines[i] = ln[i];
    r.n_lines = 6;
    uint8_t d[32];
    derive(h, 'I', r.idem_key);
    derive(h, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "VHUB-");
    pay_w_u64(&w, h->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "v-bill hub swap", sizeof r.memo);
    r.tick = tick;
    r.kind = PAY_KIND_TRADE;
    r.ext_cap = PAY_CAP_FINANCIAL;
    h->seq++;
    pay_receipt_t rc;
    h->last_ledger = pay_ledger_post(h->L, &r, &rc);
    if (h->last_ledger != PAY_OK) return VHUB_ERR_LEDGER;
    h->posted++;
    vhub_ccy_t *side[2] = {A, B};
    for (uint8_t s = 0; s < 2; s++)
        for (uint32_t i = 0; i < side[s]->n_rules; i++)
            if (side[s]->rule[i].commit) side[s]->rule[i].commit(side[s]->rule[i].ctx, sw, s);
    return VHUB_OK;
}

/* ---- H6 ---- */
static int limit_find(vhub_limit_t *l, const vhub_swap_t *sw, bool add)
{
    if (l->epoch != sw->epoch) {
        l->epoch = sw->epoch;
        l->n = 0;
    }
    for (uint32_t i = 0; i < l->n; i++)
        if (l->sender[i] == sw->sender) return (int) i;
    if (!add || l->n >= VHUB_LIMIT_SENDERS) return -1;
    l->sender[l->n] = sw->sender;
    l->used[l->n] = 0;
    return (int) l->n++;
}

lpx_t vhub_rule_limit(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    vhub_limit_t *l = ctx;
    if (!l || !sw) return lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    if (side != 0) return lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    int i = limit_find(l, sw, false);
    if (i < 0 && l->n >= VHUB_LIMIT_SENDERS) /* cannot track one more: refuse */
        return lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    uint64_t used = i < 0 ? 0 : l->used[i];
    if (sw->amount > l->limit || used > l->limit - sw->amount)
        return lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    return lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

void vhub_limit_commit(void *ctx, const vhub_swap_t *sw, uint8_t side)
{
    vhub_limit_t *l = ctx;
    if (!l || !sw || side != 0) return;
    int i = limit_find(l, sw, true);
    if (i >= 0) l->used[i] += sw->amount;
}
