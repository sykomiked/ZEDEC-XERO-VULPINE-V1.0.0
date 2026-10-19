/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vbond.c — V-Bonds: issue, transfer, redeem, profit share. See vbond.h. */
#include "vbond.h"
#include "../robin_debanks/sha256.h"

void vbond_cfg_default(vbond_cfg_t *c)
{
    static const uint32_t k[3] = {13, 17, 21};
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->lock = VBOND_NONE;
    c->n_terms = 3;
    for (uint32_t i = 0; i < 3; i++) {
        c->k[i] = k[i];
        c->weight[i] = vbond_fib(k[i]); /* 233, 1597, 10946 */
    }
    c->share_num = 1;
    c->share_den = 2;
}

uint64_t vbond_fib(uint32_t k)
{
    if (k < VBOND_FIB_KMIN || k > VBOND_FIB_KMAX) return 0;
    uint64_t a = 0, b = 1;
    for (uint32_t i = 1; i < k; i++) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return b;
}

static const pay_account_t *of(const vbond_t *s, uint32_t a, uint16_t asset)
{
    const pay_account_t *x = pay_ledger_account(s->L, a);
    return (x && x->asset == asset) ? x : 0;
}

/* an account of `owner` holding `asset`, never the lock account */
static bool mine(const vbond_t *s, uint32_t a, uint16_t asset, uint32_t owner)
{
    const pay_account_t *x = of(s, a, asset);
    return x && x->owner == owner && a != s->cfg.lock;
}

vbond_status_t vbond_init(vbond_t *s, pay_ledger_t *L, const vbond_cfg_t *c)
{
    if (!s || !L || !c) return VBOND_ERR_ARG;
    pay_memset(s, 0, sizeof *s);
    s->L = L;
    s->cfg = *c;
    s->next_serial = 1;
    if (!c->verify || c->vino == c->vbill) return VBOND_ERR_ARG;
    if (!c->share_num || c->share_num > c->share_den) return VBOND_ERR_ARG;
    const pay_account_t *lk = of(s, c->lock, c->vbill);
    if (!lk) return VBOND_ERR_ARG;
    if (lk->debit) return VBOND_ERR_STATE; /* B5 needs an empty lock account */
    if (!c->n_terms || c->n_terms > VBOND_MAX_TERMS) return VBOND_ERR_ARG;
    for (uint32_t i = 0; i < c->n_terms; i++) {
        if (!vbond_fib(c->k[i]) || !c->weight[i]) return VBOND_ERR_ARG;
        if (i && (c->k[i] <= c->k[i - 1] || c->weight[i] < c->weight[i - 1])) return VBOND_ERR_ARG;
    }
    return VBOND_OK;
}

/* ---- B0 ---- */
static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put_tag(uint8_t *p, const char *tag)
{
    for (uint32_t i = 0; i < 15; i++) p[i] = (uint8_t) tag[i];
}

void vbond_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VBOND_SWITCH_MSG])
{
    put_tag(out, "zxv/vbond-sw/v1");
    put32(out + 15, juris);
    out[19] = enable ? 1 : 0;
    put64(out + 20, epoch);
}

vbond_status_t vbond_enable(vbond_t *s, uint32_t juris, bool enable, uint64_t epoch,
                            const uint8_t *sig, uint32_t sig_len)
{
    if (!s || !s->L || !sig) return VBOND_ERR_ARG;
    if (juris != s->cfg.juris) return VBOND_ERR_JURIS;
    if (epoch <= s->switch_epoch) return VBOND_ERR_EPOCH;
    uint8_t m[VBOND_SWITCH_MSG];
    vbond_switch_msg(juris, enable, epoch, m);
    if (!s->cfg.verify(s->cfg.vctx, s->cfg.gateway, m, sizeof m, sig, sig_len))
        return VBOND_ERR_SIG;
    s->enabled = enable;
    s->switch_epoch = epoch;
    return VBOND_OK;
}

/* ---- postings ---- */
static void derive(const vbond_t *s, uint8_t tag, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, s->cfg.seed, 32);
    in[32] = tag;
    put64(in + 33, s->seq);
    sha256(in, sizeof in, out);
}

static vbond_status_t post(vbond_t *s, const pay_line_t *ln, uint32_t n, uint8_t kind,
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
    pay_w_s(&w, "VBOND-");
    pay_w_u64(&w, s->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "v-bond", sizeof r.memo);
    r.tick = tick;
    r.kind = kind;
    r.ext_cap = PAY_CAP_FINANCIAL;
    s->seq++;
    pay_receipt_t rc;
    s->last_ledger = pay_ledger_post(s->L, &r, &rc);
    if (s->last_ledger == PAY_OK) {
        s->posted++;
        return VBOND_OK;
    }
    return s->last_ledger == PAY_ERR_FUNDS ? VBOND_ERR_FUNDS : VBOND_ERR_LEDGER;
}

static pay_line_t line(uint32_t a, uint64_t d, bool neg)
{
    pay_line_t l = {a, neg ? -(int64_t) d : (int64_t) d, 0};
    return l;
}

/* one transfer of `amt` from `a` to `b` */
static vbond_status_t move(vbond_t *s, uint32_t a, uint32_t b, uint64_t amt, uint64_t tick)
{
    const pay_line_t ln[2] = {line(a, amt, true), line(b, amt, false)};
    return post(s, ln, 2, PAY_KIND_TRANSFER, tick);
}

/* ---- B1 ---- */
vbond_status_t vbond_issue(vbond_t *s, uint32_t owner, uint32_t vbill_acct, uint32_t vino_payee,
                           uint64_t qty, uint32_t k, uint64_t tick, uint64_t *serial)
{
    if (!s || !s->L) return VBOND_ERR_ARG;
    if (!s->enabled) return VBOND_ERR_DISABLED;
    if (!mine(s, vbill_acct, s->cfg.vbill, owner) || !mine(s, vino_payee, s->cfg.vino, owner) ||
        qty == 0 || qty > VBOND_LINE_MAX)
        return VBOND_ERR_ARG;
    uint32_t t = 0;
    while (t < s->cfg.n_terms && s->cfg.k[t] != k) t++;
    if (t == s->cfg.n_terms) return VBOND_ERR_TERM;
    uint64_t end;
    if (!pay_add_ok(tick, vbond_fib(k), &end)) return VBOND_ERR_OVERFLOW;
    uint32_t i = 0;
    while (i < VBOND_MAX_BONDS && s->bond[i].used) i++;
    if (i == VBOND_MAX_BONDS) return VBOND_ERR_FULL;
    vbond_status_t st = move(s, vbill_acct, s->cfg.lock, qty, tick);
    if (st != VBOND_OK) return st;
    vbond_bond_t *b = &s->bond[i];
    pay_memset(b, 0, sizeof *b);
    b->used = true;
    b->term = (uint8_t) t;
    b->owner = owner;
    b->payee = vino_payee;
    b->serial = s->next_serial++;
    b->qty = qty;
    b->start = tick;
    b->maturity = end;
    if (serial) *serial = b->serial;
    return VBOND_OK;
}

/* the slot of the live bond `serial`, or VBOND_MAX_BONDS */
static uint32_t slot(const vbond_t *s, uint64_t serial)
{
    uint32_t i = 0;
    while (i < VBOND_MAX_BONDS && !(s->bond[i].used && s->bond[i].serial == serial)) i++;
    return i;
}

static vbond_bond_t *find(vbond_t *s, uint64_t serial)
{
    uint32_t i = slot(s, serial);
    return i < VBOND_MAX_BONDS ? &s->bond[i] : 0;
}

const vbond_bond_t *vbond_get(const vbond_t *s, uint64_t serial)
{
    uint32_t i = s ? slot(s, serial) : VBOND_MAX_BONDS;
    return i < VBOND_MAX_BONDS ? &s->bond[i] : 0;
}

/* ---- B2 ---- */
void vbond_transfer_msg(uint32_t juris, uint64_t serial, uint32_t to, uint32_t to_payee,
                        uint64_t nonce, uint8_t out[VBOND_TX_MSG])
{
    put_tag(out, "zxv/vbond-tx/v1");
    put32(out + 15, juris);
    put64(out + 19, serial);
    put32(out + 27, to);
    put32(out + 31, to_payee);
    put64(out + 35, nonce);
}

vbond_status_t vbond_transfer(vbond_t *s, uint64_t serial, uint32_t from, uint32_t to,
                              uint32_t to_payee, const uint8_t *sig, uint32_t sig_len)
{
    if (!s || !s->L || !sig) return VBOND_ERR_ARG;
    if (!s->enabled) return VBOND_ERR_DISABLED;
    vbond_bond_t *b = find(s, serial);
    if (!b) return VBOND_ERR_NOT_FOUND;
    if (b->owner != from) return VBOND_ERR_OWNER;
    if (!mine(s, to_payee, s->cfg.vino, to)) return VBOND_ERR_ARG;
    uint8_t m[VBOND_TX_MSG];
    vbond_transfer_msg(s->cfg.juris, serial, to, to_payee, b->nonce, m);
    if (!s->cfg.verify(s->cfg.vctx, from, m, sizeof m, sig, sig_len)) return VBOND_ERR_SIG;
    b->owner = to;
    b->payee = to_payee;
    b->nonce++;
    return VBOND_OK;
}

/* ---- B3 ---- */
vbond_status_t vbond_redeem(vbond_t *s, uint64_t serial, uint32_t owner, uint32_t vbill_acct,
                            uint64_t tick)
{
    if (!s || !s->L) return VBOND_ERR_ARG;
    vbond_bond_t *b = find(s, serial);
    if (!b) return VBOND_ERR_NOT_FOUND;
    if (b->owner != owner) return VBOND_ERR_OWNER;
    if (!mine(s, vbill_acct, s->cfg.vbill, owner)) return VBOND_ERR_ARG;
    if (tick < b->maturity) return VBOND_ERR_IMMATURE;
    vbond_status_t st = move(s, s->cfg.lock, vbill_acct, b->qty, tick);
    if (st == VBOND_OK) pay_memset(b, 0, sizeof *b);
    return st;
}

/* ---- B4 ---- */
vbond_status_t vbond_premium(const vbond_t *s, uint64_t price, uint64_t floor_price, uint64_t qty,
                             uint64_t *amount)
{
    uint64_t diff, r, a, part;
    if (!s || !amount) return VBOND_ERR_ARG;
    *amount = 0;
    if (!pay_sub_ok(price, floor_price, &diff)) return VBOND_OK; /* below the floor */
    /* premium = q * den + r; amount = q * num + floor(r * num / den) >= q */
    pay_u128 q = pay_udiv128_64(pay_mul64(diff, qty), s->cfg.share_den, &r);
    if (q.hi || __builtin_mul_overflow(q.lo, s->cfg.share_num, &a)) return VBOND_ERR_OVERFLOW;
    pay_muldiv(r, s->cfg.share_num, s->cfg.share_den, &part, 0); /* < num: cannot fail */
    if (!pay_add_ok(a, part, &a) || a > VBOND_LINE_MAX) return VBOND_ERR_OVERFLOW;
    *amount = a;
    return VBOND_OK;
}

static bool is_payee(const vbond_t *s, uint32_t a)
{
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
        if (s->bond[i].used && s->bond[i].payee == a) return true;
    return false;
}

/* a pool: a Vino account that is no bond's payee and not frozen */
static bool pool_ok(const vbond_t *s, uint32_t pool)
{
    const pay_account_t *p = of(s, pool, s->cfg.vino);
    return p && !(p->flags & PAY_ACCT_FROZEN) && !is_payee(s, pool);
}

vbond_status_t vbond_pool_credit(vbond_t *s, uint32_t src, uint32_t pool, uint64_t amount,
                                 uint64_t tick)
{
    if (!s || !s->L) return VBOND_ERR_ARG;
    if (!s->enabled) return VBOND_ERR_DISABLED;
    if (!pool_ok(s, pool) || !of(s, src, s->cfg.vino) || src == pool || amount == 0 ||
        amount > VBOND_LINE_MAX)
        return VBOND_ERR_ARG;
    return move(s, src, pool, amount, tick);
}

vbond_status_t vbond_premium_credit(vbond_t *s, uint32_t src, uint32_t pool, uint64_t price,
                                    uint64_t floor_price, uint64_t qty, uint64_t tick,
                                    uint64_t *amount)
{
    uint64_t a, tmp;
    if (!amount) amount = &tmp;
    *amount = 0;
    if (!s || !s->L) return VBOND_ERR_ARG;
    if (!s->enabled) return VBOND_ERR_DISABLED;
    vbond_status_t st = vbond_premium(s, price, floor_price, qty, &a);
    if (st != VBOND_OK || a == 0) return st;
    st = vbond_pool_credit(s, src, pool, a, tick);
    if (st == VBOND_OK) *amount = a;
    return st;
}

vbond_status_t vbond_distribute(vbond_t *s, uint32_t pool, uint64_t tick, vbond_dist_t *out)
{
    vbond_dist_t tmp;
    if (!out) out = &tmp;
    pay_memset(out, 0, sizeof *out);
    if (!s || !s->L) return VBOND_ERR_ARG;
    if (!pool_ok(s, pool)) return VBOND_ERR_ARG;
    const uint64_t P = of(s, pool, s->cfg.vino)->debit;
    if (P == 0) return VBOND_ERR_EMPTY;
    if (P > VBOND_LINE_MAX) return VBOND_ERR_OVERFLOW;
    out->pool = P;

    uint64_t U = 0, u[VBOND_MAX_BONDS], share[VBOND_MAX_BONDS], rem[VBOND_MAX_BONDS], sum = 0;
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
        const vbond_bond_t *b = &s->bond[i];
        u[i] = 0;
        if (b->used && b->start <= tick &&
            (__builtin_mul_overflow(b->qty, s->cfg.weight[b->term], &u[i]) ||
             __builtin_add_overflow(U, u[i], &U)))
            return VBOND_ERR_OVERFLOW;
    }
    if (U == 0) return VBOND_ERR_NOHOLD;
    out->units = U;
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
        pay_muldiv(P, u[i], U, &share[i], &rem[i]); /* <= P: cannot fail */
        sum += share[i];
    }
    /* leftover by largest remainder, ties to the lower slot. The leftover
     * is below the number of nonzero remainders, so a bond with u == 0
     * (remainder 0) never wins a unit. */
    bool got[VBOND_MAX_BONDS] = {false};
    for (uint64_t left = P - sum; left > 0; left--) {
        uint32_t best = 0;
        bool any = false;
        for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
            if (!got[i] && (!any || rem[i] > rem[best])) {
                best = i;
                any = true;
            }
        got[best] = true;
        share[best]++;
    }
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) s->bond[i].last_paid = 0;

    /* payouts: batches of up to PAY_MAX_LINES - 1 payees */
    uint32_t i = 0;
    while (i < VBOND_MAX_BONDS) {
        pay_line_t ln[PAY_MAX_LINES];
        uint32_t idx[PAY_MAX_LINES], k = 1;
        uint64_t tot = 0;
        for (; i < VBOND_MAX_BONDS && k < PAY_MAX_LINES; i++) {
            if (!share[i]) continue;
            idx[k] = i;
            ln[k++] = line(s->bond[i].payee, share[i], false);
            tot += share[i];
        }
        if (k == 1) break;
        ln[0] = line(pool, tot, true);
        vbond_status_t st = post(s, ln, k, PAY_KIND_TRANSFER, tick);
        if (st != VBOND_OK) return out->postings ? VBOND_ERR_PARTIAL : st;
        out->postings++;
        out->paid += tot;
        out->payees += k - 1;
        for (uint32_t m = 1; m < k; m++) s->bond[idx[m]].last_paid = share[idx[m]];
    }
    return VBOND_OK;
}

/* ---- B5 ---- */
vbond_status_t vbond_check(const vbond_t *s)
{
    if (!s || !s->L) return VBOND_ERR_ARG;
    uint64_t sum = 0, end;
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
        const vbond_bond_t *b = &s->bond[i];
        if (!b->used) continue;
        if (b->term >= s->cfg.n_terms ||
            !pay_add_ok(b->start, vbond_fib(s->cfg.k[b->term]), &end) || end != b->maturity ||
            !pay_add_ok(sum, b->qty, &sum))
            return VBOND_ERR_STATE;
    }
    const pay_account_t *lk = of(s, s->cfg.lock, s->cfg.vbill);
    if (!lk || lk->debit != sum) return VBOND_ERR_STATE;
    return pay_ledger_check(s->L) ? VBOND_OK : VBOND_ERR_STATE;
}
