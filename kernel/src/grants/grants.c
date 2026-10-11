/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* grants.c — Olive Oil Barrel startup grants. See grants.h G1-G5. */
#include "grants.h"
#include "../robin_debanks/sha256.h"

#define LINE_LIMIT ((uint64_t) PAY_BAL_MAX / 8u) /* pay_ledger's per-line bound, 2^59 */

static grant_epoch_t *cur(grant_t *g)
{
    return &g->hist[g->epoch & (GRANT_HIST - 1u)];
}

static uint64_t debit(const grant_t *g, uint32_t acct)
{
    return pay_ledger_account(g->L, acct)->debit;
}

grant_status_t grant_init(grant_t *g, pay_ledger_t *L, uint16_t vino_asset, uint32_t owner,
                          uint32_t funnel, grant_verify_fn verify, void *vctx,
                          const uint8_t seed[32])
{
    if (!g || !L || !verify || !pay_ledger_asset(L, vino_asset)) return GRANT_ERR_ARG;
    pay_memset(g, 0, sizeof *g);
    g->L = L;
    g->vino = vino_asset;
    g->funnel = funnel;
    g->verify = verify;
    g->vctx = vctx;
    if (seed) pay_memcpy(g->seed, seed, 32);
    g->th = (grant_thresholds_t){GRANT_DEF_UPTIME_BPS, GRANT_DEF_MIN_VOLUME, GRANT_DEF_MIN_STAKE};
    g->cap_num = g->cap_den = 1;
    if (pay_ledger_add_asset(L, GRANT_ASSET_CODE, 0, PAY_ASSET_UNIT, &g->barrel) != PAY_OK ||
        pay_ledger_open(L, owner, g->barrel, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &g->issuer) !=
            PAY_OK ||
        pay_ledger_open(L, owner, vino_asset, PAY_CAP_FINANCIAL, 0, &g->pool) != PAY_OK ||
        pay_ledger_open(L, owner, vino_asset, PAY_CAP_FINANCIAL, 0, &g->reserve) != PAY_OK) {
        g->L = 0;
        return GRANT_ERR_LEDGER;
    }
    return GRANT_OK;
}

uint32_t grant_pool_account(const grant_t *g)
{
    return g ? g->pool : 0xFFFFFFFFu;
}

grant_status_t grant_set_thresholds(grant_t *g, const grant_thresholds_t *th)
{
    if (!g || !th || th->min_uptime_bps > GRANT_BPS_MAX) return GRANT_ERR_ARG;
    g->th = *th;
    return GRANT_OK;
}

grant_status_t grant_set_cap(grant_t *g, uint64_t num, uint64_t den)
{
    if (!g || num == 0 || num > den) return GRANT_ERR_ARG;
    g->cap_num = num;
    g->cap_den = den;
    return GRANT_OK;
}

grant_status_t grant_underwriter_add(grant_t *g, uint32_t id)
{
    if (!g) return GRANT_ERR_ARG;
    for (uint32_t i = 0; i < g->n_underw; i++)
        if (g->underwriter[i] == id) return GRANT_OK;
    if (g->n_underw >= GRANT_MAX_UNDERW) return GRANT_ERR_FULL;
    g->underwriter[g->n_underw++] = id;
    return GRANT_OK;
}

grant_status_t grant_epoch_begin(grant_t *g, uint64_t epoch)
{
    if (!g || !g->L) return GRANT_ERR_ARG;
    if (epoch <= g->epoch) return GRANT_ERR_EPOCH;
    g->epoch = epoch;
    grant_epoch_t *e = cur(g);
    pay_memset(e, 0, sizeof *e);
    e->epoch = epoch;
    for (uint32_t i = 0; i < GRANT_MAX_APPEALS; i++)
        if (g->appeal[i].epoch < epoch) g->appeal[i].used = false;
    return GRANT_OK;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* identifiers: SHA-256(seed || tag || seq), as vhub.c */
static void derive(const grant_t *g, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, g->seed, 32);
    in[32] = tag;
    put64(in + 33, g->seq);
    sha256(in, sizeof in, out);
}

static pay_status_t post(grant_t *g, pay_posting_req_t *r, uint8_t kind, const char *memo,
                         uint64_t tick)
{
    uint8_t d[32];
    derive(g, 'I', r->idem_key);
    derive(g, 'U', d);
    pay_uetr_from_random(d, r->uetr);
    pay_w w;
    pay_w_init(&w, r->e2e, sizeof r->e2e);
    pay_w_s(&w, "GRANT-");
    pay_w_u64(&w, g->seq);
    pay_w_finish(&w);
    pay_strlcpy(r->memo, memo, sizeof r->memo);
    r->tick = tick;
    r->kind = kind;
    r->ext_cap = PAY_CAP_FINANCIAL;
    g->seq++;
    pay_receipt_t rc;
    return g->last_ledger = pay_ledger_post(g->L, r, &rc);
}

grant_status_t grant_pool_credit(grant_t *g, uint32_t from, uint64_t amount, uint64_t tick)
{
    if (!g || !g->L || amount == 0) return GRANT_ERR_ARG;
    grant_epoch_t *e = cur(g);
    uint64_t in;
    if (!pay_add_ok(e->inflow, amount, &in)) return GRANT_ERR_OVERFLOW;
    if (amount > LINE_LIMIT) return GRANT_ERR_ARG;
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    r.lines[0] = (pay_line_t){from, -(int64_t) amount, 0};
    r.lines[1] = (pay_line_t){g->pool, (int64_t) amount, 0};
    r.n_lines = 2;
    if (post(g, &r, PAY_KIND_TRANSFER, "raffle slice to grant pool", tick) != PAY_OK)
        return GRANT_ERR_LEDGER;
    e->inflow = in;
    return GRANT_OK;
}

/* ---- G2 ---- */

void grant_appeal_msg(uint32_t subject, uint64_t epoch, uint32_t mask,
                      uint8_t out[GRANT_APPEAL_MSG])
{
    pay_memcpy(out, "zxv/grant-appeal/v1", 19);
    put32(out + 19, subject);
    put64(out + 23, epoch);
    put32(out + 31, mask);
}

grant_status_t grant_appeal(grant_t *g, uint32_t subject, uint64_t epoch, uint32_t mask,
                            const uint8_t *sig, uint32_t sig_len)
{
    if (!g || !sig || mask == 0 || (mask & ~GRANT_FAIL_ALL)) return GRANT_ERR_ARG;
    if (epoch < g->epoch) return GRANT_ERR_EPOCH;
    uint8_t m[GRANT_APPEAL_MSG];
    grant_appeal_msg(subject, epoch, mask, m);
    if (!g->verify(g->vctx, g->funnel, m, sizeof m, sig, sig_len)) return GRANT_ERR_SIG;
    grant_appeal_t *slot = 0;
    for (uint32_t i = 0; i < GRANT_MAX_APPEALS; i++) {
        grant_appeal_t *a = &g->appeal[i];
        if (a->used && a->subject == subject && a->epoch == epoch) {
            a->mask |= mask;
            return GRANT_OK;
        }
        if (!a->used && !slot) slot = a;
    }
    if (!slot) return GRANT_ERR_FULL;
    *slot = (grant_appeal_t){true, subject, epoch, mask};
    return GRANT_OK;
}

bool grant_eval(const grant_t *g, const grant_metrics_t *m, uint64_t epoch, uint32_t *reasons)
{
    uint32_t r = 0;
    if (!g || !m) {
        r = GRANT_FAIL_INVALID;
    } else {
        if (m->uptime_bps > GRANT_BPS_MAX) r |= GRANT_FAIL_INVALID;
        if (m->uptime_bps < g->th.min_uptime_bps) r |= GRANT_FAIL_UPTIME;
        if (m->dropped_reports) r |= GRANT_FAIL_DROPPED;
        if (m->routing_reports) r |= GRANT_FAIL_ROUTING;
        if (m->failed_challenges) r |= GRANT_FAIL_AUDIT;
        if (m->fraud_findings) r |= GRANT_FAIL_FRAUD;
        if (m->volume < g->th.min_volume) r |= GRANT_FAIL_VOLUME;
        if (m->stake < g->th.min_stake) r |= GRANT_FAIL_STAKE;
        for (uint32_t i = 0; i < GRANT_MAX_APPEALS; i++) {
            const grant_appeal_t *a = &g->appeal[i];
            if (a->used && a->subject == m->subject && a->epoch == epoch) r &= ~a->mask;
        }
    }
    if (reasons) *reasons = r;
    return r == 0;
}

/* ---- G1 tables ---- */

static grant_lot_t *find_lot(grant_t *g, uint32_t id)
{
    for (uint32_t i = 0; i < GRANT_MAX_LOTS; i++)
        if (g->lot[i].used && g->lot[i].id == id) return &g->lot[i];
    return 0;
}

const grant_lot_t *grant_lot(const grant_t *g, uint32_t lot)
{
    return g ? find_lot((grant_t *) g, lot) : 0;
}

grant_status_t grant_lot_open(grant_t *g, uint32_t lot)
{
    if (!g || find_lot(g, lot)) return GRANT_ERR_ARG;
    for (uint32_t i = 0; i < GRANT_MAX_LOTS; i++)
        if (!g->lot[i].used) {
            pay_memset(&g->lot[i], 0, sizeof g->lot[i]);
            g->lot[i].used = true;
            g->lot[i].id = lot;
            return GRANT_OK;
        }
    return GRANT_ERR_FULL;
}

static grant_hold_t *find_hold(grant_t *g, uint32_t account, uint32_t lot)
{
    for (uint32_t i = 0; i < GRANT_MAX_HOLD; i++)
        if (g->hold[i].used && g->hold[i].account == account && g->hold[i].lot == lot)
            return &g->hold[i];
    return 0;
}

static grant_hold_t *free_hold(grant_t *g)
{
    for (uint32_t i = 0; i < GRANT_MAX_HOLD; i++)
        if (!g->hold[i].used) return &g->hold[i];
    return 0;
}

uint64_t grant_holding(const grant_t *g, uint32_t account, uint32_t lot)
{
    const grant_hold_t *h = g ? find_hold((grant_t *) g, account, lot) : 0;
    return h ? h->count : 0;
}

/* ---- G3 ---- */

static bool barrel_acct(const grant_t *g, uint32_t acct)
{
    const pay_account_t *a = pay_ledger_account(g->L, acct);
    return a && a->active && a->asset == g->barrel && a->cap == PAY_CAP_FINANCIAL;
}

static grant_status_t validate(grant_t *g, const grant_recipient_t *r, uint32_t n, uint64_t *W,
                               grant_dist_t *out)
{
    uint64_t w = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!barrel_acct(g, r[i].account) || r[i].account == g->issuer || r[i].weight == 0)
            return GRANT_ERR_ARG;
        for (uint32_t j = 0; j < i; j++)
            if (r[j].account == r[i].account) return GRANT_ERR_ARG;
        if (!pay_add_ok(w, r[i].weight, &w)) return GRANT_ERR_OVERFLOW;
    }
    for (uint32_t i = 0; i < n; i++)
        if (!grant_eval(g, &r[i].m, g->epoch, 0)) {
            out->bad = i;
            return GRANT_ERR_INELIGIBLE;
        }
    *W = w;
    return GRANT_OK;
}

/* floor(B*W_i/W) plus largest remainder, ties to the lower index */
static void split(uint64_t B, const grant_recipient_t *r, uint32_t n, uint64_t W, uint64_t *share)
{
    uint64_t rem[GRANT_MAX_RECIP], given = 0;
    for (uint32_t i = 0; i < n; i++) {
        pay_muldiv(B, r[i].weight, W, &share[i], &rem[i]);
        given += share[i];
    }
    bool bumped[GRANT_MAX_RECIP] = {false};
    for (uint64_t left = B - given; left > 0; left--) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++)
            if (!bumped[i] && (best == n || rem[i] > rem[best])) best = i;
        bumped[best] = true;
        share[best]++;
    }
}

grant_status_t grant_epoch_distribute(grant_t *g, uint32_t lot, const grant_recipient_t *r,
                                      uint32_t n, uint64_t price, uint64_t tick, grant_dist_t *out)
{
    grant_dist_t tmp;
    if (!out) out = &tmp;
    pay_memset(out, 0, sizeof *out);
    out->bad = n;
    if (!g || !g->L || !r || n == 0 || n > GRANT_MAX_RECIP || price == 0) return GRANT_ERR_ARG;
    grant_lot_t *lt = find_lot(g, lot);
    if (!lt) return GRANT_ERR_NOT_FOUND;
    grant_epoch_t *e = cur(g);
    if (e->distributed || lt->attested) return GRANT_ERR_STATE;
    uint64_t W;
    grant_status_t st = validate(g, r, n, &W, out);
    if (st != GRANT_OK) return st;
    e->weight = W;

    uint64_t budget, B;
    pay_muldiv(debit(g, g->pool), g->cap_num, g->cap_den, &budget, 0); /* num <= den: fits */
    B = pay_udiv64(budget, price, 0);
    if (B == 0) return GRANT_ERR_EMPTY;
    out->barrels = B;
    out->cost = B * price; /* <= budget <= pool: no overflow */
    split(B, r, n, W, out->share);

    /* every new (account, lot) pair needs a holding slot; counts must fit */
    uint32_t need = 0, have = 0;
    for (uint32_t i = 0; i < GRANT_MAX_HOLD; i++) have += !g->hold[i].used;
    for (uint32_t i = 0; i < n; i++)
        if (out->share[i] && !find_hold(g, r[i].account, lot)) need++;
    if (need > have) return GRANT_ERR_FULL;
    uint64_t total; /* lot counts stay below the issuer CREDIT bound; the
                       running total is the one that could wrap */
    if (!pay_add_ok(g->total_barrels, B, &total)) return GRANT_ERR_OVERFLOW;

    uint32_t i = 0;
    while (i < n) {
        pay_posting_req_t q;
        pay_memset(&q, 0, sizeof q);
        uint32_t k = 3, idx[GRANT_BATCH];
        uint64_t sb = 0;
        for (; i < n && k < PAY_MAX_LINES; i++) {
            if (!out->share[i]) continue;
            idx[k - 3] = i;
            q.lines[k++] = (pay_line_t){r[i].account, (int64_t) out->share[i], 0};
            sb += out->share[i];
        }
        if (k == 3) break;
        int64_t c = (int64_t) (sb * price);
        q.lines[0] = (pay_line_t){g->pool, -c, 0};
        q.lines[1] = (pay_line_t){g->reserve, c, 0};
        q.lines[2] = (pay_line_t){g->issuer, 0, (int64_t) sb};
        q.n_lines = k;
        if (post(g, &q, PAY_KIND_ISSUE, "barrel grant (virtual unit)", tick) != PAY_OK)
            return GRANT_ERR_LEDGER;
        e->distributed = true;
        out->postings++;
        for (uint32_t j = 0; j + 3 < k; j++) {
            uint32_t x = idx[j];
            grant_hold_t *h = find_hold(g, r[x].account, lot);
            if (!h) {
                h = free_hold(g);
                *h = (grant_hold_t){true, r[x].account, lot, 0};
            }
            h->count += out->share[x];
        }
        lt->count += sb;
        g->total_barrels += sb;
        e->barrels += sb;
        e->cost += sb * price;
    }
    return GRANT_OK;
}

/* ---- G4 ---- */

grant_status_t grant_rate(const grant_t *g, uint64_t epoch, uint64_t *num, uint64_t *den,
                          bool *defined)
{
    if (!g || !num || !den || !defined) return GRANT_ERR_ARG;
    const grant_epoch_t *e = &g->hist[epoch & (GRANT_HIST - 1u)];
    if (e->epoch != epoch) return GRANT_ERR_NOT_FOUND;
    *defined = e->weight != 0;
    *num = *defined ? e->inflow : 0;
    *den = e->weight;
    return GRANT_OK;
}

void grant_snapshot(const grant_t *g, uint64_t now, grant_snapshot_t *s)
{
    if (!s) return;
    pay_memset(s, 0, sizeof *s);
    if (!g || !g->L) return;
    const grant_epoch_t *e = &g->hist[g->epoch & (GRANT_HIST - 1u)];
    s->epoch = g->epoch;
    s->inflow = e->inflow;
    s->weight = e->weight;
    grant_rate(g, g->epoch, &s->rate_num, &s->rate_den, &s->rate_defined);
    s->granted_barrels = e->barrels;
    s->granted_cost = e->cost;
    s->total_barrels_granted = g->total_barrels;
    s->pool_balance = debit(g, g->pool);
    s->reserve_balance = debit(g, g->reserve);
    for (uint32_t i = 0; i < GRANT_MAX_LOTS; i++) {
        const grant_lot_t *l = &g->lot[i];
        if (!l->used) continue;
        s->lots++;
        s->barrels_outstanding += l->count;
        if (grant_backed(g, l->id, now)) {
            s->lots_backed++;
            s->barrels_backed += l->count;
        } else {
            s->barrels_unbacked += l->count;
        }
    }
}

/* ---- G5 ---- */

void grant_attest_msg(uint32_t lot, uint64_t count, uint32_t underwriter, uint64_t expiry,
                      uint8_t out[GRANT_ATTEST_MSG])
{
    pay_memcpy(out, "zxv/grant-attest/v1", 19);
    put32(out + 19, lot);
    put64(out + 23, count);
    put32(out + 31, underwriter);
    put64(out + 35, expiry);
}

void grant_dispute_msg(uint32_t lot, uint64_t expiry, uint8_t out[GRANT_DISPUTE_MSG])
{
    pay_memcpy(out, "zxv/grant-dispute/v1", 20);
    put32(out + 20, lot);
    put64(out + 24, expiry);
}

static bool listed(const grant_t *g, uint32_t id)
{
    for (uint32_t i = 0; i < g->n_underw; i++)
        if (g->underwriter[i] == id) return true;
    return false;
}

grant_status_t grant_attest(grant_t *g, uint32_t lot, uint64_t count, uint32_t underwriter,
                            uint64_t expiry, uint64_t now, const uint8_t *sig, uint32_t sig_len)
{
    if (!g || !sig) return GRANT_ERR_ARG;
    grant_lot_t *lt = find_lot(g, lot);
    if (!lt) return GRANT_ERR_NOT_FOUND;
    if (count == 0 || count != lt->count) return GRANT_ERR_ARG;
    if (expiry <= now) return GRANT_ERR_EXPIRED;
    if (expiry <= lt->expiry) return GRANT_ERR_EPOCH;
    if (!listed(g, underwriter)) return GRANT_ERR_SIG;
    uint8_t m[GRANT_ATTEST_MSG];
    grant_attest_msg(lot, count, underwriter, expiry, m);
    if (!g->verify(g->vctx, underwriter, m, sizeof m, sig, sig_len)) return GRANT_ERR_SIG;
    lt->attested = true;
    lt->disputed = false;
    lt->underwriter = underwriter;
    lt->att_count = count;
    lt->expiry = expiry;
    return GRANT_OK;
}

grant_status_t grant_dispute(grant_t *g, uint32_t lot, const uint8_t *sig, uint32_t sig_len)
{
    if (!g || !sig) return GRANT_ERR_ARG;
    grant_lot_t *lt = find_lot(g, lot);
    if (!lt) return GRANT_ERR_NOT_FOUND;
    if (!lt->attested) return GRANT_ERR_UNBACKED;
    uint8_t m[GRANT_DISPUTE_MSG];
    grant_dispute_msg(lot, lt->expiry, m);
    if (!g->verify(g->vctx, g->funnel, m, sizeof m, sig, sig_len)) return GRANT_ERR_SIG;
    lt->disputed = true;
    return GRANT_OK;
}

static grant_status_t lot_state(const grant_t *g, uint32_t lot, uint64_t now)
{
    const grant_lot_t *lt = grant_lot(g, lot);
    if (!lt) return GRANT_ERR_NOT_FOUND;
    if (!lt->attested) return GRANT_ERR_UNBACKED;
    if (lt->disputed) return GRANT_ERR_DISPUTED;
    if (now >= lt->expiry) return GRANT_ERR_EXPIRED;
    return GRANT_OK;
}

bool grant_backed(const grant_t *g, uint32_t lot, uint64_t now)
{
    return lot_state(g, lot, now) == GRANT_OK;
}

bool grant_lock_ok(const grant_t *g, uint32_t lot, uint64_t now)
{
    return grant_backed(g, lot, now);
}

const char *grant_label(const grant_t *g, uint32_t lot, uint64_t now, const char **status)
{
    bool b = grant_backed(g, lot, now);
    if (status) *status = b ? GRANT_STATUS_BACKED : GRANT_STATUS_UNBACKED;
    return b ? GRANT_LABEL_BACKED : GRANT_LABEL_UNBACKED;
}

grant_status_t grant_redeem(grant_t *g, uint32_t holder, uint32_t holder_vino, uint32_t lot,
                            uint64_t n, uint64_t price, uint64_t now)
{
    if (!g || !g->L || n == 0 || price == 0) return GRANT_ERR_ARG;
    grant_status_t st = lot_state(g, lot, now);
    if (st != GRANT_OK) return st;
    grant_hold_t *h = find_hold(g, holder, lot);
    if (!h || h->count < n) return GRANT_ERR_FUNDS;
    const pay_account_t *v = pay_ledger_account(g->L, holder_vino);
    if (!v || v->asset != g->vino) return GRANT_ERR_ARG;
    uint64_t pay;
    if (__builtin_mul_overflow(n, price, &pay) || pay > LINE_LIMIT) return GRANT_ERR_OVERFLOW;
    pay_posting_req_t q;
    pay_memset(&q, 0, sizeof q);
    q.lines[0] = (pay_line_t){holder, -(int64_t) n, 0};
    q.lines[1] = (pay_line_t){g->issuer, 0, -(int64_t) n};
    q.lines[2] = (pay_line_t){g->reserve, -(int64_t) pay, 0};
    q.lines[3] = (pay_line_t){holder_vino, (int64_t) pay, 0};
    q.n_lines = 4;
    if (post(g, &q, PAY_KIND_REDEEM, "attested barrel redemption", now) != PAY_OK)
        return GRANT_ERR_LEDGER;
    h->count -= n;
    if (h->count == 0) h->used = false;
    find_lot(g, lot)->count -= n;
    return GRANT_OK;
}

bool grant_check(const grant_t *g)
{
    if (!g || !g->L || !pay_ledger_check(g->L)) return false;
    uint64_t lots = 0;
    for (uint32_t i = 0; i < GRANT_MAX_LOTS; i++) {
        const grant_lot_t *l = &g->lot[i];
        if (!l->used) continue;
        uint64_t s = 0;
        for (uint32_t j = 0; j < GRANT_MAX_HOLD; j++)
            if (g->hold[j].used && g->hold[j].lot == l->id) s += g->hold[j].count;
        if (s != l->count) return false;
        lots += l->count;
    }
    if (pay_ledger_account(g->L, g->issuer)->credit != lots) return false;
    for (uint32_t j = 0; j < GRANT_MAX_HOLD; j++) {
        if (!g->hold[j].used) continue;
        uint64_t s = 0;
        for (uint32_t k = 0; k < GRANT_MAX_HOLD; k++)
            if (g->hold[k].used && g->hold[k].account == g->hold[j].account) s += g->hold[k].count;
        if (debit(g, g->hold[j].account) != s) return false;
    }
    return true;
}
