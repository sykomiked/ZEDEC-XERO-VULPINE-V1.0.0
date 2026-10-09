/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* capmkt.c — uniform-price double auction for capacity, escrow and pay on
 * delivery. See capmkt.h. */
#include "capmkt.h"

#include "../mlkem/keccak.h"
#include "../pay/pay_util.h"

/* ===== small helpers ===== */
static void mcpy(void *d, const void *s, size_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) a[i] = b[i];
}

static void mzero(void *d, size_t n)
{
    volatile uint8_t *a = (volatile uint8_t *) d;
    for (size_t i = 0; i < n; i++) a[i] = 0;
}

static bool meq(const void *x, const void *y, size_t n)
{
    const uint8_t *a = (const uint8_t *) x, *b = (const uint8_t *) y;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

/* a * b, false if it exceeds CM_AMOUNT_MAX */
static bool mul_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    pay_u128 p = pay_mul64(a, b);
    if (p.hi || p.lo > CM_AMOUNT_MAX) return false;
    *out = p.lo;
    return true;
}

static bool add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    uint64_t s;
    if (!pay_add_ok(a, b, &s) || s > CM_AMOUNT_MAX) return false;
    *out = s;
    return true;
}

static bool key_eq(cm_key_t a, cm_key_t b)
{
    return a.resource == b.resource && a.tenor == b.tenor && a.region == b.region;
}

static bool key_ok(cm_key_t k)
{
    return k.resource >= CM_RES_COMPUTE && k.resource <= CM_RES_BANDWIDTH &&
           k.tenor >= CM_TENOR_HOUR && k.tenor <= CM_TENOR_MONTH;
}

uint64_t cm_tenor_ms(uint8_t tenor)
{
    switch (tenor) {
    case CM_TENOR_HOUR:
        return 3600000ull;
    case CM_TENOR_DAY:
        return 86400000ull;
    case CM_TENOR_WEEK:
        return 604800000ull;
    case CM_TENOR_MONTH:
        return 2592000000ull;
    default:
        return 0;
    }
}

/* ===== tithe ===== */
uint64_t cm_tithe_phi(uint64_t a)
{
    if (a > CM_AMOUNT_MAX) a = CM_AMOUNT_MAX;
    pay_u128 sq = pay_mul64(a, a); /* a^2 < 2^124 */
    pay_u128 x4 = {(sq.hi << 2) | (sq.lo >> 62), sq.lo << 2};
    pay_u128 five = pay_u128_add(x4, sq); /* 5 a^2 < 2^127 */
    uint64_t s = pay_isqrt128(five);      /* floor(a sqrt 5) < 2^63.2 */
    return pay_udiv64(a + s, 200u, 0);    /* a + s < 2^64 */
}

static uint64_t tithe_of(const cm_market_t *m, uint64_t amount)
{
    uint64_t t = m->p.tithe ? m->p.tithe(m->p.ctx, amount) : cm_tithe_phi(amount);
    return t > amount ? amount : t;
}

static void post(const cm_market_t *m, uint8_t kind, const uint8_t *from, const uint8_t *to,
                 uint64_t amount)
{
    if (m->p.post && amount) m->p.post(m->p.ctx, kind, from, to, amount);
}

/* ===== setup ===== */
void cm_params_default(cm_params_t *p)
{
    if (!p) return;
    mzero(p, sizeof *p);
    p->share_num = CM_SHARE_NUM;
    p->share_den = CM_SHARE_DEN;
}

void cm_init(cm_market_t *m, const cm_params_t *p)
{
    if (!m) return;
    mzero(m, sizeof *m);
    if (p)
        mcpy(&m->p, p, sizeof *p);
    else
        cm_params_default(&m->p);
    if (!m->p.share_den || m->p.share_num > m->p.share_den) {
        m->p.share_num = CM_SHARE_NUM;
        m->p.share_den = CM_SHARE_DEN;
    }
}

/* ===== accounts ===== */
static cm_account_t *acct_find(cm_market_t *m, const uint8_t id[CM_ID_BYTES])
{
    for (uint32_t i = 0; i < CM_MAX_ACCOUNTS; i++)
        if (m->acct[i].used && meq(m->acct[i].id, id, CM_ID_BYTES)) return &m->acct[i];
    return 0;
}

static cm_account_t *acct_get(cm_market_t *m, const uint8_t id[CM_ID_BYTES])
{
    cm_account_t *a = acct_find(m, id);
    if (a) return a;
    for (uint32_t i = 0; i < CM_MAX_ACCOUNTS; i++) {
        if (!m->acct[i].used) {
            a = &m->acct[i];
            mzero(a, sizeof *a);
            a->used = 1;
            mcpy(a->id, id, CM_ID_BYTES);
            return a;
        }
    }
    return 0;
}

const cm_account_t *cm_account(const cm_market_t *m, const uint8_t id[CM_ID_BYTES])
{
    return acct_find((cm_market_t *) m, id);
}

cm_status_t cm_deposit(cm_market_t *m, const uint8_t id[CM_ID_BYTES], uint64_t amount)
{
    if (!m || !id || !amount) return CM_ERR_ARG;
    cm_account_t *a = acct_get(m, id);
    if (!a) return CM_ERR_FULL;
    uint64_t av, dep;
    if (!add_ok(a->available, amount, &av) || !add_ok(m->deposits, amount, &dep))
        return CM_ERR_OVERFLOW;
    a->available = av;
    m->deposits = dep;
    post(m, CM_POST_DEPOSIT, 0, id, amount);
    return CM_OK;
}

cm_status_t cm_withdraw(cm_market_t *m, const uint8_t id[CM_ID_BYTES], uint64_t amount)
{
    if (!m || !id || !amount) return CM_ERR_ARG;
    cm_account_t *a = acct_find(m, id);
    if (!a) return CM_ERR_UNKNOWN;
    if (a->available < amount) return CM_ERR_FUNDS;
    a->available -= amount;
    m->deposits -= amount;
    post(m, CM_POST_WITHDRAW, id, 0, amount);
    return CM_OK;
}

/* ===== orders ===== */
static cm_order_t *ord_find(cm_market_t *m, uint32_t id)
{
    for (uint32_t i = 0; i < CM_MAX_ORDERS; i++)
        if (m->ord[i].used && m->ord[i].id == id) return &m->ord[i];
    return 0;
}

const cm_order_t *cm_order(const cm_market_t *m, uint32_t id)
{
    return ord_find((cm_market_t *) m, id);
}

static bool has_side(const cm_market_t *m, const uint8_t id[CM_ID_BYTES], cm_key_t key,
                     uint8_t side)
{
    for (uint32_t i = 0; i < CM_MAX_ORDERS; i++) {
        const cm_order_t *o = &m->ord[i];
        if (o->used && o->side == side && key_eq(o->key, key) && meq(o->owner, id, CM_ID_BYTES))
            return true;
    }
    return false;
}

static cm_status_t place(cm_market_t *m, uint8_t side, const uint8_t owner[CM_ID_BYTES],
                         cm_key_t key, uint64_t qty, uint64_t price, uint64_t expires,
                         uint32_t *order_id)
{
    if (!m || !owner || !qty || !price || !key_ok(key)) return CM_ERR_ARG;
    uint64_t total;
    if (!mul_ok(qty, price, &total)) return CM_ERR_OVERFLOW;
    if (has_side(m, owner, key, side == CM_ASK ? CM_BID : CM_ASK)) return CM_ERR_SELF;
    cm_order_t *o = 0;
    for (uint32_t i = 0; i < CM_MAX_ORDERS && !o; i++)
        if (!m->ord[i].used) o = &m->ord[i];
    if (!o) return CM_ERR_FULL;
    cm_account_t *a = acct_get(m, owner);
    if (!a) return CM_ERR_FULL;
    if (side == CM_BID) {
        if (a->available < total) return CM_ERR_FUNDS;
        a->available -= total;
        a->locked += total;
    }
    mzero(o, sizeof *o);
    o->used = 1;
    o->side = side;
    o->key = key;
    o->id = ++m->next_order;
    mcpy(o->owner, owner, CM_ID_BYTES);
    o->qty = qty;
    o->price = price;
    o->seq = ++m->next_seq;
    o->expires = expires;
    o->locked = side == CM_BID ? total : 0;
    if (order_id) *order_id = o->id;
    return CM_OK;
}

cm_status_t cm_ask(cm_market_t *m, const uint8_t provider[CM_ID_BYTES], cm_key_t key, uint64_t qty,
                   uint64_t price, uint64_t expires_ms, uint32_t *order_id)
{
    return place(m, CM_ASK, provider, key, qty, price, expires_ms, order_id);
}

cm_status_t cm_bid(cm_market_t *m, const uint8_t buyer[CM_ID_BYTES], cm_key_t key, uint64_t qty,
                   uint64_t limit, uint64_t expires_ms, uint32_t *order_id)
{
    return place(m, CM_BID, buyer, key, qty, limit, expires_ms, order_id);
}

static void order_free(cm_market_t *m, cm_order_t *o)
{
    if (o->side == CM_BID && o->locked) {
        cm_account_t *a = acct_find(m, o->owner);
        if (a) {
            a->locked -= o->locked;
            a->available += o->locked;
        }
    }
    mzero(o, sizeof *o);
}

cm_status_t cm_cancel(cm_market_t *m, const uint8_t owner[CM_ID_BYTES], uint32_t order_id)
{
    if (!m || !owner) return CM_ERR_ARG;
    cm_order_t *o = ord_find(m, order_id);
    if (!o) return CM_ERR_UNKNOWN;
    if (!meq(o->owner, owner, CM_ID_BYTES)) return CM_ERR_ARG;
    order_free(m, o);
    return CM_OK;
}

/* ===== the auction ===== */
static bool live_order(const cm_order_t *o, cm_key_t key, uint64_t now)
{
    return o->used && key_eq(o->key, key) && o->expires >= now && o->filled < o->qty;
}

static uint64_t rem(const cm_order_t *o)
{
    return o->qty - o->filled;
}

/* asks: price asc, seq asc. bids: price desc, seq asc. Insertion sort. */
static void sort_idx(const cm_market_t *m, uint16_t *ix, uint32_t n, bool asc)
{
    for (uint32_t i = 1; i < n; i++) {
        uint16_t v = ix[i];
        const cm_order_t *ov = &m->ord[v];
        uint32_t j = i;
        while (j > 0) {
            const cm_order_t *oj = &m->ord[ix[j - 1]];
            bool before;
            if (ov->price != oj->price)
                before = asc ? ov->price < oj->price : ov->price > oj->price;
            else
                before = ov->seq < oj->seq;
            if (!before) break;
            ix[j] = ix[j - 1];
            j--;
        }
        ix[j] = v;
    }
}

typedef struct {
    uint64_t q, am, bm, lo, hi, unmet;
    bool any;
} match_t;

/* The uncapped walk down both curves: volume and competitive interval. */
static void match(cm_market_t *m, uint32_t na, uint32_t nb, match_t *r)
{
    const uint16_t *ai = m->ai, *bi = m->bi;
    mzero(r, sizeof *r);
    uint32_t i = 0, j = 0;
    uint64_t ar = na ? rem(&m->ord[ai[0]]) : 0, br = nb ? rem(&m->ord[bi[0]]) : 0;
    for (;;) {
        if (i < na && ar == 0) {
            i++;
            ar = i < na ? rem(&m->ord[ai[i]]) : 0;
            continue;
        }
        if (j < nb && br == 0) {
            j++;
            br = j < nb ? rem(&m->ord[bi[j]]) : 0;
            continue;
        }
        if (i >= na || j >= nb) break;
        const cm_order_t *a = &m->ord[ai[i]], *b = &m->ord[bi[j]];
        if (a->price > b->price) break;
        uint64_t t = ar < br ? ar : br;
        r->q += t;
        r->am = a->price;
        r->bm = b->price;
        r->any = true;
        ar -= t;
        br -= t;
    }
    if (!r->any) return;
    r->lo = r->am;
    r->hi = r->bm;
    if (j < nb && m->ord[bi[j]].price > r->lo) r->lo = m->ord[bi[j]].price;
    if (i < na && m->ord[ai[i]].price < r->hi) r->hi = m->ord[ai[i]].price;
    uint64_t p = r->lo + ((r->hi - r->lo) >> 1);
    if (j < nb && m->ord[bi[j]].price >= p) r->unmet += br;
    for (uint32_t k = j + 1; k < nb; k++)
        if (m->ord[bi[k]].price >= p) r->unmet += rem(&m->ord[bi[k]]);
}

static uint64_t cap_for(const cm_market_t *m, uint64_t q, uint32_t k)
{
    if (k <= 1) return q;
    uint64_t num = k >= 3 ? m->p.share_num : 1u, den = k >= 3 ? m->p.share_den : 2u;
    uint64_t c = 0;
    if (!pay_muldiv(q, num, den, &c, 0)) c = q;
    return c ? c : 1u;
}

static void order_digest(const cm_order_t *o, uint8_t h[32])
{
    uint8_t b[32 + 1 + 4 + CM_ID_BYTES + 8 * 4];
    uint32_t n = 0;
    mcpy(b, h, 32);
    n = 32;
    b[n++] = o->side;
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) (o->id >> (24 - 8 * i));
    mcpy(b + n, o->owner, CM_ID_BYTES);
    n += CM_ID_BYTES;
    uint64_t v[4] = {rem(o), o->price, o->seq, o->expires};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (v[k] >> (56 - 8 * i));
    sha3_256(b, n, h);
}

void cm_round_digest(const cm_market_t *m, cm_key_t key, uint64_t now_ms, uint8_t out[32])
{
    static const char L[] = "zxv-capmkt/v1/round";
    uint8_t b[sizeof L - 1 + 4];
    mcpy(b, L, sizeof L - 1);
    b[sizeof L - 1] = key.resource;
    b[sizeof L] = key.tenor;
    b[sizeof L + 1] = (uint8_t) (key.region >> 8);
    b[sizeof L + 2] = (uint8_t) key.region;
    sha3_256(b, sizeof b, out);
    /* canonical: ascending order id */
    uint32_t last = 0;
    for (;;) {
        const cm_order_t *best = 0;
        for (uint32_t i = 0; i < CM_MAX_ORDERS; i++) {
            const cm_order_t *o = &m->ord[i];
            if (!live_order(o, key, now_ms) || o->id <= last) continue;
            if (!best || o->id < best->id) best = o;
        }
        if (!best) break;
        order_digest(best, out);
        last = best->id;
    }
}

static uint32_t free_contracts(const cm_market_t *m)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++)
        if (!m->con[i].used) n++;
    return n;
}

cm_status_t cm_auction(cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_result_t *out)
{
    if (!m || !out || !key_ok(key)) return CM_ERR_ARG;
    mzero(out, sizeof *out);
    out->key = key;
    out->round = m->round + 1u;
    cm_round_digest(m, key, now_ms, out->digest);
    uint32_t na = 0, nb = 0;
    for (uint32_t i = 0; i < CM_MAX_ORDERS; i++) {
        const cm_order_t *o = &m->ord[i];
        if (!live_order(o, key, now_ms)) continue;
        if (o->side == CM_ASK)
            m->ai[na++] = (uint16_t) i;
        else
            m->bi[nb++] = (uint16_t) i;
    }
    sort_idx(m, m->ai, na, true);
    sort_idx(m, m->bi, nb, false);
    match_t r;
    match(m, na, nb, &r);
    if (!r.any) return CM_OK;
    uint64_t p = r.lo + ((r.hi - r.lo) >> 1);
    uint64_t q = r.q;

    /* who supplies q: providers willing at p, first claims capped */
    uint32_t ne = 0;
    while (ne < na && m->ord[m->ai[ne]].price <= p) ne++;
    uint32_t k = 0;
    for (uint32_t i = 0; i < ne; i++) {
        m->rep[i] = (uint16_t) i;
        for (uint32_t j = 0; j < i; j++)
            if (meq(m->ord[m->ai[j]].owner, m->ord[m->ai[i]].owner, CM_ID_BYTES)) {
                m->rep[i] = m->rep[j];
                break;
            }
        if (m->rep[i] == i) k++;
        m->sold[i] = 0;
        m->alloc[i] = 0;
    }
    uint64_t cap = cap_for(m, q, k), total = 0;
    for (uint32_t i = 0; i < ne && total < q; i++) {
        uint64_t room = cap - m->sold[m->rep[i]], t = rem(&m->ord[m->ai[i]]);
        if (t > room) t = room;
        if (t > q - total) t = q - total;
        m->alloc[i] = t;
        m->sold[m->rep[i]] += t;
        total += t;
    }
    uint64_t first = total;
    for (uint32_t i = 0; i < ne && total < q; i++) {
        uint64_t t = rem(&m->ord[m->ai[i]]) - m->alloc[i];
        if (t > q - total) t = q - total;
        m->alloc[i] += t;
        total += t;
    }
    /* the top bids take q (their limits are all >= p) */
    uint64_t need = q;
    for (uint32_t j = 0; j < nb; j++) {
        uint64_t t = rem(&m->ord[m->bi[j]]);
        if (t > need) t = need;
        m->balloc[j] = t;
        need -= t;
    }
    /* pair the two allocations into fills, bounded by free contract slots */
    uint32_t maxf = free_contracts(m);
    if (maxf > CM_MAX_FILLS) maxf = CM_MAX_FILLS;
    uint32_t i = 0, j = 0;
    uint64_t vol = 0;
    while (out->nfills < maxf) {
        while (i < ne && m->alloc[i] == 0) i++;
        while (j < nb && m->balloc[j] == 0) j++;
        if (i >= ne || j >= nb) break;
        uint64_t t = m->alloc[i] < m->balloc[j] ? m->alloc[i] : m->balloc[j];
        cm_fill_t *f = &out->fills[out->nfills++];
        f->ask_id = m->ord[m->ai[i]].id;
        f->bid_id = m->ord[m->bi[j]].id;
        f->qty = t;
        m->alloc[i] -= t;
        m->balloc[j] -= t;
        vol += t;
    }
    out->providers = k;
    out->cap = cap;
    out->overflow = total - first;
    out->volume = vol;
    out->price = p;
    out->lo = r.lo;
    out->hi = r.hi;
    out->unmet_demand = r.unmet + (q - vol);
    return CM_OK;
}

static void expire_orders(cm_market_t *m, cm_key_t key, uint64_t now)
{
    for (uint32_t i = 0; i < CM_MAX_ORDERS; i++) {
        cm_order_t *o = &m->ord[i];
        if (o->used && key_eq(o->key, key) && o->expires < now) order_free(m, o);
    }
}

static cm_contract_t *con_alloc(cm_market_t *m)
{
    for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++)
        if (!m->con[i].used) return &m->con[i];
    return 0;
}

static cm_book_stat_t *stat_of(cm_market_t *m, cm_key_t key)
{
    cm_book_stat_t *freeslot = 0;
    for (uint32_t i = 0; i < CM_MAX_MARKETS; i++) {
        if (m->stat[i].used && key_eq(m->stat[i].key, key)) return &m->stat[i];
        if (!m->stat[i].used && !freeslot) freeslot = &m->stat[i];
    }
    if (freeslot) {
        mzero(freeslot, sizeof *freeslot);
        freeslot->used = 1;
        freeslot->key = key;
    }
    return freeslot;
}

cm_status_t cm_clear(cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_result_t *out)
{
    if (!m || !out || !key_ok(key)) return CM_ERR_ARG;
    expire_orders(m, key, now_ms);
    cm_status_t st = cm_auction(m, key, now_ms, out);
    if (st != CM_OK) return st;
    m->round++;
    uint64_t p = out->price;
    for (uint32_t f = 0; f < out->nfills; f++) {
        cm_fill_t *fl = &out->fills[f];
        cm_order_t *b = ord_find(m, fl->bid_id), *a = ord_find(m, fl->ask_id);
        cm_contract_t *c = con_alloc(m);
        if (!a || !b || !c) return CM_ERR_STATE; /* cannot happen: slots reserved */
        cm_account_t *buyer = acct_find(m, b->owner);
        if (!buyer) return CM_ERR_STATE;
        uint64_t cost = fl->qty * p; /* <= qty * limit <= CM_AMOUNT_MAX */
        uint64_t lockpart = fl->qty * b->price;
        b->locked -= lockpart;
        buyer->locked -= lockpart;
        buyer->escrow += cost;
        buyer->available += lockpart - cost;
        post(m, CM_POST_ESCROW, b->owner, 0, cost);
        post(m, CM_POST_REFUND, 0, b->owner, lockpart - cost);
        b->filled += fl->qty;
        a->filled += fl->qty;
        mzero(c, sizeof *c);
        c->used = 1;
        c->state = CM_C_OPEN;
        c->key = key;
        c->id = ++m->next_contract;
        c->bid_id = b->id;
        c->ask_id = a->id;
        mcpy(c->buyer, b->owner, CM_ID_BYTES);
        mcpy(c->provider, a->owner, CM_ID_BYTES);
        c->qty = fl->qty;
        c->price = p;
        c->escrow = cost;
        c->start_ms = now_ms;
        c->end_ms = now_ms + cm_tenor_ms(key.tenor);
        c->round = m->round;
        mcpy(c->round_digest, out->digest, 32);
        sha3_256(out->digest, 32, c->proof_tip); /* chain anchor */
        if (b->filled == b->qty) order_free(m, b);
        if (a->filled == a->qty) order_free(m, a);
    }
    out->round = m->round;
    cm_book_stat_t *s = stat_of(m, key);
    if (s && out->volume) {
        s->last_price = p;
        s->last_volume = out->volume;
        s->last_round = m->round;
    }
    return CM_OK;
}

void cm_quote(const cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_quote_t *q)
{
    if (!m || !q) return;
    mzero(q, sizeof *q);
    for (uint32_t i = 0; i < CM_MAX_MARKETS; i++) {
        const cm_book_stat_t *s = &m->stat[i];
        if (s->used && key_eq(s->key, key)) {
            q->last_price = s->last_price;
            q->last_volume = s->last_volume;
            q->last_round = s->last_round;
        }
    }
    uint64_t maxshare = 0;
    for (uint32_t i = 0; i < CM_MAX_ORDERS; i++) {
        const cm_order_t *o = &m->ord[i];
        if (!live_order(o, key, now_ms)) continue;
        if (o->side == CM_BID) {
            if (o->price > q->best_bid) q->best_bid = o->price;
            q->bid_units += rem(o);
            continue;
        }
        if (!q->best_ask || o->price < q->best_ask) q->best_ask = o->price;
        q->ask_units += rem(o);
        bool first = true;
        for (uint32_t j = 0; j < i; j++) {
            const cm_order_t *p = &m->ord[j];
            if (live_order(p, key, now_ms) && p->side == CM_ASK &&
                meq(p->owner, o->owner, CM_ID_BYTES))
                first = false;
        }
        if (!first) continue;
        q->providers++;
        uint64_t units = 0;
        for (uint32_t j = i; j < CM_MAX_ORDERS; j++) {
            const cm_order_t *p = &m->ord[j];
            if (live_order(p, key, now_ms) && p->side == CM_ASK &&
                meq(p->owner, o->owner, CM_ID_BYTES))
                units += rem(p);
        }
        if (units > maxshare) maxshare = units;
    }
    /* concentrated: one provider, or one holding more than share of supply */
    pay_u128 lhs = pay_mul64(maxshare, m->p.share_den),
             rhs = pay_mul64(q->ask_units, m->p.share_num);
    q->concentrated = q->providers == 1 || (q->providers > 1 && pay_u128_cmp(lhs, rhs) > 0);
}

/* ===== delivery ===== */
static cm_contract_t *con_find(cm_market_t *m, uint32_t id)
{
    for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++)
        if (m->con[i].used && m->con[i].id == id) return &m->con[i];
    return 0;
}

const cm_contract_t *cm_contract(const cm_market_t *m, uint32_t id)
{
    return con_find((cm_market_t *) m, id);
}

void cm_proof_hash(const cm_proof_t *p, uint8_t out[32])
{
    uint8_t b[4 + 4 + 8 + 8 + 32 + 64];
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) (p->contract_id >> (24 - 8 * i));
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) (p->seq >> (24 - 8 * i));
    for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (p->units >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (p->at_ms >> (56 - 8 * i));
    mcpy(b + n, p->prev, 32);
    n += 32;
    mcpy(b + n, p->evidence, 64);
    n += 64;
    sha3_256(b, n, out);
}

cm_status_t cm_deliver(cm_market_t *m, const cm_proof_t *proof)
{
    if (!m || !proof) return CM_ERR_ARG;
    cm_contract_t *c = con_find(m, proof->contract_id);
    if (!c) return CM_ERR_UNKNOWN;
    if (c->state != CM_C_OPEN) return CM_ERR_STATE;
    if (proof->seq != c->proof_seq + 1u || !meq(proof->prev, c->proof_tip, 32)) return CM_ERR_PROOF;
    if (!proof->units || proof->units > c->qty - c->delivered) return CM_ERR_ARG;
    if (proof->at_ms < c->start_ms) return CM_ERR_ARG;
    if (proof->at_ms > c->end_ms) return CM_ERR_LATE;
    if (!m->p.verify || !m->p.verify(m->p.ctx, c, proof)) return CM_ERR_PROOF;
    cm_account_t *buyer = acct_find(m, c->buyer), *prov = acct_find(m, c->provider);
    if (!buyer || !prov) return CM_ERR_STATE;
    uint64_t pay = proof->units * c->price; /* <= escrow */
    if (pay > c->escrow) return CM_ERR_STATE;
    uint64_t t = tithe_of(m, pay);
    c->escrow -= pay;
    buyer->escrow -= pay;
    prov->available += pay - t;
    prov->earned += pay - t;
    prov->delivered += proof->units;
    m->commons += t;
    c->paid += pay - t;
    c->tithe += t;
    c->delivered += proof->units;
    post(m, CM_POST_PAY, c->buyer, c->provider, pay - t);
    post(m, CM_POST_TITHE, c->buyer, 0, t);
    c->proof_seq++;
    cm_proof_hash(proof, c->proof_tip);
    if (c->delivered == c->qty) c->state = CM_C_CLOSED;
    return CM_OK;
}

uint32_t cm_expire(cm_market_t *m, uint64_t now_ms)
{
    if (!m) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++) {
        cm_contract_t *c = &m->con[i];
        if (!c->used || c->state != CM_C_OPEN || now_ms < c->end_ms) continue;
        cm_account_t *buyer = acct_find(m, c->buyer), *prov = acct_find(m, c->provider);
        if (buyer) {
            buyer->escrow -= c->escrow;
            buyer->available += c->escrow; /* exactly what is left: no fee, no interest */
            post(m, CM_POST_REFUND, 0, c->buyer, c->escrow);
        }
        if (prov) prov->missed += c->qty - c->delivered;
        c->escrow = 0;
        c->state = CM_C_CLOSED;
        n++;
    }
    return n;
}

bool cm_conserved(const cm_market_t *m)
{
    if (!m) return false;
    uint64_t sum = m->commons;
    for (uint32_t i = 0; i < CM_MAX_ACCOUNTS; i++) {
        const cm_account_t *a = &m->acct[i];
        if (!a->used) continue;
        sum += a->available + a->locked + a->escrow;
        uint64_t lk = 0, es = 0;
        for (uint32_t j = 0; j < CM_MAX_ORDERS; j++) {
            const cm_order_t *o = &m->ord[j];
            if (o->used && o->side == CM_BID && meq(o->owner, a->id, CM_ID_BYTES)) lk += o->locked;
        }
        for (uint32_t j = 0; j < CM_MAX_CONTRACTS; j++) {
            const cm_contract_t *c = &m->con[j];
            if (c->used && meq(c->buyer, a->id, CM_ID_BYTES)) es += c->escrow;
        }
        if (lk != a->locked || es != a->escrow) return false;
    }
    for (uint32_t j = 0; j < CM_MAX_CONTRACTS; j++) {
        const cm_contract_t *c = &m->con[j];
        if (!c->used) continue;
        if (c->escrow + c->delivered * c->price != c->qty * c->price && c->state == CM_C_OPEN)
            return false;
        if (c->paid + c->tithe != c->delivered * c->price) return false;
    }
    return sum == m->deposits;
}
