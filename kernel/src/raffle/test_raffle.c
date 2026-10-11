/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_raffle.c — host tests for raffle.h (built with ASan/UBSan).
 *
 * Usage: test_raffle REF   where REF is the output of gen_raffle_ref.py, an
 * independent Python model of the same scenario run by test_replay(). */
#include "raffle.h"
#include "keccak.h"
#include <stdio.h>
#include <stdlib.h>
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

static void ident(const char *tag, uint8_t i, uint8_t out[32])
{
    uint8_t b[64];
    size_t n = strlen(tag);
    memcpy(b, tag, n);
    b[n] = i;
    sha3_256(b, n + 1, out);
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void epoch_secret(uint64_t epoch, uint8_t i, uint8_t out[32])
{
    uint8_t b[6 + 8 + 1];
    memcpy(b, "secret", 6);
    put64(b + 6, epoch);
    b[14] = i;
    sha3_256(b, sizeof b, out);
}

static raffle_config_t enabled_cfg(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    c.jurisdiction_permits = true;
    c.share_den = 0; /* F6 off here; test_cap turns it on */
    return c;
}

static raffle_range_t g_ranges[4096];

/* ===== Config and the jurisdiction switch ===== */
static void test_config(void)
{
    raffle_config_t c;
    raffle_config_default(&c);
    CHECK(!c.jurisdiction_permits && !c.free_entry, "defaults are off");
    CHECK(c.fee_num == 987 && c.fee_den == 61000 && c.skim_num == 987 && c.skim_den == 61000,
          "default ratios");
    CHECK(c.fib_index == 18 && c.min_reveals == 0, "default epoch");
    CHECK(c.max_multiplier == 13 && c.share_num == 8 && c.share_den == 21, "default M and cap");
    CHECK(raffle_config_check(&c) == RAFFLE_OK, "defaults valid");
    CHECK(raffle_config_check(NULL) == RAFFLE_ERR_ARG, "null config");

    raffle_config_t b = c;
    b.fee_den = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "fee den 0");
    b = c;
    b.fee_num = b.fee_den;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "fee >= 100%%");
    b = c;
    b.skim_num = 61000;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "skim >= 100%%");
    b = c;
    b.skim_den = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "skim den 0");
    b = c;
    b.fib_index = RAFFLE_FIB_MIN - 1;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "fib index low");
    b.fib_index = RAFFLE_FIB_MAX + 1;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "fib index high");
    b.fib_index = RAFFLE_FIB_MAX;
    CHECK(raffle_config_check(&b) == RAFFLE_OK, "fib index max ok");
    b = c;
    b.max_multiplier = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "max M 0");
    b.max_multiplier = RAFFLE_MAX_MULT + 1;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "max M too high");
    b = c;
    b.share_num = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "share 0 with cap on");
    b.share_num = 22;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "share > 1");
    b.share_den = 0;
    CHECK(raffle_config_check(&b) == RAFFLE_OK, "cap off");
    b = c;
    b.min_reveals = RAFFLE_MAX_PARTIES + 1;
    CHECK(raffle_config_check(&b) == RAFFLE_ERR_CONFIG, "min_reveals too high");

    /* L1: the default (disabled) raffle refuses every entry and changes nothing. */
    raffle_t r;
    uint8_t who[32];
    ident("owner", 1, who);
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 16) == RAFFLE_OK, "init disabled");
    raffle_t before = r;
    raffle_receipt_t rc;
    CHECK(raffle_enter(&r, who, 1000000000u, 1, &rc) == RAFFLE_ERR_DISABLED, "disabled enter");
    CHECK(raffle_free_entry(&r, who, &rc) == RAFFLE_ERR_DISABLED, "disabled free entry");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "disabled: state untouched");

    /* L2: free entry needs both switches. */
    c.jurisdiction_permits = true;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 16) == RAFFLE_OK, "init");
    CHECK(raffle_free_entry(&r, who, &rc) == RAFFLE_ERR_DISABLED, "free entry off by default");
    c.free_entry = true;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 16) == RAFFLE_OK, "init free");
    CHECK(raffle_free_entry(&r, who, &rc) == RAFFLE_OK, "free entry");
    CHECK(rc.fee == 0 && rc.count == 1 && rc.first_ticket == 0, "one free ticket, no fee");
    CHECK(r.pool == 0 && r.tickets == 1 && r.free_entries == 1 && r.paid_entries == 0,
          "free entry counted");

    CHECK(raffle_init(NULL, &c, 1, NULL, g_ranges, 16) == RAFFLE_ERR_ARG, "init null");
    CHECK(raffle_init(&r, &c, 1, NULL, NULL, 16) == RAFFLE_ERR_ARG, "init no storage");
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 0) == RAFFLE_ERR_ARG, "init cap 0");
}

/* ===== Fibonacci ===== */
static void test_fib(void)
{
    CHECK(raffle_fib(0) == 0 && raffle_fib(94) == 0, "fib out of range");
    CHECK(raffle_fib(1) == 1 && raffle_fib(2) == 1 && raffle_fib(3) == 2, "fib small");
    CHECK(raffle_fib(10) == 55, "F10");
    CHECK(raffle_fib(18) == 2584, "F18");
    CHECK(raffle_fib(20) == 6765, "F20");
    CHECK(raffle_fib(40) == 102334155u, "F40");
    CHECK(raffle_fib(93) == 12200160415121876738ull, "F93");
    for (uint32_t n = 3; n <= 93; n++)
        CHECK(raffle_fib(n) == raffle_fib(n - 1) + raffle_fib(n - 2), "fib recurrence %u", n);
}

/* ===== F2/F3: fee and minting ===== */
static uint64_t xorshift(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void test_fee(void)
{
    raffle_config_t c = enabled_cfg();
    uint64_t fee, n, seed = 0x2026101114u;
    unsigned bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t a;
        if (i < 70000)
            a = (uint64_t) i;
        else if (i < 70100)
            a = UINT64_MAX - (uint64_t) (i - 70000);
        else
            a = xorshift(&seed) >> (i % 64);
        if (raffle_quote(&c, a, 1, &fee, &n) != RAFFLE_OK) {
            bad++;
            continue;
        }
        unsigned __int128 x = (unsigned __int128) a * 987u;
        if (!((unsigned __int128) fee * 61000u <= x && x < ((unsigned __int128) fee + 1) * 61000u))
            bad++;
        if (n != fee) bad++; /* F3: one ticket per fee unit */
    }
    CHECK(bad == 0, "fee is the exact floor and tickets == fee (%u bad)", bad);
    CHECK(raffle_quote(&c, 61, 1, &fee, &n) == RAFFLE_OK && fee == 0 && n == 0, "61 rounds to 0");
    CHECK(raffle_quote(&c, 62, 1, &fee, &n) == RAFFLE_OK && fee == 1 && n == 1, "62 is one ticket");
    CHECK(raffle_quote(&c, 100000, 1, &fee, &n) == RAFFLE_OK && fee == 1618 && n == 1618,
          "1.618%%");

    /* Sequential ids; tickets == pool. */
    raffle_t r;
    c.fib_index = 60; /* large: no close here */
    CHECK(raffle_init(&r, &c, 7, NULL, g_ranges, 4096) == RAFFLE_OK, "init");
    uint64_t next = 0, fees = 0;
    seed = 99;
    for (int k = 0; k < 500; k++) {
        uint8_t who[32];
        ident("owner", (uint8_t) (k % 11), who);
        uint64_t a = 62 + xorshift(&seed) % 5000000;
        raffle_receipt_t rc;
        uint64_t qf, qn;
        raffle_quote(&c, a, 1, &qf, &qn);
        if (raffle_enter(&r, who, a, 1, &rc) != RAFFLE_OK) {
            bad++;
            continue;
        }
        if (rc.first_ticket != next || rc.count != qn || rc.fee != qf || rc.closed_epoch) bad++;
        next += rc.count;
        fees += rc.fee;
    }
    CHECK(bad == 0, "ids sequential, receipts match quotes");
    CHECK(r.tickets == next && r.pool == fees && r.tickets == r.pool,
          "pool holds every fee and tickets == pool (no dust)");
    uint64_t sum = 0;
    for (uint32_t i = 0; i < r.range_len; i++) {
        if (r.ranges[i].first != sum) bad++;
        sum += r.ranges[i].count;
    }
    CHECK(bad == 0 && sum == r.tickets, "ranges contiguous");
    for (uint64_t id = 0; id < r.tickets; id += 37) {
        uint8_t o[32];
        if (!raffle_owner_of(&r, id, o)) {
            bad++;
            continue;
        }
        uint32_t j = 0;
        while (!(r.ranges[j].first <= id && id < r.ranges[j].first + r.ranges[j].count)) j++;
        if (memcmp(o, r.ranges[j].owner, 32) != 0) bad++;
    }
    CHECK(bad == 0, "owner_of matches a linear scan");
    CHECK(!raffle_owner_of(&r, r.tickets, NULL), "no owner past the end");

    /* F4: dust refused, nothing changes. */
    raffle_t before = r;
    uint8_t who[32];
    ident("owner", 1, who);
    raffle_receipt_t rc;
    CHECK(raffle_enter(&r, who, 61, 1, &rc) == RAFFLE_ERR_DUST, "zero fee refused");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "zero fee: state untouched");

    /* F3, the owner's example: Alice 610, Bob 987, pool 1597 = F(17). */
    uint8_t alice[32], bob[32], o[32];
    ident("alice", 0, alice);
    ident("bob", 0, bob);
    uint64_t a610 = 0, a987 = 0;
    for (uint64_t a = 1; !a610 || !a987; a++) {
        raffle_quote(&c, a, 1, &fee, &n);
        if (fee == 610 && !a610) a610 = a;
        if (fee == 987 && !a987) a987 = a;
    }
    c.fib_index = 17;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 8) == RAFFLE_OK, "init F17");
    CHECK(raffle_enter(&r, alice, a610, 1, &rc) == RAFFLE_OK && rc.first_ticket == 0 &&
              rc.count == 610 && !rc.closed_epoch,
          "Alice holds [0, 609]");
    CHECK(raffle_enter(&r, bob, a987, 1, &rc) == RAFFLE_OK && rc.first_ticket == 610 &&
              rc.count == 987 && rc.closed_epoch,
          "Bob holds [610, 1596] and closes F(17)");
    CHECK(r.tickets == 1597 && r.pool == 1597, "1597 tickets, pool 1597");
    CHECK(raffle_owner_of(&r, 0, o) && memcmp(o, alice, 32) == 0, "0 is Alice");
    CHECK(raffle_owner_of(&r, 609, o) && memcmp(o, alice, 32) == 0, "609 is Alice");
    CHECK(raffle_owner_of(&r, 610, o) && memcmp(o, bob, 32) == 0, "610 is Bob");
    CHECK(raffle_owner_of(&r, 1596, o) && memcmp(o, bob, 32) == 0, "1596 is Bob");
    CHECK(!raffle_owner_of(&r, 1597, o), "1597 is nobody");

    /* Range table full: refused, untouched. */
    c.fib_index = 30;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 2) == RAFFLE_OK, "init cap 2");
    CHECK(raffle_enter(&r, who, 100000, 1, &rc) == RAFFLE_OK, "1st");
    CHECK(raffle_enter(&r, who, 100000, 1, &rc) == RAFFLE_OK, "2nd");
    before = r;
    CHECK(raffle_enter(&r, who, 100000, 1, &rc) == RAFFLE_ERR_FULL, "full");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "full: state untouched");

    /* Pool overflow: refused, untouched. */
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 16) == RAFFLE_OK, "init");
    r.pool = UINT64_MAX - 5;
    before = r;
    CHECK(raffle_enter(&r, who, 100000, 1, &rc) == RAFFLE_ERR_OVERFLOW, "pool overflow");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "overflow: state untouched");
    CHECK(raffle_enter(NULL, who, 1, 1, &rc) == RAFFLE_ERR_ARG, "enter null");
    CHECK(raffle_enter(&r, NULL, 1, 1, &rc) == RAFFLE_ERR_ARG, "enter null owner");
}

/* ===== F5/F6: the multiplier and the share cap ===== */
static void test_multiplier_cap(void)
{
    raffle_config_t c = enabled_cfg();
    uint64_t fee, n, seed = 5;
    unsigned bad = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t a = xorshift(&seed) >> (i % 64);
        uint32_t m = 1 + (uint32_t) (xorshift(&seed) % 13);
        raffle_status_t st = raffle_quote(&c, a, m, &fee, &n);
        unsigned __int128 x = (unsigned __int128) a * 987u * m;
        if (x / 61000u > UINT64_MAX) {
            if (st != RAFFLE_ERR_OVERFLOW) bad++;
            continue;
        }
        if (st != RAFFLE_OK || fee != (uint64_t) (x / 61000u) || n != fee) bad++;
    }
    CHECK(bad == 0, "fee = floor(a * 987 * M / 61000) or overflow refused (%u bad)", bad);
    CHECK(raffle_quote(&c, 100000, 10, &fee, &n) == RAFFLE_OK && fee == 16180, "M = 10");
    CHECK(raffle_quote(&c, 100000, 0, &fee, &n) == RAFFLE_ERR_MULTIPLIER, "M = 0 refused");
    CHECK(raffle_quote(&c, 100000, 14, &fee, &n) == RAFFLE_ERR_MULTIPLIER, "M > max refused");
    CHECK(raffle_quote(&c, 7, 13, &fee, &n) == RAFFLE_OK && fee == 1, "M lifts a micro entry");
    c.max_multiplier = RAFFLE_MAX_MULT; /* 987 * 1000 / 61000 > 16: the fee can pass 2^64 */
    CHECK(raffle_quote(&c, UINT64_MAX, 1000, &fee, &n) == RAFFLE_ERR_OVERFLOW, "overflow refused");
    CHECK(raffle_quote(&c, UINT64_MAX / 17, 1000, &fee, &n) == RAFFLE_OK, "just fits");
    c.max_multiplier = 13;

    /* F6: 8/21 of F(18) = 2584 is 984 tickets per owner. */
    c.share_num = 8;
    c.share_den = 21;
    raffle_t r;
    raffle_receipt_t rc;
    uint8_t whale[32], minnow[32];
    ident("whale", 0, whale);
    ident("minnow", 0, minnow);
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 64) == RAFFLE_OK, "init");
    CHECK(raffle_room(&r, whale) == 984, "room 984");
    uint64_t a984 = 0, a985 = 0;
    for (uint64_t a = 1; !a985; a++) {
        raffle_quote(&c, a, 1, &fee, &n);
        if (fee == 984 && !a984) a984 = a;
        if (fee == 985) a985 = a;
    }
    raffle_t before = r;
    CHECK(raffle_enter(&r, whale, a985, 1, &rc) == RAFFLE_ERR_CAP, "985 refused");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "cap: state untouched");
    CHECK(raffle_enter(&r, whale, a984, 1, &rc) == RAFFLE_OK && rc.count == 984, "984 fits");
    CHECK(raffle_room(&r, whale) == 0 && raffle_room(&r, minnow) == 984, "whale full");
    CHECK(raffle_enter(&r, whale, 62, 1, &rc) == RAFFLE_ERR_CAP, "whale cannot add one");
    CHECK(raffle_enter(&r, minnow, 62, 13, &rc) == RAFFLE_OK && rc.count == 13, "minnow at M=13");
    CHECK(raffle_room(&r, minnow) == 971, "room counts every range");
    c.free_entry = true;
    r.cfg.free_entry = true;
    CHECK(raffle_free_entry(&r, whale, &rc) == RAFFLE_ERR_CAP, "free entry capped too");
    CHECK(raffle_free_entry(&r, minnow, &rc) == RAFFLE_OK, "free entry under cap");

    /* Tiny threshold: the cap is at least 1 ticket. */
    c.fib_index = 2;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 4) == RAFFLE_OK, "init F2");
    CHECK(raffle_room(&r, whale) == 1, "cap floor 1");
    c.share_den = 0;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 4) == RAFFLE_OK, "init no cap");
    CHECK(raffle_room(&r, whale) == UINT64_MAX, "cap off");
}

/* ===== E1/E2: the Fibonacci trigger ===== */
static void test_epoch(void)
{
    raffle_config_t c = enabled_cfg();
    uint8_t who[32], p0[32], s0[32], cm[32];
    ident("owner", 3, who);
    ident("party", 0, p0);
    epoch_secret(1, 0, s0);
    raffle_receipt_t rc;
    raffle_t r;

    for (uint32_t idx = 2; idx <= 18; idx++) {
        c.fib_index = idx;
        CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 4096) == RAFFLE_OK, "init");
        CHECK(r.threshold == raffle_fib(idx), "threshold F(%u)", idx);
        uint64_t closes = 0, last_before = 0;
        while (r.phase == RAFFLE_OPEN) {
            last_before = r.tickets;
            /* 2 tickets per entry: overshoot happens on odd thresholds. */
            if (raffle_enter(&r, who, 124, 1, &rc) != RAFFLE_OK) break;
            closes += rc.closed_epoch;
        }
        CHECK(r.phase == RAFFLE_CLOSED && closes == 1, "closes exactly once at F(%u)", idx);
        CHECK(last_before < r.threshold && r.tickets >= r.threshold && r.tickets <= r.threshold + 1,
              "the crossing entry closes it, overshoot kept (F(%u))", idx);
    }

    /* Exactly at the threshold: F(10) = 55 = 5 x 11 tickets. */
    c.fib_index = 10;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 64) == RAFFLE_OK, "init");
    raffle_commitment(1, p0, s0, cm);
    CHECK(raffle_commit(&r, p0, cm) == RAFFLE_OK, "commit before tickets");
    CHECK(raffle_commit(&r, p0, cm) == RAFFLE_ERR_PARTY, "duplicate party");
    uint64_t amt11 = 0;
    for (uint64_t a = 1;; a++) {
        uint64_t f, n;
        raffle_quote(&c, a, 1, &f, &n);
        if (n == 11) {
            amt11 = a;
            break;
        }
    }
    for (int i = 0; i < 4; i++) {
        CHECK(raffle_enter(&r, who, amt11, 1, &rc) == RAFFLE_OK && !rc.closed_epoch, "below F10");
        if (i == 0) {
            uint8_t p1[32];
            ident("party", 1, p1);
            CHECK(raffle_commit(&r, p1, cm) == RAFFLE_ERR_PHASE, "no commit after first ticket");
        }
    }
    CHECK(raffle_reveal(&r, p0, s0) == RAFFLE_ERR_PHASE, "no reveal while open");
    raffle_settlement_t s;
    CHECK(raffle_draw(&r, &s) == RAFFLE_ERR_PHASE, "no draw while open");
    CHECK(raffle_enter(&r, who, amt11, 1, &rc) == RAFFLE_OK && rc.closed_epoch,
          "55th ticket closes");
    CHECK(r.tickets == 55 && r.phase == RAFFLE_CLOSED, "exactly F10");
    raffle_t before = r;
    CHECK(raffle_enter(&r, who, amt11, 1, &rc) == RAFFLE_ERR_PHASE, "closed epoch refuses entries");
    CHECK(memcmp(&before, &r, sizeof r) == 0, "closed: state untouched");
    CHECK(raffle_next_epoch(&r) == RAFFLE_ERR_PHASE, "no next epoch before settle");

    /* F(2) = 1: the first ticket closes. */
    c.fib_index = 2;
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 64) == RAFFLE_OK, "init");
    CHECK(raffle_enter(&r, who, 61805, 1, &rc) == RAFFLE_OK && rc.closed_epoch,
          "F2 closes at once");
}

/* ===== R2/R3: commit-reveal ===== */
static void fill_epoch(raffle_t *r, uint32_t parties, uint64_t epoch)
{
    for (uint32_t i = 0; i < parties; i++) {
        uint8_t p[32], s[32], cm[32];
        ident("party", (uint8_t) i, p);
        epoch_secret(epoch, (uint8_t) i, s);
        raffle_commitment(epoch, p, s, cm);
        raffle_commit(r, p, cm);
    }
    for (uint8_t k = 0; r->phase == RAFFLE_OPEN; k++) {
        uint8_t who[32];
        ident("owner", (uint8_t) (k % 5), who);
        raffle_enter(r, who, 62u * (1u + k % 4), 1, NULL);
    }
}

static void test_commit_reveal(void)
{
    raffle_config_t c = enabled_cfg();
    c.fib_index = 8;
    raffle_t r;
    raffle_settlement_t s;
    uint8_t p[3][32], sec[3][32], bad[32];
    for (uint8_t i = 0; i < 3; i++) {
        ident("party", i, p[i]);
        epoch_secret(1, i, sec[i]);
    }

    /* No parties: no entropy, no draw. */
    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 4096) == RAFFLE_OK, "init");
    fill_epoch(&r, 0, 1);
    CHECK(raffle_draw(&r, &s) == RAFFLE_ERR_REVEALS, "no parties, no draw");

    CHECK(raffle_init(&r, &c, 1, NULL, g_ranges, 4096) == RAFFLE_OK, "init");
    fill_epoch(&r, 3, 1);
    CHECK(r.n_parties == 3 && r.phase == RAFFLE_CLOSED, "3 parties, closed");
    memcpy(bad, sec[0], 32);
    bad[31] ^= 1;
    CHECK(raffle_reveal(&r, p[0], bad) == RAFFLE_ERR_COMMIT, "wrong secret refused");
    uint8_t stranger[32];
    ident("party", 9, stranger);
    CHECK(raffle_reveal(&r, stranger, sec[0]) == RAFFLE_ERR_PARTY, "unknown party");
    CHECK(raffle_reveal(&r, p[1], sec[0]) == RAFFLE_ERR_COMMIT, "secret of another party");
    CHECK(raffle_reveal(&r, p[0], sec[0]) == RAFFLE_OK, "reveal 0");
    CHECK(raffle_reveal(&r, p[0], sec[0]) == RAFFLE_ERR_PARTY, "double reveal");
    CHECK(raffle_reveal(&r, p[1], sec[1]) == RAFFLE_OK, "reveal 1");
    CHECK(raffle_draw(&r, &s) == RAFFLE_ERR_REVEALS, "default needs every party");
    raffle_t two = r;
    two.cfg.min_reveals = 2;
    CHECK(raffle_draw(&two, &s) == RAFFLE_OK, "min_reveals 2 of 3 draws");
    raffle_settlement_t s2of3 = s;
    CHECK(raffle_reveal(&r, p[2], sec[2]) == RAFFLE_OK, "reveal 2");
    CHECK(raffle_draw(&r, &s) == RAFFLE_OK, "all revealed: draw");
    CHECK(memcmp(s.seed, s2of3.seed, 32) != 0, "a withheld reveal changes the seed");

    /* Every party's secret moves the seed. */
    for (uint8_t i = 0; i < 3; i++) {
        raffle_t r2;
        raffle_settlement_t t;
        raffle_init(&r2, &c, 1, NULL, g_ranges + 2048, 2048);
        for (uint8_t j = 0; j < 3; j++) {
            uint8_t sj[32], cm[32];
            epoch_secret(j == i ? 50 : 1, j, sj);
            raffle_commitment(1, p[j], sj, cm);
            raffle_commit(&r2, p[j], cm);
        }
        for (uint8_t k = 0; r2.phase == RAFFLE_OPEN; k++) {
            uint8_t who[32];
            ident("owner", (uint8_t) (k % 5), who);
            raffle_enter(&r2, who, 62u * (1u + k % 4), 1, NULL);
        }
        for (uint8_t j = 0; j < 3; j++) {
            uint8_t sj[32];
            epoch_secret(j == i ? 50 : 1, j, sj);
            raffle_reveal(&r2, p[j], sj);
        }
        CHECK(raffle_draw(&r2, &t) == RAFFLE_OK && memcmp(t.seed, s.seed, 32) != 0,
              "party %u's secret moves the seed", i);
        CHECK(memcmp(t.header_hash, s.header_hash, 32) != 0, "commitments are in the header");
    }
}

/* ===== S1: the split, no rounding loss ===== */
static void test_split(void)
{
    raffle_config_t c = enabled_cfg();
    c.fib_index = 2;
    uint64_t seed = 7;
    unsigned bad = 0;
    for (int i = 0; i < 3000; i++) {
        raffle_t r;
        raffle_settlement_t s;
        uint8_t who[32], p[32], sec[32], cm[32];
        ident("owner", 1, who);
        ident("party", 0, p);
        epoch_secret((uint64_t) i, 0, sec);
        if (i % 3 == 1) { /* other ratios too */
            c.skim_num = 1 + xorshift(&seed) % 999;
            c.skim_den = c.skim_num + 1 + xorshift(&seed) % 100000;
        } else {
            c.skim_num = 987;
            c.skim_den = 61000;
        }
        raffle_init(&r, &c, 1, NULL, g_ranges, 4);
        raffle_commitment(1, p, sec, cm);
        raffle_commit(&r, p, cm);
        uint64_t a = i < 100 ? 61805u + (uint64_t) i : xorshift(&seed);
        if (i == 2999) a = UINT64_MAX;
        if (raffle_enter(&r, who, a, 1, NULL) != RAFFLE_OK) {
            bad++;
            continue;
        }
        raffle_reveal(&r, p, sec);
        if (raffle_draw(&r, &s) != RAFFLE_OK) {
            bad++;
            continue;
        }
        unsigned __int128 x = (unsigned __int128) s.pool * c.skim_num;
        if (s.reserve_skim + s.crown_pot != s.pool) bad++;
        if (!((unsigned __int128) s.reserve_skim * c.skim_den <= x &&
              x < ((unsigned __int128) s.reserve_skim + 1) * c.skim_den))
            bad++;
        if (i % 3 != 1 && s.crown_pot < s.reserve_skim) bad++; /* default 1.618% skim */
    }
    CHECK(bad == 0, "skim + pot == pool, skim is the exact floor (%u bad)", bad);
}

/* ===== R4: no modulo bias ===== */
static void test_draw_uniform(void)
{
    /* 3 tickets, one per owner; 6000 independent seeds. 2^64 mod 3 = 1, so
     * rejection matters. Loose chi-square bound (df 2, p ~ 1e-6: 27.6). */
    raffle_config_t c = enabled_cfg();
    c.fib_index = 4; /* F4 = 3 */
    unsigned cnt[3] = {0, 0, 0}, bad = 0;
    const unsigned N = 6000;
    for (unsigned i = 0; i < N; i++) {
        raffle_t r;
        raffle_settlement_t s;
        uint8_t p[32], sec[32], cm[32];
        ident("party", 0, p);
        epoch_secret(1000000u + i, 0, sec);
        raffle_init(&r, &c, 1, NULL, g_ranges, 8);
        raffle_commitment(1, p, sec, cm);
        raffle_commit(&r, p, cm);
        for (uint8_t k = 0; k < 3; k++) {
            uint8_t who[32];
            ident("owner", k, who);
            raffle_enter(&r, who, 62, 1, NULL); /* floor(62*987/61000) = 1 */
        }
        raffle_reveal(&r, p, sec);
        if (r.tickets != 3 || raffle_draw(&r, &s) != RAFFLE_OK || s.winning_ticket >= 3) {
            bad++;
            continue;
        }
        uint8_t want[32];
        ident("owner", (uint8_t) s.winning_ticket, want);
        if (memcmp(want, s.winner, 32) != 0) bad++;
        cnt[s.winning_ticket]++;
    }
    CHECK(bad == 0, "every draw lands on a ticket and its owner");
    unsigned long long chi = 0; /* the chi-square statistic times 2000 */
    for (int k = 0; k < 3; k++) {
        long long d = (long long) cnt[k] - 2000;
        chi += (unsigned long long) (d * d);
    }
    CHECK(chi / 2000 < 28, "uniform: counts %u %u %u", cnt[0], cnt[1], cnt[2]);
}

/* R4 directly: t = 2^63 + 1 makes 2^64 mod t = 2^63 - 1, so about half of all
 * raw draws must be rejected. An independent rejection loop must agree. */
static void test_pick(void)
{
    const uint64_t t = (1ull << 63) + 1;
    unsigned bad = 0, rejected_first = 0;
    uint64_t out;
    for (unsigned i = 0; i < 2000; i++) {
        uint8_t seed[32];
        epoch_secret(i, 7, seed);
        uint64_t want = 0;
        int found = 0;
        for (uint64_t ctr = 0; ctr < RAFFLE_DRAW_TRIES && !found; ctr++) {
            uint8_t b[15 + 32 + 8], x8[8];
            memcpy(b, "ZXV-RAFFLE-DRAW", 15);
            memcpy(b + 15, seed, 32);
            put64(b + 47, ctr);
            shake256(b, sizeof b, x8, 8);
            uint64_t x = 0;
            for (int k = 0; k < 8; k++) x |= (uint64_t) x8[k] << (8 * k);
            /* 2t > 2^64, so the only whole block of t values is [0, t) */
            if (x < t) {
                want = x;
                found = 1;
            } else if (ctr == 0)
                rejected_first++;
        }
        if (!found || !raffle_pick(seed, t, &out) || out != want) bad++;
    }
    CHECK(bad == 0, "raffle_pick matches an independent rejection loop (%u bad)", bad);
    CHECK(rejected_first > 800 && rejected_first < 1200, "about half rejected (%u)",
          rejected_first);
    uint8_t z[32] = {0};
    CHECK(!raffle_pick(z, 0, &out), "t = 0 refused");
    CHECK(raffle_pick(z, 1, &out) && out == 0, "t = 1 gives 0");
}

/* ===== Deterministic replay, against the Python model ===== */
static int ref_get(const char *path, const char *key, char *val, size_t cap)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(key);
        if (strncmp(line, key, n) == 0 && line[n] == ' ') {
            snprintf(val, cap, "%s", line + n + 1);
            val[strcspn(val, "\r\n")] = 0;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
}

/* The scenario of gen_raffle_ref.py: F(22) = 17,711 fee units, 3 parties,
 * share cap 8/21, owners k % 7 at multiplier 1 + j % 3, amounts (j = k + 1000 * epoch) j mod 61
 * when 5 divides j (fee 0: refused), else 62 + (j * 104,729) mod 300,000; three chained epochs.
 * Returns each epoch's settlement. */
static void run_scenario(raffle_settlement_t out[3], uint32_t refused[3])
{
    static raffle_range_t ranges[128];
    raffle_config_t c = enabled_cfg();
    c.fib_index = 22;
    c.share_num = 8;
    c.share_den = 21;
    raffle_t r;
    raffle_init(&r, &c, 1, NULL, ranges, 128);
    for (uint64_t e = 1; e <= 3; e++) {
        uint8_t p[3][32], s[3][32];
        for (uint8_t i = 0; i < 3; i++) {
            uint8_t cm[32];
            ident("party", i, p[i]);
            epoch_secret(e, i, s[i]);
            raffle_commitment(e, p[i], s[i], cm);
            raffle_commit(&r, p[i], cm);
        }
        refused[e - 1] = 0;
        for (uint64_t k = 0; r.phase == RAFFLE_OPEN; k++) {
            uint8_t who[32];
            ident("owner", (uint8_t) (k % 7), who);
            uint64_t j = k + 1000 * e;
            uint64_t a = j % 5 == 0 ? j % 61 : 62 + (j * 104729u) % 300000u;
            raffle_status_t st = raffle_enter(&r, who, a, (uint32_t) (1 + j % 3), NULL);
            if (st == RAFFLE_ERR_DUST || st == RAFFLE_ERR_CAP) refused[e - 1]++;
        }
        for (uint8_t i = 0; i < 3; i++) raffle_reveal(&r, p[i], s[i]);
        raffle_draw(&r, &out[e - 1]);
        raffle_settle(&r, &out[e - 1]);
        if (e < 3) raffle_next_epoch(&r);
    }
}

static void test_replay(const char *ref)
{
    raffle_settlement_t a[3], b[3];
    uint32_t ra[3], rb[3];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    run_scenario(a, ra);
    run_scenario(b, rb);
    CHECK(memcmp(a, b, sizeof a) == 0 && memcmp(ra, rb, sizeof ra) == 0,
          "two runs give byte-identical settlements");

    char key[32], want[128], got[128];
    for (int e = 0; e < 3; e++) {
        struct {
            const char *k;
            uint64_t v;
        } nums[] = {{"tickets", a[e].tickets},   {"pool", a[e].pool},
                    {"refused", ra[e]},          {"winning", a[e].winning_ticket},
                    {"skim", a[e].reserve_skim}, {"pot", a[e].crown_pot}};
        for (size_t i = 0; i < sizeof nums / sizeof nums[0]; i++) {
            snprintf(key, sizeof key, "e%d.%s", e + 1, nums[i].k);
            snprintf(got, sizeof got, "%llu", (unsigned long long) nums[i].v);
            CHECK(ref_get(ref, key, want, sizeof want) && strcmp(want, got) == 0,
                  "%s: C %s, Python %s", key, got, want);
        }
        struct {
            const char *k;
            const uint8_t *p;
        } hs[] = {{"header", a[e].header_hash},
                  {"seed", a[e].seed},
                  {"winner", a[e].winner},
                  {"link", a[e].link}};
        for (size_t i = 0; i < 4; i++) {
            snprintf(key, sizeof key, "e%d.%s", e + 1, hs[i].k);
            hex(hs[i].p, 32, got);
            CHECK(ref_get(ref, key, want, sizeof want) && strcmp(want, got) == 0,
                  "%s: C %s, Python %s", key, got, want);
        }
    }
    CHECK(memcmp(a[0].link, a[1].link, 32) != 0, "epochs chain to distinct links");

    /* raffle_settle refuses a record that is not the draw. */
    static raffle_range_t ranges[128];
    raffle_config_t c = enabled_cfg();
    c.fib_index = 10;
    raffle_t r;
    raffle_init(&r, &c, 1, NULL, ranges, 128);
    fill_epoch(&r, 1, 1);
    uint8_t p[32], s[32];
    ident("party", 0, p);
    epoch_secret(1, 0, s);
    raffle_reveal(&r, p, s);
    raffle_settlement_t t;
    CHECK(raffle_draw(&r, &t) == RAFFLE_OK, "draw");
    raffle_settlement_t forged = t;
    forged.crown_pot += 1;
    forged.reserve_skim -= 1;
    CHECK(raffle_settle(&r, &forged) == RAFFLE_ERR_SETTLEMENT, "tampered split refused");
    forged = t;
    forged.winner[0] ^= 1;
    CHECK(raffle_settle(&r, &forged) == RAFFLE_ERR_SETTLEMENT, "tampered winner refused");
    CHECK(r.phase == RAFFLE_CLOSED, "still closed after refusals");
    CHECK(raffle_settle(&r, &t) == RAFFLE_OK && r.phase == RAFFLE_SETTLED, "settle");
    CHECK(raffle_settle(&r, &t) == RAFFLE_ERR_PHASE, "settle once");
    CHECK(raffle_next_epoch(&r) == RAFFLE_OK && r.epoch == 2 && r.phase == RAFFLE_OPEN &&
              r.tickets == 0 && r.pool == 0 && r.n_parties == 0,
          "next epoch starts clean");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s REF\n", argv[0]);
        return 2;
    }
    test_config();
    test_fib();
    test_fee();
    test_multiplier_cap();
    test_epoch();
    test_commit_reveal();
    test_split();
    test_draw_uniform();
    test_pick();
    test_replay(argv[1]);
    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
