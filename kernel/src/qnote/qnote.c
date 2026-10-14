/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qnote.c — Q-Note lots: Dutch auction, first loss, salvage recovery. See
 * qnote.h Q1-Q8. */
#include "qnote.h"

static const pay_account_t *acct(const qnote_t *q, uint32_t id)
{
    return pay_ledger_account(q->L, id); /* NULL when unknown or inactive */
}

static bool is_issuer(const pay_account_t *a)
{
    return (a->flags & PAY_ACCT_ISSUER) != 0;
}

qnote_status_t qnote_init(qnote_t *q, pay_ledger_t *L, uint32_t reserve, uint32_t escrow,
                          const uint8_t seed[32])
{
    if (!q || !L) return QNOTE_ERR_ARG;
    pay_memset(q, 0, sizeof *q);
    q->L = L;
    const pay_account_t *r = acct(q, reserve), *e = acct(q, escrow);
    if (!r || !e || reserve == escrow || r->asset != e->asset) return QNOTE_ERR_ARG;
    if (is_issuer(r) || is_issuer(e) || e->debit) return QNOTE_ERR_ARG;
    q->reserve = reserve;
    q->escrow = escrow;
    q->fiat = r->asset;
    if (seed) pay_memcpy(q->seed, seed, 32);
    q->last_ledger = PAY_OK;
    q->ready = true;
    return QNOTE_OK;
}

/* ---- ledger postings ---- */

/* SHA3-256 of "zxv/qnote/req" || seed || kind || seq: the tag keeps these
 * ids apart from another module's ids built from the same seed */
static void derive(const qnote_t *q, uint8_t kind, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "zxv/qnote/req");
    pay_hbuf_put(&h, q->seed, 32);
    pay_hbuf_put(&h, &kind, 1);
    pay_hbuf_u64(&h, q->seq);
    pay_hbuf_final(&h, out);
}

/* a request with identifiers derived from the seed and the counter */
static void mkreq(qnote_t *q, pay_posting_req_t *r, uint64_t tick)
{
    uint8_t d[32];
    pay_memset(r, 0, sizeof *r);
    derive(q, 'I', r->idem_key);
    derive(q, 'U', d);
    pay_uetr_from_random(d, r->uetr);
    pay_w w;
    pay_w_init(&w, r->e2e, sizeof r->e2e);
    pay_w_s(&w, "QNOTE-");
    pay_w_u64(&w, q->seq);
    pay_w_finish(&w);
    r->tick = tick;
    r->ext_cap = PAY_CAP_FINANCIAL;
    q->seq++;
}

static qnote_status_t post(qnote_t *q, const pay_line_t *ln, uint32_t n, uint8_t kind,
                           uint64_t tick)
{
    pay_posting_req_t r;
    mkreq(q, &r, tick);
    for (uint32_t i = 0; i < n; i++) r.lines[i] = ln[i];
    r.n_lines = n;
    r.kind = kind;
    pay_receipt_t rc;
    q->last_ledger = pay_ledger_post(q->L, &r, &rc);
    return q->last_ledger == PAY_OK ? QNOTE_OK : QNOTE_ERR_LEDGER;
}

/* ---- Q1 ---- */

static qnote_lot_t *lot_of(const qnote_t *q, uint32_t lot)
{
    if (!q || !q->ready || lot >= QNOTE_MAX_LOTS || q->lot[lot].state == QNOTE_FREE) return 0;
    return (qnote_lot_t *) &q->lot[lot];
}

const qnote_lot_t *qnote_lot(const qnote_t *q, uint32_t lot)
{
    return lot_of(q, lot);
}

qnote_status_t qnote_lot_open(qnote_t *q, const uint8_t claim[32], uint64_t face,
                              uint64_t unit_face, uint32_t issuer, uint64_t p_start,
                              uint64_t p_step, uint64_t p_min, uint32_t *lot)
{
    if (!q || !q->ready || !claim || !face || face >= PAY_BAL_MAX || !unit_face)
        return QNOTE_ERR_ARG;
    uint64_t dust, rem;
    uint64_t units = pay_udiv64(face, unit_face, &dust);
    if (!units || !p_min || !p_step || p_min > p_start || p_start > unit_face) return QNOTE_ERR_ARG;
    uint64_t steps = pay_udiv64(p_start - p_min, p_step, &rem);
    if (steps + (rem != 0) >= QNOTE_MAX_RUNGS) return QNOTE_ERR_ARG;
    const pay_account_t *i = acct(q, issuer);
    if (!i || !is_issuer(i) || i->asset == q->fiat || i->credit || i->debit) return QNOTE_ERR_ARG;
    uint32_t slot = QNOTE_MAX_LOTS;
    for (uint32_t n = 0; n < QNOTE_MAX_LOTS; n++) {
        const qnote_lot_t *x = &q->lot[n];
        if (x->state == QNOTE_FREE) {
            if (slot == QNOTE_MAX_LOTS) slot = n;
            continue;
        }
        if (pay_memeq(x->claim, claim, 32)) return QNOTE_ERR_DUP;
        if (x->asset == i->asset) return QNOTE_ERR_ARG;
    }
    if (slot == QNOTE_MAX_LOTS) return QNOTE_ERR_FULL;
    qnote_lot_t *k = &q->lot[slot];
    pay_memset(k, 0, sizeof *k);
    k->state = QNOTE_OPEN;
    pay_memcpy(k->claim, claim, 32);
    k->face = face;
    k->unit_face = unit_face;
    k->units = units;
    k->dust = dust;
    k->issuer = issuer;
    k->asset = i->asset;
    k->p_start = p_start;
    k->p_step = p_step;
    k->p_min = p_min;
    if (lot) *lot = slot;
    return QNOTE_OK;
}

/* ---- Q2 ---- */

qnote_status_t qnote_bid(qnote_t *q, uint32_t lot, uint32_t fiat, uint32_t qacct, uint64_t qty,
                         uint64_t limit, uint64_t tick, uint32_t *index)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k) return QNOTE_ERR_ARG;
    if (k->state != QNOTE_OPEN) return QNOTE_ERR_STATE;
    const pay_account_t *f = acct(q, fiat), *a = acct(q, qacct);
    if (!f || !a || f->asset != q->fiat || a->asset != k->asset) return QNOTE_ERR_ARG;
    if (fiat == q->escrow || fiat == q->reserve || qacct == k->issuer) return QNOTE_ERR_ARG;
    uint64_t lock, total;
    if (!qty || qty > k->units || limit < k->p_min || limit > k->p_start ||
        __builtin_mul_overflow(qty, limit, &lock) || !pay_add_ok(k->locked, lock, &total) ||
        total >= PAY_BAL_MAX)
        return QNOTE_ERR_ARG;
    if (k->n_bids >= QNOTE_MAX_BIDS) return QNOTE_ERR_FULL;
    const pay_line_t ln[] = {{fiat, -(int64_t) lock, 0}, {q->escrow, (int64_t) lock, 0}};
    qnote_status_t st = post(q, ln, 2, PAY_KIND_TRANSFER, tick);
    if (st != QNOTE_OK) return st;
    qnote_bid_t *b = &k->bid[k->n_bids];
    pay_memset(b, 0, sizeof *b);
    b->fiat = fiat;
    b->qacct = qacct;
    b->qty = qty;
    b->limit = limit;
    b->locked = lock;
    k->locked = total;
    if (index) *index = k->n_bids;
    k->n_bids++;
    return QNOTE_OK;
}

/* ---- Q3 ---- */

/* sums fit: every qty <= units < 2^62 and at most QNOTE_MAX_BIDS bids */
static uint64_t demand(const qnote_lot_t *k, uint64_t p)
{
    uint64_t d = 0;
    for (uint32_t i = 0; i < k->n_bids; i++)
        if (k->bid[i].limit >= p) d += k->bid[i].qty;
    return d;
}

uint64_t qnote_demand(const qnote_t *q, uint32_t lot, uint64_t p)
{
    const qnote_lot_t *k = lot_of(q, lot);
    return k ? demand(k, p) : 0;
}

static bool in_set(const qnote_bid_t *b, uint64_t lo, uint64_t hi)
{
    return b->limit >= lo && (hi == 0 || b->limit < hi);
}

/* share `amount` (<= the set's total) pro rata by qty, floors, then one
 * leftover unit each in submission order */
static void prorate(qnote_lot_t *k, uint64_t lo, uint64_t hi, uint64_t amount)
{
    uint64_t total = 0, given = 0;
    for (uint32_t i = 0; i < k->n_bids; i++)
        if (in_set(&k->bid[i], lo, hi)) total += k->bid[i].qty;
    for (uint32_t i = 0; i < k->n_bids; i++) {
        qnote_bid_t *b = &k->bid[i];
        if (!in_set(b, lo, hi)) continue;
        pay_muldiv(amount, b->qty, total, &b->fill, 0); /* <= qty: fits */
        given += b->fill;
    }
    for (uint32_t i = 0; i < k->n_bids && given < amount; i++) {
        qnote_bid_t *b = &k->bid[i];
        if (!in_set(b, lo, hi)) continue;
        b->fill++;
        given++;
    }
}

qnote_status_t qnote_clear(qnote_t *q, uint32_t lot)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k) return QNOTE_ERR_ARG;
    if (k->state != QNOTE_OPEN) return QNOTE_ERR_STATE;
    uint64_t p = k->p_start, prev = 0;
    bool found = false;
    for (;;) {
        if (demand(k, p) >= k->units) {
            found = true;
            break;
        }
        if (p == k->p_min) break;
        prev = p;
        p = p - k->p_min > k->p_step ? p - k->p_step : k->p_min;
    }
    if (found) {
        uint64_t dprev = prev ? demand(k, prev) : 0; /* < units */
        for (uint32_t i = 0; prev && i < k->n_bids; i++)
            if (k->bid[i].limit >= prev) k->bid[i].fill = k->bid[i].qty;
        prorate(k, p, prev, k->units - dprev);
        k->sold = k->units;
    } else { /* undersubscribed: every bid fills at p_min */
        for (uint32_t i = 0; i < k->n_bids; i++) k->bid[i].fill = k->bid[i].qty;
        k->sold = demand(k, p);
    }
    k->price = p;
    k->unsold = k->units - k->sold;
    k->winners = 0;
    for (uint32_t i = 0; i < k->n_bids; i++) k->winners += k->bid[i].fill > 0;
    k->state = QNOTE_CLEARED;
    return QNOTE_OK;
}

/* ---- Q4 ---- */

qnote_status_t qnote_settle(qnote_t *q, uint32_t lot, uint64_t tick)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k) return QNOTE_ERR_ARG;
    if (k->state != QNOTE_CLEARED) return QNOTE_ERR_STATE;
    for (uint32_t i = 0; i < k->n_bids; i++) {
        qnote_bid_t *b = &k->bid[i];
        if (b->settled) continue;
        uint64_t pay = b->fill * k->price, back = b->locked - pay; /* price <= limit */
        qnote_status_t st;
        if (b->fill) {
            const pay_line_t ln[] = {{q->escrow, -(int64_t) b->locked, 0},
                                     {q->reserve, (int64_t) pay, 0},
                                     {k->issuer, 0, (int64_t) b->fill},
                                     {b->qacct, (int64_t) b->fill, 0},
                                     {b->fiat, (int64_t) back, 0}};
            st = post(q, ln, back ? 5u : 4u, PAY_KIND_ISSUE, tick);
        } else {
            const pay_line_t ln[] = {{q->escrow, -(int64_t) b->locked, 0},
                                     {b->fiat, (int64_t) b->locked, 0}};
            st = post(q, ln, 2, PAY_KIND_RETURN, tick);
        }
        if (st != QNOTE_OK) return st;
        b->settled = true;
        k->locked -= b->locked;
        k->proceeds += pay; /* <= the lot's locks < 2^62 */
        q->reserve_in += pay;
    }
    k->loss = k->face > k->proceeds ? k->face - k->proceeds : 0;
    k->state = QNOTE_SETTLED;
    return QNOTE_OK;
}

static bool settled(const qnote_lot_t *k)
{
    return k->state == QNOTE_SETTLED || k->state == QNOTE_CLOSED;
}

qnote_status_t qnote_loss(const qnote_t *q, uint32_t lot, uint64_t *loss)
{
    const qnote_lot_t *k = lot_of(q, lot);
    if (!k || !loss) return QNOTE_ERR_ARG;
    if (!settled(k)) return QNOTE_ERR_STATE;
    *loss = k->loss;
    return QNOTE_OK;
}

/* ---- Q6 ---- */

qnote_status_t qnote_first_loss(qnote_t *q, uint32_t lot, svault_t *vault, uint64_t backing,
                                uint64_t *burned, uint64_t *uncovered)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k || !vault || !backing) return QNOTE_ERR_ARG;
    if (!settled(k) || k->fl_done) return QNOTE_ERR_STATE;
    if (burned) *burned = 0;
    if (uncovered) *uncovered = 0;
    if (!k->loss) {
        k->fl_done = true;
        return QNOTE_ZERO;
    }
    uint64_t rem, unc;
    uint64_t lv = pay_udiv64(k->loss, backing, &rem);
    lv += rem != 0; /* ceil: loss < 2^62 */
    pay_posting_req_t r;
    mkreq(q, &r, 0);
    svault_status_t st = svault_first_loss(vault, q->L, &r, lv, &unc);
    q->last_ledger = vault->last_ledger;
    if (st != SVAULT_OK && st != SVAULT_ZERO) return QNOTE_ERR_VAULT;
    uint64_t b = lv - unc;
    /* b < lv implies b * backing < loss: no overflow */
    uint64_t covered = b == lv ? k->loss : b * backing;
    k->fl_done = true;
    k->fl_vbills = lv;
    k->fl_burned = b;
    k->fl_covered = covered;
    k->fl_uncovered = k->loss - covered;
    if (burned) *burned = b;
    if (uncovered) *uncovered = k->fl_uncovered;
    return QNOTE_OK;
}

/* ---- Q5 ---- */

/* the first active non-issuer fiat account of `owner`, not escrow / reserve;
 * PAY_MAX_ACCOUNTS when there is none */
static uint32_t payee_of(const qnote_t *q, uint32_t owner)
{
    for (uint32_t i = 0; i < q->L->n_accounts; i++) {
        const pay_account_t *a = &q->L->acct[i];
        if (a->active && a->owner == owner && a->asset == q->fiat && !is_issuer(a) &&
            i != q->escrow && i != q->reserve)
            return i;
    }
    return PAY_MAX_ACCOUNTS;
}

static bool holder(const qnote_t *q, const qnote_lot_t *k, uint32_t i)
{
    const pay_account_t *a = &q->L->acct[i];
    return a->active && a->asset == k->asset && i != k->issuer && a->debit > 0;
}

qnote_status_t qnote_recover(qnote_t *q, uint32_t lot, uint32_t from, uint64_t amount,
                             uint64_t tick)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k || !amount) return QNOTE_ERR_ARG;
    if (k->state != QNOTE_SETTLED) return QNOTE_ERR_STATE;
    const pay_account_t *f = acct(q, from), *iss = acct(q, k->issuer);
    if (!f || !iss || f->asset != q->fiat || from == q->escrow || from == q->reserve)
        return QNOTE_ERR_ARG;
    uint64_t H = iss->credit, sum = 0, room, rec, exc, rin;
    for (uint32_t i = 0; i < q->L->n_accounts; i++) {
        if (!holder(q, k, i)) continue;
        if (payee_of(q, q->L->acct[i].owner) == PAY_MAX_ACCOUNTS) return QNOTE_ERR_PAYEE;
        sum += q->L->acct[i].debit; /* each < 2^62, at most 512 accounts */
    }
    if (sum != H) return QNOTE_ERR_STATE; /* notes outside this lot's issuer */
    if (__builtin_mul_overflow(H, k->unit_face, &room)) return QNOTE_ERR_OVERFLOW;
    uint64_t avail = room > k->carry ? room - k->carry : 0;
    uint64_t x = amount < avail ? amount : avail, excess = amount - x;
    if (!pay_add_ok(k->recovered, amount, &rec) || !pay_add_ok(k->excess, excess, &exc) ||
        !pay_add_ok(q->reserve_in, excess, &rin))
        return QNOTE_ERR_OVERFLOW;
    pay_line_t ln[3];
    uint32_t n = 0;
    ln[n++] = (pay_line_t){from, -(int64_t) amount, 0};
    if (x) ln[n++] = (pay_line_t){q->escrow, (int64_t) x, 0};
    if (excess) ln[n++] = (pay_line_t){q->reserve, (int64_t) excess, 0};
    qnote_status_t st = post(q, ln, n, PAY_KIND_TRANSFER, tick);
    if (st != QNOTE_OK) return st;
    k->recovered = rec;
    k->excess = exc;
    q->reserve_in = rin;
    k->carry += x; /* <= room */
    uint64_t T = pay_udiv64(k->carry, k->unit_face, 0), given = 0, left;
    /* T <= H: pro rata floors, then leftovers one each in account order */
    for (uint32_t i = 0; i < q->L->n_accounts; i++) {
        uint64_t r;
        if (!holder(q, k, i)) continue;
        pay_muldiv(T, q->L->acct[i].debit, H, &r, 0);
        given += r;
    }
    left = T - given;
    for (uint32_t i = 0; i < q->L->n_accounts; i++) {
        uint64_t r;
        if (!holder(q, k, i)) continue;
        pay_muldiv(T, q->L->acct[i].debit, H, &r, 0);
        if (left) {
            r++;
            left--;
        }
        if (!r) continue;
        uint32_t p = payee_of(q, q->L->acct[i].owner);
        uint64_t cash = r * k->unit_face; /* <= carry */
        const pay_line_t rl[] = {{q->escrow, -(int64_t) cash, 0},
                                 {p, (int64_t) cash, 0},
                                 {i, -(int64_t) r, 0},
                                 {k->issuer, 0, -(int64_t) r}};
        st = post(q, rl, 4, PAY_KIND_REDEEM, tick);
        if (st != QNOTE_OK) return st; /* its share stays in the carry */
        k->carry -= cash;
        k->paid += cash;
        k->retired += r;
    }
    return QNOTE_OK;
}

/* ---- Q7 ---- */

qnote_status_t qnote_close(qnote_t *q, uint32_t lot, uint64_t tick)
{
    qnote_lot_t *k = lot_of(q, lot);
    if (!k) return QNOTE_ERR_ARG;
    if (k->state != QNOTE_SETTLED) return QNOTE_ERR_STATE;
    if (k->carry) {
        const pay_line_t ln[] = {{q->escrow, -(int64_t) k->carry, 0},
                                 {q->reserve, (int64_t) k->carry, 0}};
        qnote_status_t st = post(q, ln, 2, PAY_KIND_TRANSFER, tick);
        if (st != QNOTE_OK) return st;
    }
    k->closed_carry = k->carry;
    q->reserve_in += k->carry; /* the carry came in through recover: fits */
    k->carry = 0;
    k->written_off = k->sold - k->retired;
    k->state = QNOTE_CLOSED;
    return QNOTE_OK;
}

/* ---- Q8 ---- */

static bool lot_ok(const qnote_t *q, const qnote_lot_t *k, uint64_t *escrow, uint64_t *rin)
{
    const pay_account_t *iss = acct(q, k->issuer);
    uint64_t lk = 0, fills = 0, v, out;
    /* the lot's share of escrow and reserve inflow first, so that every
     * check below stands on its own */
    if (!pay_add_ok(*escrow, k->locked, escrow) || !pay_add_ok(*escrow, k->carry, escrow) ||
        !pay_add_ok(*rin, k->proceeds, rin) || !pay_add_ok(*rin, k->excess, rin) ||
        !pay_add_ok(*rin, k->closed_carry, rin))
        return false;
    if (!iss || k->state > QNOTE_CLOSED || !k->unit_face || !k->units) return false;
    if (__builtin_mul_overflow(k->units, k->unit_face, &v) || !pay_add_ok(v, k->dust, &v) ||
        v != k->face || k->dust >= k->unit_face)
        return false;
    for (uint32_t i = 0; i < k->n_bids; i++) {
        const qnote_bid_t *b = &k->bid[i];
        if (b->fill > b->qty) return false;
        if (b->settled)
            fills += b->fill;
        else
            lk += b->locked;
    }
    if (lk != k->locked || iss->credit > fills) return false;
    if (!pay_sub_ok(fills, k->retired, &out) || iss->credit != out) return false;
    if (__builtin_mul_overflow(fills, k->price, &v) || v != k->proceeds) return false;
    if (__builtin_mul_overflow(k->retired, k->unit_face, &v) || v != k->paid) return false;
    if (__builtin_mul_overflow(out, k->unit_face, &v) || k->carry > v) return false;
    if (k->state >= QNOTE_SETTLED) {
        if (k->locked || k->sold + k->unsold != k->units || fills != k->sold) return false;
        if (k->loss != (k->face > k->proceeds ? k->face - k->proceeds : 0)) return false;
    }
    if (k->state == QNOTE_CLOSED && (k->carry || k->written_off != out)) return false;
    if (k->fl_burned > k->fl_vbills ||
        k->fl_covered + k->fl_uncovered != (k->fl_done ? k->loss : 0))
        return false;
    return true;
}

bool qnote_check(const qnote_t *q)
{
    if (!q || !q->ready) return false;
    const pay_account_t *e = acct(q, q->escrow);
    uint64_t escrow = 0, rin = 0;
    if (!e || e->credit) return false;
    for (uint32_t n = 0; n < QNOTE_MAX_LOTS; n++)
        if (q->lot[n].state != QNOTE_FREE && !lot_ok(q, &q->lot[n], &escrow, &rin)) return false;
    return e->debit == escrow && rin == q->reserve_in;
}
