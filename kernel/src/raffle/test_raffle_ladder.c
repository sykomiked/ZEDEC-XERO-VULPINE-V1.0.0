/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_raffle_ladder.c — host tests for raffle_ladder.h (ASan/UBSan). */
#define _POSIX_C_SOURCE 199309L
#include "raffle_ladder.h"
#include "keccak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned g_pass, g_fail;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define N   144u
#define PER 4096u
static raffle_t g_lv[N];
static raffle_range_t g_rg[N * PER];
#define TPER 256u
static raffle_tally_t g_tl[N * TPER];

static void ident(const char *tag, uint32_t i, uint8_t out[32])
{
    uint8_t b[64];
    size_t n = strlen(tag);
    memcpy(b, tag, n);
    memcpy(b + n, &i, 4);
    sha3_256(b, n + 4, out);
}

static void secret_of(uint32_t level, uint64_t epoch, uint8_t out[32])
{
    uint8_t b[12];
    memcpy(b, &level, 4);
    memcpy(b + 4, &epoch, 8);
    sha3_256(b, sizeof b, out);
}

static void commit_level(raffle_t *r, uint32_t k)
{
    uint8_t p[32], s[32], cm[32];
    ident("party", 0, p);
    secret_of(k, r->epoch, s);
    raffle_commitment(r->epoch, p, s, cm);
    raffle_commit(r, p, cm);
}

/* Reveal, draw, post and reopen every closed level. Returns pool settled. */
static uint64_t settle_closed(raffle_ladder_t *L, uint8_t digest[32])
{
    uint64_t settled = 0;
    for (uint32_t k = 0; k < L->n; k++) {
        raffle_t *r = &L->lv[k];
        if (r->phase != RAFFLE_CLOSED) continue;
        uint8_t p[32], s[32];
        ident("party", 0, p);
        secret_of(k, r->epoch, s);
        raffle_settlement_t st;
        if (raffle_reveal(r, p, s) != RAFFLE_OK || raffle_draw(r, &st) != RAFFLE_OK ||
            raffle_settle(r, &st) != RAFFLE_OK || raffle_next_epoch(r) != RAFFLE_OK) {
            g_fail++;
            printf("[FAIL] settle level %u\n", k);
            continue;
        }
        if (st.reserve_skim + st.grant_pool + st.crown_pot != st.pool) g_fail++;
        settled += st.pool;
        uint8_t b[64];
        memcpy(b, digest, 32);
        memcpy(b + 32, st.link, 32);
        sha3_256(b, 64, digest);
        commit_level(r, k);
    }
    return settled;
}

static raffle_config_t base_cfg(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    c.share_den = 0;
    c.min_rate.num = 987; /* this test deployment's floor: 987/61000 */
    c.min_rate.den = 61000;
    return c;
}

static void test_thresholds(void)
{
    uint64_t prev = 0;
    unsigned bad = 0;
    for (uint32_t j = 0; j < N; j++) {
        uint64_t t = raffle_ladder_threshold(j, N, 13, 40);
        if (t <= prev) bad++;
        prev = t;
    }
    CHECK(bad == 0, "144 thresholds strictly increase");
    CHECK(raffle_ladder_threshold(0, N, 13, 40) == 233, "lowest = F(13) = 233");
    CHECK(raffle_ladder_threshold(N - 1, N, 13, 40) == 102334155u, "apex = F(40)");
    CHECK(raffle_ladder_threshold(0, 3, 13, 22) == 233 &&
              raffle_ladder_threshold(2, 3, 13, 22) == 17711,
          "3 tiers: Sprint F(13), Sovereign F(22)");
    /* level j sits between F(i) and F(i+1) of its curve position */
    for (uint32_t j = 0; j < N; j++) {
        uint64_t q = (uint64_t) j * 27 / 143, r = (uint64_t) j * 27 % 143;
        uint64_t lo = raffle_fib(13 + (uint32_t) q), hi = raffle_fib(14 + (uint32_t) q);
        uint64_t want = lo + (hi - lo) * r / 143;
        if (raffle_ladder_threshold(j, N, 13, 40) != want) bad++;
    }
    CHECK(bad == 0, "threshold formula K2");
    CHECK(raffle_ladder_threshold(0, 0, 13, 40) == 0 &&
              raffle_ladder_threshold(144, N, 13, 40) == 0,
          "bad level");
    CHECK(raffle_ladder_threshold(0, 145, 13, 40) == 0, "more than 144 levels");
    CHECK(raffle_ladder_threshold(0, N, 13, 81) == 0 && raffle_ladder_threshold(0, N, 20, 19) == 0,
          "bad index range");
    CHECK(raffle_ladder_threshold(0, 1, 18, 18) == 2584, "one level");
}

static void test_split_cascade(void)
{
    raffle_config_t c = base_cfg();
    raffle_ladder_t L;
    raffle_ladder_receipt_t rc;
    uint8_t who[32];
    ident("owner", 1, who);

    CHECK(raffle_ladder_init(&L, &c, N, 13, 40, g_lv, g_rg, PER, g_tl, TPER, NULL) == RAFFLE_OK,
          "init 144");
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_ERR_PHASE,
          "no level has parties: refused");
    for (uint32_t k = 0; k < N; k++) commit_level(&L.lv[k], k);

    /* Equal weights: fee 16180 -> 112 per level (16128), 52 remainder to the apex. */
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_OK, "enter");
    uint64_t sum = 0;
    unsigned bad = 0;
    for (uint32_t k = 0; k < N; k++) {
        sum += rc.share[k];
        if (k < N - 1 && rc.share[k] != 112) bad++;
        if (L.lv[k].pool != rc.share[k] || L.lv[k].tickets != rc.share[k]) bad++;
    }
    CHECK(rc.fee == 16180 && sum == 16180, "shares sum to the fee");
    CHECK(bad == 0 && rc.share[N - 1] == 112 + 52, "112 each, remainder 52 to the apex");
    CHECK(rc.cascaded == 0 && rc.closed == 0, "nothing cascaded");

    /* The 3rd payment (336 tickets each) closes every level whose threshold is
     * <= 336: levels 0-3 (233, 260, 287, 314). Then their shares cascade. */
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_OK && rc.closed == 0,
          "2nd");
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_OK && rc.closed == 4,
          "3rd closes levels 0-3");
    CHECK(L.lv[0].phase == RAFFLE_CLOSED && L.lv[3].phase == RAFFLE_CLOSED &&
              L.lv[4].phase == RAFFLE_OPEN && L.lv[4].threshold == 341,
          "level 4 (341) still open");
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_OK, "4th");
    CHECK(rc.share[0] == 0 && rc.share[3] == 0 && rc.share[4] == 5 * 112 && rc.cascaded == 4 * 112,
          "closed levels 0-3 cascade 448 into level 4");

    /* A level with no parties in the middle passes its share up too. */
    raffle_t *m = &L.lv[70];
    raffle_config_t mc = m->cfg;
    raffle_range_t *mr = m->ranges;
    raffle_init(m, &mc, 1, NULL, mr, PER); /* fresh, no parties */
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_OK, "5th");
    CHECK(rc.share[70] == 0 && rc.share[71] == 224, "unready level 70 cascades to 71");
    commit_level(m, 70);

    /* Player-card override: 55 / 21 / 13 on three levels, rest zero. */
    uint64_t w[N];
    memset(w, 0, sizeof w);
    w[10] = 55;
    w[50] = 21;
    w[143] = 13;
    raffle_ladder_receipt_t plan;
    CHECK(raffle_ladder_plan(&L, who, 1000000, NULL, w, &plan) == RAFFLE_OK, "override plan");
    CHECK(plan.share[10] == 16180 * 55 / 89 && plan.share[50] == 16180 * 21 / 89 &&
              plan.share[143] == 16180 - 16180 * 55 / 89 - 16180 * 21 / 89,
          "55/89, 21/89, rest to 13/89 + remainder (%llu %llu %llu)",
          (unsigned long long) plan.share[10], (unsigned long long) plan.share[50],
          (unsigned long long) plan.share[143]);
    memset(w, 0, sizeof w);
    CHECK(raffle_ladder_plan(&L, who, 1000000, NULL, w, &plan) == RAFFLE_ERR_CONFIG,
          "all-zero weights");
    CHECK(raffle_ladder_plan(&L, who, 61, NULL, NULL, &plan) == RAFFLE_ERR_DUST, "zero fee");
    raffle_rate_t r60 = {3, 5};
    CHECK(raffle_ladder_plan(&L, who, 1000, &r60, NULL, &plan) == RAFFLE_ERR_RATE, "60%% refused");
    /* Small fee: every level's floor share is 0, the whole fee goes to the apex. */
    CHECK(raffle_ladder_plan(&L, who, 6200, NULL, NULL, &plan) == RAFFLE_OK &&
              plan.share[N - 1] == plan.fee && plan.fee == 100,
          "fee below 144 lands on the apex");

    /* All or nothing: a full range table on one level refuses the payment. */
    raffle_t *f = &L.lv[100];
    uint32_t saved = f->range_cap;
    f->range_cap = f->range_len;
    static raffle_t snap[N];
    memcpy(snap, g_lv, sizeof snap);
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_ERR_FULL,
          "full refused");
    CHECK(memcmp(snap, g_lv, sizeof snap) == 0, "refused payment changed no level");
    f->range_cap = saved;

    /* L1: one level switched off disables the ladder. */
    L.lv[5].cfg.jurisdiction_permits = false;
    CHECK(raffle_ladder_enter(&L, who, 1000000, NULL, NULL, &rc) == RAFFLE_ERR_DISABLED,
          "a level switched off refuses");
    L.lv[5].cfg.jurisdiction_permits = true;

    uint64_t w2[N];
    for (uint32_t k = 0; k < N; k++) w2[k] = 0;
    CHECK(raffle_ladder_init(&L, &c, N, 13, 40, g_lv, g_rg, PER, NULL, 0, w2) == RAFFLE_ERR_CONFIG,
          "init all-zero weights");
    CHECK(raffle_ladder_init(&L, &c, 145, 13, 40, g_lv, g_rg, PER, NULL, 0, NULL) ==
              RAFFLE_ERR_CONFIG,
          "init 145");
}

/* Random payments with settles; returns a digest of every settlement. */
static void run_random(uint32_t payments, uint8_t digest[32], double *ns_per)
{
    raffle_config_t c = base_cfg();
    c.share_num = 8;
    c.share_den = 21;
    raffle_ladder_t L;
    raffle_ladder_init(&L, &c, N, 13, 40, g_lv, g_rg, PER, g_tl, TPER, NULL);
    for (uint32_t k = 0; k < N; k++) commit_level(&L.lv[k], k);
    memset(digest, 0, 32);
    uint64_t seed = 0x5eed, settled = 0, refused = 0;
    struct timespec t0, t1;
    double ns = 0;
    for (uint32_t i = 0; i < payments; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        uint8_t who[32];
        ident("owner", (uint32_t) (seed % 64), who);
        uint64_t amount = 62 + (seed >> 20) % 2000000;
        raffle_rate_t m = {987 * (1 + (seed >> 8) % 13), 61000};
        uint64_t w[N], *wp = NULL;
        if (seed % 5 == 0) { /* a player-card override now and then */
            for (uint32_t k = 0; k < N; k++) w[k] = (k * 7 + seed) % 4;
            w[N - 1] = 1;
            wp = w;
        }
        raffle_ladder_receipt_t rc;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        raffle_status_t st = raffle_ladder_enter(&L, who, amount, &m, wp, &rc);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ns += (double) (t1.tv_sec - t0.tv_sec) * 1e9 + (double) (t1.tv_nsec - t0.tv_nsec);
        if (st != RAFFLE_OK) {
            refused++;
            continue;
        }
        if (rc.closed) settled += settle_closed(&L, digest);
    }
    uint64_t open = 0;
    for (uint32_t k = 0; k < N; k++) open += L.lv[k].pool;
    CHECK(settled + open == L.fees, "conservation: settled %llu + open %llu == fees %llu",
          (unsigned long long) settled, (unsigned long long) open, (unsigned long long) L.fees);
    CHECK(L.payments + refused == payments && L.payments > payments / 2,
          "%llu accepted, %llu refused (share cap)", (unsigned long long) L.payments,
          (unsigned long long) refused);
    CHECK(L.lv[0].epoch > 10, "the lowest level settled many times (%llu)",
          (unsigned long long) L.lv[0].epoch);
    if (ns_per) *ns_per = ns / payments;
}

int main(void)
{
    test_thresholds();
    test_split_cascade();
    uint8_t d1[32], d2[32];
    double ns;
    run_random(2000, d1, &ns);
    run_random(2000, d2, NULL);
    CHECK(memcmp(d1, d2, 32) == 0, "two runs settle identically");
    printf("cost: %.0f ns per payment across 144 levels (this build)\n", ns);
    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
