/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_conform.c — the pinned safety-core conformance suite.
 *
 * Every check is a RULE, not a copy of someone's implementation: the tithe is
 * checked by exact squared inequalities (no square root needed), the ledger
 * by the L1 balance rule and the equity derivation, usury by "due ==
 * principal", consent by the gate formula, crypto by FIPS 202 vectors and by
 * this build's own SHA3-256. A fork may implement any of them however it
 * likes; it conforms iff its answers obey the rules. */
#include "evo_util.h"
#include "keccak.h"

/* ===== 192-bit unsigned arithmetic in six 32-bit limbs ===== */
typedef struct {
    uint32_t w[6]; /* w[0] least significant */
} big_t;

static void big_set(big_t *x, uint64_t v)
{
    evo_zero(x, sizeof(*x));
    x->w[0] = (uint32_t) v;
    x->w[1] = (uint32_t) (v >> 32);
}

static void big_mul_small(big_t *x, uint32_t k)
{
    uint64_t c = 0;
    for (uint32_t i = 0; i < 6; i++) {
        uint64_t p = (uint64_t) x->w[i] * k + c;
        x->w[i] = (uint32_t) p;
        c = p >> 32;
    }
}

static void big_add_small(big_t *x, uint32_t k)
{
    uint64_t c = k;
    for (uint32_t i = 0; i < 6 && c; i++) {
        uint64_t s = (uint64_t) x->w[i] + c;
        x->w[i] = (uint32_t) s;
        c = s >> 32;
    }
}

static int big_cmp(const big_t *a, const big_t *b)
{
    for (int i = 5; i >= 0; i--)
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* a -= b, requires a >= b */
static void big_sub(big_t *a, const big_t *b)
{
    int64_t br = 0;
    for (uint32_t i = 0; i < 6; i++) {
        int64_t d = (int64_t) a->w[i] - (int64_t) b->w[i] - br;
        br = d < 0 ? 1 : 0;
        a->w[i] = (uint32_t) (d + (br ? ((int64_t) 1 << 32) : 0));
    }
}

/* out = a * b; a and b below 2^96 (low three limbs). */
static void big_mul(const big_t *a, const big_t *b, big_t *out)
{
    uint32_t r[6] = {0};
    for (uint32_t i = 0; i < 3; i++) {
        uint64_t c = 0;
        for (uint32_t j = 0; j < 3; j++) {
            uint64_t p = (uint64_t) a->w[i] * b->w[j] + r[i + j] + c;
            r[i + j] = (uint32_t) p;
            c = p >> 32;
        }
        for (uint32_t k = i + 3; k < 6 && c; k++) {
            uint64_t s = (uint64_t) r[k] + c;
            r[k] = (uint32_t) s;
            c = s >> 32;
        }
    }
    for (uint32_t i = 0; i < 6; i++) out->w[i] = r[i];
}

bool evo_tithe_is_exact(uint64_t a, uint64_t t)
{
    big_t A, T, F, X;
    big_set(&A, a);
    big_mul(&A, &A, &F);
    big_mul_small(&F, 5); /* 5a^2 < 2^131 */
    big_set(&T, t);
    big_mul_small(&T, 200);     /* 200t < 2^72 */
    if (big_cmp(&T, &A) >= 0) { /* lower bound: (200t - a)^2 <= 5a^2 */
        X = T;
        big_sub(&X, &A);
        big_t X2;
        big_mul(&X, &X, &X2);
        if (big_cmp(&X2, &F) > 0) return false;
    }
    big_t Y = T;
    big_add_small(&Y, 200);
    if (big_cmp(&Y, &A) <= 0) return false; /* 200(t+1) - a must be > a*sqrt5 >= 0 */
    big_sub(&Y, &A);
    big_t Y2;
    big_mul(&Y, &Y, &Y2);
    return big_cmp(&Y2, &F) > 0;
}

/* ===== The rules ===== */
#define LINES_MAX 8u

static bool rule_posting(const evo_line_t *l, uint32_t n)
{
    if (n == 0 || n > LINES_MAX) return false;
    int64_t sd = 0, sc = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (l[i].d_equity != l[i].d_debit - l[i].d_credit) return false;
        sd += l[i].d_debit;
        sc += l[i].d_credit;
    }
    return sd == sc;
}

static bool rule_spend(const evo_consent_t *c, uint64_t amount, uint64_t now)
{
    return c->granted && amount <= c->max_amount && now < c->expires_at;
}

/* ===== Pinned vectors ===== */
static const uint64_t pin_tithe[] = {0,
                                     1,
                                     2,
                                     3,
                                     5,
                                     7,
                                     61,
                                     62,
                                     123,
                                     124,
                                     161,
                                     199,
                                     200,
                                     201,
                                     1000,
                                     10000,
                                     123456789,
                                     4294967296ull,
                                     9007199254740993ull,
                                     9223372036854775808ull,
                                     18446744073709551615ull};

static const evo_line_t pin_lines[][3] = {
    {{100, 0, 100}, {-100, 0, -100}, {0, 0, 0}}, /* transfer */
    {{50, 0, 50}, {0, 50, -50}, {0, 0, 0}},      /* issue */
    {{50, 0, 50}, {0, 0, 0}, {0, 0, 0}},         /* unbalanced */
    {{100, 0, 99}, {-100, 0, -100}, {0, 0, 0}},  /* equity not derived */
    {{7, 3, 4}, {-7, 0, -7}, {0, -3, 3}},        /* redemption */
};
static const uint8_t pin_lines_n[] = {2, 2, 1, 2, 3};

static const uint64_t pin_usury[][2] = {
    {1000, 0}, {1000, 30}, {1000, 365}, {1, 36500}, {1099511627776ull, 10000}};

typedef struct {
    evo_consent_t c;
    uint64_t amount, now;
} consent_case_t;

static const consent_case_t pin_consent[] = {
    {{true, 500, 100}, 400, 50}, {{true, 500, 100}, 500, 99}, {{true, 500, 100}, 501, 50},
    {{true, 500, 100}, 10, 100}, {{false, 500, 100}, 10, 1},  {{true, 0, 1}, 0, 0},
};

static const uint8_t kat_empty[32] = {
    0xa7, 0xff, 0xc6, 0xf8, 0xbf, 0x1e, 0xd7, 0x66, 0x51, 0xc1, 0x47, 0x56, 0xa0, 0x61, 0xd6, 0x62,
    0xf5, 0x80, 0xff, 0x4d, 0xe4, 0x3b, 0x49, 0xfa, 0x82, 0xd8, 0x0a, 0x4b, 0x80, 0xf8, 0x43, 0x4a};
static const uint8_t kat_abc[32] = {
    0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2, 0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b, 0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32};

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

void evo_conform_suite_cid(evo_cid_t *out)
{
    uint8_t buf[1024];
    evo_w_t w = {buf, sizeof(buf), 0, false};
    evo_w_bytes(&w, "ZXV-EVO-safety-core-v1", 22);
    evo_w_u32(&w, EVO_CHK_ALL);
    evo_w_u16(&w, EVO_RAIL_DEBIT);
    evo_w_u16(&w, EVO_RAIL_CREDIT);
    evo_w_u16(&w, EVO_RAIL_EQUITY);
    for (uint32_t i = 0; i < NELEM(pin_tithe); i++) evo_w_u64(&w, pin_tithe[i]);
    for (uint32_t i = 0; i < NELEM(pin_lines); i++)
        for (uint32_t j = 0; j < pin_lines_n[i]; j++) {
            evo_w_u64(&w, (uint64_t) pin_lines[i][j].d_debit);
            evo_w_u64(&w, (uint64_t) pin_lines[i][j].d_credit);
            evo_w_u64(&w, (uint64_t) pin_lines[i][j].d_equity);
        }
    for (uint32_t i = 0; i < NELEM(pin_usury); i++) {
        evo_w_u64(&w, pin_usury[i][0]);
        evo_w_u64(&w, pin_usury[i][1]);
    }
    for (uint32_t i = 0; i < NELEM(pin_consent); i++) {
        evo_w_u8(&w, pin_consent[i].c.granted ? 1 : 0);
        evo_w_u64(&w, pin_consent[i].c.max_amount);
        evo_w_u64(&w, pin_consent[i].c.expires_at);
        evo_w_u64(&w, pin_consent[i].amount);
        evo_w_u64(&w, pin_consent[i].now);
    }
    evo_w_bytes(&w, kat_empty, 32);
    evo_w_bytes(&w, kat_abc, 32);
    evo_w_u8(&w, EVO_CHAL_TITHE);
    evo_w_u8(&w, EVO_CHAL_POST);
    evo_w_u8(&w, EVO_CHAL_USURY);
    evo_w_u8(&w, EVO_CHAL_CONSENT);
    evo_cid_of(buf, w.len, out);
}

uint32_t evo_conform_local(const evo_core_impl_t *im)
{
    uint32_t ok = 0;
    if (!im) return 0;
    if (im->hash) {
        uint8_t h[32];
        im->hash((const uint8_t *) "", 0, h);
        bool good = evo_cmp(h, kat_empty, 32) == 0;
        im->hash((const uint8_t *) "abc", 3, h);
        if (good && evo_cmp(h, kat_abc, 32) == 0) ok |= EVO_CHK_HASH;
    }
    if (im->tithe) {
        bool good = true;
        for (uint32_t i = 0; i < NELEM(pin_tithe) && good; i++)
            good = evo_tithe_is_exact(pin_tithe[i], im->tithe(pin_tithe[i]));
        if (good) ok |= EVO_CHK_TITHE;
    }
    if (im->posting_ok) {
        bool good = !im->posting_ok(pin_lines[0], 0);
        for (uint32_t i = 0; i < NELEM(pin_lines) && good; i++)
            good = im->posting_ok(pin_lines[i], pin_lines_n[i]) ==
                   rule_posting(pin_lines[i], pin_lines_n[i]);
        if (good) ok |= EVO_CHK_LEDGER;
    }
    if (im->repay_due) {
        bool good = true;
        for (uint32_t i = 0; i < NELEM(pin_usury) && good; i++)
            good = im->repay_due(pin_usury[i][0], (uint32_t) pin_usury[i][1]) == pin_usury[i][0];
        if (good) ok |= EVO_CHK_USURY;
    }
    if (im->may_spend) {
        bool good = true;
        for (uint32_t i = 0; i < NELEM(pin_consent) && good; i++) {
            const consent_case_t *k = &pin_consent[i];
            good = im->may_spend(&k->c, k->amount, k->now) == rule_spend(&k->c, k->amount, k->now);
        }
        if (good) ok |= EVO_CHK_CONSENT;
    }
    return ok;
}

/* ===== Challenges: inputs expanded from the verifier's seed ===== */
typedef struct {
    uint8_t msg[EVO_CHAL_MSG];
    uint64_t tithe[EVO_CHAL_TITHE];
    uint8_t n_lines[EVO_CHAL_POST];
    evo_line_t lines[EVO_CHAL_POST][EVO_CHAL_LINES];
    uint64_t principal[EVO_CHAL_USURY];
    uint32_t days[EVO_CHAL_USURY];
    consent_case_t consent[EVO_CHAL_CONSENT];
} chal_in_t;

typedef struct {
    uint8_t s[1536];
    uint32_t pos;
} stream_t;

static uint64_t take(stream_t *st, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n && st->pos < sizeof(st->s); i++)
        v |= (uint64_t) st->s[st->pos++] << (8 * i);
    return v;
}

static int64_t small_signed(stream_t *st)
{
    return (int64_t) (take(st, 5) & 0xffffffffffull) - ((int64_t) 1 << 39);
}

static void expand(const uint8_t seed[32], chal_in_t *ci)
{
    static stream_t st; /* 1.5 KB: off the stack */
    uint8_t in[32 + 13];
    evo_cpy(in, seed, 32);
    evo_cpy(in + 32, "ZXV-EVO-chal1", 13);
    shake256(in, sizeof(in), st.s, sizeof(st.s));
    st.pos = 0;
    evo_zero(ci, sizeof(*ci));
    for (uint32_t i = 0; i < EVO_CHAL_MSG; i++) ci->msg[i] = (uint8_t) take(&st, 1);
    for (uint32_t i = 0; i < EVO_CHAL_TITHE; i++) {
        uint64_t sel = take(&st, 1), v = take(&st, 8);
        if ((sel & 3) == 0)
            v &= 0xffff; /* small: rounding edges */
        else if ((sel & 3) == 1)
            v &= 0xffffffffu; /* 32-bit */
        ci->tithe[i] = v;
    }
    for (uint32_t p = 0; p < EVO_CHAL_POST; p++) {
        uint32_t sel = (uint32_t) take(&st, 1);
        uint32_t n = 1u + (sel & 7u) % EVO_CHAL_LINES;
        ci->n_lines[p] = (uint8_t) n;
        int64_t sd = 0, sc = 0;
        for (uint32_t i = 0; i < n; i++) {
            evo_line_t *l = &ci->lines[p][i];
            l->d_debit = small_signed(&st);
            l->d_credit = small_signed(&st);
            if (i + 1 == n && (sel & 0x10) && n > 1) /* balance it */
                l->d_credit = sd + l->d_debit - sc;
            sd += l->d_debit;
            sc += l->d_credit;
            l->d_equity = l->d_debit - l->d_credit;
        }
        if ((sel & 0x60) == 0x60) ci->lines[p][0].d_equity += 1; /* corrupt */
    }
    for (uint32_t i = 0; i < EVO_CHAL_USURY; i++) {
        ci->principal[i] = take(&st, 6);
        ci->days[i] = (uint32_t) take(&st, 2);
    }
    for (uint32_t i = 0; i < EVO_CHAL_CONSENT; i++) {
        consent_case_t *k = &ci->consent[i];
        uint32_t sel = (uint32_t) take(&st, 1);
        k->c.granted = (sel & 7) != 0;
        k->c.max_amount = take(&st, 4);
        k->c.expires_at = take(&st, 4);
        uint64_t da = take(&st, 1), dn = take(&st, 1);
        k->amount = (sel & 8) ? k->c.max_amount + (da & 3) : (k->c.max_amount >> 1);
        k->now = (sel & 16) ? k->c.expires_at - (dn & 1) : k->c.expires_at + (dn & 1);
    }
}

void evo_conform_respond(const evo_core_impl_t *im, const evo_challenge_t *c, evo_response_t *r)
{
    static chal_in_t ci;
    evo_zero(r, sizeof(*r));
    evo_conform_suite_cid(&r->suite);
    evo_cpy(r->seed, c->seed, 32);
    expand(c->seed, &ci);
    if (im->hash) im->hash(ci.msg, EVO_CHAL_MSG, r->hash);
    for (uint32_t i = 0; i < EVO_CHAL_TITHE; i++)
        r->tithe[i] = im->tithe ? im->tithe(ci.tithe[i]) : 0;
    for (uint32_t p = 0; p < EVO_CHAL_POST; p++)
        if (im->posting_ok && im->posting_ok(ci.lines[p], ci.n_lines[p])) r->post_ok |= 1u << p;
    for (uint32_t i = 0; i < EVO_CHAL_USURY; i++)
        r->repay[i] = im->repay_due ? im->repay_due(ci.principal[i], ci.days[i]) : 0;
    for (uint32_t i = 0; i < EVO_CHAL_CONSENT; i++) {
        const consent_case_t *k = &ci.consent[i];
        if (im->may_spend && im->may_spend(&k->c, k->amount, k->now)) r->spend_ok |= 1u << i;
    }
}

uint32_t evo_conform_check(const evo_challenge_t *c, const evo_response_t *r)
{
    static chal_in_t ci;
    evo_cid_t suite;
    evo_conform_suite_cid(&suite);
    if (!evo_cid_eq(&suite, &r->suite) || evo_cmp(c->seed, r->seed, 32) != 0) return 0;
    expand(c->seed, &ci);
    uint32_t ok = 0;
    uint8_t h[32];
    sha3_256(ci.msg, EVO_CHAL_MSG, h);
    if (evo_cmp(h, r->hash, 32) == 0) ok |= EVO_CHK_HASH;
    bool good = true;
    for (uint32_t i = 0; i < EVO_CHAL_TITHE && good; i++)
        good = evo_tithe_is_exact(ci.tithe[i], r->tithe[i]);
    if (good) ok |= EVO_CHK_TITHE;
    good = true;
    for (uint32_t p = 0; p < EVO_CHAL_POST && good; p++)
        good = (((r->post_ok >> p) & 1u) != 0) == rule_posting(ci.lines[p], ci.n_lines[p]);
    if (good) ok |= EVO_CHK_LEDGER;
    good = true;
    for (uint32_t i = 0; i < EVO_CHAL_USURY && good; i++) good = r->repay[i] == ci.principal[i];
    if (good) ok |= EVO_CHK_USURY;
    good = true;
    for (uint32_t i = 0; i < EVO_CHAL_CONSENT && good; i++) {
        const consent_case_t *k = &ci.consent[i];
        good = (((r->spend_ok >> i) & 1u) != 0) == rule_spend(&k->c, k->amount, k->now);
    }
    if (good) ok |= EVO_CHK_CONSENT;
    return ok;
}

#define RESP_MAGIC EVO_MAGIC('E', 'V', 'R', '1')

uint32_t evo_response_encode(const evo_response_t *r, uint8_t *out, uint32_t cap)
{
    if (!r || !out) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, RESP_MAGIC);
    evo_w_bytes(&w, r->suite.b, EVO_CID_LEN);
    evo_w_bytes(&w, r->seed, 32);
    evo_w_bytes(&w, r->hash, 32);
    for (uint32_t i = 0; i < EVO_CHAL_TITHE; i++) evo_w_u64(&w, r->tithe[i]);
    evo_w_u8(&w, r->post_ok);
    for (uint32_t i = 0; i < EVO_CHAL_USURY; i++) evo_w_u64(&w, r->repay[i]);
    evo_w_u8(&w, r->spend_ok);
    return w.err ? 0 : w.len;
}

evo_status_t evo_response_decode(const uint8_t *in, uint32_t len, evo_response_t *r)
{
    if (!in || !r) return EVO_ERR_ARG;
    evo_zero(r, sizeof(*r));
    evo_r_t rd = {in, len, 0, false};
    if (len != EVO_RESP_ENC_LEN || evo_r_le(&rd, 4) != RESP_MAGIC) return EVO_ERR_PARSE;
    evo_r_copy(&rd, r->suite.b, EVO_CID_LEN);
    evo_r_copy(&rd, r->seed, 32);
    evo_r_copy(&rd, r->hash, 32);
    for (uint32_t i = 0; i < EVO_CHAL_TITHE; i++) r->tithe[i] = evo_r_le(&rd, 8);
    r->post_ok = (uint8_t) evo_r_le(&rd, 1);
    for (uint32_t i = 0; i < EVO_CHAL_USURY; i++) r->repay[i] = evo_r_le(&rd, 8);
    r->spend_ok = (uint8_t) evo_r_le(&rd, 1);
    return rd.err || rd.pos != len ? EVO_ERR_PARSE : EVO_OK;
}
