/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_raffle_savings.c — host tests for the savings mode (M1), the rebate
 * (S6) and the luck weight (W1-W3, G7) (ASan/UBSan). */
#include "raffle.h"
#include "raffle_guard.h"
#include "raffle_ladder.h"
#include <stdio.h>
#include <string.h>

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

static uint64_t xorshift(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static void id_of(uint8_t tag, uint8_t out[32])
{
    memset(out, tag, 32);
}

static void commit_party(raffle_t *r, uint8_t tag)
{
    uint8_t p[32], s[32], cm[32];
    id_of(tag, p);
    id_of((uint8_t) (tag ^ 0x5A), s);
    s[0] = (uint8_t) r->epoch;
    raffle_commitment(r->epoch, p, s, cm);
    raffle_commit(r, p, cm);
}

static void reveal_party(raffle_t *r, uint8_t tag)
{
    uint8_t p[32], s[32];
    id_of(tag, p);
    id_of((uint8_t) (tag ^ 0x5A), s);
    s[0] = (uint8_t) r->epoch;
    raffle_reveal(r, p, s);
}

#define MAXLEGS 64
typedef struct {
    unsigned n;
    raffle_dest_t dest[MAXLEGS];
    uint8_t who[MAXLEGS];
    uint64_t amount[MAXLEGS];
} legs_t;

static bool record(void *ctx, raffle_dest_t d, const uint8_t *who, uint64_t amount)
{
    legs_t *l = ctx;
    if (l->n >= MAXLEGS) return false;
    l->dest[l->n] = d;
    l->who[l->n] = who ? who[0] : 0;
    l->amount[l->n++] = amount;
    return true;
}

static bool refuse_rebates(void *ctx, raffle_dest_t d, const uint8_t *who, uint64_t amount)
{
    (void) ctx;
    (void) who;
    (void) amount;
    return d != RAFFLE_DEST_REBATE;
}

/* G7 test hook: accounts 0x10..0x1F belong to person 0x01, others to
 * themselves; account 0xEE has no verified identity. */
static bool identity(void *ctx, const uint8_t owner[RAFFLE_ID_LEN], uint8_t out[RAFFLE_ID_LEN])
{
    (void) ctx;
    if (owner[0] == 0xEE) return false;
    id_of((owner[0] & 0xF0) == 0x10 ? 0x01 : owner[0], out);
    return true;
}

static raffle_range_t g_rg[4096];

static void test_defaults_and_mode(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    CHECK(c.mode == RAFFLE_MODE_SAVINGS && c.rebate_num == 1 && c.rebate_den == 5 &&
              c.luck_cap == RAFFLE_LUCK_DEFAULT && !c.jurisdiction_permits,
          "defaults: savings mode, 1/5 rebate, luck cap 5, switch off");
    CHECK(raffle_config_check(&c) == RAFFLE_OK, "defaults valid");
    raffle_config_t b = c;
    b.mode = 2;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "unknown mode");
    b = c;
    b.luck_cap = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "luck cap 0");
    b.luck_cap = RAFFLE_LUCK_CEIL + 1;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "luck cap above 8");
    b.luck_cap = RAFFLE_LUCK_CEIL;
    CHECK(raffle_config_check(&b) == RAFFLE_OK, "luck cap 8");
    b = c;
    b.rebate_num = 4;
    b.rebate_den = 5; /* 1.6% + 9.1% + 80% < 1 */
    CHECK(raffle_config_check(&b) == RAFFLE_OK, "80%% rebate fits");
    b.rebate_num = 9;
    b.rebate_den = 10; /* 1.6% + 9.1% + 90% > 1 */
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "the rebate cannot eat the skim");
    b.rebate_num = 5;
    b.rebate_den = 5;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "rebate below 1");

    raffle_t r;
    uint8_t a[32];
    id_of(0x21, a);
    raffle_receipt_t rc;
    raffle_init(&r, &c, 1, NULL, g_rg, 64);
    commit_party(&r, 0xA0);
    CHECK(raffle_enter_yield(&r, a, 1000, (raffle_rate_t){1, 1}, NULL, &rc) == RAFFLE_ERR_DISABLED,
          "savings mode still needs the switch");
    c.jurisdiction_permits = true;
    c.share_den = 0;
    raffle_init(&r, &c, 1, NULL, g_rg, 64);
    commit_party(&r, 0xA0);
    CHECK(raffle_enter(&r, a, 100000, NULL, &rc) == RAFFLE_ERR_MODE,
          "savings mode refuses a paid entry");
    CHECK(raffle_enter_yield(&r, a, 1000, (raffle_rate_t){0, 1}, NULL, &rc) == RAFFLE_ERR_RATE,
          "share 0");
    CHECK(raffle_enter_yield(&r, a, 1000, (raffle_rate_t){3, 2}, NULL, &rc) == RAFFLE_ERR_RATE,
          "share above 1 (more than the profit)");
    CHECK(raffle_enter_yield(&r, a, 1000, (raffle_rate_t){1, 0}, NULL, &rc) == RAFFLE_ERR_RATE,
          "share den 0");
    CHECK(raffle_enter_yield(&r, a, 3, (raffle_rate_t){1, 4}, NULL, &rc) == RAFFLE_ERR_DUST,
          "no profit, no ticket");
    CHECK(raffle_enter_yield(&r, a, 1003, (raffle_rate_t){1, 4}, NULL, &rc) == RAFFLE_OK &&
              rc.fee == 250 && rc.count == 250 && r.pool == 250,
          "a quarter of 1003 profit: 250");
    CHECK(raffle_enter_yield(&r, a, UINT64_MAX, (raffle_rate_t){1, 1}, NULL, &rc) ==
              RAFFLE_ERR_OVERFLOW,
          "pool overflow refused");

    /* Paid mode still works when the deployment picks it. */
    c.mode = RAFFLE_MODE_PAID;
    raffle_init(&r, &c, 1, NULL, g_rg, 64);
    commit_party(&r, 0xA0);
    CHECK(raffle_enter(&r, a, 100000, NULL, &rc) == RAFFLE_OK && rc.fee == 1000,
          "paid mode: 1%% of 100,000");
    CHECK(raffle_enter_yield(&r, a, 1000, (raffle_rate_t){1, 1}, NULL, &rc) == RAFFLE_OK,
          "savings entries allowed in paid mode too");

    /* Guard: the paid check refuses in savings mode; the fee check does not. */
    raffle_guard_policy_t pol;
    raffle_guard_policy_default(&pol);
    pol.age_gate = NULL;
    raffle_player_t pl;
    raffle_player_init(&pl, a);
    c.mode = RAFFLE_MODE_SAVINGS;
    pl.rate = (raffle_rate_t){1, 4};
    raffle_player_set_limits(&pol, &pl, 10000, 10000, 0);
    uint64_t fee = 0;
    CHECK(raffle_guard_check(&pol, &c, &pl, 1000, 0, 1, 0, &fee) == RAFFLE_ERR_AGE,
          "gates come first");
}

static bool adult(void *ctx, const uint8_t owner[RAFFLE_ID_LEN])
{
    (void) ctx;
    (void) owner;
    return true;
}

static void test_guard_mode(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    raffle_guard_policy_t pol;
    raffle_guard_policy_default(&pol);
    pol.age_gate = adult;
    uint8_t a[32];
    id_of(0x22, a);
    raffle_player_t pl;
    raffle_player_init(&pl, a);
    pl.rate = (raffle_rate_t){1, 100};
    raffle_player_set_limits(&pol, &pl, 10000, 10000, 0);
    uint64_t fee = 0;
    CHECK(raffle_guard_check(&pol, &c, &pl, 1000, 0, 1, 0, &fee) == RAFFLE_ERR_MODE,
          "paid guard check refused in savings mode");
    CHECK(raffle_guard_check_fee(&pol, &c, &pl, 250, 0, 1) == RAFFLE_OK,
          "savings fee check passes");
    CHECK(raffle_guard_check_fee(&pol, &c, &pl, 10001, 0, 1) == RAFFLE_ERR_DAY_LIMIT,
          "limits apply in savings mode");
}

/* S6 on a hand-checked epoch: fees 100 (A), 200 (B), 300 (C), 50 (A
 * again), plus one free ticket (D). */
static void test_rebate_exact(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    c.free_entry = true;
    c.share_den = 0;
    c.threshold = 652; /* closes on the 5th entry */
    raffle_t r;
    raffle_settlement_t s;
    raffle_receipt_t rc;
    uint8_t A[32], B[32], C[32], D[32];
    id_of(0x0A, A);
    id_of(0x0B, B);
    id_of(0x0C, C);
    id_of(0x0D, D);
    const raffle_rate_t one = {1, 1};
    unsigned seen_winner[4] = {0, 0, 0, 0}, bad = 0;
    for (uint64_t e = 1; e <= 60; e++) {
        raffle_init(&r, &c, e, NULL, g_rg, 64);
        commit_party(&r, 0xA0);
        raffle_enter_yield(&r, A, 100, one, NULL, &rc);
        raffle_enter_yield(&r, B, 200, one, NULL, &rc);
        raffle_enter_yield(&r, C, 300, one, NULL, &rc);
        raffle_free_entry(&r, D, &rc);
        raffle_enter_yield(&r, A, 52, one, NULL, &rc);
        if (r.phase != RAFFLE_CLOSED) {
            bad++;
            continue;
        }
        reveal_party(&r, 0xA0);
        if (raffle_draw(&r, &s) != RAFFLE_OK) {
            bad++;
            continue;
        }
        /* pool 652: skim 10, grant 59, rebate 130 */
        uint64_t fa = 152, fb = 200, fc = 300, want = 0, wa = 0, wb = 0, wc = 0;
        switch (s.winner[0]) {
        case 0x0A:
            wb = 130 * fb / 500, wc = 130 * fc / 500;
            seen_winner[0]++;
            break;
        case 0x0B:
            wa = 130 * 100 / 452 + 130 * 52 / 452, wc = 130 * fc / 452;
            seen_winner[1]++;
            break;
        case 0x0C:
            wa = 130 * 100 / 352 + 130 * 52 / 352, wb = 130 * fb / 352;
            seen_winner[2]++;
            break;
        default: /* D's one free ticket */
            wa = 130 * 100 / 652 + 130 * 52 / 652, wb = 130 * fb / 652, wc = 130 * fc / 652;
            seen_winner[3]++;
        }
        (void) fa;
        want = wa + wb + wc;
        if (s.pool != 652 || s.reserve_skim != 10 || s.grant_pool != 59 || s.rebate_pool != 130 ||
            s.rebate_paid != want || s.crown_pot != 652 - 10 - 59 - want)
            bad++;
        legs_t l;
        memset(&l, 0, sizeof l);
        if (!raffle_post(&r, &s, record, &l)) bad++;
        uint64_t got[256] = {0}, sum = 0;
        for (unsigned i = 0; i < l.n; i++) {
            sum += l.amount[i];
            if (l.dest[i] == RAFFLE_DEST_REBATE) got[l.who[i]] += l.amount[i];
        }
        if (sum != 652 || got[0x0A] != wa || got[0x0B] != wb || got[0x0C] != wc || got[0x0D] ||
            l.dest[0] != RAFFLE_DEST_RESERVE_FLOOR || l.dest[l.n - 1] != RAFFLE_DEST_WINNER)
            bad++;
        if (got[s.winner[0]]) bad++; /* the winner gets no rebate */
    }
    CHECK(bad == 0, "hand-checked rebates over 60 epochs (%u bad)", bad);
    CHECK(seen_winner[0] && seen_winner[1] && seen_winner[2],
          "each paying owner won some epoch (%u %u %u %u)", seen_winner[0], seen_winner[1],
          seen_winner[2], seen_winner[3]);

    /* Post refuses a record that does not match, and a refused rebate. */
    raffle_settlement_t z = s;
    z.rebate_paid++;
    legs_t l;
    memset(&l, 0, sizeof l);
    CHECK(!raffle_post(&r, &z, record, &l) && l.n == 0, "mismatched rebate total refused");
    z = s;
    z.epoch++;
    CHECK(!raffle_post(&r, &z, record, &l), "wrong epoch refused");
    if (s.rebate_paid)
        CHECK(!raffle_post(&r, &s, refuse_rebates, NULL), "a refused rebate stops the posting");

    /* One payer: the whole rebate stays in the crown pot. */
    raffle_init(&r, &c, 99, NULL, g_rg, 64);
    commit_party(&r, 0xA0);
    raffle_enter_yield(&r, A, 700, one, NULL, &rc);
    reveal_party(&r, 0xA0);
    CHECK(raffle_draw(&r, &s) == RAFFLE_OK && s.rebate_pool == 140 && s.rebate_paid == 0 &&
              s.crown_pot == 700 - 11 - 63,
          "sole payer: no rebate, crown keeps it (%llu)", (unsigned long long) s.crown_pot);
}

/* S6 conservation with random entries and ratios. */
static void test_rebate_random(void)
{
    uint64_t seed = 99;
    unsigned bad = 0, ran = 0;
    for (int i = 0; i < 400; i++) {
        raffle_config_t c;
        raffle_config_default(&c);
        c.jurisdiction_permits = true;
        c.share_den = 0;
        c.rebate_num = 1 + xorshift(&seed) % 70;
        c.rebate_den = 100;
        c.fib_index = 20 + (uint32_t) (xorshift(&seed) % 10);
        raffle_t r;
        raffle_settlement_t s;
        if (raffle_init(&r, &c, 1, NULL, g_rg, 4096) != RAFFLE_OK) {
            bad++;
            continue;
        }
        commit_party(&r, 0xA0);
        while (r.phase == RAFFLE_OPEN) {
            uint8_t o[32];
            id_of((uint8_t) (1 + xorshift(&seed) % 9), o);
            uint64_t p = 1 + xorshift(&seed) % 5000;
            if (i == 0) p = UINT64_MAX / 4; /* large values */
            if (raffle_enter_yield(&r, o, p, (raffle_rate_t){1, 1}, NULL, NULL) != RAFFLE_OK) break;
        }
        reveal_party(&r, 0xA0);
        if (raffle_draw(&r, &s) != RAFFLE_OK) {
            bad++;
            continue;
        }
        unsigned __int128 wf = 0, paid = 0;
        for (uint32_t k = 0; k < r.range_len; k++)
            if (!memcmp(r.ranges[k].owner, s.winner, 32)) wf += r.ranges[k].fee;
        unsigned __int128 base = s.pool - wf;
        for (uint32_t k = 0; k < r.range_len && base; k++)
            if (memcmp(r.ranges[k].owner, s.winner, 32))
                paid += (unsigned __int128) s.rebate_pool * r.ranges[k].fee / base;
        if (paid != s.rebate_paid || s.rebate_paid > s.rebate_pool) bad++;
        if ((unsigned __int128) s.reserve_skim + s.grant_pool + s.nav_slice + s.rebate_paid +
                s.crown_pot !=
            s.pool)
            bad++;
        if (s.rebate_pool != (uint64_t) ((unsigned __int128) s.pool * c.rebate_num / 100)) bad++;
        legs_t l;
        memset(&l, 0, sizeof l);
        raffle_post(&r, &s, record, &l); /* may exceed MAXLEGS: then it stops early */
        ran++;
    }
    CHECK(bad == 0, "random rebates conserve the pool (%u bad)", bad);
    CHECK(ran == 400, "ran %u", ran);
}

static void test_luck(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    raffle_rate_t m = raffle_luck_mult(&c, 0);
    CHECK(m.num == 1 && m.den == 1, "no misses: 1x");
    m = raffle_luck_mult(&c, 1);
    CHECK(m.num == 10946 + 6765 && m.den == 10946, "one miss: 1 + 6765/10946");
    m = raffle_luck_mult(&c, 6);
    CHECK(m.num == 10946 + 6 * 6765 && m.den == 10946, "six misses: about 4.71x");
    m = raffle_luck_mult(&c, 7);
    CHECK(m.num == 5 && m.den == 1, "seven misses: capped at 5x");
    m = raffle_luck_mult(&c, UINT32_MAX);
    CHECK(m.num == 5 && m.den == 1, "huge miss count: still 5x");
    c.luck_cap = 8;
    m = raffle_luck_mult(&c, 11);
    CHECK(m.num == 10946 + 11 * 6765, "cap 8: eleven misses about 7.8x");
    m = raffle_luck_mult(&c, 12);
    CHECK(m.num == 8 && m.den == 1, "cap 8: twelve misses 8x");
    c.luck_cap = 1;
    m = raffle_luck_mult(&c, 100);
    CHECK(m.num == 1 && m.den == 1, "cap 1: never weighted");

    raffle_config_default(&c);
    CHECK(raffle_luck_tickets(&c, 1000, (raffle_rate_t){17711, 10946}) == 1618,
          "floor(1000 * 17711 / 10946) = 1618");
    CHECK(raffle_luck_tickets(&c, 1000, (raffle_rate_t){1, 2}) == 0, "weight below 1 refused");
    CHECK(raffle_luck_tickets(&c, 1000, (raffle_rate_t){11, 2}) == 0, "weight above cap refused");
    CHECK(raffle_luck_tickets(&c, 1000, (raffle_rate_t){5, 0}) == 0, "den 0 refused");
    CHECK(raffle_luck_tickets(&c, UINT64_MAX, (raffle_rate_t){2, 1}) == 0, "overflow refused");

    c.jurisdiction_permits = true;
    c.share_den = 0;
    raffle_t r;
    raffle_receipt_t rc;
    uint8_t a[32];
    id_of(0x31, a);
    raffle_init(&r, &c, 1, NULL, g_rg, 64);
    commit_party(&r, 0xA0);
    raffle_rate_t two = {2, 1}, six = {6, 1};
    CHECK(raffle_enter_yield(&r, a, 400, (raffle_rate_t){1, 2}, &two, &rc) == RAFFLE_OK &&
              rc.fee == 200 && rc.count == 400 && r.pool == 200 && r.tickets == 400,
          "2x weight: 200 fee, 400 tickets");
    CHECK(raffle_enter_yield(&r, a, 400, (raffle_rate_t){1, 2}, &six, &rc) == RAFFLE_ERR_RATE,
          "weight above the cap refused");
    CHECK(raffle_enter_weighted(&r, a, 0, NULL, 10, 9, &rc) == RAFFLE_ERR_ARG,
          "fewer tickets than fee refused");
    CHECK(raffle_enter_weighted(&r, a, 0, NULL, 10, 51, &rc) == RAFFLE_ERR_ARG,
          "more than cap x fee refused");
    CHECK(raffle_enter_weighted(&r, a, 0, NULL, 10, 50, &rc) == RAFFLE_OK && rc.count == 50,
          "exactly cap x fee");
    CHECK(raffle_enter_weighted(&r, a, 0, NULL, 0, 0, &rc) == RAFFLE_ERR_DUST, "zero fee");

    /* G7: by verified person, not by account. */
    raffle_guard_policy_t pol;
    raffle_guard_policy_default(&pol);
    raffle_luck_t book[3];
    id_of(0x01, book[0].identity);
    book[0].misses = 3;
    id_of(0x40, book[1].identity);
    book[1].misses = 0;
    id_of(0xEE, book[2].identity); /* a record that an unverified account must not reach */
    book[2].misses = 5;
    uint8_t acc1[32], acc2[32], other[32], anon[32];
    id_of(0x11, acc1);
    id_of(0x1F, acc2);
    id_of(0x40, other);
    id_of(0xEE, anon);
    raffle_config_default(&c);
    m = raffle_guard_luck(&pol, &c, acc1, book, 3);
    CHECK(m.num == 1 && m.den == 1, "no identity hook: 1x");
    pol.identity_of = identity;
    raffle_rate_t m1 = raffle_guard_luck(&pol, &c, acc1, book, 3),
                  m2 = raffle_guard_luck(&pol, &c, acc2, book, 3);
    CHECK(m1.num == 10946 + 3 * 6765 && m1.num == m2.num && m1.den == m2.den,
          "two accounts of one person share one weight");
    m = raffle_guard_luck(&pol, &c, other, book, 3);
    CHECK(m.num == 1 && m.den == 1, "another person with no misses: 1x");
    m = raffle_guard_luck(&pol, &c, anon, book, 3);
    CHECK(m.num == 1 && m.den == 1, "unverified account: 1x");
    m = raffle_guard_luck(&pol, &c, other, book, 1);
    CHECK(m.num == 1 && m.den == 1, "no record: 1x");
    raffle_luck_note(&book[0], false);
    CHECK(book[0].misses == 4, "a miss counts");
    raffle_luck_note(&book[0], true);
    CHECK(book[0].misses == 0, "a win resets");
    book[0].misses = UINT32_MAX;
    raffle_luck_note(&book[0], false);
    CHECK(book[0].misses == UINT32_MAX, "saturates");
}

#define LN 3u
static raffle_t g_lv[LN];
static raffle_range_t g_lrg[LN * 64];

static void test_ladder(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    c.share_den = 0; /* F6 off until the cap test below */
    raffle_ladder_t L;
    raffle_ladder_receipt_t lr;
    uint8_t a[32];
    id_of(0x51, a);
    CHECK(raffle_ladder_init(&L, &c, LN, RAFFLE_LADDER_IDX_LO, RAFFLE_LADDER_IDX_HI, g_lv, g_lrg,
                             64, NULL, 0, NULL) == RAFFLE_OK &&
              L.luck_levels == RAFFLE_LADDER_LUCK_LEVELS,
          "ladder, luck on the lowest 24 levels by default");
    for (uint32_t k = 0; k < LN; k++) commit_party(&g_lv[k], (uint8_t) (0xB0 + k));
    raffle_rate_t pct = {1, 100}, share = {1, 1}, two = {2, 1};
    CHECK(raffle_ladder_enter(&L, a, 100000, &pct, NULL, &lr) == RAFFLE_ERR_MODE,
          "savings ladder refuses a paid entry");
    CHECK(raffle_ladder_enter_span(&L, a, 100000, &pct, 0, NULL, NULL, 0, LN - 1, &lr) ==
              RAFFLE_ERR_MODE,
          "and a quoted span entry");
    CHECK(raffle_ladder_enter_span(&L, a, 90, &share, 90, NULL, NULL, 0, LN - 1, &lr) ==
                  RAFFLE_OK &&
              lr.share[0] == 30 && lr.tickets[0] == 30 && lr.tickets[2] == 30,
          "a savings tick spread over the ladder");
    L.luck_levels = 1;
    CHECK(raffle_ladder_enter_span(&L, a, 90, &share, 90, &two, NULL, 0, LN - 1, &lr) ==
                  RAFFLE_OK &&
              lr.share[0] == 30 && lr.tickets[0] == 60 && lr.tickets[1] == 30 &&
              lr.tickets[2] == 30 && g_lv[0].tickets == 90 && g_lv[0].pool == 60,
          "luck applies only below luck_levels");
    raffle_rate_t bad = {9, 1};
    CHECK(raffle_ladder_enter_span(&L, a, 90, &share, 90, &bad, NULL, 0, LN - 1, &lr) ==
              RAFFLE_ERR_RATE,
          "weight above the cap refused on the ladder");

    /* The share cap counts tickets: with 2x luck, level 0 takes half the
     * fee units it would otherwise. */
    raffle_config_t cc = c;
    cc.share_num = 8;
    cc.share_den = 21; /* level 0: F(13) = 233 -> cap 88 tickets */
    raffle_ladder_init(&L, &cc, LN, RAFFLE_LADDER_IDX_LO, RAFFLE_LADDER_IDX_HI, g_lv, g_lrg, 64,
                       NULL, 0, NULL);
    for (uint32_t k = 0; k < LN; k++) commit_party(&g_lv[k], (uint8_t) (0xB0 + k));
    uint64_t w[LN] = {1, 0, 0};
    CHECK(raffle_ladder_enter_span(&L, a, 100, &share, 100, &two, w, 0, LN - 1, &lr) == RAFFLE_OK &&
              lr.share[0] == 44 && lr.tickets[0] == 88 && lr.share[1] + lr.share[2] == 56,
          "2x luck: 44 fee units fill the 88-ticket cap, the rest moves up (%llu %llu)",
          (unsigned long long) lr.share[0], (unsigned long long) lr.tickets[0]);
}

int main(void)
{
    test_defaults_and_mode();
    test_guard_mode();
    test_rebate_exact();
    test_rebate_random();
    test_luck();
    test_ladder();
    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
