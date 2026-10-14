/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_vhub.c — conformance test of the ABI 1.5 vhub group,
 * linked against the shared library only (no kernel headers):
 *   - the 40-byte peg message against the documented bytes;
 *   - ML-DSA-87 signed pegs: wrong signer, other numbers, a flipped bit,
 *     stale epochs, zero terms, and a refused peg changing nothing;
 *   - exact swaps fiat A -> V-Bill -> fiat B with known answers, the round
 *     trip, the implied cross rate, and quotes against a brute-force search
 *     computed here over 49 peg pairs;
 *   - the limit rule (per sender, per epoch, exact boundary, inflows not
 *     counted, 17th sender refused), missing rules failing closed, backing;
 *   - A = L + E on every account and DEBIT == CREDIT per asset after every
 *     posting, and a byte-for-byte unchanged ledger after every refusal;
 *   - every error path, capability gating, context binding and poisoning. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_),                 \
              zxv_status_name(want));                                                          \
    } while (0)
#define OK(call) IS(call, ZXV_OK)

enum { A = 0, B = 1, C = 2, NK = 4 };
enum { G_VBL = 100 }; /* asset group of V-Bill accounts in the test's table */

static zxv_ctx *ctx;
static zxv_cap *root, *rd, *post;
static uint8_t pk[NK][2592], sk[NK][4896];

/* ---- the test's own view of one hub ---- */
#define MAXACCT 64
typedef struct {
    zxv_vhub *h;
    uint32_t n;            /* accounts the test knows of */
    int grp[MAXACCT];      /* slot, or G_VBL */
    uint32_t iss[3], pool[3], vfl[3];
    uint64_t epoch[3];
} hub_t;

typedef struct {
    uint64_t d[MAXACCT], c[MAXACCT];
    int64_t e[MAXACCT];
    uint64_t postings;
} snap_t;

static void snap(hub_t *t, snap_t *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t a = 0; a < t->n; a++)
        OK(zxv_vhub_balance(rd, t->h, a, &s->d[a], &s->c[a], &s->e[a]));
    OK(zxv_vhub_check(rd, t->h, &s->postings));
}
static int same(const snap_t *x, const snap_t *y)
{
    return !memcmp(x, y, sizeof *x);
}

/* A = L + E on every account; DEBIT == CREDIT, EQUITY sums to 0 per asset;
 * the account count is exactly what the test opened; the library agrees. */
static void invariant(hub_t *t, const char *when)
{
    int64_t sd[G_VBL + 1], sc[G_VBL + 1], se[G_VBL + 1];
    memset(sd, 0, sizeof sd), memset(sc, 0, sizeof sc), memset(se, 0, sizeof se);
    int bad = 0;
    for (uint32_t a = 0; a < t->n; a++) {
        uint64_t d = 0, c = 0;
        int64_t e = 0;
        OK(zxv_vhub_balance(rd, t->h, a, &d, &c, &e));
        if ((int64_t) d != (int64_t) c + e) bad++;
        sd[t->grp[a]] += (int64_t) d, sc[t->grp[a]] += (int64_t) c, se[t->grp[a]] += e;
    }
    for (int g = 0; g <= G_VBL; g++)
        if (sd[g] != sc[g] || se[g] != 0) bad++;
    CHECK(bad == 0, "A = L + E and per-asset balance %s", when);
    IS(zxv_vhub_balance(rd, t->h, t->n, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    OK(zxv_vhub_check(rd, t->h, NULL));
}

static int64_t eqty(hub_t *t, uint32_t a)
{
    int64_t e = INT64_MIN;
    OK(zxv_vhub_balance(rd, t->h, a, NULL, NULL, &e));
    return e;
}

static int32_t peg_by(hub_t *t, uint8_t slot, uint64_t num, uint64_t den, uint64_t epoch, int key)
{
    uint8_t m[40], sig[4627];
    OK(zxv_vhub_peg_msg(slot, num, den, epoch, m));
    OK(zxv_mldsa87_sign_deterministic(root, sk[key], m, sizeof m, NULL, 0, sig));
    return zxv_vhub_set_peg(post, t->h, slot, num, den, epoch, sig);
}
static void peg(hub_t *t, uint8_t slot, uint64_t num, uint64_t den)
{
    OK(peg_by(t, slot, num, den, ++t->epoch[slot], slot));
}

static uint32_t open_acct(hub_t *t, uint8_t slot, uint32_t owner)
{
    uint32_t a = UINT32_MAX;
    OK(zxv_vhub_open_account(post, t->h, slot, owner, &a));
    CHECK(a == t->n, "accounts are numbered in order (%u, want %u)", a, t->n);
    t->grp[t->n++] = slot;
    return a;
}

static void setup(hub_t *t)
{
    static const uint8_t seed[32] = {1, 2, 3};
    static const char *code[3] = {"ZTA", "ZTB", "ZTC"};
    memset(t, 0, sizeof *t);
    OK(zxv_vhub_create(post, seed, &t->h));
    t->grp[0] = G_VBL;
    t->n = 1;
    for (uint8_t c = 0; c < 3; c++) {
        OK(zxv_vhub_add_ccy(post, t->h, c, code[c], 2, pk[c], 500000, 10000));
        OK(zxv_vhub_ccy_info(rd, t->h, c, &t->iss[c], &t->pool[c], &t->vfl[c], NULL, NULL, NULL));
        CHECK(t->iss[c] == t->n && t->pool[c] == t->n + 1 && t->vfl[c] == t->n + 2,
              "three accounts per currency, in order");
        t->grp[t->n++] = c, t->grp[t->n++] = c, t->grp[t->n++] = G_VBL;
    }
}

static void ready(hub_t *t)
{
    setup(t);
    peg(t, A, 350, 1); /* 350 A cents per V-Bill */
    peg(t, B, 120, 1); /* 120 B cents per V-Bill */
    peg(t, C, 9, 4);   /* 9 C cents per 4 V-Bills */
}

static void test_peg_msg(void)
{
    uint8_t ref[40], m[40];
    memcpy(ref, "zxv/vhub-peg/v1", 15);
    ref[15] = 2;
    for (int i = 0; i < 8; i++) {
        ref[16 + i] = (uint8_t) (UINT64_C(0x0102030405060708) >> (8 * i));
        ref[24 + i] = (uint8_t) (UINT64_C(9) >> (8 * i));
        ref[32 + i] = (uint8_t) (UINT64_C(0xA0B0C0D0E0F01122) >> (8 * i));
    }
    memset(m, 0xEE, sizeof m);
    OK(zxv_vhub_peg_msg(2, UINT64_C(0x0102030405060708), 9, UINT64_C(0xA0B0C0D0E0F01122), m));
    CHECK(!memcmp(m, ref, 40), "the peg message is exactly the documented 40 bytes");
    IS(zxv_vhub_peg_msg(0, 1, 1, 1, NULL), ZXV_E_ARG);
}

static void test_create_and_ccy(void)
{
    zxv_vhub *h = (zxv_vhub *) 1;
    IS(zxv_vhub_create(post, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_create(NULL, NULL, &h), ZXV_E_ARG);
    CHECK(h == NULL, "out is cleared on failure");
    IS(zxv_vhub_create(rd, NULL, &h), ZXV_E_CAP);
    OK(zxv_vhub_create(post, NULL, &h)); /* a random seed */
    hub_t t;
    memset(&t, 0, sizeof t);
    t.h = h;
    t.grp[0] = G_VBL;
    t.n = 1;
    uint64_t d = 9, c = 9, p = 9;
    int64_t e = 9;
    OK(zxv_vhub_balance(rd, h, ZXV_VHUB_VBILL_ISSUER, &d, &c, &e));
    CHECK(d == 0 && c == 0 && e == 0, "the V-Bill issuer starts empty");
    OK(zxv_vhub_check(rd, h, &p));
    CHECK(p == 0, "no postings yet");

    IS(zxv_vhub_add_ccy(post, NULL, 0, "ZTA", 2, pk[0], 0, 0), ZXV_E_ARG);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZTA", 2, NULL, 0, 0), ZXV_E_ARG);
    IS(zxv_vhub_add_ccy(post, h, 8, "ZTA", 2, pk[0], 0, 0), ZXV_E_RANGE);
    IS(zxv_vhub_add_ccy(post, h, 0, NULL, 2, pk[0], 0, 0), ZXV_E_ARG);
    IS(zxv_vhub_add_ccy(post, h, 0, "", 2, pk[0], 0, 0), ZXV_E_ARG);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZT", 2, pk[0], 0, 0), ZXV_E_RANGE);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZTABCDEFGHIJK", 2, pk[0], 0, 0), ZXV_E_TOO_LONG);
    IS(zxv_vhub_add_ccy(post, h, 0, "zta", 2, pk[0], 0, 0), ZXV_E_FORMAT);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZT-A", 2, pk[0], 0, 0), ZXV_E_FORMAT);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZT\xff", 2, pk[0], 0, 0), ZXV_E_UTF8);
    IS(zxv_vhub_add_ccy(post, h, 0, "VBL", 2, pk[0], 0, 0), ZXV_E_DUPLICATE);
    IS(zxv_vhub_add_ccy(post, h, 0, "VFV", 2, pk[0], 0, 0), ZXV_E_DUPLICATE);
    if (zxv_iso4217_by_alpha("NGN", NULL, NULL, NULL) == ZXV_OK)
        IS(zxv_vhub_add_ccy(post, h, 0, "NGN", 2, pk[0], 0, 0), ZXV_E_CURRENCY);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZTA", 5, pk[0], 0, 0), ZXV_E_RANGE);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZTA", 2, pk[0], ZXV_VHUB_AMOUNT_MAX + 1, 0), ZXV_E_RANGE);
    IS(zxv_vhub_add_ccy(post, h, 0, "ZTA", 2, pk[0], 0, ZXV_VHUB_AMOUNT_MAX + 1), ZXV_E_RANGE);
    IS(zxv_vhub_add_ccy(rd, h, 0, "ZTA", 2, pk[0], 0, 0), ZXV_E_CAP);
    IS(zxv_vhub_ccy_info(rd, h, 0, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    OK(zxv_vhub_check(rd, h, &p));
    CHECK(p == 0, "refused registrations post nothing");
    IS(zxv_vhub_balance(rd, h, 1, NULL, NULL, NULL), ZXV_E_NOT_FOUND);

    /* the boundary amounts: one amount up to 2^59 (the ledger's per-line
     * maximum), any balance below 2^62; minor units 0 and 4 */
    IS(zxv_vhub_add_ccy(post, h, 7, "Z9", 0, pk[0], 0, 0), ZXV_E_RANGE);
    OK(zxv_vhub_add_ccy(post, h, 7, "ZTA", 4, pk[0], ZXV_VHUB_AMOUNT_MAX, 0));
    t.grp[1] = 7, t.grp[2] = 7, t.grp[3] = G_VBL, t.n = 4;
    uint32_t iss = 9, pool = 9, vfl = 9;
    uint64_t num = 9, den = 9, ep = 9;
    OK(zxv_vhub_ccy_info(rd, h, 7, &iss, &pool, &vfl, &num, &den, &ep));
    CHECK(iss == 1 && pool == 2 && vfl == 3 && num == 0 && den == 0 && ep == 0,
          "accounts 1,2,3 and an unset peg");
    OK(zxv_vhub_balance(rd, h, pool, &d, &c, &e));
    CHECK(d == ZXV_VHUB_AMOUNT_MAX && c == 0 && e == (int64_t) ZXV_VHUB_AMOUNT_MAX, "pool funded");
    OK(zxv_vhub_balance(rd, h, iss, &d, &c, &e));
    CHECK(d == 0 && c == ZXV_VHUB_AMOUNT_MAX && e == -(int64_t) ZXV_VHUB_AMOUNT_MAX,
          "the issuer owes what it issued");
    OK(zxv_vhub_check(rd, h, &p));
    CHECK(p == 1, "one issue posting (no V-Bill backing asked)");
    IS(zxv_vhub_add_ccy(post, h, 7, "ZTB", 2, pk[0], 0, 0), ZXV_E_DUPLICATE);
    IS(zxv_vhub_add_ccy(post, h, 6, "ZTA", 2, pk[0], 0, 0), ZXV_E_DUPLICATE);
    for (int i = 0; i < 6; i++) OK(zxv_vhub_issue(post, h, pool, ZXV_VHUB_AMOUNT_MAX));
    IS(zxv_vhub_issue(post, h, pool, ZXV_VHUB_AMOUNT_MAX), ZXV_E_RANGE); /* would be 2^62 */
    OK(zxv_vhub_issue(post, h, pool, ZXV_VHUB_AMOUNT_MAX - 1));
    IS(zxv_vhub_issue(post, h, pool, 1), ZXV_E_RANGE);
    OK(zxv_vhub_balance(rd, h, iss, &d, &c, &e));
    CHECK(c == (UINT64_C(1) << 62) - 1 && e == -(int64_t) c, "the issuer owes 2^62 - 1");
    OK(zxv_vhub_add_ccy(post, h, 0, "ZTB", 0, pk[0], 0, ZXV_VHUB_AMOUNT_MAX));
    t.grp[4] = 0, t.grp[5] = 0, t.grp[6] = G_VBL, t.n = 7;
    for (int i = 0; i < 6; i++) OK(zxv_vhub_issue(post, h, 6, ZXV_VHUB_AMOUNT_MAX));
    IS(zxv_vhub_add_ccy(post, h, 1, "ZTC", 2, pk[0], 0, ZXV_VHUB_AMOUNT_MAX), ZXV_E_RANGE);
    IS(zxv_vhub_ccy_info(rd, h, 1, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    invariant(&t, "after registrations");
    IS(zxv_vhub_ccy_info(rd, h, 8, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_ccy_info(rd, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_ccy_info(post, h, 0, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_CAP);

    /* accounts and the faucet */
    uint32_t acct = 0;
    IS(zxv_vhub_open_account(post, h, 0, 1, NULL), ZXV_E_ARG);
    IS(zxv_vhub_open_account(post, h, 1, 1, &acct), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_open_account(rd, h, 0, 1, &acct), ZXV_E_CAP);
    IS(zxv_vhub_open_account(post, NULL, 0, 1, &acct), ZXV_E_ARG);
    acct = open_acct(&t, 0, 77);
    IS(zxv_vhub_issue(post, h, acct, 0), ZXV_E_RANGE);
    IS(zxv_vhub_issue(post, h, acct, ZXV_VHUB_AMOUNT_MAX + 1), ZXV_E_RANGE);
    IS(zxv_vhub_issue(post, h, t.n, 1), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_issue(post, h, ZXV_VHUB_VBILL_ISSUER, 1), ZXV_E_ARG);
    IS(zxv_vhub_issue(post, h, 4, 1), ZXV_E_ARG); /* ZTB's issuer */
    IS(zxv_vhub_issue(rd, h, acct, 1), ZXV_E_CAP);
    IS(zxv_vhub_issue(post, NULL, acct, 1), ZXV_E_ARG);
    OK(zxv_vhub_issue(post, h, acct, 4242));
    OK(zxv_vhub_issue(post, h, 5, 1)); /* ZTB's pool, by ZTB's issuer */
    OK(zxv_vhub_balance(rd, h, 4, &d, &c, &e));
    CHECK(c == 4243 && e == -4243, "ZTB's issuer owes 4242 + 1");
    OK(zxv_vhub_balance(rd, h, acct, &d, NULL, NULL));
    CHECK(d == 4242, "the holder got 4242");
    IS(zxv_vhub_issue(post, h, 6, ZXV_VHUB_AMOUNT_MAX), ZXV_E_RANGE); /* V-Bills at 2^62 */
    OK(zxv_vhub_issue(post, h, 6, ZXV_VHUB_AMOUNT_MAX - 1));
    OK(zxv_vhub_balance(rd, h, ZXV_VHUB_VBILL_ISSUER, &d, &c, &e));
    CHECK(d == 0 && c == (UINT64_C(1) << 62) - 1, "the V-Bill issuer owes 2^62 - 1");
    OK(zxv_vhub_check(rd, h, &p));
    CHECK(p == 18, "18 postings: 1 + 7 + 1 + 6 issues, 3 from the faucet (got %llu)",
          (unsigned long long) p);
    invariant(&t, "after the faucet");
    IS(zxv_vhub_balance(rd, NULL, 0, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_balance(post, h, 0, NULL, NULL, NULL), ZXV_E_CAP);
    IS(zxv_vhub_check(post, h, NULL), ZXV_E_CAP);
    IS(zxv_vhub_check(rd, NULL, NULL), ZXV_E_ARG);
    zxv_vhub_destroy(h);
    zxv_vhub_destroy(NULL);
}

static void test_pegs(void)
{
    hub_t t;
    setup(&t);
    uint32_t alice = open_acct(&t, A, 50), bob = open_acct(&t, B, 51);
    OK(zxv_vhub_issue(post, t.h, alice, 1000000));
    OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX));
    OK(zxv_vhub_add_limit(post, t.h, B, UINT64_MAX));
    snap_t s0, s1;
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 35000, 1, NULL, NULL), ZXV_E_PEG);
    IS(zxv_vhub_quote(rd, t.h, A, B, 35000, NULL, NULL, NULL), ZXV_E_PEG);
    IS(peg_by(&t, A, 350, 1, 1, B), ZXV_E_VERIFY);   /* another issuer's key */
    IS(peg_by(&t, A, 350, 1, 1, 3), ZXV_E_VERIFY);   /* a stranger's key */
    IS(peg_by(&t, A, 0, 1, 1, A), ZXV_E_RANGE);
    IS(peg_by(&t, A, 1, 0, 1, A), ZXV_E_RANGE);
    IS(peg_by(&t, A, 350, 1, 0, A), ZXV_E_PEG);      /* epoch 0 is not above 0 */
    OK(peg_by(&t, A, 350, 1, 1, A));
    IS(zxv_vhub_quote(rd, t.h, A, B, 35000, NULL, NULL, NULL), ZXV_E_PEG); /* B still unset */
    IS(zxv_vhub_quote(rd, t.h, B, A, 35000, NULL, NULL, NULL), ZXV_E_PEG);
    IS(peg_by(&t, A, 351, 1, 1, A), ZXV_E_PEG);
    uint64_t num = 0, den = 0, ep = 0;
    OK(zxv_vhub_ccy_info(rd, t.h, A, NULL, NULL, NULL, &num, &den, &ep));
    CHECK(num == 350 && den == 1 && ep == 1, "a refused update changes nothing");

    uint8_t m[40], sig[4627];
    OK(zxv_vhub_peg_msg(A, 351, 1, 2, m));
    OK(zxv_mldsa87_sign_deterministic(root, sk[A], m, sizeof m, NULL, 0, sig));
    IS(zxv_vhub_set_peg(post, t.h, A, 352, 1, 2, sig), ZXV_E_VERIFY);
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 2, 2, sig), ZXV_E_VERIFY);
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 1, 3, sig), ZXV_E_VERIFY);
    IS(zxv_vhub_set_peg(post, t.h, B, 351, 1, 2, sig), ZXV_E_VERIFY); /* slot is signed */
    /* a signature made with a FIPS 204 context string is a different one */
    uint8_t sig_ctx[4627];
    OK(zxv_mldsa87_sign_deterministic(root, sk[A], m, sizeof m, (const uint8_t *) "x", 1, sig_ctx));
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 1, 2, sig_ctx), ZXV_E_VERIFY);
    sig[100] ^= 1;
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 1, 2, sig), ZXV_E_VERIFY);
    sig[100] ^= 1;
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 1, 2, NULL), ZXV_E_ARG);
    IS(zxv_vhub_set_peg(post, t.h, 7, 351, 1, 2, sig), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_set_peg(post, t.h, 8, 351, 1, 2, sig), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_set_peg(rd, t.h, A, 351, 1, 2, sig), ZXV_E_CAP);
    IS(zxv_vhub_set_peg(post, NULL, A, 351, 1, 2, sig), ZXV_E_ARG);
    OK(zxv_vhub_ccy_info(rd, t.h, A, NULL, NULL, NULL, &num, &den, &ep));
    CHECK(num == 350 && den == 1 && ep == 1, "refused updates change nothing");
    OK(zxv_vhub_set_peg(post, t.h, A, 351, 1, 2, sig));
    OK(zxv_vhub_ccy_info(rd, t.h, A, NULL, NULL, NULL, &num, &den, &ep));
    CHECK(num == 351 && den == 1 && ep == 2, "the signed update applies");
    IS(zxv_vhub_set_peg(post, t.h, A, 351, 1, 2, sig), ZXV_E_PEG); /* replay */
    OK(peg_by(&t, A, 350, 1, 1000, A));                              /* epochs may skip */
    IS(peg_by(&t, A, 350, 1, 999, A), ZXV_E_PEG);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "pegs never touch the ledger");
    zxv_vhub_destroy(t.h);
}

static void test_swaps(void)
{
    hub_t t;
    ready(&t);
    uint32_t alice = open_acct(&t, A, 50), bob = open_acct(&t, B, 51), carol = open_acct(&t, C, 52),
             dave = open_acct(&t, A, 53), erin = open_acct(&t, A, 54);
    OK(zxv_vhub_issue(post, t.h, alice, 1000000));
    OK(zxv_vhub_issue(post, t.h, dave, 1000000));
    invariant(&t, "after funding");
    snap_t s0, s1;
    uint64_t v = 9, out = 9, q = 9;

    /* fail closed: no rule on a currency, nothing posts */
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 35000, 1, &v, &out), ZXV_E_STATE);
    OK(zxv_vhub_add_limit(post, t.h, A, 105000)); /* 300 V-Bills of A per sender per epoch */
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 35000, 1, &v, &out), ZXV_E_STATE);
    IS(zxv_vhub_swap(post, t.h, B, A, bob, alice, 120, 1, &v, &out), ZXV_E_STATE);
    snap(&t, &s1);
    CHECK(same(&s0, &s1) && v == 9 && out == 9, "a missing rule changes nothing, writes nothing");
    OK(zxv_vhub_add_limit(post, t.h, B, UINT64_MAX));
    OK(zxv_vhub_add_limit(post, t.h, C, UINT64_MAX));

    /* known answer: 35000 A -> 100 V-Bills -> 12000 B */
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 35000, 1, &v, &out));
    CHECK(v == 100 && out == 12000, "35000 A -> 100 V -> 12000 B (got %llu, %llu)",
          (unsigned long long) v, (unsigned long long) out);
    CHECK(eqty(&t, alice) == 1000000 - 35000 && eqty(&t, t.pool[A]) == 500000 + 35000 &&
              eqty(&t, t.vfl[A]) == 10000 - 100 && eqty(&t, t.vfl[B]) == 10000 + 100 &&
              eqty(&t, t.pool[B]) == 500000 - 12000 && eqty(&t, bob) == 12000,
          "all six accounts moved");
    snap(&t, &s1);
    CHECK(s1.postings == s0.postings + 1, "one posting for one swap");
    invariant(&t, "after swap 1");

    /* inexact amounts are refused and change nothing */
    snap(&t, &s0);
    v = out = 9;
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 35001, 2, &v, &out), ZXV_E_RANGE);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 349, 2, &v, &out), ZXV_E_RANGE);
    snap(&t, &s1);
    CHECK(same(&s0, &s1) && v == 9 && out == 9, "inexact: nothing changes");
    uint64_t qv = 0, qo = 0;
    OK(zxv_vhub_quote(rd, t.h, A, B, 35001, &q, &qv, &qo));
    CHECK(q == 35000 && qv == 100 && qo == 12000, "the quote rounds down to the exact amount");
    q = qv = qo = 9;
    OK(zxv_vhub_quote(rd, t.h, A, B, 349, &q, &qv, &qo));
    CHECK(q == 0 && qv == 0 && qo == 0, "no exact amount below one V-Bill");
    OK(zxv_vhub_quote(rd, t.h, A, B, 350, NULL, NULL, NULL));

    /* round trip returns exactly what went out */
    OK(zxv_vhub_swap(post, t.h, B, A, bob, alice, 12000, 3, &v, &out));
    CHECK(v == 100 && out == 35000 && eqty(&t, alice) == 1000000 && eqty(&t, bob) == 0 &&
              eqty(&t, t.vfl[A]) == 10000 && eqty(&t, t.vfl[B]) == 10000 &&
              eqty(&t, t.pool[A]) == 500000 && eqty(&t, t.pool[B]) == 500000,
          "B -> A gives back exactly 35000: nothing leaks");
    invariant(&t, "after the round trip");

    /* one implied cross rate: A -> C direct equals A -> B -> C */
    IS(zxv_vhub_swap(post, t.h, A, C, alice, carol, 3500, 4, NULL, NULL), ZXV_E_RANGE);
    OK(zxv_vhub_swap(post, t.h, A, C, alice, carol, 14000, 4, &v, &out));
    CHECK(v == 40 && out == 90, "14000 A -> 40 V -> 90 C");
    invariant(&t, "after A -> C");
    uint64_t direct = out, mid = 0;
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 14000, 5, NULL, &mid));
    OK(zxv_vhub_swap(post, t.h, B, C, bob, carol, mid, 6, NULL, &out));
    CHECK(mid == 4800 && out == direct, "A -> B -> C equals A -> C");
    invariant(&t, "after A -> B -> C");
    OK(zxv_vhub_quote(rd, t.h, A, C, 3499, &q, &qv, &qo));
    CHECK(q == 2800 && qv == 8 && qo == 18, "A -> C steps of 1400 (9/4 needs four V-Bills)");
    OK(zxv_vhub_quote(rd, t.h, C, A, 100, &q, &qv, &qo));
    CHECK(q == 99 && qv == 44 && qo == 15400, "C -> A: 99 C -> 44 V -> 15400 A");

    /* the limit: alice has sent 35000 + 14000 + 14000 = 63000 of 105000 */
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 42350, 7, &v, &out), ZXV_E_LIMIT);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "a limit refusal changes nothing");
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 42000, 7, &v, &out)); /* exactly the limit */
    invariant(&t, "at the limit");
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350, 8, NULL, NULL), ZXV_E_LIMIT);
    IS(zxv_vhub_swap(post, t.h, A, C, alice, carol, 1400, 8, NULL, NULL), ZXV_E_LIMIT);
    OK(zxv_vhub_swap(post, t.h, A, B, dave, bob, 105000, 8, NULL, NULL)); /* per sender */
    IS(zxv_vhub_swap(post, t.h, A, B, dave, bob, 350, 8, NULL, NULL), ZXV_E_LIMIT);
    uint64_t ep = 0;
    OK(zxv_vhub_next_epoch(post, t.h, &ep));
    CHECK(ep == 1, "epoch 1");
    IS(zxv_vhub_swap(post, t.h, A, B, dave, bob, 105350, 9, NULL, NULL), ZXV_E_LIMIT);
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 105000, 9, NULL, NULL)); /* fresh epoch */
    OK(zxv_vhub_next_epoch(post, t.h, NULL));
    OK(zxv_vhub_next_epoch(post, t.h, &ep));
    CHECK(ep == 3, "epochs count up");
    /* B's limit (UINT64_MAX) ignores inflows; a tight B limit refuses outflows only */
    OK(zxv_vhub_add_limit(post, t.h, B, 120));
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 3500, 10, NULL, &out)); /* 1200 B in */
    CHECK(out == 1200, "inflows to B are not limited by B's limit");
    IS(zxv_vhub_swap(post, t.h, B, A, bob, alice, 240, 10, NULL, NULL), ZXV_E_LIMIT);
    OK(zxv_vhub_swap(post, t.h, B, A, bob, alice, 120, 10, NULL, &out));
    CHECK(out == 350, "120 B -> 350 A");
    IS(zxv_vhub_swap(post, t.h, B, A, bob, alice, 120, 10, NULL, NULL), ZXV_E_LIMIT);
    invariant(&t, "after the limits");

    /* funds: sender, backing float, receiving pool; each refusal changes nothing */
    OK(zxv_vhub_next_epoch(post, t.h, NULL));
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, erin, bob, 350, 11, NULL, NULL), ZXV_E_FUNDS);
    OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX)); /* a second limit on A: both apply */
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 105350, 11, NULL, NULL), ZXV_E_LIMIT);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "funds and limit refusals change nothing");
    zxv_vhub_destroy(t.h);
}

static void test_backing(void)
{
    hub_t t;
    ready(&t);
    uint32_t alice = open_acct(&t, A, 50), bob = open_acct(&t, B, 51);
    OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX));
    OK(zxv_vhub_add_limit(post, t.h, B, UINT64_MAX));
    OK(zxv_vhub_issue(post, t.h, alice, 100000000));
    OK(zxv_vhub_issue(post, t.h, t.vfl[A], 100000)); /* top up A's backing */
    CHECK(eqty(&t, t.vfl[A]) == 110000, "A's float topped up by the V-Bill issuer");
    uint64_t c = 0;
    OK(zxv_vhub_balance(rd, t.h, ZXV_VHUB_VBILL_ISSUER, NULL, &c, NULL));
    CHECK(c == 3 * 10000 + 100000, "the V-Bill issuer owes every V-Bill it issued");
    /* B's pool holds 500000: 4200 V-Bills would pay 504000 */
    snap_t s0, s1;
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350 * 4200, 1, NULL, NULL), ZXV_E_FUNDS);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "a payout past B's pool is refused: the peg pays only while backed");
    uint64_t out = 0;
    OK(zxv_vhub_quote(rd, t.h, A, B, 350 * 4200, NULL, NULL, &out));
    CHECK(out == 504000, "the quote does not look at balances");
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350 * 4166, 1, NULL, &out));
    CHECK(out == 499920 && eqty(&t, t.pool[B]) == 80, "4166 V-Bills: 499920 B; 80 left");
    invariant(&t, "after draining B's pool");
    /* a peg change applies to the next swap */
    peg(&t, B, 40, 1);
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 700, 2, NULL, &out));
    CHECK(out == 80 && eqty(&t, t.pool[B]) == 0, "the next swap uses the new peg (2 V -> 80 B)");
    invariant(&t, "after the new peg");
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350, 3, NULL, NULL), ZXV_E_FUNDS);
    OK(zxv_vhub_issue(post, t.h, t.pool[B], 40));
    OK(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350, 3, NULL, &out));
    CHECK(out == 40, "the issuer refills its pool");
    invariant(&t, "after the refill");
    zxv_vhub_destroy(t.h);
}

static void test_swap_args(void)
{
    hub_t t;
    ready(&t);
    uint32_t alice = open_acct(&t, A, 50), bob = open_acct(&t, B, 51), carol = open_acct(&t, C, 52);
    OK(zxv_vhub_issue(post, t.h, alice, 1000000));
    OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX));
    OK(zxv_vhub_add_limit(post, t.h, B, UINT64_MAX));
    snap_t s0, s1;
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, A, alice, alice, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, 5, alice, bob, 350, 1, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_swap(post, t.h, 9, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 0, 1, NULL, NULL), ZXV_E_RANGE);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, ZXV_VHUB_AMOUNT_MAX + 1, 1, NULL, NULL),
       ZXV_E_RANGE);
    IS(zxv_vhub_swap(post, t.h, A, B, t.n, bob, 350, 1, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, t.n, 350, 1, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_swap(post, t.h, A, B, t.pool[A], bob, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, B, t.iss[A], bob, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, t.pool[B], 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, t.vfl[B], 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, ZXV_VHUB_VBILL_ISSUER, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(post, t.h, A, B, bob, alice, 350, 1, NULL, NULL), ZXV_E_CURRENCY);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, carol, 350, 1, NULL, NULL), ZXV_E_CURRENCY);
    IS(zxv_vhub_swap(post, NULL, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_swap(rd, t.h, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_CAP);
    IS(zxv_vhub_swap(NULL, t.h, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_quote(rd, t.h, A, A, 350, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_vhub_quote(rd, t.h, A, 5, 350, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_quote(rd, t.h, A, B, ZXV_VHUB_AMOUNT_MAX + 1, NULL, NULL, NULL), ZXV_E_RANGE);
    IS(zxv_vhub_quote(post, t.h, A, B, 350, NULL, NULL, NULL), ZXV_E_CAP);
    IS(zxv_vhub_quote(rd, NULL, A, B, 350, NULL, NULL, NULL), ZXV_E_ARG);
    uint64_t q = 0;
    OK(zxv_vhub_quote(rd, t.h, A, B, ZXV_VHUB_AMOUNT_MAX, &q, NULL, NULL));
    CHECK(q == ZXV_VHUB_AMOUNT_MAX / 350 * 350, "the quote at the top of the range");
    IS(zxv_vhub_add_limit(post, t.h, 5, 1), ZXV_E_NOT_FOUND);
    IS(zxv_vhub_add_limit(rd, t.h, A, 1), ZXV_E_CAP);
    IS(zxv_vhub_add_limit(post, NULL, A, 1), ZXV_E_ARG);
    for (int i = 1; i < 4; i++) OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX));
    IS(zxv_vhub_add_limit(post, t.h, A, 1), ZXV_E_CAPACITY);
    IS(zxv_vhub_next_epoch(rd, t.h, NULL), ZXV_E_CAP);
    IS(zxv_vhub_next_epoch(post, NULL, NULL), ZXV_E_ARG);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "argument refusals change nothing");

    /* a payout above the ledger's per-line maximum is refused, not wrapped */
    const uint64_t big = UINT64_C(1) << 50;
    OK(zxv_vhub_issue(post, t.h, alice, 350 * big));
    OK(zxv_vhub_issue(post, t.h, t.vfl[A], big));
    peg(&t, B, 1024, 1);
    uint64_t qv = 0, qo = 0;
    OK(zxv_vhub_quote(rd, t.h, A, B, 350 * big, &q, &qv, &qo));
    CHECK(q == 350 * big && qv == big && qo == (UINT64_C(1) << 60), "2^50 V-Bills -> 2^60 B");
    snap(&t, &s0);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350 * big, 2, NULL, NULL), ZXV_E_RANGE);
    snap(&t, &s1);
    CHECK(same(&s0, &s1), "an oversized payout changes nothing");
    invariant(&t, "after the oversized payout");

    /* the limit's sender table: 16 senders tracked, the 17th refused */
    OK(zxv_vhub_add_limit(post, t.h, C, UINT64_MAX));
    hub_t u;
    ready(&u);
    OK(zxv_vhub_add_limit(post, u.h, A, 1000000));
    OK(zxv_vhub_add_limit(post, u.h, B, UINT64_MAX));
    uint32_t rcv = open_acct(&u, B, 1), okc = 0, first = u.n;
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t a = open_acct(&u, A, 100 + i);
        OK(zxv_vhub_issue(post, u.h, a, 700));
        okc += zxv_vhub_swap(post, u.h, A, B, a, rcv, 350, 1, NULL, NULL) == ZXV_OK;
    }
    uint32_t extra = open_acct(&u, A, 200);
    OK(zxv_vhub_issue(post, u.h, extra, 350));
    CHECK(okc == 16, "16 senders tracked");
    IS(zxv_vhub_swap(post, u.h, A, B, extra, rcv, 350, 2, NULL, NULL), ZXV_E_LIMIT);
    OK(zxv_vhub_swap(post, u.h, A, B, first, rcv, 350, 2, NULL, NULL)); /* a tracked one */
    OK(zxv_vhub_next_epoch(post, u.h, NULL));
    OK(zxv_vhub_swap(post, u.h, A, B, extra, rcv, 350, 3, NULL, NULL));
    invariant(&u, "after 18 senders");
    zxv_vhub_destroy(u.h);
    zxv_vhub_destroy(t.h);
}

/* The quote against a brute-force search computed here. */
static void test_quote_brute(void)
{
    static const uint64_t pg[][2] = {{350, 1}, {120, 1}, {9, 4}, {6, 10}, {7, 3}, {1, 1}, {12, 18}};
    const uint32_t n = sizeof pg / sizeof pg[0];
    hub_t t;
    setup(&t);
    uint32_t bad = 0, cases = 0;
    for (uint32_t x = 0; x < n; x++)
        for (uint32_t y = 0; y < n; y++) {
            peg(&t, A, pg[x][0], pg[x][1]);
            peg(&t, B, pg[y][0], pg[y][1]);
            for (uint64_t want = 0; want <= 400; want++) {
                uint64_t best = 0, bv = 0, bo = 0;
                for (uint64_t a = want; a > 0; a--) {
                    if ((a * pg[x][1]) % pg[x][0]) continue;
                    uint64_t v = a * pg[x][1] / pg[x][0];
                    if ((v * pg[y][0]) % pg[y][1]) continue;
                    best = a, bv = v, bo = v * pg[y][0] / pg[y][1];
                    break;
                }
                uint64_t q = 9, qv = 9, qo = 9;
                cases++;
                if (zxv_vhub_quote(rd, t.h, A, B, want, &q, &qv, &qo) != ZXV_OK || q != best ||
                    qv != bv || qo != bo)
                    bad++;
            }
        }
    CHECK(bad == 0 && cases == 49 * 401, "quote equals brute force (%u of %u wrong)", bad, cases);
    zxv_vhub_destroy(t.h);
}

static void test_context(void)
{
    hub_t t;
    ready(&t);
    uint32_t alice = open_acct(&t, A, 50), bob = open_acct(&t, B, 51);
    OK(zxv_vhub_issue(post, t.h, alice, 3500));
    OK(zxv_vhub_add_limit(post, t.h, A, UINT64_MAX));
    OK(zxv_vhub_add_limit(post, t.h, B, UINT64_MAX));

    /* a cap of another context, a revoked cap, a cap whose attestation fell */
    zxv_ctx *ctx2 = NULL;
    zxv_cap *other = NULL, *child = NULL;
    OK(zxv_ctx_create(&ctx2));
    OK(zxv_cap_create(ctx2, 1, NULL, ZXV_CAP_ALL, &other));
    IS(zxv_vhub_swap(other, t.h, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_CAP);
    IS(zxv_vhub_quote(other, t.h, A, B, 350, NULL, NULL, NULL), ZXV_E_CAP);
    OK(zxv_cap_attenuate(post, ZXV_CAP_LEDGER_POST, &child));
    OK(zxv_vhub_swap(child, t.h, A, B, alice, bob, 350, 1, NULL, NULL));
    OK(zxv_cap_set_attestation(child, ZXV_LP_BOTH));
    IS(zxv_vhub_swap(child, t.h, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_CAP);
    OK(zxv_cap_revoke(child));
    IS(zxv_vhub_swap(child, t.h, A, B, alice, bob, 350, 1, NULL, NULL), ZXV_E_CAP);
    zxv_cap_destroy(child);
    zxv_cap_destroy(other);
    zxv_ctx_destroy(ctx2);
    invariant(&t, "before the halt");

    /* a halted context refuses every call on the hub, before validation */
    OK(zxv_ctx_halt(ctx, "test halt"));
    uint8_t sig[4627] = {0};
    zxv_vhub *h2 = NULL;
    uint64_t x = 0;
    IS(zxv_vhub_create(post, NULL, &h2), ZXV_E_POISONED);
    IS(zxv_vhub_add_ccy(post, t.h, 5, "ZTZ", 2, pk[0], 0, 0), ZXV_E_POISONED);
    IS(zxv_vhub_ccy_info(rd, t.h, A, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vhub_set_peg(post, t.h, A, 1, 1, 99, sig), ZXV_E_POISONED);
    IS(zxv_vhub_open_account(post, t.h, A, 1, &alice), ZXV_E_POISONED);
    IS(zxv_vhub_issue(post, t.h, alice, 1), ZXV_E_POISONED);
    IS(zxv_vhub_balance(rd, t.h, alice, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vhub_add_limit(post, t.h, A, 1), ZXV_E_POISONED);
    IS(zxv_vhub_next_epoch(post, t.h, &x), ZXV_E_POISONED);
    IS(zxv_vhub_quote(rd, t.h, A, B, 350, &x, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vhub_swap(post, t.h, A, B, alice, bob, 350, 2, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vhub_check(rd, t.h, &x), ZXV_E_POISONED);
    IS(zxv_vhub_swap(post, NULL, A, B, 0, 0, 0, 0, NULL, NULL), ZXV_E_ARG); /* no hub to tie */
    uint8_t m[40];
    OK(zxv_vhub_peg_msg(0, 1, 1, 1, m)); /* pure: no context */
    zxv_vhub_destroy(t.h);
}

int main(void)
{
    uint32_t m2 = 0;
    if (zxv_features2(&m2) != ZXV_OK) return 1;
    if (zxv_ctx_create(&ctx) != ZXV_OK) return 1;
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &rd));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST, &post));
    if (!(m2 & ZXV_FEAT2_VHUB)) {
        /* Built with ZXV_NO_PQC: pegs cannot be verified (rule R9). */
        hub_t t;
        static const uint8_t sig[4627];
        setup(&t);
        IS(zxv_vhub_set_peg(post, t.h, A, 1, 1, 1, sig), ZXV_E_NOTIMPL);
        zxv_vhub_destroy(t.h);
        printf("test_zxv_abi15_vhub: feature not built; %d checks, %d failures\n", checks,
               failures);
        return failures ? 1 : 0;
    }
    for (int k = 0; k < NK; k++) {
        uint8_t seed[32];
        memset(seed, 0x40 + k, sizeof seed);
        OK(zxv_mldsa87_keygen_seed(root, seed, pk[k], sk[k]));
    }
    test_peg_msg();
    test_create_and_ccy();
    test_pegs();
    test_swaps();
    test_backing();
    test_swap_args();
    test_quote_brute();
    test_context();
    zxv_cap_destroy(post);
    zxv_cap_destroy(rd);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    printf("test_zxv_abi15_vhub: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
