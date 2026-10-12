/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* privacy.c — the privacy layer (privacy.h P1-P5). */
#include "privacy.h"
#include "../robin_debanks/sha256.h"

__extension__ typedef unsigned __int128 u128;

/* A broken source (stuck words) shows up as an endless rejection loop;
 * these caps turn it into PRIV_E_RNG. With a working CSPRNG they are never
 * reached: each bound is a run of events of probability below 2^-60. */
#define UNIFORM_TRIES 64u
#define BERN_EXP_K    64u
#define DLAP_TRIES    4096u
#define DLAP_V        4096u

/* ---- P1 ---- */
static priv_status_t uniform(priv_rng_t *r, uint64_t b, uint64_t *out)
{
    if (!r || !r->next || b == 0) return PRIV_E_ARG;
    if (b == 1) {
        *out = 0;
        return PRIV_OK;
    }
    uint64_t floor_ = (uint64_t) (0 - b) % b; /* 2^64 mod b: reject below it */
    for (unsigned i = 0; i < UNIFORM_TRIES; i++) {
        uint64_t x = r->next(r->ctx);
        if (x >= floor_) {
            *out = x % b;
            return PRIV_OK;
        }
    }
    return PRIV_E_RNG;
}

priv_status_t priv_bern(priv_rng_t *r, uint64_t num, uint64_t den, bool *out)
{
    if (!out || den == 0 || num > den) return PRIV_E_ARG;
    uint64_t u;
    priv_status_t s = uniform(r, den, &u);
    if (s != PRIV_OK) return s;
    *out = u < num;
    return PRIV_OK;
}

/* CKS20 Algorithm 1, gamma = num/den in [0, 1] */
static priv_status_t bern_exp_le1(priv_rng_t *r, uint64_t num, uint64_t den, bool *out)
{
    uint64_t k = 1;
    for (;;) {
        uint64_t dk;
        if (k > BERN_EXP_K || __builtin_mul_overflow(den, k, &dk)) return PRIV_E_RNG;
        bool a;
        priv_status_t s = priv_bern(r, num, dk, &a);
        if (s != PRIV_OK) return s;
        if (!a) break;
        k++;
    }
    *out = (k & 1u) != 0;
    return PRIV_OK;
}

priv_status_t priv_bern_exp(priv_rng_t *r, uint64_t num, uint64_t den, bool *out)
{
    if (!out || den == 0) return PRIV_E_ARG;
    if (num <= den) return bern_exp_le1(r, num, den, out);
    /* exp(-g) = exp(-1)^floor(g) * exp(-(g - floor g)) */
    for (uint64_t i = 0; i < num / den; i++) {
        bool b;
        priv_status_t s = bern_exp_le1(r, 1, 1, &b);
        if (s != PRIV_OK) return s;
        if (!b) {
            *out = false;
            return PRIV_OK;
        }
    }
    return bern_exp_le1(r, num % den, den, out);
}

/* CKS20 Algorithm 2 */
priv_status_t priv_dlap(priv_rng_t *r, uint64_t s, uint64_t t, int64_t *out)
{
    if (!out || s == 0 || t == 0) return PRIV_E_ARG;
    *out = 0;
    for (unsigned tries = 0; tries < DLAP_TRIES; tries++) {
        uint64_t u;
        bool d;
        priv_status_t st = uniform(r, t, &u);
        if (st != PRIV_OK) return st;
        if ((st = priv_bern_exp(r, u, t, &d)) != PRIV_OK) return st;
        if (!d) continue;
        uint64_t v = 0;
        for (;;) {
            bool a;
            if ((st = priv_bern_exp(r, 1, 1, &a)) != PRIV_OK) return st;
            if (!a) break;
            if (++v > DLAP_V) return PRIV_E_RNG;
        }
        uint64_t x;
        if (__builtin_mul_overflow(t, v, &x) || __builtin_add_overflow(x, u, &x))
            return PRIV_E_OVERFLOW;
        uint64_t y = x / s;
        bool b;
        if ((st = priv_bern(r, 1, 2, &b)) != PRIV_OK) return st;
        if (b && y == 0) continue;
        if (y > (uint64_t) INT64_MAX) return PRIV_E_OVERFLOW;
        *out = b ? -(int64_t) y : (int64_t) y;
        return PRIV_OK;
    }
    return PRIV_E_RNG;
}

/* ---- P2 ---- */
#define FRAC_MAX ((uint64_t) 1 << 32)

static uint64_t gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

priv_status_t priv_budget_init(priv_budget_t *b, uint64_t num, uint64_t den)
{
    if (!b) return PRIV_E_ARG;
    b->total_num = b->spent_num = 0;
    b->total_den = b->spent_den = 1;
    b->releases = 0;
    if (den == 0 || num == 0 || num > FRAC_MAX || den > FRAC_MAX) return PRIV_E_ARG;
    b->total_num = num;
    b->total_den = den;
    return PRIV_OK;
}

priv_status_t priv_budget_spend(priv_budget_t *b, uint64_t en, uint64_t ed)
{
    if (!b || en == 0 || ed == 0 || en > FRAC_MAX || ed > FRAC_MAX || b->total_den == 0)
        return PRIV_E_ARG;
    /* new spent = (sn*ed + en*sd) / (sd*ed); every factor <= 2^32 */
    u128 nn = (u128) b->spent_num * ed + (u128) en * b->spent_den;
    u128 nd = (u128) b->spent_den * ed;
    /* nn/nd <= tn/td  <=>  nn*td <= tn*nd  (all below 2^98) */
    if (nn * b->total_den > (u128) b->total_num * nd) return PRIV_E_BUDGET;
    if (nn >> 64 || nd >> 64) return PRIV_E_OVERFLOW; /* nn < 2^65 is possible */
    uint64_t g = gcd((uint64_t) nn, (uint64_t) nd);
    uint64_t sn = (uint64_t) nn / g, sd = (uint64_t) nd / g;
    if (sn > FRAC_MAX || sd > FRAC_MAX) return PRIV_E_OVERFLOW; /* fail closed */
    if (b->releases == UINT32_MAX) return PRIV_E_OVERFLOW;
    b->spent_num = sn;
    b->spent_den = sd;
    b->releases++;
    return PRIV_OK;
}

/* ---- P3 ---- */
priv_status_t priv_release_count(priv_budget_t *b, priv_rng_t *r, int64_t true_value, uint64_t en,
                                 uint64_t ed, uint64_t sensitivity, int64_t *out)
{
    if (!out) return PRIV_E_ARG;
    *out = 0;
    if (!r || sensitivity == 0) return PRIV_E_ARG;
    uint64_t t;
    if (__builtin_mul_overflow(ed, sensitivity, &t)) return PRIV_E_OVERFLOW;
    priv_status_t s = priv_budget_spend(b, en, ed);
    if (s != PRIV_OK) return s;
    int64_t noise;
    if ((s = priv_dlap(r, en, t, &noise)) != PRIV_OK) return s; /* spent stays spent */
    if (__builtin_add_overflow(true_value, noise, out)) {
        *out = 0;
        return PRIV_E_OVERFLOW;
    }
    return PRIV_OK;
}

/* ---- P4 ---- */
static lpx_t tag(uint8_t base)
{
    return lpx_make(base, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static bool match(uint16_t rule, uint16_t v)
{
    return rule == PRIV_ANY || rule == v;
}

lpx_t priv_access(const priv_policy_t *p, const priv_request_t *q)
{
    if (!p || !q || (p->n_rules && !p->rules) || (p->n_consents && !p->consents))
        return tag(LPX_B_NEITHER);
    bool allow = false, deny = false;
    for (uint32_t i = 0; i < p->n_rules; i++) {
        const priv_rule_t *r = &p->rules[i];
        if (!match(r->role, q->role) || !match(r->category, q->category) ||
            !match(r->purpose, q->purpose))
            continue;
        if (r->verdict == PRIV_ALLOW)
            allow = true;
        else
            deny = true; /* anything but ALLOW denies */
    }
    if (allow && deny) return tag(LPX_B_BOTH);
    if (deny) return tag(LPX_B_FALSE);
    if (!allow) return tag(LPX_B_NEITHER);
    if (q->category >= PRIV_MAX_CATEGORIES) return tag(LPX_B_FALSE);
    if (p->special_mask & (1u << q->category)) {
        bool ok = false;
        for (uint32_t i = 0; i < p->n_consents && !ok; i++) {
            const priv_consent_t *c = &p->consents[i];
            ok = c->subject == q->subject && c->purpose == q->purpose && c->explicit_consent &&
                 !c->withdrawn && c->granted <= q->now && (c->expires == 0 || q->now < c->expires);
        }
        if (!ok) return tag(LPX_B_FALSE);
    }
    uint64_t keep = p->retention[q->category];
    if (keep) {
        if (q->record_time > q->now || q->now - q->record_time > keep) return tag(LPX_B_FALSE);
    }
    return tag(LPX_B_TRUE);
}

/* ---- P5 ---- */
static size_t slen(const char *s, size_t cap)
{
    size_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

priv_status_t priv_sd_digest(const priv_claim_t *c, uint8_t out[32])
{
    if (!c || !c->name || !c->value || !out) return PRIV_E_ARG;
    size_t nl = slen(c->name, PRIV_SD_MAX), vl = slen(c->value, PRIV_SD_MAX);
    if (nl == 0 || nl > PRIV_SD_MAX || vl > PRIV_SD_MAX) return PRIV_E_ARG;
    static const uint8_t dom[9] = {'z', 'x', 'v', '/', 's', 'd', '/', 'v', '1'};
    uint8_t l;
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, dom, sizeof dom);
    sha256_update(&h, c->salt, PRIV_SD_SALT);
    l = (uint8_t) nl;
    sha256_update(&h, &l, 1);
    sha256_update(&h, (const uint8_t *) c->name, nl);
    l = (uint8_t) vl;
    sha256_update(&h, &l, 1);
    sha256_update(&h, (const uint8_t *) c->value, vl);
    sha256_final(&h, out);
    return PRIV_OK;
}

#define SD_LIST_MAX 1024u

priv_status_t priv_sd_list_msg(const uint8_t (*digests)[32], uint32_t n, uint8_t out[32])
{
    if (!out || (n && !digests) || n > SD_LIST_MAX) return PRIV_E_ARG;
    static const uint8_t dom[14] = {'z', 'x', 'v', '/', 's', 'd', '-',
                                    'l', 'i', 's', 't', '/', 'v', '1'};
    uint8_t le[4] = {(uint8_t) n, (uint8_t) (n >> 8), (uint8_t) (n >> 16), (uint8_t) (n >> 24)};
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, dom, sizeof dom);
    sha256_update(&h, le, 4);
    for (uint32_t i = 0; i < n; i++) sha256_update(&h, digests[i], 32);
    sha256_final(&h, out);
    return PRIV_OK;
}

priv_status_t priv_sd_check(const uint8_t (*digests)[32], uint32_t n, const uint8_t *sig,
                            uint32_t sig_len, priv_verify_fn verify, void *vctx,
                            const priv_claim_t *shown, uint32_t n_shown)
{
    if (!verify || !sig || (n_shown && !shown)) return PRIV_E_ARG;
    uint8_t m[32];
    priv_status_t s = priv_sd_list_msg(digests, n, m);
    if (s != PRIV_OK) return s;
    if (!verify(vctx, m, sig, sig_len)) return PRIV_E_SIG;
    for (uint32_t k = 0; k < n_shown; k++) {
        uint8_t d[32];
        if ((s = priv_sd_digest(&shown[k], d)) != PRIV_OK) return s;
        bool found = false;
        for (uint32_t i = 0; i < n && !found; i++) {
            uint8_t diff = 0;
            for (unsigned j = 0; j < 32; j++) diff |= (uint8_t) (d[j] ^ digests[i][j]);
            found = diff == 0;
        }
        if (!found) return PRIV_E_CLAIM;
    }
    return PRIV_OK;
}
