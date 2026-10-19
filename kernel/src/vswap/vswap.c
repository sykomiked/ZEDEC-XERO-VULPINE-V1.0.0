/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vswap.c — the Player Card swap, lock tiers and dividend. See vswap.h. */
#include "vswap.h"
#include "../robin_debanks/sha256.h"

#define LOW  VSWAP_TIER_LOW
#define MED  VSWAP_TIER_MEDIUM
#define DEEP VSWAP_TIER_DEEP

void vswap_cfg_default(vswap_cfg_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->vault = VSWAP_NONE;
    for (uint32_t t = 0; t < VSWAP_TIERS; t++) {
        c->lock[t] = VSWAP_NONE;
        c->weight[t] = t + 1; /* 1, 2, 3 */
    }
    c->par_num = 1;
    c->par_den = 1;
    c->k_medium = 12; /* 144 ticks */
    c->k_deep = 20;   /* 6765 ticks */
}

uint64_t vswap_fib(uint32_t k)
{
    if (k < VSWAP_FIB_KMIN || k > VSWAP_FIB_KMAX) return 0;
    uint64_t a = 0, b = 1;
    for (uint32_t i = 1; i < k; i++) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return b;
}

static const pay_account_t *of(const vswap_t *s, uint32_t a, uint16_t asset)
{
    const pay_account_t *x = pay_ledger_account(s->L, a);
    return (x && x->asset == asset) ? x : 0;
}

static bool is_lock(const vswap_t *s, uint32_t a)
{
    return a == s->cfg.lock[MED] || a == s->cfg.lock[DEEP];
}

vswap_status_t vswap_init(vswap_t *s, pay_ledger_t *L, const vswap_cfg_t *c)
{
    if (!s || !L || !c) return VSWAP_ERR_ARG;
    pay_memset(s, 0, sizeof *s);
    s->L = L;
    s->cfg = *c;
    s->cfg.lock[LOW] = VSWAP_NONE;
    if (!c->verify || !c->par_num || !c->par_den || c->vino == c->vbill) return VSWAP_ERR_ARG;
    const pay_account_t *is = of(s, c->issuer, c->vbill);
    if (!of(s, c->reserve, c->vino) || !is || !(is->flags & PAY_ACCT_ISSUER)) return VSWAP_ERR_ARG;
    if (c->vault != VSWAP_NONE && (!of(s, c->vault, c->vbill) || c->vault == c->issuer))
        return VSWAP_ERR_ARG;
    for (uint32_t t = MED; t <= DEEP; t++) {
        const pay_account_t *lk = of(s, c->lock[t], c->vbill);
        if (!lk || c->lock[t] == c->issuer || c->lock[t] == c->vault) return VSWAP_ERR_ARG;
        if (lk->debit) return VSWAP_ERR_STATE; /* T4 needs empty lock accounts */
    }
    if (c->lock[MED] == c->lock[DEEP]) return VSWAP_ERR_ARG;
    for (uint32_t t = 0; t < VSWAP_TIERS; t++)
        if (c->weight[t] < 1 || c->weight[t] > VSWAP_WEIGHT_MAX) return VSWAP_ERR_ARG;
    s->period[MED] = vswap_fib(c->k_medium);
    s->period[DEEP] = vswap_fib(c->k_deep);
    if (!s->period[MED] || s->period[DEEP] <= s->period[MED]) return VSWAP_ERR_ARG;
    return VSWAP_OK;
}

/* ---- W1 ---- */
static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void vswap_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VSWAP_SWITCH_MSG])
{
    static const char tag[] = "zxv/vswap-sw/v1";
    for (uint32_t i = 0; i < 15; i++) out[i] = (uint8_t) tag[i];
    for (int i = 0; i < 4; i++) out[15 + i] = (uint8_t) (juris >> (8 * i));
    out[19] = enable ? 1 : 0;
    put64(out + 20, epoch);
}

vswap_status_t vswap_enable(vswap_t *s, uint32_t juris, bool enable, uint64_t epoch,
                            const uint8_t *sig, uint32_t sig_len)
{
    if (!s || !s->L || !sig) return VSWAP_ERR_ARG;
    if (juris != s->cfg.juris) return VSWAP_ERR_JURIS;
    if (epoch <= s->switch_epoch) return VSWAP_ERR_EPOCH;
    uint8_t m[VSWAP_SWITCH_MSG];
    vswap_switch_msg(juris, enable, epoch, m);
    if (!s->cfg.verify(s->cfg.vctx, s->cfg.gateway, m, sizeof m, sig, sig_len))
        return VSWAP_ERR_SIG;
    s->enabled = enable;
    s->switch_epoch = epoch;
    return VSWAP_OK;
}

/* ---- W2 ---- */
static vswap_status_t base(const vswap_t *s, uint64_t *R, uint64_t *B)
{
    const pay_account_t *r = of(s, s->cfg.reserve, s->cfg.vino);
    const pay_account_t *n = of(s, s->cfg.issuer, s->cfg.vbill);
    const pay_account_t *v = s->cfg.vault == VSWAP_NONE ? 0 : of(s, s->cfg.vault, s->cfg.vbill);
    if (!r || !n) return VSWAP_ERR_STATE;
    uint64_t h = v ? v->debit : 0;
    if (h > n->credit) return VSWAP_ERR_STATE;
    *R = r->debit;
    *B = n->credit - h;
    return VSWAP_OK;
}

vswap_status_t vswap_rate(const vswap_t *s, vswap_quote_t *q)
{
    uint64_t R, B;
    if (!s || !s->L || !q) return VSWAP_ERR_ARG;
    pay_memset(q, 0, sizeof *q);
    vswap_status_t st = base(s, &R, &B);
    if (st != VSWAP_OK) return st;
    /* B == 0 with R > 0: no outside holder, yet the reserve holds value.
     * Any price lets the first buyer redeem the whole reserve (W4 pays
     * floor(v * R / B) with B == v); the ratio claim a' * B >= v * R is
     * false. Fail closed. */
    if (B == 0 && R > 0) return VSWAP_ERR_STATE;
    q->par = (B == 0 || R == 0);
    q->num = q->par ? s->cfg.par_num : R;
    q->den = q->par ? s->cfg.par_den : B;
    return VSWAP_OK;
}

/* W3 at rate num/den: v = floor(a*den/num), a' = ceil(v*num/den) */
static vswap_status_t buy(uint64_t num, uint64_t den, uint64_t a, uint64_t *v, uint64_t *cost)
{
    uint64_t c, r;
    if (!pay_muldiv(a, den, num, v, 0) || *v > VSWAP_LINE_MAX) return VSWAP_ERR_OVERFLOW;
    if (*v == 0) return VSWAP_ERR_DUST;
    pay_muldiv(*v, num, den, &c, &r); /* <= a: cannot fail */
    *cost = c + (r != 0);
    return VSWAP_OK;
}

vswap_status_t vswap_quote_in(const vswap_t *s, uint64_t a, vswap_quote_t *q)
{
    if (a == 0 || a > VSWAP_LINE_MAX) return VSWAP_ERR_ARG;
    vswap_status_t st = vswap_rate(s, q);
    if (st != VSWAP_OK) return st;
    return buy(q->num, q->den, a, &q->vbills, &q->vino);
}

vswap_status_t vswap_quote_out(const vswap_t *s, uint64_t v, vswap_quote_t *q)
{
    uint64_t R, B;
    if (!s || !s->L || !q || v == 0 || v > VSWAP_LINE_MAX) return VSWAP_ERR_ARG;
    pay_memset(q, 0, sizeof *q);
    vswap_status_t st = base(s, &R, &B);
    if (st != VSWAP_OK) return st;
    if (v > B) return VSWAP_ERR_FUNDS;
    q->num = R;
    q->den = B;
    q->vbills = v;
    pay_muldiv(v, R, B, &q->vino, 0); /* <= R: cannot fail */
    return q->vino ? VSWAP_OK : VSWAP_ERR_DUST;
}

/* ---- postings ---- */
static void derive(const vswap_t *s, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, s->cfg.seed, 32);
    in[32] = tag;
    put64(in + 33, s->seq);
    sha256(in, sizeof in, out);
}

static vswap_status_t post(vswap_t *s, const pay_line_t *ln, uint32_t n, uint8_t kind,
                           uint64_t tick)
{
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    for (uint32_t i = 0; i < n; i++) r.lines[i] = ln[i];
    r.n_lines = n;
    uint8_t d[32];
    derive(s, 'I', r.idem_key);
    derive(s, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    derive(s, 'E', d); /* unguessable without the seed */
    pay_w_s(&w, "VSWAP-");
    pay_w_hex(&w, d, 12);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "player card v-bill swap", sizeof r.memo);
    r.tick = tick;
    r.kind = kind;
    r.ext_cap = PAY_CAP_FINANCIAL;
    s->seq++;
    pay_receipt_t rc;
    s->last_ledger = pay_ledger_post(s->L, &r, &rc);
    if (s->last_ledger == PAY_OK) {
        s->posted++;
        return VSWAP_OK;
    }
    return s->last_ledger == PAY_ERR_FUNDS ? VSWAP_ERR_FUNDS : VSWAP_ERR_LEDGER;
}

static pay_line_t line(uint32_t a, uint64_t d, uint64_t c, bool neg)
{
    pay_line_t l = {a, neg ? -(int64_t) d : (int64_t) d, neg ? -(int64_t) c : (int64_t) c};
    return l;
}

/* a user's pair of accounts for a swap: both must have the same owner.
 * Authenticating the caller as that owner is the zxvd layer's job. */
static vswap_status_t party(const vswap_t *s, uint32_t vino, uint32_t vb)
{
    if (is_lock(s, vb)) return VSWAP_ERR_LOCKED;
    const pay_account_t *a = of(s, vino, s->cfg.vino), *b = of(s, vb, s->cfg.vbill);
    if (!a || vino == s->cfg.reserve || !b || vb == s->cfg.issuer || vb == s->cfg.vault)
        return VSWAP_ERR_ARG;
    if (a->owner != b->owner) return VSWAP_ERR_AUTH;
    return VSWAP_OK;
}

vswap_status_t vswap_swap_in(vswap_t *s, uint32_t user_vino, uint32_t user_vbill, uint64_t a,
                             uint64_t tick, vswap_quote_t *q)
{
    vswap_quote_t tmp;
    if (!q) q = &tmp;
    if (!s || !s->L) return VSWAP_ERR_ARG;
    if (!s->enabled) return VSWAP_ERR_DISABLED;
    vswap_status_t st = party(s, user_vino, user_vbill);
    if (st == VSWAP_OK) st = vswap_quote_in(s, a, q);
    if (st != VSWAP_OK) return st;
    const pay_line_t ln[4] = {
        line(user_vino, q->vino, 0, true), line(s->cfg.reserve, q->vino, 0, false),
        line(user_vbill, q->vbills, 0, false), line(s->cfg.issuer, 0, q->vbills, false)};
    return post(s, ln, 4, PAY_KIND_ISSUE, tick);
}

vswap_status_t vswap_swap_out(vswap_t *s, uint32_t user_vbill, uint32_t user_vino, uint64_t v,
                              uint64_t tick, vswap_quote_t *q)
{
    vswap_quote_t tmp;
    if (!q) q = &tmp;
    if (!s || !s->L) return VSWAP_ERR_ARG;
    vswap_status_t st = party(s, user_vino, user_vbill);
    if (st == VSWAP_OK) st = vswap_quote_out(s, v, q);
    if (st != VSWAP_OK) return st;
    const pay_line_t ln[4] = {line(user_vbill, v, 0, true), line(s->cfg.issuer, 0, v, true),
                              line(s->cfg.reserve, q->vino, 0, true),
                              line(user_vino, q->vino, 0, false)};
    return post(s, ln, 4, PAY_KIND_REDEEM, tick);
}

/* ---- T1 / T3 ---- */
vswap_status_t vswap_lock(vswap_t *s, uint32_t owner, uint32_t payee, uint8_t tier, uint64_t v,
                          bool reinvest, uint64_t tick, uint32_t *id)
{
    if (!s || !s->L) return VSWAP_ERR_ARG;
    if (!s->enabled) return VSWAP_ERR_DISABLED;
    if (tier != MED && tier != DEEP) return VSWAP_ERR_ARG;
    vswap_status_t st = party(s, payee, owner);
    if (st != VSWAP_OK) return st;
    if (v == 0 || v > VSWAP_LINE_MAX) return VSWAP_ERR_ARG;
    uint32_t i = 0;
    while (i < VSWAP_MAX_POS && s->pos[i].used) i++;
    if (i == VSWAP_MAX_POS) return VSWAP_ERR_FULL;
    const pay_line_t ln[2] = {line(owner, v, 0, true), line(s->cfg.lock[tier], v, 0, false)};
    st = post(s, ln, 2, PAY_KIND_TRANSFER, tick);
    if (st != VSWAP_OK) return st;
    vswap_pos_t *p = &s->pos[i];
    pay_memset(p, 0, sizeof *p);
    p->used = true;
    p->reinvest = reinvest;
    p->tier = tier;
    p->owner = owner;
    p->payee = payee;
    p->vbills = v;
    p->opened = tick;
    if (id) *id = i;
    return VSWAP_OK;
}

static vswap_pos_t *held(vswap_t *s, uint32_t id, uint32_t owner)
{
    if (!s || id >= VSWAP_MAX_POS || !s->pos[id].used || s->pos[id].owner != owner) return 0;
    return &s->pos[id];
}

vswap_status_t vswap_notice(vswap_t *s, uint32_t id, uint32_t owner, uint64_t tick)
{
    vswap_pos_t *p = held(s, id, owner);
    if (!p) return VSWAP_ERR_ARG;
    if (p->noticed) return VSWAP_ERR_STATE;
    p->noticed = true;
    p->notice_tick = tick;
    return VSWAP_OK;
}

vswap_status_t vswap_unlock(vswap_t *s, uint32_t id, uint32_t owner, uint64_t tick)
{
    vswap_pos_t *p = held(s, id, owner);
    uint64_t end;
    if (!p || !s->L) return VSWAP_ERR_ARG;
    if (!p->noticed || !pay_add_ok(p->notice_tick, s->period[p->tier], &end) || tick < end)
        return VSWAP_ERR_LOCKED;
    const pay_line_t ln[2] = {line(s->cfg.lock[p->tier], p->vbills, 0, true),
                              line(owner, p->vbills, 0, false)};
    vswap_status_t st = post(s, ln, 2, PAY_KIND_TRANSFER, tick);
    if (st == VSWAP_OK) pay_memset(p, 0, sizeof *p);
    return st;
}

/* ---- W5 / T2 ---- */
#define NENT (VSWAP_MAX_HOLD + VSWAP_MAX_POS)

static vswap_status_t div_check(const vswap_t *s, uint32_t pool, const vswap_holder_t *h,
                                uint32_t n, uint64_t *P)
{
    const pay_account_t *pa = of(s, pool, s->cfg.vino);
    if (!pa || pool == s->cfg.reserve || (pa->flags & PAY_ACCT_FROZEN)) return VSWAP_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if (!of(s, h[i].vbill, s->cfg.vbill) || h[i].vbill == s->cfg.issuer ||
            is_lock(s, h[i].vbill) || !of(s, h[i].payee, s->cfg.vino) || h[i].payee == pool)
            return VSWAP_ERR_ARG;
        for (uint32_t j = 0; j < i; j++)
            if (h[j].vbill == h[i].vbill) return VSWAP_ERR_ARG;
    }
    /* A position whose payee is this pool is NOT a reason to refuse: anyone
     * may open one (vswap_lock does not know the pool), and refusing here let
     * one locked V-Bill block every dividend. It gets weight 0 instead. */
    *P = pa->debit;
    if (*P == 0) return VSWAP_ERR_EMPTY;
    if (*P > VSWAP_LINE_MAX) return VSWAP_ERR_OVERFLOW;
    return VSWAP_OK;
}

vswap_status_t vswap_dividend(vswap_t *s, uint32_t pool, const vswap_holder_t *h, uint32_t n,
                              bool exclude_vault, uint64_t tick, uint64_t *paid, vswap_div_t *out)
{
    vswap_div_t tmp;
    if (!out) out = &tmp;
    pay_memset(out, 0, sizeof *out);
    if (!s || !s->L || (n && !h) || n > VSWAP_MAX_HOLD) return VSWAP_ERR_ARG;
    if (!s->enabled) return VSWAP_ERR_DISABLED;
    uint64_t P, U = 0, u[NENT], share[NENT], rem[NENT], sum = 0;
    const uint32_t cnt = n + VSWAP_MAX_POS;
    vswap_status_t st = div_check(s, pool, h, n, &P);
    if (st != VSWAP_OK) return st;
    out->pool = P;

    /* weight units: holders first, then positions in table order */
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t bal = 0, w = 0;
        if (i < n) {
            bal = of(s, h[i].vbill, s->cfg.vbill)->debit;
            w = (exclude_vault && h[i].vbill == s->cfg.vault) ? 0 : s->cfg.weight[LOW];
        } else if (s->pos[i - n].used && s->pos[i - n].payee != pool) {
            bal = s->pos[i - n].vbills;
            w = s->cfg.weight[s->pos[i - n].tier];
        }
        if (__builtin_mul_overflow(bal, w, &u[i]) || __builtin_add_overflow(U, u[i], &U))
            return VSWAP_ERR_OVERFLOW;
    }
    if (U == 0) return VSWAP_ERR_NOHOLD;
    out->units = U;
    for (uint32_t i = 0; i < cnt; i++) {
        pay_muldiv(P, u[i], U, &share[i], &rem[i]); /* <= P: cannot fail */
        sum += share[i];
    }
    /* leftover by largest remainder, ties to the lower index */
    bool got[NENT] = {false};
    for (uint64_t left = P - sum; left > 0; left--) {
        uint32_t best = 0;
        bool any = false;
        for (uint32_t i = 0; i < cnt; i++)
            if (!got[i] && (!any || rem[i] > rem[best])) {
                best = i;
                any = true;
            }
        got[best] = true;
        share[best]++;
    }

    /* reinvest at the rate before the dividend (T2, W3) */
    vswap_quote_t rt;
    st = vswap_rate(s, &rt);
    if (st != VSWAP_OK) return st;
    uint64_t pay[NENT], add[NENT], tot_a = 0, tot_v = 0, tv[VSWAP_TIERS] = {0};
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t cost = 0;
        add[i] = 0;
        if (i >= n && s->pos[i - n].used && s->pos[i - n].reinvest && share[i]) {
            st = buy(rt.num, rt.den, share[i], &add[i], &cost);
            if (st == VSWAP_ERR_OVERFLOW) return st;
            /* dust leaves add = cost = 0: the share is paid out whole */
            tot_a += cost; /* sums stay <= P */
            if (__builtin_add_overflow(tot_v, add[i], &tot_v) || tot_v > VSWAP_LINE_MAX)
                return VSWAP_ERR_OVERFLOW;
            tv[s->pos[i - n].tier] += add[i];
        }
        pay[i] = share[i] - cost;
    }

    for (uint32_t i = 0; i < n; i++)
        if (paid) paid[i] = 0;
    for (uint32_t i = 0; i < VSWAP_MAX_POS; i++) s->pos[i].last_vino = s->pos[i].last_vbills = 0;

    if (tot_v) {
        pay_line_t ln[5];
        uint32_t k = 0;
        ln[k++] = line(pool, tot_a, 0, true);
        ln[k++] = line(s->cfg.reserve, tot_a, 0, false);
        ln[k++] = line(s->cfg.issuer, 0, tot_v, false);
        for (uint32_t t = MED; t <= DEEP; t++)
            if (tv[t]) ln[k++] = line(s->cfg.lock[t], tv[t], 0, false);
        st = post(s, ln, k, PAY_KIND_ISSUE, tick);
        if (st != VSWAP_OK) return st;
        out->postings++;
        out->reinvest = tot_a;
        out->vbills = tot_v;
        for (uint32_t i = n; i < cnt; i++) {
            s->pos[i - n].vbills += add[i];
            s->pos[i - n].last_vbills = add[i];
        }
    }

    /* payouts: batches of up to PAY_MAX_LINES - 1 payees */
    uint32_t i = 0;
    while (i < cnt) {
        pay_line_t ln[PAY_MAX_LINES];
        uint32_t idx[PAY_MAX_LINES], k = 1, j = i;
        uint64_t tot = 0;
        for (; j < cnt && k < PAY_MAX_LINES; j++) {
            if (!pay[j]) continue;
            idx[k] = j;
            ln[k++] = line(j < n ? h[j].payee : s->pos[j - n].payee, pay[j], 0, false);
            tot += pay[j];
        }
        i = j;
        if (k == 1) break;
        ln[0] = line(pool, tot, 0, true);
        st = post(s, ln, k, PAY_KIND_TRANSFER, tick);
        if (st != VSWAP_OK) return out->postings ? VSWAP_ERR_PARTIAL : st;
        out->postings++;
        out->paid += tot;
        for (uint32_t m = 1; m < k; m++) {
            uint32_t e = idx[m];
            if (e >= n)
                s->pos[e - n].last_vino = pay[e];
            else if (paid)
                paid[e] = pay[e];
        }
    }
    return VSWAP_OK;
}

/* ---- T4 ---- */
vswap_status_t vswap_epoch_check(const vswap_t *s)
{
    if (!s || !s->L) return VSWAP_ERR_ARG;
    uint64_t sum[VSWAP_TIERS] = {0};
    for (uint32_t i = 0; i < VSWAP_MAX_POS; i++) {
        const vswap_pos_t *p = &s->pos[i];
        if (!p->used) continue;
        if ((p->tier != MED && p->tier != DEEP) ||
            __builtin_add_overflow(sum[p->tier], p->vbills, &sum[p->tier]))
            return VSWAP_ERR_STATE;
    }
    for (uint32_t t = MED; t <= DEEP; t++) {
        const pay_account_t *lk = of(s, s->cfg.lock[t], s->cfg.vbill);
        if (!lk || lk->debit != sum[t]) return VSWAP_ERR_STATE;
    }
    return pay_ledger_check(s->L) ? VSWAP_OK : VSWAP_ERR_STATE;
}
