/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vauction.c — V-Bill issuance auctions. See vauction.h A1-A8. */
#include "vauction.h"
#include "../robin_debanks/sha256.h"

static const pay_account_t *acct(const vauc_t *a, uint32_t id)
{
    return pay_ledger_account(a->L, id); /* NULL when unknown or inactive */
}

static bool cfg_ok(const vauc_config_t *c)
{
    uint64_t k, rem;
    if (!c || !c->floor_num || !c->floor_den || !c->backing) return false;
    if (__builtin_mul_overflow(c->floor_num, c->backing, &k)) return false;
    if (!c->cap || c->cap >= PAY_BAL_MAX) return false;
    if (!c->p_min || !c->p_step || c->p_start < c->p_min) return false;
    if (!c->min_quotes || c->min_quotes > VAUC_MAX_QUOTES) return false;
    /* A6: rungs = ceil((p_start - p_min) / step) + 1 */
    uint64_t steps = pay_udiv64(c->p_start - c->p_min, c->p_step, &rem);
    return steps + (rem != 0) < VAUC_MAX_RUNGS;
}

vauc_status_t vauc_init(vauc_t *a, pay_ledger_t *L, const vauc_config_t *cfg, uint32_t reserve,
                        uint32_t escrow, uint32_t issuer, vauc_verify_fn verify, void *vctx,
                        const uint8_t seed[32])
{
    if (!a || !L || !verify || !cfg_ok(cfg)) return VAUC_ERR_ARG;
    pay_memset(a, 0, sizeof *a);
    a->L = L;
    const pay_account_t *r = acct(a, reserve), *e = acct(a, escrow), *i = acct(a, issuer);
    if (!r || !e || !i || reserve == escrow) return VAUC_ERR_ARG;
    if (r->asset != e->asset || i->asset == r->asset) return VAUC_ERR_ARG;
    if ((r->flags | e->flags) & PAY_ACCT_ISSUER || !(i->flags & PAY_ACCT_ISSUER))
        return VAUC_ERR_ARG;
    a->reserve = reserve;
    a->escrow = escrow;
    a->issuer = issuer;
    a->fiat = r->asset;
    a->vbill = i->asset;
    a->cfg = *cfg;
    a->verify = verify;
    a->vctx = vctx;
    if (seed) pay_memcpy(a->seed, seed, 32);
    return VAUC_OK;
}

static bool between_epochs(const vauc_t *a)
{
    return a->state == VAUC_IDLE || a->state == VAUC_SETTLED;
}

vauc_status_t vauc_set_config(vauc_t *a, const vauc_config_t *cfg)
{
    if (!a || !cfg_ok(cfg)) return VAUC_ERR_ARG;
    if (!between_epochs(a)) return VAUC_ERR_STATE;
    a->cfg = *cfg;
    return VAUC_OK;
}

/* ---- A1 collateral ---- */

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void vauc_quote_msg(uint32_t commodity, uint64_t price, uint64_t tick, uint8_t out[VAUC_QUOTE_MSG])
{
    static const char tag[] = "zxv/vauc-quote/v1";
    for (uint32_t i = 0; i < 17; i++) out[i] = (uint8_t) tag[i];
    put32(out + 17, commodity);
    put64(out + 21, price);
    put64(out + 29, tick);
}

void vauc_receipt_msg(uint32_t custodian, const uint8_t id[32], uint32_t commodity, uint64_t qty,
                      uint64_t expiry, uint8_t out[VAUC_RCPT_MSG])
{
    static const char tag[] = "zxv/vauc-rcpt/v1";
    for (uint32_t i = 0; i < 16; i++) out[i] = (uint8_t) tag[i];
    put32(out + 16, custodian);
    pay_memcpy(out + 20, id, 32);
    put32(out + 52, commodity);
    put64(out + 56, qty);
    put64(out + 64, expiry);
}

static vauc_custodian_t *cust_find(const vauc_t *a, uint32_t id)
{
    for (uint32_t i = 0; i < VAUC_MAX_CUSTODIANS; i++)
        if (a->cust[i].used && a->cust[i].id == id) return (vauc_custodian_t *) &a->cust[i];
    return 0;
}

vauc_status_t vauc_custodian_set(vauc_t *a, uint32_t custodian, uint32_t haircut_bps)
{
    if (!a || haircut_bps > VAUC_BPS) return VAUC_ERR_ARG;
    vauc_custodian_t *c = cust_find(a, custodian);
    for (uint32_t i = 0; !c && i < VAUC_MAX_CUSTODIANS; i++)
        if (!a->cust[i].used) c = &a->cust[i];
    if (!c) return VAUC_ERR_FULL;
    c->used = true;
    c->id = custodian;
    c->haircut_bps = haircut_bps;
    return VAUC_OK;
}

static int oracle_find(const vauc_t *a, uint32_t id)
{
    for (uint32_t i = 0; i < a->n_oracles; i++)
        if (a->oracle[i] == id) return (int) i;
    return -1;
}

vauc_status_t vauc_oracle_add(vauc_t *a, uint32_t oracle)
{
    if (!a || oracle_find(a, oracle) >= 0) return VAUC_ERR_ARG;
    if (a->n_oracles >= VAUC_MAX_QUOTES) return VAUC_ERR_FULL;
    a->oracle[a->n_oracles++] = oracle;
    return VAUC_OK;
}

static vauc_feed_t *feed_find(const vauc_t *a, uint32_t commodity)
{
    for (uint32_t i = 0; i < VAUC_MAX_FEEDS; i++)
        if (a->feed[i].used && a->feed[i].commodity == commodity)
            return (vauc_feed_t *) &a->feed[i];
    return 0;
}

vauc_status_t vauc_feed_add(vauc_t *a, uint32_t commodity, uint64_t price_scale)
{
    if (!a || !price_scale || feed_find(a, commodity)) return VAUC_ERR_ARG;
    for (uint32_t i = 0; i < VAUC_MAX_FEEDS; i++) {
        vauc_feed_t *f = &a->feed[i];
        if (f->used) continue;
        pay_memset(f, 0, sizeof *f);
        f->used = true;
        f->commodity = commodity;
        f->scale = price_scale;
        return VAUC_OK;
    }
    return VAUC_ERR_FULL;
}

vauc_status_t vauc_quote(vauc_t *a, uint32_t oracle, uint32_t commodity, uint64_t price,
                         uint64_t tick, const uint8_t *sig, uint32_t sig_len)
{
    if (!a || !price || !sig) return VAUC_ERR_ARG;
    int o = oracle_find(a, oracle);
    vauc_feed_t *f = feed_find(a, commodity);
    if (o < 0 || !f) return VAUC_ERR_UNKNOWN;
    if (f->has[o] && tick <= f->tick[o]) return VAUC_ERR_STALE;
    uint8_t m[VAUC_QUOTE_MSG];
    vauc_quote_msg(commodity, price, tick, m);
    if (!a->verify(a->vctx, oracle, m, sizeof m, sig, sig_len)) return VAUC_ERR_SIG;
    f->has[o] = true;
    f->price[o] = price;
    f->tick[o] = tick;
    return VAUC_OK;
}

vauc_status_t vauc_median(const vauc_t *a, uint32_t commodity, uint64_t now, uint64_t *price)
{
    if (!a || !price) return VAUC_ERR_ARG;
    *price = 0;
    const vauc_feed_t *f = feed_find(a, commodity);
    if (!f) return VAUC_ERR_UNKNOWN;
    uint64_t v[VAUC_MAX_QUOTES];
    uint32_t n = 0;
    for (uint32_t i = 0; i < VAUC_MAX_QUOTES; i++) {
        if (!f->has[i] || f->tick[i] > now || now - f->tick[i] > a->cfg.max_age) continue;
        /* insertion sort, ascending */
        uint32_t j = n++;
        while (j > 0 && v[j - 1] > f->price[i]) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = f->price[i];
    }
    if (n < a->cfg.min_quotes) return VAUC_ERR_QUOTES;
    *price = v[(n - 1) / 2]; /* the lower median */
    return VAUC_OK;
}

static vauc_receipt_t *rcpt_find(const vauc_t *a, const uint8_t id[32])
{
    for (uint32_t i = 0; i < VAUC_MAX_RECEIPTS; i++)
        if (a->rcpt[i].used && pay_memeq(a->rcpt[i].id, id, 32))
            return (vauc_receipt_t *) &a->rcpt[i];
    return 0;
}

vauc_status_t vauc_receipt_add(vauc_t *a, uint32_t custodian, const uint8_t id[32],
                               uint32_t commodity, uint64_t qty, uint64_t expiry, uint64_t now,
                               const uint8_t *sig, uint32_t sig_len)
{
    if (!a || !id || !sig || !qty) return VAUC_ERR_ARG;
    if (!cust_find(a, custodian) || !feed_find(a, commodity)) return VAUC_ERR_UNKNOWN;
    if (expiry <= now) return VAUC_ERR_EXPIRED;
    uint8_t m[VAUC_RCPT_MSG];
    vauc_receipt_msg(custodian, id, commodity, qty, expiry, m);
    if (!a->verify(a->vctx, custodian, m, sizeof m, sig, sig_len)) return VAUC_ERR_SIG;
    if (rcpt_find(a, id)) return VAUC_ERR_DUP;
    vauc_receipt_t *r = 0;
    for (uint32_t i = 0; !r && i < VAUC_MAX_RECEIPTS; i++) {
        vauc_receipt_t *x = &a->rcpt[i];
        /* free, or expired and not quarantined (its replay is refused above) */
        if (!x->used || (!x->quarantined && x->expiry <= now)) r = x;
    }
    if (!r) return VAUC_ERR_FULL;
    pay_memset(r, 0, sizeof *r);
    r->used = true;
    pay_memcpy(r->id, id, 32);
    r->custodian = custodian;
    r->commodity = commodity;
    r->qty = qty;
    r->expiry = expiry;
    return VAUC_OK;
}

vauc_status_t vauc_receipt_dispute(vauc_t *a, const uint8_t id[32])
{
    if (!a || !id) return VAUC_ERR_ARG;
    vauc_receipt_t *r = rcpt_find(a, id);
    if (!r) return VAUC_ERR_UNKNOWN;
    if (r->quarantined) return VAUC_ERR_STATE;
    r->quarantined = true;
    return VAUC_OK;
}

static uint64_t counted(const vauc_t *a, const vauc_receipt_t *r, uint64_t now)
{
    uint64_t med, v, h;
    if (r->quarantined || now >= r->expiry) return 0;
    const vauc_feed_t *f = feed_find(a, r->commodity);
    const vauc_custodian_t *c = cust_find(a, r->custodian);
    if (!f || !c || vauc_median(a, r->commodity, now, &med) != VAUC_OK) return 0;
    if (!pay_muldiv(r->qty, med, f->scale, &v, 0)) return 0; /* does not fit: fail closed */
    pay_muldiv(v, VAUC_BPS - c->haircut_bps, VAUC_BPS, &h, 0);
    return h;
}

uint64_t vauc_receipt_counted(const vauc_t *a, const uint8_t id[32], uint64_t now)
{
    const vauc_receipt_t *r = (a && id) ? rcpt_find(a, id) : 0;
    return r ? counted(a, r, now) : 0;
}

uint32_t vauc_quarantine_list(const vauc_t *a, uint8_t (*out)[32], uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; a && i < VAUC_MAX_RECEIPTS; i++) {
        if (!a->rcpt[i].used || !a->rcpt[i].quarantined) continue;
        if (out && n < max) pay_memcpy(out[n], a->rcpt[i].id, 32);
        n++;
    }
    return n;
}

/* ---- A1-A4 ---- */

vauc_status_t vauc_reserve(const vauc_t *a, uint64_t now, uint64_t *R)
{
    if (!a || !R) return VAUC_ERR_ARG;
    uint64_t s = acct(a, a->reserve)->debit;
    for (uint32_t i = 0; i < VAUC_MAX_RECEIPTS; i++) {
        if (!a->rcpt[i].used) continue;
        if (!pay_add_ok(s, counted(a, &a->rcpt[i], now), &s)) return VAUC_ERR_OVERFLOW;
    }
    *R = s;
    return VAUC_OK;
}

uint64_t vauc_outstanding(const vauc_t *a)
{
    return a ? acct(a, a->issuer)->credit : 0;
}

/* A3: R * den >= k * N */
static bool floor_holds(const vauc_t *a, uint64_t R, uint64_t N)
{
    uint64_t k = a->cfg.floor_num * a->cfg.backing; /* fits: cfg_ok */
    return pay_u128_cmp(pay_mul64(R, a->cfg.floor_den), pay_mul64(k, N)) >= 0;
}

bool vauc_floor_ok(const vauc_t *a, uint64_t now)
{
    uint64_t R;
    if (!a || vauc_reserve(a, now, &R) != VAUC_OK) return false;
    return floor_holds(a, R, vauc_outstanding(a));
}

uint64_t vauc_dilution_q(const vauc_t *a, uint64_t R, uint64_t N, uint64_t p)
{
    if (!a || !floor_holds(a, R, N)) return 0;
    uint64_t k = a->cfg.floor_num * a->cfg.backing;
    pay_u128 c = pay_mul64(p, a->cfg.floor_den);
    if (pay_u128_cmp(c, pay_u128_from(k)) >= 0) return VAUC_UNBOUNDED;
    pay_u128 d = pay_u128_sub(pay_mul64(R, a->cfg.floor_den), pay_mul64(k, N));
    pay_u128 q = pay_udiv128_64(d, k - c.lo, 0);
    return q.hi ? VAUC_UNBOUNDED : q.lo;
}

uint64_t vauc_supply(const vauc_t *a, uint64_t R, uint64_t N, uint64_t p)
{
    if (!a) return 0;
    uint64_t q = vauc_dilution_q(a, R, N, p);
    return q < a->cfg.cap ? q : a->cfg.cap;
}

/* Sums of bid quantities fit: each qty <= locked and the epoch's locked total
 * is below PAY_BAL_MAX (A5). */
uint64_t vauc_demand(const vauc_t *a, uint64_t p)
{
    uint64_t d = 0;
    for (uint32_t i = 0; a && i < a->n_bids; i++)
        if (a->bid[i].limit >= p) d += a->bid[i].qty;
    return d;
}

/* ---- ledger postings ---- */

static void derive(const vauc_t *a, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, a->seed, 32);
    in[32] = tag;
    put64(in + 33, a->seq);
    sha256(in, sizeof in, out);
}

static vauc_status_t post(vauc_t *a, const pay_line_t *ln, uint32_t n, uint8_t kind, uint64_t tick,
                          const char *memo)
{
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    for (uint32_t i = 0; i < n; i++) r.lines[i] = ln[i];
    r.n_lines = n;
    uint8_t d[32];
    derive(a, 'I', r.idem_key);
    derive(a, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    derive(a, 'E', d); /* unguessable without the seed */
    pay_w_s(&w, "VAUC-");
    pay_w_hex(&w, d, 12);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, memo, sizeof r.memo);
    r.tick = tick;
    r.kind = kind;
    r.ext_cap = PAY_CAP_FINANCIAL;
    a->seq++;
    pay_receipt_t rc;
    a->last_ledger = pay_ledger_post(a->L, &r, &rc);
    return a->last_ledger == PAY_OK ? VAUC_OK : VAUC_ERR_LEDGER;
}

/* ---- A5-A8 ---- */

vauc_status_t vauc_open(vauc_t *a)
{
    if (!a) return VAUC_ERR_ARG;
    if (!between_epochs(a)) return VAUC_ERR_STATE;
    a->epoch++;
    a->state = VAUC_OPEN;
    a->mode = 0;
    a->n_bids = 0;
    a->locked = 0;
    pay_memset(a->bid, 0, sizeof a->bid);
    pay_memset(&a->cur, 0, sizeof a->cur);
    a->cur.epoch = a->epoch;
    return VAUC_OK;
}

vauc_status_t vauc_bid(vauc_t *a, uint32_t fiat, uint32_t vbill, uint64_t qty, uint64_t limit,
                       uint64_t tick, uint32_t *index)
{
    if (!a) return VAUC_ERR_ARG;
    if (a->state != VAUC_OPEN) return VAUC_ERR_STATE;
    const pay_account_t *f = acct(a, fiat), *v = acct(a, vbill);
    if (!f || !v || f->asset != a->fiat || v->asset != a->vbill) return VAUC_ERR_ARG;
    if (fiat == a->escrow || fiat == a->reserve || vbill == a->issuer) return VAUC_ERR_ARG;
    /* one owner holds both; authenticating the caller is the zxvd layer's job */
    if (f->owner != v->owner) return VAUC_ERR_AUTH;
    uint64_t lock, total;
    if (!qty || limit < a->cfg.p_min || __builtin_mul_overflow(qty, limit, &lock) ||
        !pay_add_ok(a->locked, lock, &total) || total >= PAY_BAL_MAX)
        return VAUC_ERR_ARG;
    if (a->n_bids >= VAUC_MAX_BIDS) return VAUC_ERR_FULL;
    const pay_line_t ln[2] = {{fiat, -(int64_t) lock, 0}, {a->escrow, (int64_t) lock, 0}};
    vauc_status_t st = post(a, ln, 2, PAY_KIND_TRANSFER, tick, "v-bill auction bid lock");
    if (st != VAUC_OK) return st;
    vauc_bid_t *b = &a->bid[a->n_bids];
    b->fiat = fiat;
    b->vbill = vbill;
    b->qty = qty;
    b->limit = limit;
    b->locked = lock;
    b->seq = a->bid_seq++;
    a->locked = total;
    if (index) *index = a->n_bids;
    a->n_bids++;
    return VAUC_OK;
}

static bool in_set(const vauc_bid_t *b, uint64_t lo, uint64_t hi)
{
    return b->limit >= lo && (hi == 0 || b->limit < hi);
}

/* A6: share `amount` (<= the set's total) pro rata by qty, floors, then one
 * leftover unit each in submission order. */
static void prorate(vauc_t *a, uint64_t lo, uint64_t hi, uint64_t amount)
{
    uint64_t total = 0, given = 0;
    for (uint32_t i = 0; i < a->n_bids; i++)
        if (in_set(&a->bid[i], lo, hi)) total += a->bid[i].qty;
    for (uint32_t i = 0; i < a->n_bids; i++) {
        vauc_bid_t *b = &a->bid[i];
        if (!in_set(b, lo, hi)) continue;
        pay_muldiv(amount, b->qty, total, &b->fill, 0); /* <= qty: fits */
        given += b->fill;
    }
    for (uint32_t i = 0; i < a->n_bids && given < amount; i++) {
        vauc_bid_t *b = &a->bid[i];
        if (!in_set(b, lo, hi)) continue;
        b->fill++;
        given++;
    }
}

vauc_status_t vauc_clear(vauc_t *a, uint64_t now)
{
    if (!a) return VAUC_ERR_ARG;
    if (a->state != VAUC_OPEN) return VAUC_ERR_STATE;
    uint64_t R, N = vauc_outstanding(a);
    vauc_status_t st = vauc_reserve(a, now, &R);
    if (st != VAUC_OK) return st;
    const vauc_config_t *c = &a->cfg;
    uint64_t p = c->p_start, prev = 0, last = 0, S = 0, D = 0;
    bool found = false;
    for (;;) {
        S = vauc_supply(a, R, N, p);
        if (S == 0) break; /* S never rises as p falls: the clock stops */
        D = vauc_demand(a, p);
        last = p;
        if (D >= S) {
            found = true;
            break;
        }
        if (p == c->p_min) break;
        prev = p;
        p = p - c->p_min > c->p_step ? p - c->p_step : c->p_min;
    }
    uint64_t q = 0; /* every fill is still 0: vauc_open cleared the bids */
    if (found) {
        uint64_t dprev = prev ? vauc_demand(a, prev) : 0;
        if (dprev <= S) {
            for (uint32_t i = 0; prev && i < a->n_bids; i++)
                if (a->bid[i].limit >= prev) a->bid[i].fill = a->bid[i].qty;
            prorate(a, p, prev, S - dprev);
        } else {
            prorate(a, prev, 0, S);
        }
        q = S;
    } else if (last) { /* undersubscribed: every bid at or above `last` fills */
        p = last;
        for (uint32_t i = 0; i < a->n_bids; i++)
            if (a->bid[i].limit >= p) a->bid[i].fill = a->bid[i].qty;
        q = vauc_demand(a, p);
    } else {
        p = c->p_min;
    }
    a->cur.price = p;
    a->cur.qty = q;
    a->cur.r_before = R;
    a->cur.n_before = N;
    a->cur.winners = 0;
    for (uint32_t i = 0; i < a->n_bids; i++) a->cur.winners += a->bid[i].fill > 0;
    a->state = VAUC_CLEARED;
    return VAUC_OK;
}

static void finish(vauc_t *a, uint64_t now)
{
    uint64_t R = 0;
    vauc_reserve(a, now, &R);
    a->cur.r_after = R;
    a->cur.n_after = vauc_outstanding(a);
    a->cur.floor_ok = vauc_floor_ok(a, now);
    a->hist[a->epoch % VAUC_HISTORY] = a->cur;
    a->state = VAUC_SETTLED;
}

static vauc_status_t refund(vauc_t *a, vauc_bid_t *b, uint64_t now)
{
    const pay_line_t ln[2] = {{a->escrow, -(int64_t) b->locked, 0},
                              {b->fiat, (int64_t) b->locked, 0}};
    return post(a, ln, 2, PAY_KIND_RETURN, now, "v-bill auction refund");
}

vauc_status_t vauc_settle(vauc_t *a, uint64_t now)
{
    if (!a) return VAUC_ERR_ARG;
    if (a->state != VAUC_CLEARED || a->mode == 2) return VAUC_ERR_STATE;
    uint64_t p = a->cur.price, rem = 0, R, add;
    for (uint32_t i = 0; i < a->n_bids; i++)
        if (!a->bid[i].settled) rem += a->bid[i].fill;
    vauc_status_t st = VAUC_OK;
    if (rem) { /* A7: the floor must still hold for what is left to issue */
        st = vauc_reserve(a, now, &R);
        if (st != VAUC_OK) return st;
        /* rem * p <= the epoch's locked total < PAY_BAL_MAX */
        if (!pay_add_ok(R, rem * p, &add) || !floor_holds(a, add, vauc_outstanding(a) + rem))
            return VAUC_ERR_FLOOR;
    }
    for (uint32_t i = 0; i < a->n_bids; i++) {
        vauc_bid_t *b = &a->bid[i];
        if (b->settled) continue;
        if (b->fill) {
            uint64_t pay = b->fill * p, back = b->locked - pay; /* p <= limit */
            pay_line_t ln[5] = {{a->escrow, -(int64_t) b->locked, 0},
                                {a->reserve, (int64_t) pay, 0},
                                {a->issuer, 0, (int64_t) b->fill},
                                {b->vbill, (int64_t) b->fill, 0},
                                {b->fiat, (int64_t) back, 0}};
            st = post(a, ln, back ? 5u : 4u, PAY_KIND_ISSUE, now, "v-bill auction issue");
        } else {
            st = refund(a, b, now);
        }
        if (st != VAUC_OK) return st;
        b->settled = true;
        if (b->fill) a->mode = 1; /* something was issued: no cancel from here on */
    }
    finish(a, now);
    return VAUC_OK;
}

vauc_status_t vauc_cancel(vauc_t *a, uint64_t now)
{
    if (!a) return VAUC_ERR_ARG;
    if ((a->state != VAUC_OPEN && a->state != VAUC_CLEARED) || a->mode == 1) return VAUC_ERR_STATE;
    a->state = VAUC_CLEARED; /* closed to bids and clearing from here on */
    a->mode = 2;
    for (uint32_t i = 0; i < a->n_bids; i++) {
        vauc_bid_t *b = &a->bid[i];
        if (b->settled) continue;
        vauc_status_t st = refund(a, b, now);
        if (st != VAUC_OK) return st;
        b->settled = true;
        b->fill = 0;
    }
    a->cur.cancelled = true;
    a->cur.qty = 0;
    a->cur.winners = 0;
    finish(a, now);
    return VAUC_OK;
}

const vauc_record_t *vauc_record(const vauc_t *a, uint64_t epoch)
{
    if (!a || !epoch) return 0;
    const vauc_record_t *r = &a->hist[epoch % VAUC_HISTORY];
    return r->epoch == epoch ? r : 0;
}
