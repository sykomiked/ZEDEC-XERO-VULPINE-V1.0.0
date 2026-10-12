/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_privacy.c — the privacy layer (privacy.h P1-P5).
 *
 * The samplers are integer-only; this test uses floating point only to
 * compare sample frequencies with the exact probabilities (exp is not
 * rational). Each statistical check allows six standard errors, so a
 * correct sampler fails it with probability below 1e-8. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "privacy.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* splitmix64: a test source, not a CSPRNG */
static uint64_t sm(void *ctx)
{
    uint64_t *s = ctx, z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint64_t seed = 42;
static priv_rng_t R = {sm, &seed};

static uint64_t stuck_v;
static uint64_t stuck(void *ctx)
{
    (void) ctx;
    return stuck_v;
}

static bool near(double got, double want, double n)
{
    double se = sqrt(want * (1 - want) / n);
    return fabs(got - want) <= 6 * se + 1e-12;
}

#define N 200000

static void test_bernoulli(void)
{
    int k = 0, errs = 0;
    bool b;
    for (int i = 0; i < N; i++) {
        errs += priv_bern(&R, 1, 3, &b) != PRIV_OK;
        k += b;
    }
    CHECK(errs == 0, "bern draws");
    CHECK(near((double) k / N, 1.0 / 3, N), "Bernoulli(1/3): %f", (double) k / N);
    const struct { uint64_t n, d; } g[] = {{1, 1}, {3, 2}, {1, 10}, {7, 3}, {0, 5}};
    for (unsigned j = 0; j < sizeof g / sizeof g[0]; j++) {
        k = 0;
        for (int i = 0; i < N; i++) {
            priv_bern_exp(&R, g[j].n, g[j].d, &b);
            k += b;
        }
        double want = exp(-(double) g[j].n / (double) g[j].d);
        CHECK(near((double) k / N, want, N), "Bernoulli(exp(-%llu/%llu)): %f vs %f",
              (unsigned long long) g[j].n, (unsigned long long) g[j].d, (double) k / N, want);
    }
    CHECK(priv_bern(&R, 2, 1, &b) == PRIV_E_ARG && priv_bern(&R, 0, 0, &b) == PRIV_E_ARG &&
              priv_bern_exp(&R, 1, 0, &b) == PRIV_E_ARG,
          "bad fractions");
}

static void test_dlap(void)
{
    /* P(y) = (1 - q)/(1 + q) * q^|y|, q = exp(-s/t) */
    const struct { uint64_t s, t; } p[] = {{1, 1}, {1, 10}, {3, 2}};
    for (unsigned j = 0; j < sizeof p / sizeof p[0]; j++) {
        static int cnt[41];
        memset(cnt, 0, sizeof cnt);
        long long sum = 0;
        for (int i = 0; i < N; i++) {
            int64_t y;
            if (priv_dlap(&R, p[j].s, p[j].t, &y) != PRIV_OK) {
                CHECK(0, "dlap failed");
                break;
            }
            sum += y;
            if (y >= -20 && y <= 20) cnt[y + 20]++;
        }
        double q = exp(-(double) p[j].s / (double) p[j].t), ok = 1;
        for (int y = -5; y <= 5; y++) {
            double want = (1 - q) / (1 + q) * pow(q, y < 0 ? -y : y);
            if (!near((double) cnt[y + 20] / N, want, N)) {
                ok = 0;
                printf("  s/t=%llu/%llu y=%d: %f vs %f\n", (unsigned long long) p[j].s,
                       (unsigned long long) p[j].t, y, (double) cnt[y + 20] / N, want);
            }
        }
        CHECK(ok, "discrete Laplace s/t = %llu/%llu matches exp(-|y| s/t)",
              (unsigned long long) p[j].s, (unsigned long long) p[j].t);
        double var = 2 * q / ((1 - q) * (1 - q));
        CHECK(fabs((double) sum / N) <= 6 * sqrt(var / N), "mean zero");
    }
    int64_t y;
    CHECK(priv_dlap(&R, 0, 1, &y) == PRIV_E_ARG && priv_dlap(&R, 1, 0, &y) == PRIV_E_ARG, "zeros");

    /* a stuck source ends with PRIV_E_RNG or a value, never a hang */
    priv_rng_t S = {stuck, NULL};
    stuck_v = 0;
    CHECK(priv_dlap(&S, 1, 1, &y) == PRIV_E_RNG, "all-zero source");
    stuck_v = UINT64_MAX;
    priv_status_t st = priv_dlap(&S, 1, 3, &y);
    CHECK(st == PRIV_E_RNG || st == PRIV_OK, "all-ones source terminates");
    priv_rng_t none = {NULL, NULL};
    bool b;
    CHECK(priv_bern(&none, 1, 2, &b) == PRIV_E_ARG, "no source");
}

static void test_budget(void)
{
    priv_budget_t b;
    CHECK(priv_budget_init(&b, 1, 1) == PRIV_OK, "epsilon 1");
    CHECK(priv_budget_spend(&b, 1, 2) == PRIV_OK && priv_budget_spend(&b, 1, 3) == PRIV_OK &&
              b.spent_num == 5 && b.spent_den == 6,
          "P2: 1/2 + 1/3 = 5/6 exactly");
    CHECK(priv_budget_spend(&b, 1, 5) == PRIV_E_BUDGET && b.spent_num == 5 && b.releases == 2,
          "P2: 1/5 does not fit, nothing spent");
    CHECK(priv_budget_spend(&b, 1, 6) == PRIV_OK && b.spent_num == 1 && b.spent_den == 1,
          "P2: exactly to the budget");
    CHECK(priv_budget_spend(&b, 1, 1000000) == PRIV_E_BUDGET, "P2: none left");
    CHECK(priv_budget_init(&b, 0, 1) == PRIV_E_ARG && priv_budget_init(&b, 1, 0) == PRIV_E_ARG &&
              priv_budget_init(&b, (1ull << 32) + 1, 1) == PRIV_E_ARG,
          "bad budgets");
    CHECK(priv_budget_spend(&b, 0, 1) == PRIV_E_ARG, "zero spend");

    /* P3 */
    priv_budget_init(&b, 1, 1);
    int64_t out;
    CHECK(priv_release_count(&b, &R, 1000, 1, 2, 1, &out) == PRIV_OK && out > 900 && out < 1100,
          "release near the true count: %lld", (long long) out);
    CHECK(priv_release_count(&b, &R, 1000, 1, 2, 1, &out) == PRIV_OK, "second half");
    CHECK(priv_release_count(&b, &R, 1000, 1, 2, 1, &out) == PRIV_E_BUDGET && out == 0,
          "P3: refused before any noise");
    priv_budget_init(&b, 1, 1);
    priv_rng_t S = {stuck, NULL};
    stuck_v = 0;
    CHECK(priv_release_count(&b, &S, 5, 1, 4, 1, &out) == PRIV_E_RNG && b.spent_num == 1 &&
              b.spent_den == 4 && out == 0,
          "P3: a failed draw keeps the spend");
    CHECK(priv_release_count(&b, &R, 5, 1, 4, 0, &out) == PRIV_E_ARG, "sensitivity 0");
}

enum { ROLE_CLIN = 1, ROLE_MKT = 2, CAT_CONTACT = 1, CAT_HEALTH = 2, PUR_TREAT = 3, PUR_ADS = 4 };

static void test_access(void)
{
    priv_rule_t rules[4] = {
        {ROLE_CLIN, CAT_HEALTH, PUR_TREAT, PRIV_ALLOW},
        {ROLE_MKT, PRIV_ANY, PRIV_ANY, PRIV_DENY},
        {ROLE_CLIN, PRIV_ANY, PUR_ADS, PRIV_DENY},
        {ROLE_CLIN, CAT_CONTACT, PRIV_ANY, PRIV_ALLOW},
    };
    priv_consent_t cons[1] = {{77, PUR_TREAT, 10, 500, true, false}};
    priv_policy_t p;
    memset(&p, 0, sizeof p);
    p.rules = rules;
    p.n_rules = 4;
    p.consents = cons;
    p.n_consents = 1;
    p.special_mask = 1u << CAT_HEALTH;
    p.retention[CAT_CONTACT] = 100;
    priv_request_t q = {ROLE_CLIN, CAT_HEALTH, PUR_TREAT, 77, 200, 150};
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "P4: allowed with explicit consent");
    q.subject = 78;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: no consent from this subject");
    q.subject = 77;
    q.now = 500;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: consent expired");
    q.now = 5;
    q.record_time = 1;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: consent not yet given");
    q.now = 200;
    cons[0].withdrawn = true;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: consent withdrawn");
    cons[0].withdrawn = false;
    cons[0].explicit_consent = false;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: implied consent is not enough");
    cons[0].explicit_consent = true;

    q.purpose = PUR_ADS;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: deny only");
    rules[0].purpose = PRIV_ANY; /* now an allow and a deny both match */
    CHECK(lpx_action(priv_access(&p, &q)) == LPX_ACT_ESCALATE, "P4: allow and deny: escalate");
    rules[0].purpose = PUR_TREAT;
    q.role = 9;
    CHECK(lpx_action(priv_access(&p, &q)) == LPX_ACT_WAIT &&
              lpx_base(priv_access(&p, &q)) == LPX_B_NEITHER,
          "P4: no rule: never allow");
    q.role = ROLE_MKT;
    q.category = CAT_CONTACT;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "P4: marketing denied");

    /* retention */
    priv_request_t r = {ROLE_CLIN, CAT_CONTACT, PUR_TREAT, 77, 140, 50};
    CHECK(lpx_base(priv_access(&p, &r)) == LPX_B_TRUE, "within retention");
    r.now = 151;
    CHECK(lpx_base(priv_access(&p, &r)) == LPX_B_FALSE, "P4: past retention");
    r.now = 40;
    CHECK(lpx_base(priv_access(&p, &r)) == LPX_B_FALSE, "P4: a record from the future");
    r.now = 140;
    r.category = 40;
    rules[3].category = PRIV_ANY;
    CHECK(lpx_base(priv_access(&p, &r)) == LPX_B_FALSE, "P4: unknown category");
    CHECK(lpx_base(priv_access(NULL, &r)) == LPX_B_NEITHER, "NULL policy");
}

static bool iss_ok(void *ctx, const uint8_t m[32], const uint8_t *s, uint32_t n)
{
    (void) ctx;
    uint8_t w[32];
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "issuer", 6);
    sha256_update(&c, m, 32);
    sha256_final(&c, w);
    return n == 32 && memcmp(w, s, 32) == 0;
}

static void test_sd(void)
{
    priv_claim_t cl[4] = {
        {{1}, "given_name", "Amara"},
        {{2}, "birth_date", "2001-04-17"},
        {{3}, "age_over_18", "true"},
        {{4}, "nationality", "KE"},
    };
    uint8_t dg[4][32], m[32], sig[32];
    for (int i = 0; i < 4; i++) CHECK(priv_sd_digest(&cl[i], dg[i]) == PRIV_OK, "digest %d", i);
    CHECK(priv_sd_list_msg((const uint8_t (*)[32]) dg, 4, m) == PRIV_OK, "list");
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "issuer", 6);
    sha256_update(&c, m, 32);
    sha256_final(&c, sig);

    const uint8_t (*D)[32] = (const uint8_t (*)[32]) dg;
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, &cl[2], 1) == PRIV_OK,
          "P5: over 18, without the birth date");
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, NULL, 0) == PRIV_OK, "nothing shown");
    priv_claim_t lie = cl[2];
    lie.value = "false";
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, &lie, 1) == PRIV_E_CLAIM, "a changed value");
    lie = cl[2];
    lie.salt[0] ^= 1;
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, &lie, 1) == PRIV_E_CLAIM, "a changed salt");
    lie = cl[2];
    lie.name = "age_over_21";
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, &lie, 1) == PRIV_E_CLAIM, "a changed name");
    dg[0][0] ^= 1;
    CHECK(priv_sd_check(D, 4, sig, 32, iss_ok, NULL, &cl[2], 1) == PRIV_E_SIG,
          "P5: a list the issuer did not sign");
    dg[0][0] ^= 1;
    CHECK(priv_sd_check(D, 3, sig, 32, iss_ok, NULL, &cl[2], 1) == PRIV_E_SIG, "a shortened list");
    /* the name/value framing keeps "ab"+"c" apart from "a"+"bc" */
    priv_claim_t x = {{0}, "ab", "c"}, y = {{0}, "a", "bc"};
    uint8_t dx[32], dy[32];
    priv_sd_digest(&x, dx);
    priv_sd_digest(&y, dy);
    CHECK(memcmp(dx, dy, 32) != 0, "length framing");
    char big[300];
    memset(big, 'n', sizeof big - 1);
    big[sizeof big - 1] = 0;
    priv_claim_t z = {{0}, big, "v"};
    CHECK(priv_sd_digest(&z, dx) == PRIV_E_ARG, "a name over 255 bytes");
    z.name = "";
    CHECK(priv_sd_digest(&z, dx) == PRIV_E_ARG, "an empty name");
}

/* A scripted source: the listed words in order, then `rest`. */
typedef struct {
    const uint64_t *v;
    unsigned n, i;
    uint64_t rest;
    unsigned calls;
} script_t;
static uint64_t scr(void *ctx)
{
    script_t *s = ctx;
    s->calls++;
    return s->i < s->n ? s->v[s->i++] : s->rest;
}

/* For CKS20 Algorithm 1 with gamma = 1: the k-th Bernoulli(1/k) draw
 * (k = 2, 3, ...) is true while k < stop_k and false at stop_k. */
static unsigned run_k, run_stop;
static uint64_t run(void *ctx)
{
    (void) ctx;
    uint64_t k = ++run_k + 1;
    uint64_t m = k * ((1ull << 63) / k + 1); /* above 2^64 mod k, a multiple of k */
    return k < run_stop ? m : m + 1;
}

static void test_edges(void)
{
    bool b;
    /* P1: Bernoulli(1/1) needs no draw */
    script_t c0 = {NULL, 0, 0, 0, 0};
    priv_rng_t C = {scr, &c0};
    CHECK(priv_bern(&C, 1, 1, &b) == PRIV_OK && b && c0.calls == 0, "den 1: no draw");
    /* rejection sampling: 2^64 mod 3 = 1, so the word 0 is rejected, 1 is not */
    priv_rng_t S = {stuck, NULL};
    stuck_v = 0;
    CHECK(priv_bern(&S, 1, 3, &b) == PRIV_E_RNG, "the word below 2^64 mod 3 is rejected");
    stuck_v = 1;
    CHECK(priv_bern(&S, 2, 3, &b) == PRIV_OK && b, "the word 2^64 mod 3 is kept (u = 1)");
    stuck_v = 2;
    CHECK(priv_bern(&S, 2, 3, &b) == PRIV_OK && !b, "u = 2 is not below 2");
    /* exactly UNIFORM_TRIES (64) attempts */
    script_t c63 = {NULL, 0, 0, 1, 0};
    uint64_t zeros[64] = {0};
    c63.v = zeros;
    c63.n = 63;
    C.ctx = &c63;
    CHECK(priv_bern(&C, 2, 3, &b) == PRIV_OK && b && c63.calls == 64, "accepted on try 64");
    script_t c64 = {zeros, 64, 0, 1, 0};
    C.ctx = &c64;
    CHECK(priv_bern(&C, 2, 3, &b) == PRIV_E_RNG && c64.calls == 64, "64 rejections: fail");

    /* Algorithm 1 runs to k = 64 and no further */
    priv_rng_t K = {run, NULL};
    run_k = 0;
    run_stop = 64;
    CHECK(priv_bern_exp(&K, 1, 1, &b) == PRIV_OK && !b && run_k == 63, "k = 64 is allowed");
    run_k = 0;
    run_stop = 1000;
    CHECK(priv_bern_exp(&K, 1, 1, &b) == PRIV_E_RNG && run_k == 63, "k = 65 is refused");
    run_k = 0;
    run_stop = 3;
    CHECK(priv_bern_exp(&K, 1, 1, &b) == PRIV_OK && b && run_k == 2, "stops at k = 3: odd");
    /* a failed draw inside exp(-1)^floor(g) is passed up */
    stuck_v = 0;
    CHECK(priv_bern_exp(&S, 3, 1, &b) == PRIV_E_RNG, "integer part: draw fails");
    priv_rng_t none = {NULL, NULL};
    CHECK(priv_bern_exp(&none, 1, 2, &b) == PRIV_E_ARG, "fraction part: no source");

    /* Algorithm 2 */
    int64_t z = 77;
    stuck_v = 0;
    CHECK(priv_dlap(&S, 1, 3, &z) == PRIV_E_RNG && z == 0, "dlap: failed draw leaves 0");
    const uint64_t T = UINT64_MAX;
    /* u = 2^63 - 1; Bernoulli(exp(-u/t)) true; v = 0; sign negative */
    const uint64_t w1[] = {(1ull << 63) - 1, T - 1, 1, 0};
    script_t s1 = {w1, 4, 0, 0, 0};
    C.ctx = &s1;
    CHECK(priv_dlap(&C, 1, T, &z) == PRIV_OK && z == -INT64_MAX, "y = INT64_MAX fits: %lld",
          (long long) z);
    const uint64_t w2[] = {(1ull << 63) + 5, T - 1, 1, 0};
    script_t s2 = {w2, 4, 0, 0, 0};
    C.ctx = &s2;
    CHECK(priv_dlap(&C, 1, T, &z) == PRIV_E_OVERFLOW, "y above INT64_MAX");

    /* P2 bounds */
    priv_budget_t bb;
    const uint64_t F = 1ull << 32;
    CHECK(priv_budget_init(NULL, 1, 1) == PRIV_E_ARG, "NULL budget");
    CHECK(priv_budget_init(&bb, 0, 1) == PRIV_E_ARG && bb.total_num == 0 && bb.total_den == 1 &&
              bb.spent_num == 0 && bb.spent_den == 1 && bb.releases == 0,
          "a refused budget is empty");
    CHECK(priv_budget_init(&bb, F, 1) == PRIV_OK && priv_budget_init(&bb, 1, F) == PRIV_OK &&
              priv_budget_init(&bb, 1, F + 1) == PRIV_E_ARG,
          "2^32 is the largest term");
    priv_budget_init(&bb, 2, 1);
    CHECK(priv_budget_spend(&bb, 1, 1) == PRIV_OK && priv_budget_spend(&bb, F, F) == PRIV_OK &&
              bb.spent_num == 2 && bb.spent_den == 1,
          "1 + 2^32/2^32 = 2");
    CHECK(priv_budget_spend(&bb, F + 1, 1) == PRIV_E_ARG && priv_budget_spend(&bb, 1, F + 1) ==
              PRIV_E_ARG && priv_budget_spend(&bb, 1, 0) == PRIV_E_ARG,
          "spend terms out of range");
    priv_budget_init(&bb, 1, 3);
    CHECK(priv_budget_spend(&bb, 1, 2) == PRIV_E_BUDGET, "1/2 > 1/3");
    bb.total_den = 0;
    CHECK(priv_budget_spend(&bb, 1, 9) == PRIV_E_ARG, "a broken budget");
    /* the reduced fraction must fit; a 2^64 denominator fails closed */
    priv_budget_init(&bb, 2, 1);
    CHECK(priv_budget_spend(&bb, 1, F) == PRIV_OK && bb.spent_den == F, "1/2^32 fits");
    CHECK(priv_budget_spend(&bb, 1, F) == PRIV_E_OVERFLOW && bb.spent_den == F && bb.releases == 1,
          "denominator 2^64");
    CHECK(priv_budget_spend(&bb, 1, F >> 1) == PRIV_OK && bb.spent_num == 3 && bb.spent_den == F,
          "1/2^32 + 1/2^31 = 3/2^32 (denominator 2^63 before reducing)");
    priv_budget_init(&bb, 2, 1);
    priv_budget_spend(&bb, 1, F);
    CHECK(priv_budget_spend(&bb, 1, F - 1) == PRIV_E_OVERFLOW && bb.releases == 1,
          "1/2^32 + 1/(2^32-1) does not reduce to fit");
    priv_budget_init(&bb, F, 1);
    CHECK(priv_budget_spend(&bb, F >> 1, 1) == PRIV_OK && priv_budget_spend(&bb, F, F) == PRIV_OK &&
              bb.spent_num == (F >> 1) + 1 && bb.spent_den == 1,
          "numerator 2^63 + 2^32 before reducing");
    priv_budget_init(&bb, F, 1);
    CHECK(priv_budget_spend(&bb, F, 1) == PRIV_OK && bb.spent_num == F, "spent reaches 2^32");
    priv_budget_init(&bb, F, 1);
    priv_budget_spend(&bb, F - 1, 1);
    CHECK(priv_budget_spend(&bb, F, F) == PRIV_E_OVERFLOW && bb.spent_num == F - 1 &&
              bb.releases == 1,
          "numerator 2^64 before reducing fails closed");
    priv_budget_init(&bb, 2, 1);
    bb.releases = UINT32_MAX;
    CHECK(priv_budget_spend(&bb, 1, 2) == PRIV_E_OVERFLOW && bb.spent_num == 0, "release count");

    /* P3 arguments */
    priv_budget_init(&bb, 1, 1);
    CHECK(priv_release_count(&bb, &R, 1, 1, 2, 1, NULL) == PRIV_E_ARG, "no output");
    CHECK(priv_release_count(&bb, NULL, 1, 1, 2, 1, &z) == PRIV_E_ARG && bb.spent_num == 0 &&
              z == 0,
          "no source: nothing spent");
    CHECK(priv_release_count(&bb, &R, 1, 1, 2, 0, &z) == PRIV_E_ARG && bb.spent_num == 0,
          "sensitivity 0: nothing spent");
    priv_budget_init(&bb, F, 1);
    priv_status_t st = PRIV_OK;
    for (int i = 0; i < 200 && st == PRIV_OK; i++)
        st = priv_release_count(&bb, &R, INT64_MAX, 1, 1, 1, &z);
    CHECK(st == PRIV_E_OVERFLOW && z == 0, "count + noise overflows: output 0");
}

static void test_access_edges(void)
{
    priv_rule_t rules[1] = {{1, PRIV_ANY, 3, PRIV_ALLOW}};
    priv_consent_t cons[2] = {{9, 3, 10, 0, true, false}, {7, 3, 100, 0, true, false}};
    priv_policy_t p;
    memset(&p, 0, sizeof p);
    p.rules = rules;
    p.n_rules = 1;
    priv_request_t q = {1, 4, 3, 7, 100, 50};
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "an ordinary category needs no consent");
    q.category = PRIV_MAX_CATEGORIES;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "category 32 is out of range");
    q.category = 4;
    p.special_mask = 1u << 4;
    p.consents = cons;
    p.n_consents = 2;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "the second consent matches, granted now");
    cons[1].expires = 1;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "expired at 1");
    cons[1].expires = 101;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "expires after now");
    cons[1].expires = 100;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "expires now");
    cons[1].expires = 0;
    p.retention[4] = 50;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "exactly at the retention limit");
    q.record_time = 100;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_TRUE, "a record from now");
    q.record_time = 49;
    CHECK(lpx_base(priv_access(&p, &q)) == LPX_B_FALSE, "one past retention");
}

static void sign(const uint8_t (*d)[32], uint32_t n, uint8_t sig[32])
{
    uint8_t m[32];
    priv_sd_list_msg(d, n, m);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "issuer", 6);
    sha256_update(&c, m, 32);
    sha256_final(&c, sig);
}

static void test_sd_edges(void)
{
    /* digest layout: "zxv/sd/v1" || salt[16] || len || name || len || value */
    priv_claim_t c = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, "age_over_18", "true"};
    uint8_t got[32], want[32];
    uint8_t l;
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv/sd/v1", 9);
    sha256_update(&h, c.salt, 16);
    l = 11;
    sha256_update(&h, &l, 1);
    sha256_update(&h, (const uint8_t *) "age_over_18", 11);
    l = 4;
    sha256_update(&h, &l, 1);
    sha256_update(&h, (const uint8_t *) "true", 4);
    sha256_final(&h, want);
    CHECK(priv_sd_digest(&c, got) == PRIV_OK && memcmp(got, want, 32) == 0, "digest layout");
    priv_claim_t bad = c;
    CHECK(priv_sd_digest(NULL, got) == PRIV_E_ARG && priv_sd_digest(&c, NULL) == PRIV_E_ARG,
          "NULL claim or output");
    bad.name = NULL;
    CHECK(priv_sd_digest(&bad, got) == PRIV_E_ARG, "NULL name");
    bad = c;
    bad.value = NULL;
    CHECK(priv_sd_digest(&bad, got) == PRIV_E_ARG, "NULL value");
    char s255[256], s256[257];
    memset(s255, 'a', 255);
    s255[255] = 0;
    memset(s256, 'a', 256);
    s256[256] = 0;
    bad = c;
    bad.name = s255;
    bad.value = s255;
    CHECK(priv_sd_digest(&bad, got) == PRIV_OK, "255-byte name and value");
    bad.value = s256;
    CHECK(priv_sd_digest(&bad, got) == PRIV_E_ARG, "256-byte value");

    /* list layout: "zxv/sd-list/v1" || n (32-bit LE) || digests */
    static uint8_t dl[300][32];
    for (unsigned i = 0; i < 300; i++)
        for (unsigned j = 0; j < 32; j++) dl[i][j] = (uint8_t) (i * 31 + j);
    const uint8_t (*D)[32] = (const uint8_t (*)[32]) dl;
    const uint8_t le[4] = {300 & 0xFF, 300 >> 8, 0, 0};
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv/sd-list/v1", 14);
    sha256_update(&h, le, 4);
    for (unsigned i = 0; i < 300; i++) sha256_update(&h, dl[i], 32);
    sha256_final(&h, want);
    CHECK(priv_sd_list_msg(D, 300, got) == PRIV_OK && memcmp(got, want, 32) == 0, "list layout");
    static uint8_t big[1025][32];
    const uint8_t (*B)[32] = (const uint8_t (*)[32]) big;
    CHECK(priv_sd_list_msg(B, 1024, got) == PRIV_OK && priv_sd_list_msg(B, 1025, got) == PRIV_E_ARG,
          "at most 1024 digests");
    CHECK(priv_sd_list_msg(NULL, 1, got) == PRIV_E_ARG && priv_sd_list_msg(D, 1, NULL) == PRIV_E_ARG &&
              priv_sd_list_msg(NULL, 0, got) == PRIV_OK,
          "list arguments");

    /* checking */
    uint8_t dg[3][32], sig[32];
    priv_claim_t cl[3] = {{{1}, "a", "1"}, {{2}, "b", "2"}, {{3}, "c", "3"}};
    for (int i = 0; i < 3; i++) priv_sd_digest(&cl[i], dg[i]);
    const uint8_t (*G)[32] = (const uint8_t (*)[32]) dg;
    sign(G, 3, sig);
    CHECK(priv_sd_check(G, 3, sig, 32, iss_ok, NULL, &cl[0], 1) == PRIV_OK, "the first digest");
    CHECK(priv_sd_check(G, 3, sig, 32, iss_ok, NULL, cl, 3) == PRIV_OK, "all three");
    CHECK(priv_sd_check(G, 3, sig, 32, NULL, NULL, cl, 1) == PRIV_E_ARG &&
              priv_sd_check(G, 3, NULL, 32, iss_ok, NULL, cl, 1) == PRIV_E_ARG &&
              priv_sd_check(G, 3, sig, 32, iss_ok, NULL, NULL, 1) == PRIV_E_ARG,
          "check arguments");
    CHECK(priv_sd_check(B, 1025, sig, 32, iss_ok, NULL, cl, 1) == PRIV_E_ARG, "list too long");
    priv_claim_t two[2] = {cl[0], {{9}, "z", "9"}};
    CHECK(priv_sd_check(G, 3, sig, 32, iss_ok, NULL, two, 2) == PRIV_E_CLAIM,
          "the second claim shown is not listed");
    two[1] = cl[2];
    two[1].name = NULL;
    CHECK(priv_sd_check(G, 3, sig, 32, iss_ok, NULL, two, 2) == PRIV_E_ARG, "a malformed claim");
    /* a listed digest one byte away from the shown claim's does not match */
    const unsigned at[] = {0, 5, 31};
    for (unsigned k = 0; k < 3; k++) {
        uint8_t one[1][32];
        memcpy(one[0], dg[1], 32);
        one[0][at[k]] ^= 0x40;
        const uint8_t (*O)[32] = (const uint8_t (*)[32]) one;
        sign(O, 1, sig);
        CHECK(priv_sd_check(O, 1, sig, 32, iss_ok, NULL, &cl[1], 1) == PRIV_E_CLAIM,
              "differs in byte %u", at[k]);
    }
}

int main(void)
{
    test_bernoulli();
    test_dlap();
    test_budget();
    test_access();
    test_sd();
    test_edges();
    test_access_edges();
    test_sd_edges();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
