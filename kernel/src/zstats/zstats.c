/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zstats.c — the public statistics feed. See zstats.h S1-S6. */
#include "zstats.h"
#include "../robin_debanks/sha256.h"

void zstats_sources_default(zstats_sources_t *s)
{
    if (!s) return;
    pay_memset(s, 0, sizeof *s);
    s->reserve_acct = ZSTATS_NONE;
    s->req_num = 1;
    s->req_den = 1;
    s->min_cohort = ZSTATS_MIN_COHORT;
}

/* a * b >= c * d, exactly */
static bool ge(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    return pay_u128_cmp(pay_mul64(a, b), pay_mul64(c, d)) >= 0;
}

static zstats_rat_t mkrat(uint64_t num, uint64_t den)
{
    zstats_rat_t r = {den ? num : 0, den, den != 0};
    return r;
}

static bool rat_ok(uint64_t num, uint64_t den, bool defined, uint64_t want_num, uint64_t want_den)
{
    return defined == (den != 0) && den == want_den && num == (defined ? want_num : 0);
}

/* ===== S4 codec: one walker serves encode and decode ===== */
typedef struct {
    uint8_t *p;
    uint32_t n;
    bool dec, bad;
} cur_t;

static void cn(cur_t *c, uint64_t *v, uint32_t width)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < width; i++) {
        if (c->dec)
            x |= (uint64_t) c->p[c->n + i] << (8 * i);
        else
            c->p[c->n + i] = (uint8_t) (*v >> (8 * i));
    }
    if (c->dec) *v = x;
    c->n += width;
}
static void c64(cur_t *c, uint64_t *v)
{
    cn(c, v, 8);
}
static void c32(cur_t *c, uint32_t *v)
{
    uint64_t t = *v;
    cn(c, &t, 4);
    *v = (uint32_t) t;
}
static void cb(cur_t *c, bool *v)
{
    uint64_t t = *v;
    cn(c, &t, 1);
    if (t > 1) c->bad = true;
    *v = t != 0;
}
static void crat(cur_t *c, zstats_rat_t *r)
{
    c64(c, &r->num);
    c64(c, &r->den);
    cb(c, &r->defined);
}
static void cgrp(cur_t *c, zstats_group_t *g)
{
    c32(c, &g->count);
    c64(c, &g->sum);
}

typedef void (*sec_fn)(cur_t *c, zstats_snapshot_t *s);

static void sec_reserve(cur_t *c, zstats_snapshot_t *s)
{
    c64(c, &s->reserve.reserve);
    c64(c, &s->reserve.claims);
    crat(c, &s->reserve.floor);
    c64(c, &s->reserve.req_num);
    c64(c, &s->reserve.req_den);
    cb(c, &s->reserve.floor_ok);
}
static void sec_vault(cur_t *c, zstats_snapshot_t *s)
{
    c64(c, &s->vault.held);
    c64(c, &s->vault.outstanding);
    crat(c, &s->vault.share);
}
static void sec_comp(cur_t *c, zstats_snapshot_t *s)
{
    c64(c, &s->comp.circulating_vino);
    c64(c, &s->comp.vbills_outstanding);
    c64(c, &s->comp.vbills_unlocked);
    c64(c, &s->comp.vbills_locked);
    for (uint32_t i = 0; i < ZSTATS_TIERS; i++) cgrp(c, &s->comp.tier[i]);
}
static void sec_bonds(cur_t *c, zstats_snapshot_t *s)
{
    c32(c, &s->bonds.n_terms);
    for (uint32_t i = 0; i < ZSTATS_TERMS; i++) {
        c32(c, &s->bonds.k[i]);
        cgrp(c, &s->bonds.term[i]);
    }
}
static void sec_auction(cur_t *c, zstats_snapshot_t *s)
{
    cb(c, &s->auction.has_last);
    c64(c, &s->auction.last_epoch);
    c64(c, &s->auction.price);
    c64(c, &s->auction.qty);
    c64(c, &s->auction.outstanding);
    c64(c, &s->auction.reserve);
    cb(c, &s->auction.reserve_valid);
    c64(c, &s->auction.floor_num);
    c64(c, &s->auction.floor_den);
    c64(c, &s->auction.backing);
    cb(c, &s->auction.floor_ok);
}
static void sec_allot(cur_t *c, zstats_snapshot_t *s)
{
    c64(c, &s->allotment.epoch);
    c64(c, &s->allotment.pot);
    c64(c, &s->allotment.ballots);
    c64(c, &s->allotment.threshold);
    c64(c, &s->allotment.entries);
    c64(c, &s->allotment.mode);
}
static void sec_barrels(cur_t *c, zstats_snapshot_t *s)
{
    grant_snapshot_t *b = &s->barrels;
    c64(c, &b->epoch);
    c64(c, &b->inflow);
    c64(c, &b->weight);
    c64(c, &b->rate_num);
    c64(c, &b->rate_den);
    cb(c, &b->rate_defined);
    c64(c, &b->granted_barrels);
    c64(c, &b->granted_cost);
    c64(c, &b->total_barrels_granted);
    c64(c, &b->pool_balance);
    c64(c, &b->reserve_balance);
    c64(c, &b->barrels_outstanding);
    c64(c, &b->barrels_backed);
    c64(c, &b->barrels_unbacked);
    c32(c, &b->lots);
    c32(c, &b->lots_backed);
}
static void sec_zenny(cur_t *c, zstats_snapshot_t *s)
{
    c32(c, &s->zenny.live);
    c64(c, &s->zenny.chips);
}

/* indexed like the ZSTATS_SEC_* bits */
static const sec_fn SEC[8] = {sec_reserve, sec_vault,  sec_comp,    sec_bonds,
                              sec_auction, sec_allot, sec_barrels, sec_zenny};

static void walk(cur_t *c, zstats_snapshot_t *s, uint8_t prev[32])
{
    for (uint32_t i = 0; i < ZSTATS_TAG_LEN; i++) {
        uint64_t t = (uint8_t) ZSTATS_TAG[i];
        cn(c, &t, 1);
        if (t != (uint8_t) ZSTATS_TAG[i]) c->bad = true;
    }
    uint64_t ver = ZSTATS_VERSION, len = ZSTATS_ENC_LEN;
    cn(c, &ver, 2);
    cn(c, &len, 4);
    if (ver != ZSTATS_VERSION || len != ZSTATS_ENC_LEN) c->bad = true;
    c32(c, &s->juris);
    c32(c, &s->publisher);
    for (uint32_t i = 0; i < 32; i++) {
        uint64_t t = prev[i];
        cn(c, &t, 1);
        prev[i] = (uint8_t) t;
    }
    c64(c, &s->epoch);
    c64(c, &s->tick);
    c32(c, &s->present);
    c32(c, &s->suppressed);
    c32(c, &s->min_cohort);
    for (uint32_t i = 0; i < 8; i++) SEC[i](c, s);
}

zstats_status_t zstats_encode(const zstats_snapshot_t *s, const uint8_t prev[32], uint8_t *out,
                              uint32_t cap, uint32_t *len)
{
    if (!s || !out || !len) return ZSTATS_ERR_ARG;
    *len = 0;
    if (cap < ZSTATS_ENC_LEN) return ZSTATS_ERR_SPACE;
    zstats_snapshot_t t = *s;
    uint8_t pv[32];
    pay_memset(pv, 0, 32);
    if (prev) pay_memcpy(pv, prev, 32);
    cur_t c = {out, 0, false, false};
    walk(&c, &t, pv);
    *len = c.n;
    return ZSTATS_OK;
}

zstats_status_t zstats_decode(const uint8_t *in, uint32_t len, zstats_snapshot_t *out,
                              uint8_t prev[32])
{
    if (!in || !out) return ZSTATS_ERR_ARG;
    if (len != ZSTATS_ENC_LEN) return ZSTATS_ERR_FORMAT;
    zstats_snapshot_t t;
    uint8_t pv[32];
    pay_memset(&t, 0, sizeof t);
    cur_t c = {(uint8_t *) in, 0, true, false};
    walk(&c, &t, pv);
    if (c.bad) return ZSTATS_ERR_FORMAT;
    *out = t;
    if (prev) pay_memcpy(prev, pv, 32);
    return ZSTATS_OK;
}

zstats_status_t zstats_digest(const zstats_snapshot_t *s, const uint8_t prev[32], uint8_t out[32])
{
    uint8_t buf[ZSTATS_ENC_LEN];
    uint32_t n;
    if (!out) return ZSTATS_ERR_ARG;
    zstats_status_t st = zstats_encode(s, prev, buf, sizeof buf, &n);
    if (st != ZSTATS_OK) return st;
    sha256(buf, n, out);
    return ZSTATS_OK;
}

/* ===== S5 consistency ===== */
/* An absent section encodes to zeros only. */
static bool sec_zero(sec_fn fn, zstats_snapshot_t *s)
{
    uint8_t buf[ZSTATS_ENC_LEN];
    cur_t c = {buf, 0, false, false};
    fn(&c, s);
    uint8_t acc = 0;
    for (uint32_t i = 0; i < c.n; i++) acc |= buf[i];
    return acc == 0;
}

static bool has(const zstats_snapshot_t *s, uint32_t sec)
{
    return (s->present & sec) != 0;
}

static bool chk_reserve(const zstats_snapshot_t *s)
{
    if (!has(s, ZSTATS_SEC_RESERVE)) return true;
    const zstats_rat_t *f = &s->reserve.floor;
    return s->reserve.req_den != 0 &&
           rat_ok(f->num, f->den, f->defined, s->reserve.reserve, s->reserve.claims) &&
           s->reserve.floor_ok ==
               ge(s->reserve.reserve, s->reserve.req_den, s->reserve.claims, s->reserve.req_num);
}

static bool chk_vault(const zstats_snapshot_t *s)
{
    const zstats_rat_t *r = &s->vault.share;
    return s->vault.held <= s->vault.outstanding &&
           rat_ok(r->num, r->den, r->defined, s->vault.held, s->vault.outstanding);
}

/* The lock family: the tiers (COMPRESSION) then the bond terms (BONDS). */
typedef struct {
    zstats_group_t *g[ZSTATS_TIERS + ZSTATS_TERMS];
    uint32_t bit[ZSTATS_TIERS + ZSTATS_TERMS];
    uint32_t n, have;
} family_t;

static void family(zstats_snapshot_t *s, family_t *f)
{
    f->n = 0;
    f->have = 0;
    for (uint32_t i = 0; has(s, ZSTATS_SEC_COMPRESSION) && i < ZSTATS_TIERS; i++) {
        f->g[f->n] = &s->comp.tier[i];
        f->bit[f->n++] = ZSTATS_SUP_TIER(i);
        f->have |= ZSTATS_SUP_TIER(i);
    }
    for (uint32_t i = 0; has(s, ZSTATS_SEC_BONDS) && i < s->bonds.n_terms; i++) {
        f->g[f->n] = &s->bonds.term[i];
        f->bit[f->n++] = ZSTATS_SUP_TERM(i);
        f->have |= ZSTATS_SUP_TERM(i);
    }
}

static bool chk_lock(zstats_snapshot_t *s)
{
    if (s->bonds.n_terms > ZSTATS_TERMS) return false;
    for (uint32_t i = s->bonds.n_terms; i < ZSTATS_TERMS; i++)
        if (s->bonds.k[i] || s->bonds.term[i].count || s->bonds.term[i].sum) return false;
    family_t f;
    family(s, &f);
    if (s->suppressed & ~f.have & ~ZSTATS_SUP_ZENNY) return false;
    uint32_t nsup = 0;
    uint64_t pub = 0, sum;
    for (uint32_t i = 0; i < f.n; i++) {
        const zstats_group_t *g = f.g[i];
        if (s->suppressed & f.bit[i]) {
            if (g->count || g->sum) return false;
            nsup++;
        } else if (g->count < s->min_cohort) {
            return false;
        }
        if (!pay_add_ok(pub, g->sum, &pub)) return false;
    }
    if (f.n >= 2 && nsup == 1) return false;
    if (!has(s, ZSTATS_SEC_COMPRESSION)) return true;
    return pay_add_ok(s->comp.vbills_locked, s->comp.vbills_unlocked, &sum) &&
           sum == s->comp.vbills_outstanding && pub <= s->comp.vbills_locked &&
           (nsup || pub == s->comp.vbills_locked);
}

static bool chk_auction(const zstats_snapshot_t *s)
{
    if (!has(s, ZSTATS_SEC_AUCTION)) return true;
    uint64_t k;
    if (s->auction.floor_den == 0 || s->auction.backing == 0) return false;
    if (__builtin_mul_overflow(s->auction.floor_num, s->auction.backing, &k)) return false;
    if (!s->auction.has_last && (s->auction.last_epoch || s->auction.price || s->auction.qty))
        return false;
    if (!s->auction.reserve_valid && s->auction.reserve) return false;
    bool ok = s->auction.reserve_valid &&
              ge(s->auction.reserve, s->auction.floor_den, k, s->auction.outstanding);
    return s->auction.floor_ok == ok;
}

static bool chk_barrels(const zstats_snapshot_t *s)
{
    const grant_snapshot_t *b = &s->barrels;
    uint64_t sum;
    return pay_add_ok(b->barrels_backed, b->barrels_unbacked, &sum) &&
           sum == b->barrels_outstanding && b->lots_backed <= b->lots &&
           rat_ok(b->rate_num, b->rate_den, b->rate_defined, b->inflow, b->weight);
}

static bool chk_zenny(const zstats_snapshot_t *s)
{
    bool sup = (s->suppressed & ZSTATS_SUP_ZENNY) != 0;
    if (!has(s, ZSTATS_SEC_ZENNY)) return !sup;
    return sup ? (s->zenny.live == 0 && s->zenny.chips == 0) : s->zenny.live >= s->min_cohort;
}

zstats_status_t zstats_check(const zstats_snapshot_t *s)
{
    if (!s) return ZSTATS_ERR_ARG;
    zstats_snapshot_t t = *s;
    if ((t.present & ~ZSTATS_SEC_ALL) || (t.suppressed & ~ZSTATS_SUP_ALL) || t.min_cohort == 0)
        return ZSTATS_ERR_CONSIST;
    for (uint32_t i = 0; i < 8; i++)
        if (!(t.present & (1u << i)) && !sec_zero(SEC[i], &t)) return ZSTATS_ERR_CONSIST;
    bool ok = chk_reserve(&t) && chk_vault(&t) && chk_lock(&t) && chk_auction(&t) &&
              t.allotment.mode <= ZSTATS_ALLOT_FREE && chk_barrels(&t) && chk_zenny(&t);
    return ok ? ZSTATS_OK : ZSTATS_ERR_CONSIST;
}

/* ===== S2 collect ===== */
static zstats_status_t col_reserve(zstats_snapshot_t *s, const zstats_sources_t *src)
{
    if (!src->L || src->reserve_acct == ZSTATS_NONE) return ZSTATS_OK;
    const pay_account_t *a = pay_ledger_account(src->L, src->reserve_acct);
    if (!a || !pay_ledger_asset(src->L, src->claims_asset) || !src->req_den) return ZSTATS_ERR_ARG;
    uint64_t d, c;
    int64_t e;
    pay_ledger_totals(src->L, src->claims_asset, PAY_CAP_FINANCIAL, &d, &c, &e);
    s->present |= ZSTATS_SEC_RESERVE;
    s->reserve.reserve = a->debit;
    s->reserve.claims = c;
    s->reserve.floor = mkrat(a->debit, c);
    s->reserve.req_num = src->req_num;
    s->reserve.req_den = src->req_den;
    s->reserve.floor_ok = ge(a->debit, src->req_den, c, src->req_num);
    return ZSTATS_OK;
}

static zstats_status_t col_vault(zstats_snapshot_t *s, const zstats_sources_t *src)
{
    uint64_t n, h;
    if (!src->L || !svault_balances(src->vault, src->L, &n, &h, 0, 0)) return ZSTATS_OK;
    s->present |= ZSTATS_SEC_VAULT; /* h > n fails zstats_check: ZSTATS_ERR_STATE */
    s->vault.held = h;
    s->vault.outstanding = n;
    s->vault.share = mkrat(h, n);
    return ZSTATS_OK;
}

static zstats_status_t add(zstats_group_t *g, uint64_t v)
{
    g->count++;
    return pay_add_ok(g->sum, v, &g->sum) ? ZSTATS_OK : ZSTATS_ERR_OVERFLOW;
}

/* BONDS first: COMPRESSION counts their V-Bills as locked */
static zstats_status_t col_bonds(zstats_snapshot_t *s, const zstats_sources_t *src, uint64_t *total)
{
    const vbond_t *b = src->bond;
    *total = 0;
    if (!b) return ZSTATS_OK;
    if (b->cfg.n_terms > ZSTATS_TERMS) return ZSTATS_ERR_STATE;
    s->present |= ZSTATS_SEC_BONDS;
    s->bonds.n_terms = b->cfg.n_terms;
    for (uint32_t i = 0; i < b->cfg.n_terms; i++) s->bonds.k[i] = b->cfg.k[i];
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
        const vbond_bond_t *x = &b->bond[i];
        if (!x->used) continue;
        if (x->term >= b->cfg.n_terms) return ZSTATS_ERR_STATE;
        if (add(&s->bonds.term[x->term], x->qty) != ZSTATS_OK || !pay_add_ok(*total, x->qty, total))
            return ZSTATS_ERR_OVERFLOW;
    }
    return ZSTATS_OK;
}

static zstats_status_t col_comp(zstats_snapshot_t *s, const zstats_sources_t *src, uint64_t bonded)
{
    const vswap_t *w = src->swap;
    if (!src->L || !w) return ZSTATS_OK;
    const pay_account_t *iss = pay_ledger_account(src->L, w->cfg.issuer);
    const pay_account_t *rsv = pay_ledger_account(src->L, w->cfg.reserve);
    if (!iss || !rsv) return ZSTATS_ERR_STATE;
    uint64_t d, c, locked = bonded;
    int64_t e;
    pay_ledger_totals(src->L, w->cfg.vino, PAY_CAP_FINANCIAL, &d, &c, &e);
    if (!pay_sub_ok(c, rsv->debit, &s->comp.circulating_vino)) return ZSTATS_ERR_STATE;
    for (uint32_t i = 0; i < VSWAP_MAX_POS; i++) {
        const vswap_pos_t *p = &w->pos[i];
        if (!p->used || p->tier < VSWAP_TIER_MEDIUM || p->tier > VSWAP_TIER_DEEP) continue;
        if (add(&s->comp.tier[p->tier - VSWAP_TIER_MEDIUM], p->vbills) != ZSTATS_OK ||
            !pay_add_ok(locked, p->vbills, &locked))
            return ZSTATS_ERR_OVERFLOW;
    }
    s->present |= ZSTATS_SEC_COMPRESSION;
    s->comp.vbills_outstanding = iss->credit;
    s->comp.vbills_locked = locked;
    if (!pay_sub_ok(iss->credit, locked, &s->comp.vbills_unlocked)) return ZSTATS_ERR_STATE;
    return ZSTATS_OK;
}

static void col_auction(zstats_snapshot_t *s, const vauc_t *a, uint64_t tick)
{
    if (!a) return;
    const vauc_record_t *last = 0;
    for (uint32_t i = 0; i < VAUC_HISTORY; i++)
        if (a->hist[i].epoch > (last ? last->epoch : 0)) last = &a->hist[i];
    s->present |= ZSTATS_SEC_AUCTION;
    if (last) {
        s->auction.has_last = true;
        s->auction.last_epoch = last->epoch;
        s->auction.price = last->price;
        s->auction.qty = last->qty;
    }
    uint64_t R;
    s->auction.reserve_valid = vauc_reserve(a, tick, &R) == VAUC_OK;
    s->auction.reserve = s->auction.reserve_valid ? R : 0;
    s->auction.outstanding = vauc_outstanding(a);
    s->auction.floor_num = a->cfg.floor_num;
    s->auction.floor_den = a->cfg.floor_den;
    s->auction.backing = a->cfg.backing;
    s->auction.floor_ok = vauc_floor_ok(a, tick);
}

static zstats_status_t col_zenny(zstats_snapshot_t *s, const zenny_t *z)
{
    if (!z) return ZSTATS_OK;
    s->present |= ZSTATS_SEC_ZENNY;
    for (uint32_t i = 0; i < ZENNY_MAX_CHIPS; i++) {
        if (!z->chip[i].used) continue;
        s->zenny.live++; /* a used chip always has its mint version */
        if (!pay_add_ok(s->zenny.chips, zenny_current(z, i)->chips, &s->zenny.chips))
            return ZSTATS_ERR_OVERFLOW;
    }
    return ZSTATS_OK;
}

/* S3 */
static void suppress(zstats_snapshot_t *s)
{
    family_t f;
    family(s, &f);
    uint32_t nsup = 0;
    for (uint32_t i = 0; i < f.n; i++)
        if (f.g[i]->count < s->min_cohort) {
            s->suppressed |= f.bit[i];
            nsup++;
        }
    if (f.n >= 2 && nsup == 1) {
        uint32_t j = f.n;
        for (uint32_t i = 0; i < f.n; i++)
            if (!(s->suppressed & f.bit[i]) && (j == f.n || f.g[i]->count < f.g[j]->count)) j = i;
        s->suppressed |= f.bit[j];
    }
    for (uint32_t i = 0; i < f.n; i++)
        if (s->suppressed & f.bit[i]) {
            f.g[i]->count = 0;
            f.g[i]->sum = 0;
        }
    if (has(s, ZSTATS_SEC_ZENNY) && s->zenny.live < s->min_cohort) {
        s->suppressed |= ZSTATS_SUP_ZENNY;
        s->zenny.live = 0;
        s->zenny.chips = 0;
    }
}

static zstats_status_t collect(zstats_snapshot_t *s, uint64_t tick, const zstats_sources_t *src)
{
    uint64_t bonded;
    zstats_status_t st = col_reserve(s, src);
    if (st == ZSTATS_OK) st = col_vault(s, src);
    if (st == ZSTATS_OK) st = col_bonds(s, src, &bonded);
    if (st == ZSTATS_OK) st = col_comp(s, src, bonded);
    if (st == ZSTATS_OK) st = col_zenny(s, src->zenny);
    if (st != ZSTATS_OK) return st;
    col_auction(s, src->auction, tick);
    if (src->allotment) {
        s->present |= ZSTATS_SEC_ALLOT;
        s->allotment = *src->allotment;
    }
    if (src->grants) {
        s->present |= ZSTATS_SEC_BARRELS;
        grant_snapshot(src->grants, tick, &s->barrels);
    }
    suppress(s);
    return zstats_check(s) == ZSTATS_OK ? ZSTATS_OK : ZSTATS_ERR_STATE;
}

zstats_status_t zstats_collect(zstats_snapshot_t *out, uint64_t epoch, uint64_t tick,
                               const zstats_sources_t *src)
{
    if (!out) return ZSTATS_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    if (!src || !src->min_cohort) return ZSTATS_ERR_ARG;
    out->epoch = epoch;
    out->tick = tick;
    out->juris = src->juris;
    out->publisher = src->publisher;
    out->min_cohort = src->min_cohort;
    zstats_status_t st = collect(out, tick, src);
    if (st != ZSTATS_OK) pay_memset(out, 0, sizeof *out);
    return st;
}

/* ===== S5 ===== */
void zstats_sign_msg(const uint8_t digest[32], uint8_t out[ZSTATS_SIG_MSG])
{
    pay_memcpy(out, ZSTATS_SIG_TAG, 16);
    pay_memcpy(out + 16, digest, 32);
}

void zstats_chain_init(zstats_chain_t *c, uint32_t juris, uint32_t publisher)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->juris = juris;
    c->publisher = publisher;
}

zstats_status_t zstats_verify(zstats_chain_t *c, const uint8_t *enc, uint32_t len,
                              const uint8_t *sig, uint32_t sig_len, zstats_verify_fn verify,
                              void *vctx, zstats_snapshot_t *snap)
{
    if (!c || !sig || !verify) return ZSTATS_ERR_ARG;
    zstats_snapshot_t t;
    uint8_t prev[32], d[32], m[ZSTATS_SIG_MSG];
    zstats_status_t st = zstats_decode(enc, len, &t, prev);
    if (st != ZSTATS_OK) return st;
    if (t.juris != c->juris || t.publisher != c->publisher) return ZSTATS_ERR_ARG;
    sha256(enc, len, d);
    zstats_sign_msg(d, m);
    if (!verify(vctx, c->publisher, m, sizeof m, sig, sig_len)) return ZSTATS_ERR_SIG;
    if (!pay_memeq(prev, c->head, 32)) return ZSTATS_ERR_CHAIN;
    if (c->started && t.epoch <= c->epoch) return ZSTATS_ERR_EPOCH;
    if (zstats_check(&t) != ZSTATS_OK) return ZSTATS_ERR_CONSIST;
    pay_memcpy(c->head, d, 32);
    c->epoch = t.epoch;
    c->started = true;
    if (snap) *snap = t;
    return ZSTATS_OK;
}

/* ===== S6 JSON ===== */
typedef struct {
    char *b;
    uint32_t cap, n;
    bool over;
} jw_t;

static void jc(jw_t *w, char ch)
{
    if (w->n >= w->cap) {
        w->over = true;
        return;
    }
    w->b[w->n++] = ch;
}
static void js(jw_t *w, const char *s)
{
    while (*s) jc(w, *s++);
}
/* a u64 as a quoted decimal string */
static void jq(jw_t *w, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        uint64_t r;
        v = pay_udiv64(v, 10, &r);
        d[n++] = (char) ('0' + r);
    } while (v);
    jc(w, '"');
    while (n) jc(w, d[--n]);
    jc(w, '"');
}
static void ku(jw_t *w, const char *key, uint64_t v)
{
    js(w, key);
    jq(w, v);
}
static void kb(jw_t *w, const char *key, bool v)
{
    js(w, key);
    js(w, v ? "true" : "false");
}
static void jpair(jw_t *w, uint64_t num, uint64_t den)
{
    ku(w, "{\"num\":", num);
    ku(w, ",\"den\":", den);
    jc(w, '}');
}
static void jrat(jw_t *w, const char *key, uint64_t num, uint64_t den, bool defined)
{
    js(w, key);
    if (defined)
        jpair(w, num, den);
    else
        js(w, "null");
}
/* the key, then null and false for an absent section */
static bool sect(jw_t *w, const zstats_snapshot_t *s, const char *key, uint32_t sec)
{
    js(w, key);
    if (has(s, sec)) return true;
    js(w, "null");
    return false;
}

static void j_reserve(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"reserve\":", ZSTATS_SEC_RESERVE)) return;
    ku(w, "{\"reserve_vino\":", s->reserve.reserve);
    ku(w, ",\"claims_vino\":", s->reserve.claims);
    jrat(w, ",\"floor\":", s->reserve.floor.num, s->reserve.floor.den, s->reserve.floor.defined);
    js(w, ",\"required\":");
    jpair(w, s->reserve.req_num, s->reserve.req_den);
    kb(w, ",\"floor_ok\":", s->reserve.floor_ok);
    jc(w, '}');
}
static void j_vault(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"vault\":", ZSTATS_SEC_VAULT)) return;
    ku(w, "{\"vbills_held\":", s->vault.held);
    ku(w, ",\"vbills_outstanding\":", s->vault.outstanding);
    jrat(w, ",\"share\":", s->vault.share.num, s->vault.share.den, s->vault.share.defined);
    jc(w, '}');
}
static void j_group(jw_t *w, const char *count_key, const zstats_group_t *g, bool sup)
{
    ku(w, count_key, g->count);
    ku(w, ",\"vbills\":", g->sum);
    kb(w, ",\"suppressed\":", sup);
    jc(w, '}');
}
static void j_comp(jw_t *w, const zstats_snapshot_t *s)
{
    static const char *const name[ZSTATS_TIERS] = {"{\"tier\":\"medium\"", "{\"tier\":\"deep\""};
    if (!sect(w, s, ",\"compression\":", ZSTATS_SEC_COMPRESSION)) return;
    ku(w, "{\"circulating_vino\":", s->comp.circulating_vino);
    ku(w, ",\"vbills_outstanding\":", s->comp.vbills_outstanding);
    ku(w, ",\"vbills_unlocked\":", s->comp.vbills_unlocked);
    ku(w, ",\"vbills_locked\":", s->comp.vbills_locked);
    js(w, ",\"tiers\":[");
    for (uint32_t i = 0; i < ZSTATS_TIERS; i++) {
        if (i) jc(w, ',');
        js(w, name[i]);
        j_group(w, ",\"positions\":", &s->comp.tier[i], (s->suppressed & ZSTATS_SUP_TIER(i)) != 0);
    }
    js(w, "]}");
}
static void j_bonds(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"bonds\":", ZSTATS_SEC_BONDS)) return;
    js(w, "{\"terms\":[");
    for (uint32_t i = 0; i < s->bonds.n_terms && i < ZSTATS_TERMS; i++) {
        if (i) jc(w, ',');
        ku(w, "{\"k\":", s->bonds.k[i]);
        j_group(w, ",\"bonds\":", &s->bonds.term[i], (s->suppressed & ZSTATS_SUP_TERM(i)) != 0);
    }
    js(w, "]}");
}
static void j_auction(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"auction\":", ZSTATS_SEC_AUCTION)) return;
    js(w, "{\"last\":");
    if (s->auction.has_last) {
        ku(w, "{\"epoch\":", s->auction.last_epoch);
        ku(w, ",\"price\":", s->auction.price);
        ku(w, ",\"qty\":", s->auction.qty);
        jc(w, '}');
    } else {
        js(w, "null");
    }
    ku(w, ",\"vbills_outstanding\":", s->auction.outstanding);
    js(w, ",\"reserve\":");
    if (s->auction.reserve_valid)
        jq(w, s->auction.reserve);
    else
        js(w, "null");
    js(w, ",\"floor\":");
    jpair(w, s->auction.floor_num, s->auction.floor_den);
    ku(w, ",\"backing\":", s->auction.backing);
    kb(w, ",\"floor_ok\":", s->auction.floor_ok);
    jc(w, '}');
}
static void j_allot(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"allotment\":", ZSTATS_SEC_ALLOT)) return;
    ku(w, "{\"epoch\":", s->allotment.epoch);
    ku(w, ",\"pot\":", s->allotment.pot);
    ku(w, ",\"ballots\":", s->allotment.ballots);
    ku(w, ",\"threshold\":", s->allotment.threshold);
    ku(w, ",\"entries\":", s->allotment.entries);
    js(w, s->allotment.mode == ZSTATS_ALLOT_PAID   ? ",\"mode\":\"paid\"}"
          : s->allotment.mode == ZSTATS_ALLOT_FREE ? ",\"mode\":\"free\"}"
                                                   : ",\"mode\":\"savings\"}");
}
static void j_barrels(jw_t *w, const zstats_snapshot_t *s)
{
    const grant_snapshot_t *b = &s->barrels;
    if (!sect(w, s, ",\"barrels\":", ZSTATS_SEC_BARRELS)) return;
    ku(w, "{\"epoch\":", b->epoch);
    ku(w, ",\"inflow\":", b->inflow);
    ku(w, ",\"weight\":", b->weight);
    jrat(w, ",\"g_rate\":", b->rate_num, b->rate_den, b->rate_defined);
    ku(w, ",\"granted_barrels\":", b->granted_barrels);
    ku(w, ",\"granted_cost\":", b->granted_cost);
    ku(w, ",\"total_barrels_granted\":", b->total_barrels_granted);
    ku(w, ",\"pool_balance\":", b->pool_balance);
    ku(w, ",\"reserve_balance\":", b->reserve_balance);
    ku(w, ",\"barrels_outstanding\":", b->barrels_outstanding);
    ku(w, ",\"barrels_backed\":", b->barrels_backed);
    ku(w, ",\"barrels_unbacked\":", b->barrels_unbacked);
    ku(w, ",\"lots\":", b->lots);
    ku(w, ",\"lots_backed\":", b->lots_backed);
    jc(w, '}');
}
static void j_zenny(jw_t *w, const zstats_snapshot_t *s)
{
    if (!sect(w, s, ",\"zenny\":", ZSTATS_SEC_ZENNY)) return;
    ku(w, "{\"live_chips\":", s->zenny.live);
    ku(w, ",\"chips_outstanding\":", s->zenny.chips);
    kb(w, ",\"suppressed\":", (s->suppressed & ZSTATS_SUP_ZENNY) != 0);
    jc(w, '}');
}

zstats_status_t zstats_json(const zstats_snapshot_t *s, char *buf, uint32_t cap, uint32_t *len)
{
    if (!s || !buf || !len) return ZSTATS_ERR_ARG;
    jw_t w = {buf, cap, 0, false};
    js(&w, "{\"format\":\"" ZSTATS_TAG "\"");
    ku(&w, ",\"epoch\":", s->epoch);
    ku(&w, ",\"tick\":", s->tick);
    ku(&w, ",\"juris\":", s->juris);
    ku(&w, ",\"publisher\":", s->publisher);
    ku(&w, ",\"min_cohort\":", s->min_cohort);
    j_reserve(&w, s);
    j_vault(&w, s);
    j_comp(&w, s);
    j_bonds(&w, s);
    j_auction(&w, s);
    j_allot(&w, s);
    j_barrels(&w, s);
    j_zenny(&w, s);
    jc(&w, '}');
    *len = w.over ? 0 : w.n;
    return w.over ? ZSTATS_ERR_SPACE : ZSTATS_OK;
}
