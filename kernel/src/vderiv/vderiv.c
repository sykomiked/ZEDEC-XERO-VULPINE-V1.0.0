/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vderiv.c — V-Calls and V-Perps on auction clearing velocity. See vderiv.h
 * V0-V6. */
#include "vderiv.h"
#include "../robin_debanks/sha256.h"

#define LONG  VDERIV_LONG
#define SHORT VDERIV_SHORT

void vderiv_cfg_default(vderiv_cfg_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->escrow = c->esc_long = c->esc_short = c->reserve = VDERIV_NONE;
    c->first_epoch = 1;
}

static const pay_account_t *of(const vderiv_t *s, uint32_t a)
{
    const pay_account_t *x = pay_ledger_account(s->L, a);
    return (x && x->active && x->asset == s->cfg.vino) ? x : 0;
}

/* one of the module's own accounts */
static bool own(const vderiv_t *s, uint32_t a)
{
    return a == s->cfg.escrow || a == s->cfg.esc_long || a == s->cfg.esc_short ||
           a == s->cfg.reserve;
}

vderiv_status_t vderiv_init(vderiv_t *s, pay_ledger_t *L, const vderiv_cfg_t *c)
{
    if (!s || !L || !c) return VDERIV_ERR_ARG;
    pay_memset(s, 0, sizeof *s);
    s->L = L;
    s->cfg = *c;
    if (!c->verify || !c->first_epoch) return VDERIV_ERR_ARG;
    const uint32_t a[4] = {c->escrow, c->esc_long, c->esc_short, c->reserve};
    for (uint32_t i = 0; i < 4; i++) {
        const pay_account_t *x = of(s, a[i]);
        if (!x || (x->flags & PAY_ACCT_ISSUER)) return VDERIV_ERR_ARG;
        for (uint32_t j = 0; j < i; j++)
            if (a[j] == a[i]) return VDERIV_ERR_ARG;
        if (i < 3 && x->debit) return VDERIV_ERR_STATE; /* V4 needs empty escrows */
    }
    return VDERIV_OK;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void tag(uint8_t *out, const char *t)
{
    pay_memcpy(out, t, 16);
}

/* ---- V5 ---- */
void vderiv_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VDERIV_SWITCH_MSG])
{
    tag(out, "zxv/vderiv-sw/v1");
    put32(out + 16, juris);
    out[20] = enable ? 1 : 0;
    put64(out + 21, epoch);
}

vderiv_status_t vderiv_enable(vderiv_t *s, uint32_t juris, bool enable, uint64_t epoch,
                              const uint8_t *sig, uint32_t sig_len)
{
    if (!s || !s->L || !sig) return VDERIV_ERR_ARG;
    if (juris != s->cfg.juris) return VDERIV_ERR_JURIS;
    if (epoch <= s->switch_epoch) return VDERIV_ERR_EPOCH;
    uint8_t m[VDERIV_SWITCH_MSG];
    vderiv_switch_msg(juris, enable, epoch, m);
    if (!s->cfg.verify(s->cfg.vctx, s->cfg.gateway, m, sizeof m, sig, sig_len))
        return VDERIV_ERR_SIG;
    s->enabled = enable;
    s->switch_epoch = epoch;
    return VDERIV_OK;
}

/* ---- V6 messages ---- */
void vderiv_call_msg(const vderiv_call_terms_t *t, uint8_t out[VDERIV_CALL_MSG])
{
    tag(out, "zxv/vderiv-cl/v1");
    put64(out + 16, t->nonce);
    put32(out + 24, t->writer_key);
    put32(out + 28, t->writer);
    put32(out + 32, t->buyer_key);
    put32(out + 36, t->buyer);
    put64(out + 40, t->strike);
    put64(out + 48, t->cap);
    put64(out + 56, t->size);
    put64(out + 64, t->expiry);
    put64(out + 72, t->premium);
}

void vderiv_perp_msg(const vderiv_perp_terms_t *t, uint8_t out[VDERIV_PERP_MSG])
{
    tag(out, "zxv/vderiv-pp/v1");
    put64(out + 16, t->nonce);
    put32(out + 24, t->long_key);
    put32(out + 28, t->long_acct);
    put32(out + 32, t->short_key);
    put32(out + 36, t->short_acct);
    put64(out + 40, t->size);
    put64(out + 48, t->band);
    put64(out + 56, t->lock_long);
    put64(out + 64, t->lock_short);
}

void vderiv_close_msg(uint64_t nonce, uint8_t side, uint8_t out[VDERIV_CLOSE_MSG])
{
    tag(out, "zxv/vderiv-cx/v1");
    put64(out + 16, nonce);
    out[24] = side;
}

/* ---- pure V1 / V2 ---- */
bool vderiv_call_payoff(uint64_t strike, uint64_t cap, uint64_t size, uint64_t index,
                        uint64_t *payoff)
{
    if (!payoff || cap <= strike) return false;
    uint64_t top = index < cap ? index : cap;
    if (top <= strike) {
        *payoff = 0;
        return true;
    }
    return !__builtin_mul_overflow(size, top - strike, payoff);
}

bool vderiv_perp_variation(uint64_t size, uint64_t band, uint64_t prev, uint64_t cur, uint64_t *v,
                           int *dir)
{
    if (!v || !dir) return false;
    uint64_t d = cur > prev ? cur - prev : prev - cur;
    if (d > band) d = band;
    if (__builtin_mul_overflow(size, d, v)) return false;
    *dir = (*v == 0) ? 0 : (cur > prev ? 1 : -1);
    return true;
}

/* ---- postings ---- */
static void derive(const vderiv_t *s, uint8_t t, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, s->cfg.seed, 32);
    in[32] = t;
    put64(in + 33, s->seq);
    sha256(in, sizeof in, out);
}

static pay_line_t line(uint32_t a, int64_t d)
{
    pay_line_t l = {a, d, 0};
    return l;
}

static vderiv_status_t post(vderiv_t *s, const pay_line_t *ln, uint32_t n, uint8_t kind,
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
    pay_w_s(&w, "VDERIV-");
    pay_w_u64(&w, s->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "v-bill velocity derivative", sizeof r.memo);
    r.tick = tick;
    r.kind = kind;
    r.ext_cap = PAY_CAP_FINANCIAL;
    s->seq++;
    pay_receipt_t rc;
    s->last_ledger = pay_ledger_post(s->L, &r, &rc);
    if (s->last_ledger == PAY_OK) {
        s->posted++;
        return VDERIV_OK;
    }
    return s->last_ledger == PAY_ERR_FUNDS ? VDERIV_ERR_FUNDS : VDERIV_ERR_LEDGER;
}

/* two distinct, signing parties with usable accounts; each key is the
 * ledger owner id of the account it commits (V6), so a signer can only
 * move value out of its own accounts */
static vderiv_status_t parties(const vderiv_t *s, uint64_t nonce, uint32_t ka, uint32_t a,
                               uint32_t kb, uint32_t b)
{
    if (!of(s, a) || !of(s, b) || own(s, a) || own(s, b) || a == b || ka == kb)
        return VDERIV_ERR_ARG;
    if (of(s, a)->owner != ka || of(s, b)->owner != kb) return VDERIV_ERR_SIG;
    if (nonce != s->nonce) return VDERIV_ERR_NONCE;
    return VDERIV_OK;
}

static vderiv_status_t both_signed(const vderiv_t *s, const uint8_t *m, uint32_t len, uint32_t ka,
                                   const uint8_t *sa, uint32_t la, uint32_t kb, const uint8_t *sb,
                                   uint32_t lb)
{
    if (!s->cfg.verify(s->cfg.vctx, ka, m, len, sa, la) ||
        !s->cfg.verify(s->cfg.vctx, kb, m, len, sb, lb))
        return VDERIV_ERR_SIG;
    return VDERIV_OK;
}

/* the escrow room: total + add <= VDERIV_LINE_MAX */
static bool room(uint64_t total, uint64_t add)
{
    uint64_t t;
    return pay_add_ok(total, add, &t) && t <= VDERIV_LINE_MAX;
}

/* ---- V1 ---- */
vderiv_status_t vderiv_call_open(vderiv_t *s, const vderiv_call_terms_t *t, const uint8_t *wsig,
                                 uint32_t wsig_len, const uint8_t *bsig, uint32_t bsig_len,
                                 uint64_t tick, uint32_t *id)
{
    if (!s || !s->L || !t || !wsig || !bsig) return VDERIV_ERR_ARG;
    if (!s->enabled) return VDERIV_ERR_DISABLED;
    if (t->size == 0 || t->cap <= t->strike || t->premium > VDERIV_LINE_MAX) return VDERIV_ERR_ARG;
    uint64_t lock;
    if (__builtin_mul_overflow(t->size, t->cap - t->strike, &lock) ||
        !room(of(s, s->cfg.escrow)->debit, lock))
        return VDERIV_ERR_OVERFLOW;
    if (t->expiry < s->cfg.first_epoch || (s->n_pushed && t->expiry <= s->last_epoch))
        return VDERIV_ERR_EPOCH;
    vderiv_status_t st = parties(s, t->nonce, t->writer_key, t->writer, t->buyer_key, t->buyer);
    if (st != VDERIV_OK) return st;
    uint32_t i = 0;
    while (i < VDERIV_MAX_CALLS && s->call[i].state != VDERIV_FREE) i++;
    if (i == VDERIV_MAX_CALLS) return VDERIV_ERR_FULL;
    uint8_t m[VDERIV_CALL_MSG];
    vderiv_call_msg(t, m);
    st = both_signed(s, m, sizeof m, t->writer_key, wsig, wsig_len, t->buyer_key, bsig, bsig_len);
    if (st != VDERIV_OK) return st;

    /* V3: the writer receives the premium less its fee */
    uint64_t fee = pay_assure_fee(t->premium);
    const pay_line_t ln[5] = {line(t->buyer, -(int64_t) t->premium),
                              line(t->writer, (int64_t) (t->premium - fee)),
                              line(s->cfg.reserve, (int64_t) fee), line(t->writer, -(int64_t) lock),
                              line(s->cfg.escrow, (int64_t) lock)};
    st = post(s, ln, 5, PAY_KIND_TRADE, tick);
    if (st != VDERIV_OK) return st;
    vderiv_call_t *c = &s->call[i];
    pay_memset(c, 0, sizeof *c);
    c->state = VDERIV_OPEN;
    c->t = *t;
    c->lock = lock;
    s->nonce++;
    s->fees_total += fee;
    if (id) *id = i;
    return VDERIV_OK;
}

vderiv_status_t vderiv_call_settle(vderiv_t *s, uint32_t id, uint64_t tick, vderiv_settle_t *out)
{
    vderiv_settle_t tmp;
    if (!out) out = &tmp;
    pay_memset(out, 0, sizeof *out);
    if (!s || !s->L || id >= VDERIV_MAX_CALLS) return VDERIV_ERR_ARG;
    vderiv_call_t *c = &s->call[id];
    if (c->state != VDERIV_FIXED) return VDERIV_ERR_STATE;
    uint64_t p = 0;
    /* checked at open: payoff <= n * (C - K) = lock */
    vderiv_call_payoff(c->t.strike, c->t.cap, c->t.size, c->fixing, &p);
    uint64_t fee = pay_assure_fee(p);
    const pay_line_t ln[4] = {
        line(s->cfg.escrow, -(int64_t) c->lock), line(c->t.buyer, (int64_t) (p - fee)),
        line(s->cfg.reserve, (int64_t) fee), line(c->t.writer, (int64_t) (c->lock - p))};
    vderiv_status_t st = post(s, ln, 4, PAY_KIND_TRADE, tick);
    if (st != VDERIV_OK) return st;
    out->payoff = p;
    out->fee = fee;
    out->to_buyer = p - fee;
    out->to_writer = c->lock - p;
    s->fees_total += fee;
    pay_memset(c, 0, sizeof *c);
    return VDERIV_OK;
}

/* ---- V2 ---- */
vderiv_status_t vderiv_perp_open(vderiv_t *s, const vderiv_perp_terms_t *t, const uint8_t *lsig,
                                 uint32_t lsig_len, const uint8_t *ssig, uint32_t ssig_len,
                                 uint64_t tick, uint32_t *id)
{
    if (!s || !s->L || !t || !lsig || !ssig) return VDERIV_ERR_ARG;
    if (!s->enabled) return VDERIV_ERR_DISABLED;
    if (t->size == 0 || t->band == 0) return VDERIV_ERR_ARG;
    uint64_t nb, both;
    if (__builtin_mul_overflow(t->size, t->band, &nb) ||
        !pay_add_ok(t->lock_long, t->lock_short, &both) ||
        !room(of(s, s->cfg.esc_long)->debit + of(s, s->cfg.esc_short)->debit, both))
        return VDERIV_ERR_OVERFLOW;
    if (t->lock_long < nb || t->lock_short < nb) return VDERIV_ERR_ARG;
    if (!s->n_pushed) return VDERIV_ERR_EPOCH; /* no reference index yet */
    vderiv_status_t st =
        parties(s, t->nonce, t->long_key, t->long_acct, t->short_key, t->short_acct);
    if (st != VDERIV_OK) return st;
    uint32_t i = 0;
    while (i < VDERIV_MAX_PERPS && s->perp[i].state != VDERIV_FREE) i++;
    if (i == VDERIV_MAX_PERPS) return VDERIV_ERR_FULL;
    uint8_t m[VDERIV_PERP_MSG];
    vderiv_perp_msg(t, m);
    st = both_signed(s, m, sizeof m, t->long_key, lsig, lsig_len, t->short_key, ssig, ssig_len);
    if (st != VDERIV_OK) return st;
    const pay_line_t ln[4] = {line(t->long_acct, -(int64_t) t->lock_long),
                              line(s->cfg.esc_long, (int64_t) t->lock_long),
                              line(t->short_acct, -(int64_t) t->lock_short),
                              line(s->cfg.esc_short, (int64_t) t->lock_short)};
    st = post(s, ln, 4, PAY_KIND_TRANSFER, tick);
    if (st != VDERIV_OK) return st;
    vderiv_perp_t *p = &s->perp[i];
    pay_memset(p, 0, sizeof *p);
    p->state = VDERIV_OPEN;
    p->t = *t;
    p->bal[LONG] = t->lock_long;
    p->bal[SHORT] = t->lock_short;
    p->ref = s->ring[(s->n_pushed - 1) % VDERIV_RING].qty;
    p->opened = s->last_epoch;
    s->nonce++;
    if (id) *id = i;
    return VDERIV_OK;
}

vderiv_status_t vderiv_perp_close(vderiv_t *s, uint32_t id, uint8_t side, const uint8_t *sig,
                                  uint32_t sig_len, uint64_t tick, uint64_t *out)
{
    if (!s || !s->L || id >= VDERIV_MAX_PERPS || side > SHORT) return VDERIV_ERR_ARG;
    vderiv_perp_t *p = &s->perp[id];
    if (p->state == VDERIV_FREE) return VDERIV_ERR_STATE;
    if (p->state == VDERIV_OPEN) {
        uint8_t m[VDERIV_CLOSE_MSG];
        vderiv_close_msg(p->t.nonce, side, m);
        uint32_t key = side == LONG ? p->t.long_key : p->t.short_key;
        if (!sig || !s->cfg.verify(s->cfg.vctx, key, m, sizeof m, sig, sig_len))
            return VDERIV_ERR_SIG;
    }
    const pay_line_t ln[4] = {line(s->cfg.esc_long, -(int64_t) p->bal[LONG]),
                              line(p->t.long_acct, (int64_t) p->bal[LONG]),
                              line(s->cfg.esc_short, -(int64_t) p->bal[SHORT]),
                              line(p->t.short_acct, (int64_t) p->bal[SHORT])};
    vderiv_status_t st = post(s, ln, 4, PAY_KIND_TRANSFER, tick);
    if (st != VDERIV_OK) return st;
    if (out) {
        out[LONG] = p->bal[LONG];
        out[SHORT] = p->bal[SHORT];
    }
    pay_memset(p, 0, sizeof *p);
    return VDERIV_OK;
}

/* ---- V0 ---- */
bool vderiv_index_at(const vderiv_t *s, uint64_t epoch, uint64_t *index)
{
    if (!s || !index || !s->n_pushed || epoch > s->last_epoch) return false;
    uint64_t back = s->last_epoch - epoch;
    if (back >= s->n_pushed || back >= VDERIV_RING) return false;
    *index = s->ring[(s->n_pushed - 1 - back) % VDERIV_RING].qty;
    return true;
}

vderiv_status_t vderiv_index_push(vderiv_t *s, uint64_t epoch, uint64_t price, uint64_t qty,
                                  uint64_t tick, vderiv_push_t *out)
{
    vderiv_push_t tmp;
    if (!out) out = &tmp;
    pay_memset(out, 0, sizeof *out);
    if (!s || !s->L) return VDERIV_ERR_ARG;
    uint64_t want = s->n_pushed ? s->last_epoch + 1 : s->cfg.first_epoch;
    if (epoch != want || !want) return VDERIV_ERR_EPOCH;

    /* V2: every open perp's move, staged; gain/loss per escrow side. Each
     * total is bounded by the escrow totals (<= VDERIV_LINE_MAX, checked at
     * open), so none of these sums can overflow. */
    uint64_t nb[VDERIV_MAX_PERPS][2], fee[VDERIV_MAX_PERPS], gain[2] = {0, 0}, loss[2] = {0, 0};
    bool stop[VDERIV_MAX_PERPS];
    for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
        vderiv_perp_t *p = &s->perp[i];
        if (p->state != VDERIV_OPEN) continue;
        uint64_t v = 0;
        int dir = 0;
        /* size * band was checked at open, and the move is clamped to band */
        vderiv_perp_variation(p->t.size, p->t.band, p->ref, qty, &v, &dir);
        uint8_t win = dir > 0 ? LONG : SHORT, lose = (uint8_t) (1 - win);
        fee[i] = pay_assure_fee(v);
        nb[i][lose] = p->bal[lose] - v;
        nb[i][win] = p->bal[win] + (v - fee[i]);
        gain[win] += v - fee[i];
        loss[lose] += v;
        out->fees += fee[i];
        if (v) out->moved++;
        uint64_t floor_ = p->t.size * p->t.band;
        stop[i] = nb[i][LONG] < floor_ || nb[i][SHORT] < floor_;
        out->stopped += stop[i];
    }
    if (out->moved) {
        const pay_line_t ln[3] = {
            line(s->cfg.esc_long, (int64_t) gain[LONG] - (int64_t) loss[LONG]),
            line(s->cfg.esc_short, (int64_t) gain[SHORT] - (int64_t) loss[SHORT]),
            line(s->cfg.reserve, (int64_t) out->fees)};
        vderiv_status_t st = post(s, ln, 3, PAY_KIND_TRADE, tick);
        if (st != VDERIV_OK) {
            pay_memset(out, 0, sizeof *out);
            return st;
        }
    }
    for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
        vderiv_perp_t *p = &s->perp[i];
        if (p->state != VDERIV_OPEN) continue;
        p->bal[LONG] = nb[i][LONG];
        p->bal[SHORT] = nb[i][SHORT];
        p->fees += fee[i];
        p->ref = qty;
        if (stop[i]) {
            p->state = VDERIV_STOPPED;
            p->stop_epoch = epoch;
        }
    }
    s->fees_total += out->fees;

    vderiv_obs_t *o = &s->ring[s->n_pushed % VDERIV_RING];
    o->epoch = epoch;
    o->price = price;
    o->qty = qty;
    s->n_pushed++;
    s->last_epoch = epoch;

    /* V1: record the fixing of every call expiring now */
    for (uint32_t i = 0; i < VDERIV_MAX_CALLS; i++) {
        vderiv_call_t *c = &s->call[i];
        if (c->state == VDERIV_OPEN && c->t.expiry == epoch) {
            c->state = VDERIV_FIXED;
            c->fixing = qty;
            out->fixed++;
        }
    }
    return VDERIV_OK;
}

/* ---- V4 ---- */
vderiv_status_t vderiv_check(const vderiv_t *s)
{
    if (!s || !s->L) return VDERIV_ERR_ARG;
    uint64_t sum[3] = {0, 0, 0};
    for (uint32_t i = 0; i < VDERIV_MAX_CALLS; i++)
        if (s->call[i].state != VDERIV_FREE &&
            __builtin_add_overflow(sum[0], s->call[i].lock, &sum[0]))
            return VDERIV_ERR_STATE;
    for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
        const vderiv_perp_t *p = &s->perp[i];
        if (p->state == VDERIV_FREE) continue;
        uint64_t a, b;
        if (__builtin_add_overflow(sum[1], p->bal[LONG], &sum[1]) ||
            __builtin_add_overflow(sum[2], p->bal[SHORT], &sum[2]) ||
            __builtin_add_overflow(p->bal[LONG], p->bal[SHORT], &a) ||
            __builtin_add_overflow(a, p->fees, &a) ||
            __builtin_add_overflow(p->t.lock_long, p->t.lock_short, &b) || a != b)
            return VDERIV_ERR_STATE;
    }
    const uint32_t acct[3] = {s->cfg.escrow, s->cfg.esc_long, s->cfg.esc_short};
    for (uint32_t k = 0; k < 3; k++) {
        const pay_account_t *x = of(s, acct[k]);
        if (!x || x->debit != sum[k] || x->credit) return VDERIV_ERR_STATE;
    }
    return pay_ledger_check(s->L) ? VDERIV_OK : VDERIV_ERR_STATE;
}
