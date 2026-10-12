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

int main(void)
{
    test_bernoulli();
    test_dlap();
    test_budget();
    test_access();
    test_sd();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
