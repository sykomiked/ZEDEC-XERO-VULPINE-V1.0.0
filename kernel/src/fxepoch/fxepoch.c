/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fxepoch.c — FX uniform-price batch epochs. See fxepoch.h F1-F13. */
#include "fxepoch.h"
#include "../robin_debanks/sha256.h"

static const pay_account_t *acct(const fx_t *m, uint32_t id)
{
    return pay_ledger_account(m->L, id); /* NULL when unknown or inactive */
}

void fx_config_default(fx_config_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->band_bps = FX_BAND_DEFAULT;
    c->max_orders = FX_MAX_ORDERS;
}

static fx_status_t cfg_check(const fx_config_t *c)
{
    if (!c) return FX_ERR_ARG;
    if (c->leverage) return FX_ERR_LEVERAGE;
    if (c->band_bps >= FX_BPS || !c->max_orders || c->max_orders > FX_MAX_ORDERS) return FX_ERR_ARG;
    return FX_OK;
}

fx_status_t fx_init(fx_t *m, pay_ledger_t *L, const fx_config_t *cfg, uint32_t market_id,
                    fx_verify_fn verify, void *vctx, const uint8_t seed[32])
{
    if (!m || !L || !verify) return FX_ERR_ARG;
    fx_status_t st = cfg_check(cfg);
    if (st != FX_OK) return st;
    pay_memset(m, 0, sizeof *m);
    m->L = L;
    m->cfg = *cfg;
    m->market_id = market_id;
    m->verify = verify;
    m->vctx = vctx;
    if (seed) pay_memcpy(m->seed, seed, 32);
    m->last_ledger = PAY_OK;
    m->ready = true;
    return FX_OK;
}

static bool between_epochs(const fx_t *m)
{
    return m->state == FX_IDLE || m->state == FX_SETTLED;
}

fx_status_t fx_set_config(fx_t *m, const fx_config_t *cfg)
{
    if (!m || !m->ready) return FX_ERR_ARG;
    fx_status_t st = cfg_check(cfg);
    if (st != FX_OK) return st;
    if (!between_epochs(m)) return FX_ERR_STATE;
    m->cfg = *cfg;
    return FX_OK;
}

/* ---- F2 books ---- */

static bool is_escrow(const fx_t *m, uint32_t a)
{
    for (uint32_t i = 0; i < m->n_books; i++)
        if (a == m->book[i].escrow_base || a == m->book[i].escrow_quote) return true;
    return false;
}

static bool is_module_acct(const fx_t *m, uint32_t a)
{
    for (uint32_t i = 0; i < m->n_books; i++)
        if (a == m->book[i].fee_acct) return true;
    return is_escrow(m, a);
}

/* sum of fee_total over the books whose fee account is `a`, mod 2^64
 * (F13: every true total is below 2^64, so equality mod 2^64 is exact) */
static uint64_t fees_of(const fx_t *m, uint32_t a)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < m->n_books; i++)
        if (m->book[i].fee_acct == a) s += m->book[i].fee_total;
    return s;
}

fx_status_t fx_add_book(fx_t *m, uint32_t escrow_base, uint32_t escrow_quote, uint32_t fee_acct,
                        uint64_t scale, uint32_t *index)
{
    if (!m || !m->ready || !scale) return FX_ERR_ARG;
    if (!between_epochs(m)) return FX_ERR_STATE;
    if (m->n_books >= FX_MAX_BOOKS) return FX_ERR_FULL;
    const pay_account_t *eb = acct(m, escrow_base), *eq = acct(m, escrow_quote),
                        *f = acct(m, fee_acct);
    if (!eb || !eq || !f) return FX_ERR_ARG;
    if (escrow_base == escrow_quote || fee_acct == escrow_base || fee_acct == escrow_quote)
        return FX_ERR_ARG;
    if ((eb->flags | eq->flags | f->flags) & PAY_ACCT_ISSUER) return FX_ERR_ARG;
    if (eb->asset == eq->asset || f->asset != eq->asset) return FX_ERR_ARG;
    if (is_module_acct(m, escrow_base) || is_module_acct(m, escrow_quote) || is_escrow(m, fee_acct))
        return FX_ERR_ARG;
    for (uint32_t i = 0; i < m->n_books; i++) {
        const fx_book_t *o = &m->book[i];
        if ((o->base == eb->asset && o->quote == eq->asset) ||
            (o->base == eq->asset && o->quote == eb->asset))
            return FX_ERR_ARG; /* one book per pair */
    }
    /* F2: a shared fee account keeps the baseline of its first book */
    uint64_t fbase = f->debit;
    for (uint32_t i = m->n_books; i-- > 0;)
        if (m->book[i].fee_acct == fee_acct) fbase = m->book[i].fee_start;
    fx_book_t *b = &m->book[m->n_books];
    pay_memset(b, 0, sizeof *b);
    b->base = eb->asset;
    b->quote = eq->asset;
    b->escrow_base = escrow_base;
    b->escrow_quote = escrow_quote;
    b->fee_acct = fee_acct;
    b->scale = scale;
    b->base_start = eb->debit;
    b->quote_start = eq->debit;
    b->fee_start = fbase;
    if (index) *index = m->n_books;
    m->n_books++;
    return FX_OK;
}

/* ---- F3, F4, F6 pure helpers ---- */

bool fx_quote_value(uint64_t x, uint64_t p, uint64_t scale, uint64_t *out)
{
    if (!out) return false;
    return pay_muldiv(x, p, scale, out, 0);
}

bool fx_buy_lock(uint64_t qty, uint64_t limit, uint64_t scale, uint64_t *out)
{
    uint64_t q, rem;
    if (!out || !pay_muldiv(qty, limit, scale, &q, &rem)) return false;
    return pay_add_ok(q, rem != 0, out);
}

bool fx_band(uint64_t ref, uint32_t band_bps, uint64_t *lo, uint64_t *hi)
{
    uint64_t l, r, h;
    if (!lo || !hi || !ref || band_bps >= FX_BPS) return false;
    pay_muldiv(ref, FX_BPS - band_bps, FX_BPS, &l, &r); /* <= ref: fits */
    *lo = l + (r != 0);
    *hi = pay_muldiv(ref, FX_BPS + band_bps, FX_BPS, &h, 0) ? h : UINT64_MAX;
    return true;
}

/* ---- F5 ---- */

static bool eligible(const fx_order_t *o, uint64_t p)
{
    return o->side == FX_SELL ? o->limit <= p : o->limit >= p;
}

/* Sums fit: each side's quantity total is below PAY_BAL_MAX (F4). */
static uint64_t side_sum(const fx_t *m, uint32_t book, uint8_t side, uint64_t p)
{
    uint64_t s = 0;
    for (uint32_t i = 0; m && i < m->n_orders; i++) {
        const fx_order_t *o = &m->order[i];
        if (o->book == book && o->side == side && eligible(o, p)) s += o->qty;
    }
    return s;
}

uint64_t fx_supply(const fx_t *m, uint32_t book, uint64_t p)
{
    return side_sum(m, book, FX_SELL, p);
}

uint64_t fx_demand(const fx_t *m, uint32_t book, uint64_t p)
{
    return side_sum(m, book, FX_BUY, p);
}

/* ---- ledger postings ---- */

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void fx_order_msg(uint32_t market_id, uint64_t epoch, uint32_t book, uint8_t side,
                  uint32_t base_acct, uint32_t quote_acct, uint64_t qty, uint64_t limit,
                  uint64_t nonce, uint8_t out[FX_ORDER_MSG])
{
    static const char tag[] = "zxv/fxepoch/v1";
    for (uint32_t i = 0; i < 14; i++) out[i] = (uint8_t) tag[i];
    put32(out + 14, market_id);
    put64(out + 18, epoch);
    put32(out + 26, book);
    out[30] = side;
    put32(out + 31, base_acct);
    put32(out + 35, quote_acct);
    put64(out + 39, qty);
    put64(out + 47, limit);
    put64(out + 55, nonce);
}

static void derive(const fx_t *m, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, m->seed, 32);
    in[32] = tag;
    put64(in + 33, m->seq);
    sha256(in, sizeof in, out);
}

static fx_status_t post(fx_t *m, const pay_line_t *ln, uint32_t n, uint8_t kind, uint64_t tick,
                        const char *memo)
{
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    for (uint32_t i = 0; i < n; i++) r.lines[i] = ln[i];
    r.n_lines = n;
    uint8_t d[32];
    derive(m, 'I', r.idem_key);
    derive(m, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "FXEP-");
    pay_w_u64(&w, m->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, memo, sizeof r.memo);
    r.tick = tick;
    r.kind = kind;
    r.ext_cap = PAY_CAP_FINANCIAL;
    m->seq++;
    pay_receipt_t rc;
    m->last_ledger = pay_ledger_post(m->L, &r, &rc);
    return m->last_ledger == PAY_OK ? FX_OK : FX_ERR_LEDGER;
}

/* ---- F4, F11 ---- */

fx_status_t fx_open(fx_t *m)
{
    if (!m || !m->ready) return FX_ERR_ARG;
    if (!m->cfg.enabled) return FX_ERR_DISABLED;
    if (!between_epochs(m) || !m->n_books) return FX_ERR_STATE;
    m->epoch++;
    m->state = FX_OPEN;
    m->mode = 0;
    m->n_orders = 0; /* slots past n_orders / n_fills are dead: never read */
    m->n_fills = 0;
    for (uint32_t i = 0; i < m->n_books; i++)
        m->book[i].sell_qty = m->book[i].buy_qty = m->book[i].quote_lock = 0;
    return FX_OK;
}

fx_status_t fx_order(fx_t *m, uint32_t book, uint8_t side, uint32_t base_acct, uint32_t quote_acct,
                     uint64_t qty, uint64_t limit, uint64_t nonce, uint64_t tick,
                     const uint8_t *sig, uint32_t sig_len, uint32_t *index)
{
    if (!m || !m->ready || !sig) return FX_ERR_ARG;
    if (!m->cfg.enabled) return FX_ERR_DISABLED;
    if (m->state != FX_OPEN) return FX_ERR_STATE;
    if (book >= m->n_books || side > FX_BUY || !qty || !limit) return FX_ERR_ARG;
    fx_book_t *b = &m->book[book];
    const pay_account_t *ba = acct(m, base_acct), *qa = acct(m, quote_acct);
    if (!ba || !qa || ba->asset != b->base || qa->asset != b->quote || ba->owner != qa->owner)
        return FX_ERR_ARG;
    if (is_module_acct(m, base_acct) || is_module_acct(m, quote_acct)) return FX_ERR_ARG;
    uint64_t lock, sells = b->sell_qty, buys = b->buy_qty, quote = b->quote_lock;
    if (side == FX_SELL) {
        lock = qty;
        if (!pay_add_ok(sells, qty, &sells) || sells >= PAY_BAL_MAX) return FX_ERR_ARG;
    } else {
        if (!fx_buy_lock(qty, limit, b->scale, &lock) || !pay_add_ok(buys, qty, &buys) ||
            buys >= PAY_BAL_MAX || !pay_add_ok(quote, lock, &quote) || quote >= PAY_BAL_MAX)
            return FX_ERR_ARG;
    }
    if (m->n_orders >= m->cfg.max_orders) return FX_ERR_FULL;
    uint8_t msg[FX_ORDER_MSG];
    fx_order_msg(m->market_id, m->epoch, book, side, base_acct, quote_acct, qty, limit, nonce, msg);
    if (!m->verify(m->vctx, ba->owner, msg, sizeof msg, sig, sig_len)) return FX_ERR_SIG;
    for (uint32_t i = 0; i < m->n_orders; i++)
        if (m->order[i].owner == ba->owner && m->order[i].nonce == nonce) return FX_ERR_DUP;
    uint32_t from = side == FX_SELL ? base_acct : quote_acct;
    uint32_t to = side == FX_SELL ? b->escrow_base : b->escrow_quote;
    const pay_line_t ln[2] = {{from, -(int64_t) lock, 0}, {to, (int64_t) lock, 0}};
    fx_status_t st = post(m, ln, 2, PAY_KIND_TRANSFER, tick, "fx order lock");
    if (st != FX_OK) return st;
    fx_order_t *o = &m->order[m->n_orders];
    pay_memset(o, 0, sizeof *o);
    o->book = book;
    o->side = side;
    o->base_acct = base_acct;
    o->quote_acct = quote_acct;
    o->owner = ba->owner;
    o->qty = qty;
    o->limit = limit;
    o->nonce = nonce;
    o->locked = lock;
    o->seq = m->order_seq++;
    b->sell_qty = sells;
    b->buy_qty = buys;
    b->quote_lock = quote;
    if (index) *index = m->n_orders;
    m->n_orders++;
    return FX_OK;
}

/* ---- F5-F9 clearing ---- */

static uint64_t absdiff(uint64_t a, uint64_t b)
{
    return a > b ? a - b : b - a;
}

static uint64_t vol(uint64_t s, uint64_t d)
{
    return s < d ? s : d;
}

/* F5: the market price p*; *found false when nothing crosses. */
static uint64_t market_price(const fx_t *m, uint32_t bi, bool *found)
{
    const fx_book_t *b = &m->book[bi];
    uint64_t bv = 0, bimb = 0, lo = UINT64_MAX, hi = 0, best = UINT64_MAX, bdist = UINT64_MAX;
    *found = false;
    /* pass 1: max V, then least |S - D| */
    for (uint32_t i = 0; i < m->n_orders; i++) {
        if (m->order[i].book != bi) continue;
        uint64_t p = m->order[i].limit, s = fx_supply(m, bi, p), d = fx_demand(m, bi, p);
        uint64_t v = vol(s, d), imb = absdiff(s, d);
        if (v > bv || (v == bv && imb < bimb)) {
            bv = v;
            bimb = imb;
        }
    }
    if (!bv) return 0;
    /* pass 2: the tie set's range */
    for (uint32_t i = 0; i < m->n_orders; i++) {
        if (m->order[i].book != bi) continue;
        uint64_t p = m->order[i].limit, s = fx_supply(m, bi, p), d = fx_demand(m, bi, p);
        if (vol(s, d) != bv || absdiff(s, d) != bimb) continue;
        if (p < lo) lo = p;
        if (p > hi) hi = p;
    }
    uint64_t ref = b->has_ref ? b->ref : lo + (hi - lo) / 2;
    /* pass 3: closest to ref, then lowest (dist < UINT64_MAX: p, ref >= 1) */
    for (uint32_t i = 0; i < m->n_orders; i++) {
        if (m->order[i].book != bi) continue;
        uint64_t p = m->order[i].limit, s = fx_supply(m, bi, p), d = fx_demand(m, bi, p);
        if (vol(s, d) != bv || absdiff(s, d) != bimb) continue;
        uint64_t dist = absdiff(p, ref);
        if (dist < bdist || (dist == bdist && p < best)) {
            best = p;
            bdist = dist;
        }
    }
    *found = true;
    return best;
}

/* F7: share `amount` (<= total) of one side pro rata by qty, floors, then one
 * leftover unit each in submission order. */
static void prorate(fx_t *m, uint32_t bi, uint8_t side, uint64_t p, uint64_t amount, uint64_t total)
{
    uint64_t given = 0;
    for (uint32_t i = 0; i < m->n_orders; i++) {
        fx_order_t *o = &m->order[i];
        if (o->book != bi || o->side != side || !eligible(o, p)) continue;
        pay_muldiv(amount, o->qty, total, &o->fill, 0); /* <= qty: fits */
        given += o->fill;
    }
    for (uint32_t i = 0; i < m->n_orders && given < amount; i++) {
        fx_order_t *o = &m->order[i];
        if (o->book != bi || o->side != side || !eligible(o, p)) continue;
        o->fill++;
        given++;
    }
}

/* next order of `side` in book bi with fill > 0, from index i */
static uint32_t next_filled(const fx_t *m, uint32_t bi, uint8_t side, uint32_t i)
{
    while (i < m->n_orders &&
           (m->order[i].book != bi || m->order[i].side != side || !m->order[i].fill))
        i++;
    return i;
}

static fx_status_t clear_book(fx_t *m, uint32_t bi)
{
    fx_book_t *b = &m->book[bi];
    fx_record_t *r = &b->cur;
    bool found;
    uint64_t p = market_price(m, bi, &found);
    r->epoch = m->epoch;
    r->book = bi;
    r->market = p;
    r->ref_before = b->has_ref ? b->ref : 0;
    b->has_ref_next = b->has_ref;
    b->ref_next = b->ref;
    b->carry_next = b->carry;
    if (!found) {
        r->price = r->ref_before;
        return FX_OK;
    }
    uint64_t lo, hi;
    if (m->cfg.band_bps && b->has_ref && fx_band(b->ref, m->cfg.band_bps, &lo, &hi)) {
        if (p < lo) p = lo, r->cooling = true;
        if (p > hi) p = hi, r->cooling = true;
    }
    uint64_t S = fx_supply(m, bi, p), D = fx_demand(m, bi, p), V = vol(S, D);
    r->price = p;
    r->supply = S;
    r->demand = D;
    r->volume = V;
    b->has_ref_next = true;
    b->ref_next = p;
    if (!V) return FX_OK; /* a cooling epoch that crossed nothing at the clamp */
    prorate(m, bi, FX_SELL, p, V, S);
    prorate(m, bi, FX_BUY, p, V, D);
    /* F8: pair the fills in submission order */
    /* V > 0: both sides have a filled order; their fills sum to V each, so
     * the two walks run out on the same step */
    uint32_t s = next_filled(m, bi, FX_SELL, 0), d = next_filled(m, bi, FX_BUY, 0);
    uint64_t rs = m->order[s].fill, rd = m->order[d].fill;
    uint64_t carry = b->carry, gross = 0, fees = 0;
    for (;;) {
        uint64_t x = rs < rd ? rs : rd, q, fee;
        fx_quote_value(x, p, b->scale, &q); /* <= the buyer's lock: fits */
        if (!pay_assure_fee_carry(carry, q, &fee, &carry)) return FX_ERR_STATE;
        fx_fill_t *f = &m->fill[m->n_fills++]; /* <= n - 1 per book: fits */
        f->book = bi;
        f->sell = s;
        f->buy = d;
        f->qty = x;
        f->quote = q;
        f->fee = fee;
        m->order[s].quote += q;
        m->order[s].fee += fee;
        m->order[d].quote += q;
        gross += q; /* <= the book's quote locks < PAY_BAL_MAX */
        fees += fee;
        r->fills++;
        rs -= x;
        rd -= x;
        if (!rs) {
            s = next_filled(m, bi, FX_SELL, s + 1);
            if (s >= m->n_orders) break;
            rs = m->order[s].fill;
        }
        if (!rd) {
            d = next_filled(m, bi, FX_BUY, d + 1);
            rd = m->order[d].fill;
        }
    }
    r->gross = gross;
    r->fee = fees;
    uint64_t t;
    if (!pay_add_ok(b->gross_total, gross, &t) || !pay_add_ok(b->fee_total, fees, &t) ||
        !pay_add_ok(b->volume_total, V, &t))
        return FX_ERR_OVERFLOW;
    b->carry_next = carry;
    return FX_OK;
}

static void drop_fills(fx_t *m)
{
    for (uint32_t i = 0; i < m->n_orders; i++) {
        m->order[i].fill = m->order[i].quote = m->order[i].fee = 0;
    }
    m->n_fills = 0;
}

fx_status_t fx_clear(fx_t *m)
{
    if (!m || !m->ready) return FX_ERR_ARG;
    if (m->state != FX_OPEN) return FX_ERR_STATE;
    drop_fills(m);
    for (uint32_t i = 0; i < m->n_books; i++) {
        pay_memset(&m->book[i].cur, 0, sizeof m->book[i].cur);
        fx_status_t st = clear_book(m, i);
        if (st != FX_OK) {
            drop_fills(m);
            return st;
        }
    }
    m->state = FX_CLEARED;
    return FX_OK;
}

/* ---- F10, F11 ---- */

static void finish(fx_t *m, bool cancelled)
{
    for (uint32_t i = 0; i < m->n_books; i++) {
        fx_book_t *b = &m->book[i];
        fx_record_t *r = &b->cur;
        r->epoch = m->epoch;
        r->book = i;
        if (cancelled) {
            uint64_t ref = b->has_ref ? b->ref : 0;
            pay_memset(r, 0, sizeof *r);
            r->epoch = m->epoch;
            r->book = i;
            r->ref_before = ref;
            r->price = ref;
            r->cancelled = true;
        } else { /* fits: checked by fx_clear */
            b->gross_total += r->gross;
            b->fee_total += r->fee;
            b->volume_total += r->volume;
            b->carry = b->carry_next;
            b->has_ref = b->has_ref_next;
            b->ref = b->ref_next;
        }
        m->hist[m->n_hist % FX_HISTORY] = *r;
        m->n_hist++;
    }
    m->state = FX_SETTLED;
    m->mode = 0;
}

fx_status_t fx_settle(fx_t *m, uint64_t now)
{
    if (!m || !m->ready) return FX_ERR_ARG;
    if (m->state != FX_CLEARED || m->mode == 2) return FX_ERR_STATE;
    m->mode = 1;
    for (uint32_t i = 0; i < m->n_orders; i++) {
        fx_order_t *o = &m->order[i];
        if (o->settled) continue;
        const fx_book_t *b = &m->book[o->book];
        pay_line_t ln[5];
        uint32_t n = 0;
        if (o->side == FX_SELL) {
            uint64_t back = o->locked - o->fill, net = o->quote - o->fee;
            if (back) {
                ln[n++] = (pay_line_t){b->escrow_base, -(int64_t) back, 0};
                ln[n++] = (pay_line_t){o->base_acct, (int64_t) back, 0};
            }
            if (o->quote) ln[n++] = (pay_line_t){b->escrow_quote, -(int64_t) o->quote, 0};
            if (net) ln[n++] = (pay_line_t){o->quote_acct, (int64_t) net, 0};
            if (o->fee) ln[n++] = (pay_line_t){b->fee_acct, (int64_t) o->fee, 0};
        } else {
            uint64_t back = o->locked - o->quote;
            if (back) {
                ln[n++] = (pay_line_t){b->escrow_quote, -(int64_t) back, 0};
                ln[n++] = (pay_line_t){o->quote_acct, (int64_t) back, 0};
            }
            if (o->fill) {
                ln[n++] = (pay_line_t){b->escrow_base, -(int64_t) o->fill, 0};
                ln[n++] = (pay_line_t){o->base_acct, (int64_t) o->fill, 0};
            }
        }
        if (n) {
            fx_status_t st = post(m, ln, n, PAY_KIND_TRADE, now, "fx epoch settle");
            if (st != FX_OK) return st;
        }
        o->settled = true;
    }
    finish(m, false);
    return FX_OK;
}

fx_status_t fx_cancel(fx_t *m, uint64_t now)
{
    if (!m || !m->ready) return FX_ERR_ARG;
    if ((m->state != FX_OPEN && m->state != FX_CLEARED) || m->mode == 1) return FX_ERR_STATE;
    m->state = FX_CLEARED; /* closed to orders and clearing from here on */
    m->mode = 2;
    drop_fills(m);
    for (uint32_t i = 0; i < m->n_books; i++) pay_memset(&m->book[i].cur, 0, sizeof m->book[i].cur);
    for (uint32_t i = 0; i < m->n_orders; i++) {
        fx_order_t *o = &m->order[i];
        if (o->settled) continue;
        const fx_book_t *b = &m->book[o->book];
        uint32_t from = o->side == FX_SELL ? b->escrow_base : b->escrow_quote;
        uint32_t to = o->side == FX_SELL ? o->base_acct : o->quote_acct;
        const pay_line_t ln[2] = {{from, -(int64_t) o->locked, 0}, {to, (int64_t) o->locked, 0}};
        fx_status_t st = post(m, ln, 2, PAY_KIND_RETURN, now, "fx epoch cancel refund");
        if (st != FX_OK) return st;
        o->settled = true;
    }
    finish(m, true);
    return FX_OK;
}

/* ---- F12 ---- */

static fx_status_t add_pos(fx_net_t *n, uint32_t owner, uint16_t asset, int64_t d)
{
    uint32_t i = 0;
    while (i < n->n_pos && (n->pos[i].owner != owner || n->pos[i].asset != asset)) i++;
    if (i == n->n_pos) {
        if (n->n_pos >= FX_NET_MAX) return FX_ERR_FULL;
        n->pos[i].owner = owner;
        n->pos[i].asset = asset;
        n->pos[i].net = 0;
        n->n_pos++;
    }
    return pay_sadd_ok(n->pos[i].net, d, &n->pos[i].net) ? FX_OK : FX_ERR_OVERFLOW;
}

static fx_status_t net_compute(const fx_t *m, fx_net_t *out)
{
    pay_memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < m->n_fills; i++) {
        const fx_fill_t *f = &m->fill[i];
        const fx_book_t *b = &m->book[f->book];
        const fx_order_t *s = &m->order[f->sell], *d = &m->order[f->buy];
        const pay_account_t *fa = acct(m, b->fee_acct);
        if (!fa) return FX_ERR_STATE;
        uint64_t net = f->quote - f->fee;
        /* every amount < PAY_BAL_MAX: the int64 casts are exact */
        fx_status_t st = add_pos(out, s->owner, b->base, -(int64_t) f->qty);
        if (st == FX_OK) st = add_pos(out, d->owner, b->base, (int64_t) f->qty);
        if (st == FX_OK) st = add_pos(out, d->owner, b->quote, -(int64_t) f->quote);
        if (st == FX_OK && net) st = add_pos(out, s->owner, b->quote, (int64_t) net);
        if (st == FX_OK && f->fee) st = add_pos(out, fa->owner, b->quote, (int64_t) f->fee);
        if (st != FX_OK) return st;
        out->gross_legs += 1u + (net != 0) + (f->fee != 0);
    }
    out->balanced = true;
    for (uint32_t i = 0; i < out->n_pos; i++) {
        uint16_t a = out->pos[i].asset;
        bool first = true;
        for (uint32_t j = 0; j < i; j++)
            if (out->pos[j].asset == a) first = false;
        if (out->pos[i].net) out->nonzero++;
        if (!first) continue;
        int64_t sum = 0;
        uint32_t k = 0;
        for (uint32_t j = i; j < out->n_pos; j++) {
            if (out->pos[j].asset != a) continue;
            if (!pay_sadd_ok(sum, out->pos[j].net, &sum)) return FX_ERR_OVERFLOW;
            k += out->pos[j].net != 0;
        }
        if (sum) out->balanced = false;
        if (k) out->net_legs += k - 1;
    }
    out->saved = out->gross_legs > out->net_legs ? out->gross_legs - out->net_legs : 0;
    return FX_OK;
}

fx_status_t fx_net(const fx_t *m, fx_net_t *out)
{
    if (!m || !m->ready || !out) return FX_ERR_ARG;
    if (m->state != FX_CLEARED && m->state != FX_SETTLED) return FX_ERR_STATE;
    return net_compute(m, out);
}

/* ---- F13 ---- */

const fx_record_t *fx_record(const fx_t *m, uint64_t epoch, uint32_t book)
{
    if (!m || !epoch) return 0;
    for (uint32_t i = 0; i < FX_HISTORY && i < m->n_hist; i++) {
        const fx_record_t *r = &m->hist[i];
        if (r->epoch == epoch && r->book == book) return r;
    }
    return 0;
}

/* escrow and fee balances the F10 postings so far leave behind */
static bool book_balances_ok(const fx_t *m, uint32_t bi)
{
    const fx_book_t *b = &m->book[bi];
    const pay_account_t *eb = acct(m, b->escrow_base), *eq = acct(m, b->escrow_quote),
                        *fa = acct(m, b->fee_acct);
    if (!eb || !eq || !fa || eb->credit || eq->credit) return false;
    /* baseline + every lock - what settled postings released, and the fee
     * account's baseline + fees, all mod 2^64: each true value is a ledger
     * balance (< 2^62), so equality mod 2^64 is exact equality */
    uint64_t base = b->base_start, quote = b->quote_start;
    uint64_t fee = b->fee_start + fees_of(m, b->fee_acct);
    for (uint32_t i = 0; i < m->n_orders; i++) {
        const fx_order_t *o = &m->order[i];
        if (o->book != bi) continue;
        if (o->side == FX_SELL) {
            base += o->locked;
            if (o->settled) base -= o->locked - o->fill, quote -= o->quote;
        } else {
            quote += o->locked;
            if (o->settled) base -= o->fill, quote -= o->locked - o->quote;
        }
    }
    /* fees of settled sells, of every book on this fee account, not yet
     * committed (CLEARED only); o->book was range-checked by order_ok */
    for (uint32_t i = 0; m->state == FX_CLEARED && i < m->n_orders; i++) {
        const fx_order_t *o = &m->order[i];
        if (o->settled && m->book[o->book].fee_acct == b->fee_acct) fee += o->fee;
    }
    return eb->debit == base && eq->debit == quote && fa->debit == fee;
}

static bool order_ok(const fx_t *m, const fx_order_t *o)
{
    uint64_t lock;
    if (o->book >= m->n_books || o->side > FX_BUY || !o->qty || !o->limit || o->fill > o->qty)
        return false;
    if (o->side == FX_SELL) return o->locked == o->qty && o->fee <= o->quote;
    return fx_buy_lock(o->qty, o->limit, m->book[o->book].scale, &lock) && o->locked == lock &&
           o->quote <= o->locked && !o->fee;
}

static bool fills_ok(const fx_t *m)
{
    uint64_t q[FX_MAX_ORDERS], v[FX_MAX_ORDERS], fe[FX_MAX_ORDERS];
    uint64_t bv[FX_MAX_BOOKS], bg[FX_MAX_BOOKS], bf[FX_MAX_BOOKS];
    pay_memset(q, 0, sizeof q);
    pay_memset(v, 0, sizeof v);
    pay_memset(fe, 0, sizeof fe);
    pay_memset(bv, 0, sizeof bv);
    pay_memset(bg, 0, sizeof bg);
    pay_memset(bf, 0, sizeof bf);
    if (m->n_fills > FX_MAX_FILLS) return false;
    for (uint32_t i = 0; i < m->n_fills; i++) {
        const fx_fill_t *f = &m->fill[i];
        uint64_t want;
        if (f->book >= m->n_books || f->sell >= m->n_orders || f->buy >= m->n_orders || !f->qty)
            return false;
        const fx_order_t *s = &m->order[f->sell], *d = &m->order[f->buy];
        const fx_book_t *b = &m->book[f->book];
        if (s->side != FX_SELL || d->side != FX_BUY || s->book != f->book || d->book != f->book)
            return false;
        if (!fx_quote_value(f->qty, b->cur.price, b->scale, &want) || want != f->quote ||
            f->fee > f->quote)
            return false;
        /* every partial sum is bounded by an order's or a book's total */
        v[f->sell] += f->qty;
        v[f->buy] += f->qty;
        q[f->sell] += f->quote;
        q[f->buy] += f->quote;
        fe[f->sell] += f->fee;
        bv[f->book] += f->qty;
        bg[f->book] += f->quote;
        bf[f->book] += f->fee;
    }
    for (uint32_t i = 0; i < m->n_orders; i++) {
        const fx_order_t *o = &m->order[i];
        if (v[i] != o->fill || q[i] != o->quote || fe[i] != o->fee) return false;
    }
    for (uint32_t i = 0; i < m->n_books; i++) {
        const fx_record_t *r = &m->book[i].cur;
        if (bv[i] != r->volume || bg[i] != r->gross || bf[i] != r->fee) return false;
    }
    return true;
}

bool fx_check(const fx_t *m)
{
    if (!m || !m->ready || m->cfg.leverage || !pay_ledger_check(m->L)) return false;
    if (m->n_books > FX_MAX_BOOKS || m->n_orders > FX_MAX_ORDERS) return false;
    for (uint32_t i = 0; i < m->n_orders; i++)
        if (!order_ok(m, &m->order[i])) return false;
    for (uint32_t i = 0; i < m->n_books; i++) {
        const fx_book_t *b = &m->book[i];
        if (b->carry >= PAY_ASSURE_DEN || !book_balances_ok(m, i)) return false;
        pay_u128 lhs = pay_u128_add64(pay_mul64(b->fee_total, PAY_ASSURE_DEN), b->carry);
        if (pay_u128_cmp(lhs, pay_mul64(b->gross_total, PAY_ASSURE_NUM)) != 0) return false;
    }
    bool epoch_fills = m->state == FX_CLEARED || m->state == FX_SETTLED;
    if (epoch_fills && !fills_ok(m)) return false;
    if (!epoch_fills && m->n_fills) return false;
    fx_net_t n;
    if (net_compute(m, &n) != FX_OK || !n.balanced) return false;
    return true;
}
