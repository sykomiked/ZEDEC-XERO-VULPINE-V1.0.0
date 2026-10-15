/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zenny.c — Zenny chips, the exact manifest core. See zenny.h Z1-Z6. */
#include "zenny.h"
#include "../robin_debanks/sha256.h"

void zenny_init(zenny_t *z, zenny_verify_fn verify, void *vctx)
{
    if (!z) return;
    pay_memset(z, 0, sizeof *z);
    z->verify = verify;
    z->vctx = vctx;
}

static zenny_chip_t *chip_at(const zenny_t *z, uint32_t s)
{
    return (z && s < ZENNY_MAX_CHIPS && z->chip[s].used) ? (zenny_chip_t *) &z->chip[s] : 0;
}

static const zenny_version_t *last(const zenny_chip_t *ch)
{
    return &ch->ver[ch->n_ver - 1];
}

/* ---- canonical content (little-endian, fixed width) ---- */
static void h64(sha256_ctx_t *c, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 8);
}

static void htag(sha256_ctx_t *c, const char *t)
{
    sha256_update(c, (const uint8_t *) t, 16);
}

static void hhead(sha256_ctx_t *c, uint32_t minter, uint32_t co, uint64_t kappa, uint32_t rules)
{
    h64(c, minter);
    h64(c, co);
    h64(c, kappa);
    h64(c, rules);
}

/* n is hashed as given; at most ZENNY_MAX_LEGS legs are read */
static void hlegs(sha256_ctx_t *c, uint32_t n, const zenny_leg_t *leg, const zenny_price_t *pr)
{
    h64(c, n);
    for (uint32_t i = 0; i < n && i < ZENNY_MAX_LEGS; i++) {
        h64(c, leg[i].kind);
        h64(c, leg[i].custodian);
        h64(c, leg[i].asset);
        h64(c, leg[i].qty);
        h64(c, leg[i].weight);
        h64(c, pr[i].num);
        h64(c, pr[i].den);
    }
}

void zenny_sign_msg(zenny_purpose_t p, const uint8_t digest[32], uint8_t out[ZENNY_SIG_MSG])
{
    static const char *const tag[3] = {"zxv/zenny/mint/1", "zxv/zenny/rebl/1", "zxv/zenny/attn/1"};
    if ((uint32_t) p > ZENNY_SIG_ATTEST) {
        pay_memset(out, 0, ZENNY_SIG_MSG);
        return;
    }
    pay_memcpy(out, tag[p], 16);
    pay_memcpy(out + 16, digest, 32);
}

void zenny_mint_digest(const zenny_req_t *r, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    htag(&c, "zxv/zenny/id/v1.");
    hhead(&c, r->minter, r->co_minter, r->kappa, r->rules);
    hlegs(&c, r->n_legs, r->leg, r->price);
    h64(&c, r->nonce);
    sha256_final(&c, out);
}

zenny_status_t zenny_rebalance_digest(const zenny_t *z, uint32_t slot, const zenny_req_t *r,
                                      uint8_t out[32])
{
    const zenny_chip_t *ch = chip_at(z, slot);
    if (!ch || !r || !out) return ZENNY_ERR_ARG;
    sha256_ctx_t c;
    sha256_init(&c);
    htag(&c, "zxv/zenny/rbd/1.");
    sha256_update(&c, ch->id, 32);
    sha256_update(&c, last(ch)->hash, 32);
    hhead(&c, r->minter, r->co_minter, r->kappa, r->rules);
    hlegs(&c, r->n_legs, r->leg, r->price);
    sha256_final(&c, out);
    return ZENNY_OK;
}

void zenny_att_digest(const uint8_t chip_id[32], const uint8_t head[32], const zenny_leg_t *leg,
                      uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    htag(&c, "zxv/zenny/att/2.");
    sha256_update(&c, chip_id, 32);
    sha256_update(&c, head, 32);
    h64(&c, leg->kind);
    h64(&c, leg->custodian);
    h64(&c, leg->asset);
    h64(&c, leg->qty);
    sha256_final(&c, out);
}

/* Z3: SHA-256(prev || content || version) */
static void version_hash(const zenny_chip_t *ch, const zenny_version_t *v, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, v->prev, 32);
    htag(&c, "zxv/zenny/ver/1.");
    sha256_update(&c, ch->id, 32);
    hhead(&c, ch->minter, ch->co_minter, ch->kappa, ch->rules);
    h64(&c, ch->nonce);
    h64(&c, v->op);
    h64(&c, v->chips);
    h64(&c, v->tick);
    h64(&c, v->recipient);
    hlegs(&c, v->n_legs, v->leg, v->price);
    h64(&c, v->version);
    sha256_final(&c, out);
}

static void append(zenny_chip_t *ch, zenny_version_t *v)
{
    /* a mint's version comes zeroed from from_req: prev is 32 zero bytes */
    if (ch->n_ver) pay_memcpy(v->prev, last(ch)->hash, 32);
    v->version = ch->n_ver;
    version_hash(ch, v, v->hash);
    ch->ver[ch->n_ver++] = *v;
}

/* ---- Z1/Z2 exact valuation ---- */
static zenny_status_t valuate(uint32_t n, const zenny_leg_t *leg, const zenny_price_t *pr,
                              uint64_t kappa, bool allow_zero, uint64_t *chips)
{
    uint64_t val[ZENNY_MAX_LEGS], t = 0, w = 0, rem;
    bool anchor = false;
    for (uint32_t i = 0; i < n; i++) {
        if (leg[i].kind < ZENNY_VBILL || leg[i].kind > ZENNY_COMPUTE) return ZENNY_ERR_KIND;
        if (pr[i].den == 0) return ZENNY_ERR_PRICE;
        if (!pay_muldiv(leg[i].qty, pr[i].num, pr[i].den, &val[i], &rem)) return ZENNY_ERR_OVERFLOW;
        if (rem) return ZENNY_ERR_INEXACT;
        if (!pay_add_ok(t, val[i], &t) || !pay_add_ok(w, leg[i].weight, &w))
            return ZENNY_ERR_OVERFLOW;
        if (leg[i].kind == ZENNY_VBILL && leg[i].weight) anchor = true;
    }
    if (!anchor) return ZENNY_ERR_ANCHOR;
    if (t == 0 && !allow_zero) return ZENNY_ERR_ARG;
    if (t % kappa) return ZENNY_ERR_INEXACT;
    for (uint32_t i = 0; i < n; i++)
        if (pay_u128_cmp(pay_mul64(val[i], w), pay_mul64(leg[i].weight, t)))
            return ZENNY_ERR_WEIGHT;
    *chips = t / kappa;
    return ZENNY_OK;
}

static zenny_status_t shape(const zenny_req_t *r)
{
    if (r->n_legs == 0 || r->n_legs > ZENNY_MAX_LEGS) return ZENNY_ERR_LEGS;
    if (r->kappa == 0) return ZENNY_ERR_KAPPA;
    if (r->rules & ~ZENNY_RULES_KNOWN) return ZENNY_ERR_ARG;
    if (r->rules & ZENNY_RULE_MARGIN) return ZENNY_ERR_LEVERAGE;
    if (r->rules & ZENNY_RULE_CREDIT) return ZENNY_ERR_CREDIT;
    if ((r->rules & ZENNY_RULE_BILATERAL) && r->co_minter == r->minter) return ZENNY_ERR_ARG;
    return ZENNY_OK;
}

static bool signed_by(const zenny_t *z, uint32_t who, zenny_purpose_t p, const uint8_t d[32],
                      const uint8_t *sig, uint32_t len)
{
    uint8_t m[ZENNY_SIG_MSG];
    if (!sig) return false;
    zenny_sign_msg(p, d, m);
    return z->verify(z->vctx, who, m, ZENNY_SIG_MSG, sig, len);
}

/* minter, co-minter (BILATERAL) and every COMMODITY custodian; head is the
 * chip's current version hash (32 zero bytes for a mint) */
static zenny_status_t authorise(const zenny_t *z, const zenny_req_t *r, zenny_purpose_t p,
                                const uint8_t d[32], const uint8_t id[32], const uint8_t head[32])
{
    if (!signed_by(z, r->minter, p, d, r->sig_minter, r->sig_minter_len)) return ZENNY_ERR_SIG;
    if ((r->rules & ZENNY_RULE_BILATERAL) &&
        !signed_by(z, r->co_minter, p, d, r->sig_co, r->sig_co_len))
        return ZENNY_ERR_SIG;
    for (uint32_t i = 0; i < r->n_legs; i++) {
        if (r->leg[i].kind != ZENNY_COMMODITY) continue;
        uint8_t a[32];
        zenny_att_digest(id, head, &r->leg[i], a);
        if (!signed_by(z, r->leg[i].custodian, ZENNY_SIG_ATTEST, a, r->att[i], r->att_len[i]))
            return ZENNY_ERR_ATTEST;
    }
    return ZENNY_OK;
}

static void from_req(zenny_version_t *v, const zenny_req_t *r, uint8_t op, uint64_t chips)
{
    pay_memset(v, 0, sizeof *v);
    v->op = op;
    v->chips = chips;
    v->tick = r->tick;
    v->n_legs = r->n_legs;
    for (uint32_t i = 0; i < r->n_legs; i++) {
        v->leg[i] = r->leg[i];
        v->price[i] = r->price[i];
    }
}

bool zenny_find(const zenny_t *z, const uint8_t id[32], uint32_t *slot)
{
    if (!z || !id) return false;
    for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++)
        if (z->chip[s].used && pay_memeq(z->chip[s].id, id, 32)) {
            if (slot) *slot = s;
            return true;
        }
    return false;
}

zenny_status_t zenny_mint(zenny_t *z, const zenny_req_t *r, uint32_t *slot)
{
    if (!z || !r || !slot || !z->verify) return ZENNY_ERR_ARG;
    zenny_status_t st = shape(r);
    if (st) return st;
    uint64_t chips;
    st = valuate(r->n_legs, r->leg, r->price, r->kappa, false, &chips);
    if (st) return st;
    uint8_t id[32];
    zenny_mint_digest(r, id);
    if (zenny_find(z, id, 0)) return ZENNY_ERR_DUP;
    static const uint8_t genesis[32] = {0};
    st = authorise(z, r, ZENNY_SIG_MINT, id, id, genesis);
    if (st) return st;
    uint32_t s = 0;
    while (s < ZENNY_MAX_CHIPS && z->chip[s].used) s++;
    if (s == ZENNY_MAX_CHIPS) return ZENNY_ERR_FULL;
    zenny_chip_t *ch = &z->chip[s];
    pay_memset(ch, 0, sizeof *ch);
    ch->used = true;
    pay_memcpy(ch->id, id, 32);
    ch->minter = r->minter;
    ch->co_minter = r->co_minter;
    ch->kappa = r->kappa;
    ch->rules = r->rules;
    ch->nonce = r->nonce;
    zenny_version_t v;
    from_req(&v, r, ZENNY_OP_MINT, chips);
    append(ch, &v);
    *slot = s;
    return ZENNY_OK;
}

zenny_status_t zenny_rebalance(zenny_t *z, uint32_t slot, const zenny_req_t *r)
{
    zenny_chip_t *ch = chip_at(z, slot);
    if (!ch || !r || !z->verify) return ZENNY_ERR_ARG;
    if (!(ch->rules & ZENNY_RULE_ALLOW_REBALANCE)) return ZENNY_ERR_NOT_ALLOWED;
    if (ch->n_ver >= ZENNY_MAX_VERSIONS) return ZENNY_ERR_FULL;
    const zenny_version_t *cur = last(ch);
    if (r->minter != ch->minter || r->co_minter != ch->co_minter || r->kappa != ch->kappa ||
        r->rules != ch->rules || r->n_legs != cur->n_legs)
        return ZENNY_ERR_IMMUTABLE;
    for (uint32_t i = 0; i < cur->n_legs; i++)
        if (r->leg[i].kind != cur->leg[i].kind || r->leg[i].asset != cur->leg[i].asset ||
            r->leg[i].custodian != cur->leg[i].custodian)
            return ZENNY_ERR_IMMUTABLE;
    uint64_t chips;
    zenny_status_t st = valuate(r->n_legs, r->leg, r->price, r->kappa, false, &chips);
    if (st) return st;
    if (chips != cur->chips) return ZENNY_ERR_CLAIM;
    uint8_t d[32];
    zenny_rebalance_digest(z, slot, r, d);
    st = authorise(z, r, ZENNY_SIG_REBALANCE, d, ch->id, cur->hash);
    if (st) return st;
    zenny_version_t v;
    from_req(&v, r, ZENNY_OP_REBALANCE, chips);
    append(ch, &v);
    return ZENNY_OK;
}

/* Z5: out_i = qty_i * c / N exact; value_i = out_i * num / den exact; and
 * out_i exact at the STORED price too, so the remainder (which keeps the
 * stored prices) still values to exactly (N - c) * kappa (zenny_check) */
static zenny_status_t payout(const zenny_version_t *v, uint64_t c, const zenny_price_t *batch,
                             uint64_t *out, uint64_t *val, uint64_t *total)
{
    uint64_t rem, sv;
    *total = 0;
    for (uint32_t i = 0; i < v->n_legs; i++) {
        (void) pay_muldiv(v->leg[i].qty, c, v->chips, &out[i], &rem); /* out_i <= qty_i */
        if (rem) return ZENNY_ERR_INEXACT;
        /* <= qty_i * num / den, which fits: the stored valuation was exact */
        if (!pay_muldiv(out[i], v->price[i].num, v->price[i].den, &sv, &rem) || rem)
            return ZENNY_ERR_INEXACT;
        if (!batch) continue;
        if (batch[i].den == 0) return ZENNY_ERR_PRICE;
        if (!pay_muldiv(out[i], batch[i].num, batch[i].den, &val[i], &rem))
            return ZENNY_ERR_OVERFLOW;
        if (rem) return ZENNY_ERR_INEXACT;
        if (!pay_add_ok(*total, val[i], total)) return ZENNY_ERR_OVERFLOW;
    }
    return ZENNY_OK;
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

uint64_t zenny_quote(const zenny_t *z, uint32_t slot, uint64_t want, const zenny_price_t *batch)
{
    const zenny_chip_t *ch = chip_at(z, slot);
    if (!ch) return 0;
    const zenny_version_t *v = last(ch);
    uint64_t n = v->chips, step = 1;
    if (want > n) want = n;
    if (want == 0) return 0;
    /* out_i exact iff c is a multiple of n / gcd(qty_i, n); every such
     * step divides n, so their lcm divides n and cannot overflow */
    for (uint32_t i = 0; i < v->n_legs; i++) {
        uint64_t s = n / gcd64(v->leg[i].qty, n);
        step = step / gcd64(step, s) * s;
    }
    /* c = k * step pays a_i = qty_i * step / n per k; its value at a price
     * num / den is exact iff k is a multiple of den / gcd(den, a_i * num).
     * Both the stored prices (the remainder must stay exact) and the batch
     * prices (the disposal value) must be exact. */
    uint64_t k = 1;
    for (uint32_t pass = 0; pass < (batch ? 2u : 1u); pass++)
        for (uint32_t i = 0; i < v->n_legs; i++) {
            const zenny_price_t *p = pass ? &batch[i] : &v->price[i];
            uint64_t a, d = p->den;
            if (d == 0) return 0;
            (void) pay_muldiv(v->leg[i].qty, step, n, &a, 0);
            d /= gcd64(d, a);
            d /= gcd64(d, p->num);
            if (__builtin_mul_overflow(k / gcd64(k, d), d, &k)) return 0;
        }
    if (__builtin_mul_overflow(step, k, &step)) return 0;
    uint64_t c = want / step * step, out[ZENNY_MAX_LEGS], val[ZENNY_MAX_LEGS], total;
    /* a candidate whose disposal value overflows 64 bits gives 0 */
    return (c && payout(v, c, batch, out, val, &total) == ZENNY_OK) ? c : 0;
}

zenny_status_t zenny_decompress(zenny_t *z, uint32_t slot, uint64_t c, const zenny_price_t *batch,
                                uint64_t tick, zenny_receipt_t *out)
{
    zenny_chip_t *ch = chip_at(z, slot);
    if (!ch || !batch || c == 0) return ZENNY_ERR_ARG;
    const zenny_version_t *cur = last(ch);
    if (c > cur->chips) return ZENNY_ERR_FUNDS;
    if (ch->n_ver >= ZENNY_MAX_VERSIONS) return ZENNY_ERR_FULL;
    if (z->r_next - z->r_acked >= ZENNY_MAX_RECEIPTS) return ZENNY_ERR_LOG;
    zenny_receipt_t rc;
    pay_memset(&rc, 0, sizeof rc);
    zenny_status_t st = payout(cur, c, batch, rc.out, rc.value, &rc.total);
    if (st) return st;
    zenny_version_t v = *cur;
    v.op = ZENNY_OP_DECOMPRESS;
    v.recipient = 0; /* only a top-up version names a recipient */
    v.chips -= c;
    v.tick = tick;
    for (uint32_t i = 0; i < v.n_legs; i++) v.leg[i].qty -= rc.out[i];
    append(ch, &v);
    rc.seq = z->r_next;
    pay_memcpy(rc.chip_id, ch->id, 32);
    rc.version = v.version;
    rc.chips = c;
    rc.n_legs = v.n_legs;
    rc.fee = pay_assure_fee(rc.total);
    rc.tick = tick;
    z->rcpt[z->r_next % ZENNY_MAX_RECEIPTS] = rc;
    z->r_next++;
    if (out) *out = rc;
    return ZENNY_OK;
}

zenny_status_t zenny_verify_chain(const zenny_t *z, uint32_t slot)
{
    const zenny_chip_t *ch = chip_at(z, slot);
    if (!ch) return ZENNY_ERR_ARG;
    if (ch->n_ver == 0 || ch->n_ver > ZENNY_MAX_VERSIONS) return ZENNY_ERR_CHAIN;
    zenny_req_t r;
    pay_memset(&r, 0, sizeof r);
    r.minter = ch->minter;
    r.co_minter = ch->co_minter;
    r.kappa = ch->kappa;
    r.rules = ch->rules;
    r.nonce = ch->nonce;
    r.n_legs = ch->ver[0].n_legs;
    pay_memcpy(r.leg, ch->ver[0].leg, sizeof r.leg);
    pay_memcpy(r.price, ch->ver[0].price, sizeof r.price);
    uint8_t h[32], prev[32];
    zenny_mint_digest(&r, h);
    if (!pay_memeq(h, ch->id, 32) || ch->ver[0].op != ZENNY_OP_MINT) return ZENNY_ERR_CHAIN;
    pay_memset(prev, 0, 32);
    for (uint32_t i = 0; i < ch->n_ver; i++) {
        const zenny_version_t *e = &ch->ver[i];
        if (!pay_memeq(e->prev, prev, 32)) return ZENNY_ERR_CHAIN;
        version_hash(ch, e, h);
        if (!pay_memeq(h, e->hash, 32)) return ZENNY_ERR_CHAIN;
        pay_memcpy(prev, h, 32);
    }
    return ZENNY_OK;
}

zenny_status_t zenny_check(const zenny_t *z, uint32_t slot)
{
    const zenny_chip_t *ch = chip_at(z, slot);
    if (!ch) return ZENNY_ERR_ARG;
    const zenny_version_t *v = last(ch);
    if (v->n_legs > ZENNY_MAX_LEGS || ch->kappa == 0) return ZENNY_ERR_CHAIN;
    uint64_t chips;
    zenny_status_t st = valuate(v->n_legs, v->leg, v->price, ch->kappa, true, &chips);
    if (st) return st;
    return chips == v->chips ? ZENNY_OK : ZENNY_ERR_CLAIM;
}

const zenny_version_t *zenny_current(const zenny_t *z, uint32_t slot)
{
    const zenny_chip_t *ch = chip_at(z, slot);
    return ch ? last(ch) : 0;
}

zenny_status_t zenny_receipt_get(const zenny_t *z, uint64_t seq, zenny_receipt_t *out)
{
    if (!z || !out || seq < z->r_acked || seq >= z->r_next) return ZENNY_ERR_ARG;
    *out = z->rcpt[seq % ZENNY_MAX_RECEIPTS];
    return ZENNY_OK;
}

zenny_status_t zenny_receipt_ack(zenny_t *z, uint64_t upto)
{
    if (!z || upto > z->r_next || upto < z->r_acked) return ZENNY_ERR_ARG;
    z->r_acked = upto;
    return ZENNY_OK;
}

/* ---- Z7: top-up ---- */
void zenny_cap_digest(const uint8_t chip_id[32], uint32_t i, const zenny_leg_t *leg, uint64_t cap,
                      uint64_t expires, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    htag(&c, "zxv/zenny/cap/1.");
    sha256_update(&c, chip_id, 32);
    h64(&c, i);
    h64(&c, leg->kind);
    h64(&c, leg->custodian);
    h64(&c, leg->asset);
    h64(&c, cap);
    h64(&c, expires);
    sha256_final(&c, out);
}

zenny_status_t zenny_attest(zenny_t *z, uint32_t slot, uint32_t i, uint64_t cap, uint64_t expires,
                            const uint8_t *sig, uint32_t sig_len)
{
    zenny_chip_t *ch = chip_at(z, slot);
    if (!ch || !z->verify || i >= last(ch)->n_legs) return ZENNY_ERR_ARG;
    const zenny_leg_t *l = &last(ch)->leg[i];
    if (l->kind != ZENNY_COMMODITY) return ZENNY_ERR_ARG;
    if (expires <= ch->att_exp[i]) return ZENNY_ERR_STALE;
    uint8_t d[32];
    zenny_cap_digest(ch->id, i, l, cap, expires, d);
    if (!signed_by(z, l->custodian, ZENNY_SIG_ATTEST, d, sig, sig_len)) return ZENNY_ERR_ATTEST;
    ch->att_cap[i] = cap;
    ch->att_exp[i] = expires;
    return ZENNY_OK;
}

/* the new version a top-up would append, or why not */
static zenny_status_t topup_plan(const zenny_chip_t *ch, uint64_t amount, const zenny_price_t *pr,
                                 uint64_t tick, zenny_version_t *nv)
{
    const zenny_version_t *cur = last(ch);
    uint64_t w = 0, add, rem;
    if (amount == 0 || !pr) return ZENNY_ERR_ARG;
    if (!(ch->rules & ZENNY_RULE_ALLOW_TOPUP)) return ZENNY_ERR_NOT_ALLOWED;
    if (amount % ch->kappa) return ZENNY_ERR_INEXACT;
    add = amount / ch->kappa;
    for (uint32_t i = 0; i < cur->n_legs; i++)
        if (!pay_add_ok(w, cur->leg[i].weight, &w)) return ZENNY_ERR_OVERFLOW;
    *nv = *cur;
    for (uint32_t i = 0; i < cur->n_legs; i++) {
        const zenny_leg_t *l = &cur->leg[i];
        uint64_t v, dq;
        nv->price[i] = pr[i];
        if (l->weight == 0) continue;
        if (l->kind == ZENNY_COMPUTE) return ZENNY_ERR_UNBACKED;
        if (pr[i].den == 0 || pr[i].num == 0) return ZENNY_ERR_PRICE;
        if (!pay_muldiv(amount, l->weight, w, &v, &rem)) return ZENNY_ERR_OVERFLOW;
        if (rem) return ZENNY_ERR_INEXACT;
        if (!pay_muldiv(v, pr[i].den, pr[i].num, &dq, &rem)) return ZENNY_ERR_OVERFLOW;
        if (rem) return ZENNY_ERR_INEXACT;
        if (!pay_add_ok(l->qty, dq, &nv->leg[i].qty)) return ZENNY_ERR_OVERFLOW;
        if (l->kind == ZENNY_COMMODITY &&
            (tick >= ch->att_exp[i] || nv->leg[i].qty > ch->att_cap[i]))
            return ZENNY_ERR_UNBACKED;
    }
    uint64_t chips, want;
    if (!pay_add_ok(cur->chips, add, &want)) return ZENNY_ERR_OVERFLOW;
    zenny_status_t st = valuate(nv->n_legs, nv->leg, nv->price, ch->kappa, false, &chips);
    if (st) return st;
    if (chips != want) return ZENNY_ERR_CLAIM;
    nv->op = ZENNY_OP_TOPUP;
    nv->chips = chips;
    nv->tick = tick;
    return ZENNY_OK;
}

zenny_status_t zenny_topup(zenny_t *z, uint32_t slot, uint64_t amount, const zenny_price_t *prices,
                           uint32_t recipient, uint64_t tick, uint64_t *added)
{
    zenny_chip_t *ch = chip_at(z, slot);
    if (!ch) return ZENNY_ERR_ARG;
    if (ch->n_ver >= ZENNY_MAX_VERSIONS) return ZENNY_ERR_FULL;
    zenny_version_t v;
    zenny_status_t st = topup_plan(ch, amount, prices, tick, &v);
    if (st) return st;
    uint64_t before = last(ch)->chips;
    v.recipient = recipient;
    append(ch, &v);
    if (added) *added = v.chips - before;
    return ZENNY_OK;
}

uint64_t zenny_topup_unit(const zenny_t *z, uint32_t slot, const zenny_price_t *prices)
{
    const zenny_chip_t *ch = chip_at(z, slot);
    if (!ch || !prices) return 0;
    const zenny_version_t *cur = last(ch);
    uint64_t w = 0, step = ch->kappa, k = 1;
    for (uint32_t i = 0; i < cur->n_legs; i++)
        if (!pay_add_ok(w, cur->leg[i].weight, &w)) return 0;
    /* whole chips: a multiple of kappa; amount * weight_i / W exact iff
     * amount is a multiple of W / gcd(W, weight_i) */
    for (uint32_t i = 0; i < cur->n_legs; i++) {
        uint64_t s = w / gcd64(w, cur->leg[i].weight);
        if (__builtin_mul_overflow(step / gcd64(step, s), s, &step)) return 0;
    }
    /* amount = k * step buys b_i = step * weight_i / W value per k; the
     * quantity b_i * den / num is exact iff k is a multiple of
     * num / gcd(num, b_i * den) */
    for (uint32_t i = 0; i < cur->n_legs; i++) {
        uint64_t b, d = prices[i].num;
        if (cur->leg[i].weight == 0) continue;
        if (d == 0 || !pay_muldiv(step, cur->leg[i].weight, w, &b, 0)) return 0;
        d /= gcd64(d, b);
        d /= gcd64(d, prices[i].den);
        if (__builtin_mul_overflow(k / gcd64(k, d), d, &k)) return 0;
    }
    if (__builtin_mul_overflow(step, k, &step)) return 0;
    return step;
}

uint64_t zenny_topup_quote(const zenny_t *z, uint32_t slot, uint64_t want,
                           const zenny_price_t *prices, uint64_t tick)
{
    uint64_t unit = zenny_topup_unit(z, slot, prices);
    if (!unit) return 0;
    uint64_t a = want / unit * unit;
    zenny_version_t v;
    /* a candidate the top-up would refuse (unbacked, over a cap, moved
     * prices, overflow) gives 0 */
    return (a && topup_plan(&z->chip[slot], a, prices, tick, &v) == ZENNY_OK) ? a : 0;
}

bool zenny_allot_credit(void *ctx, uint32_t dest, const void *who, uint64_t amount)
{
    zenny_allot_ctx_t *c = ctx;
    (void) who;
    if (!c || dest != ZENNY_ALLOT_DEST_NAV) return false;
    c->last = zenny_topup(c->z, c->slot, amount, c->prices, c->recipient, c->tick, 0);
    return c->last == ZENNY_OK;
}
