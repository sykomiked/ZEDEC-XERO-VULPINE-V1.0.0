/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_raffle_token.c — host tests for raffle_token.h (ASan/UBSan). */
#include "raffle_token.h"
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

#define DAY 86400u

static bool adult(void *ctx, const uint8_t owner[RAFFLE_ID_LEN])
{
    (void) ctx;
    (void) owner;
    return true;
}

static void commit_one(raffle_t *r, uint8_t tag)
{
    uint8_t p[32], s[32], cm[32];
    memset(p, tag, sizeof p);
    memset(s, (uint8_t) (tag ^ 0x5A), sizeof s);
    raffle_commitment(r->epoch, p, s, cm);
    raffle_commit(r, p, cm);
}

static void reveal_one(raffle_t *r, uint8_t tag)
{
    uint8_t p[32], s[32];
    memset(p, tag, sizeof p);
    memset(s, (uint8_t) (tag ^ 0x5A), sizeof s);
    raffle_reveal(r, p, s);
}

typedef struct {
    uint64_t to[3];
    unsigned calls;
} book_t;

static bool credit(void *ctx, raffle_dest_t d, const uint8_t *who, uint64_t amount)
{
    book_t *b = ctx;
    (void) who;
    b->to[d] += amount;
    b->calls++;
    return true;
}

static raffle_token_block_t zenny_block(void)
{
    raffle_token_block_t b;
    raffle_token_block_default(&b);
    b.events = RAFFLE_TOKEN_EV_TRANSFER | RAFFLE_TOKEN_EV_MINT | RAFFLE_TOKEN_EV_SAVINGS;
    b.target = RAFFLE_TOKEN_TARGET_LOCAL;
    b.slice_min = (raffle_rate_t){1, 50}; /* 2%  */
    b.slice_max = (raffle_rate_t){3, 5};  /* 60%: invalid, fixed below */
    b.savings_max = (raffle_rate_t){1, 2};
    b.local_fib_index = 12; /* 144 tickets */
    return b;
}

#define LADDER_N 3u
static raffle_t g_lv[LADDER_N];
static raffle_range_t g_rg[LADDER_N * 64];

int main(void)
{
    raffle_config_t dep;
    raffle_config_default(&dep);
    raffle_guard_policy_t pol;
    raffle_guard_policy_default(&pol);
    pol.age_gate = adult;

    /* ---- T1: the block ---- */
    raffle_token_block_t off, b, d;
    raffle_token_block_default(&off);
    uint8_t enc[RAFFLE_TOKEN_BLOCK_LEN], enc2[RAFFLE_TOKEN_BLOCK_LEN], h1[32], h2[32];
    CHECK(off.events == 0 && raffle_token_block_check(&off) == RAFFLE_OK, "default is off, valid");
    b = zenny_block();
    CHECK(raffle_token_block_check(&b) == RAFFLE_ERR_MANIFEST, "slice_max above 1/2 refused");
    b.slice_max = (raffle_rate_t){1, 2};
    CHECK(raffle_token_block_check(&b) == RAFFLE_OK, "zenny block valid");
    CHECK(raffle_token_encode(&b, enc) == RAFFLE_OK && memcmp(enc, "ZXRT\x01", 5) == 0 &&
              enc[5] == 0x0D && enc[6] == 1 && enc[59] == 12,
          "encoding layout");
    CHECK(raffle_token_decode(enc, sizeof enc, &d) == RAFFLE_OK && d.events == b.events &&
              d.slice_min.num == 1 && d.slice_min.den == 50 && d.slice_max.den == 2 &&
              d.savings_max.num == 1 && d.local_fib_index == 12,
          "round trip");
    CHECK(raffle_token_digest(&b, h1) == RAFFLE_OK && raffle_token_digest(&d, h2) == RAFFLE_OK &&
              memcmp(h1, h2, 32) == 0,
          "digest stable");
    d.slice_min.den = 49;
    raffle_token_digest(&d, h2);
    CHECK(memcmp(h1, h2, 32) != 0, "digest binds the slice range");
    CHECK(raffle_token_decode(enc, 63, &d) == RAFFLE_ERR_MANIFEST, "short");

    /* Canonical form: every single-byte change is refused or decodes to a
     * block that re-encodes to exactly the changed bytes. */
    unsigned refused = 0, accepted = 0, noncanon = 0, unknown_event = 0;
    for (unsigned i = 0; i < RAFFLE_TOKEN_BLOCK_LEN; i++) {
        for (unsigned x = 1; x < 256; x++) {
            memcpy(enc2, enc, sizeof enc);
            enc2[i] ^= (uint8_t) x;
            raffle_status_t st = raffle_token_decode(enc2, sizeof enc2, &d);
            if (st != RAFFLE_OK) {
                refused++;
                if (st == RAFFLE_ERR_EVENT) unknown_event++;
                continue;
            }
            accepted++;
            uint8_t back[RAFFLE_TOKEN_BLOCK_LEN];
            if (raffle_token_encode(&d, back) != RAFFLE_OK || memcmp(back, enc2, sizeof back))
                noncanon++;
        }
    }
    CHECK(noncanon == 0, "%u non-canonical decodes", noncanon);
    CHECK(refused > 0 && accepted > 0 && unknown_event > 0, "mutations: %u refused, %u accepted",
          refused, accepted);
    memcpy(enc2, enc, sizeof enc);
    enc2[5] |= 0x10; /* a would-be "borrow" bit */
    CHECK(raffle_token_decode(enc2, sizeof enc2, &d) == RAFFLE_ERR_EVENT, "unknown event refused");
    memcpy(enc2, enc, sizeof enc);
    enc2[4] = 2;
    CHECK(raffle_token_decode(enc2, sizeof enc2, &d) == RAFFLE_ERR_MANIFEST, "version");
    memcpy(enc2, enc, sizeof enc);
    enc2[63] = 1;
    CHECK(raffle_token_decode(enc2, sizeof enc2, &d) == RAFFLE_ERR_MANIFEST, "reserved byte");

    raffle_token_block_t t = b;
    t.events = RAFFLE_TOKEN_EV_SAVINGS; /* slices off but fields set */
    CHECK(raffle_token_block_check(&t) == RAFFLE_ERR_MANIFEST, "unused fields must be zero");
    t = b;
    t.slice_min = (raffle_rate_t){1, 3};
    t.slice_max = (raffle_rate_t){1, 4};
    CHECK(raffle_token_block_check(&t) == RAFFLE_ERR_MANIFEST, "min above max");
    t = b;
    t.savings_max = (raffle_rate_t){3, 2};
    CHECK(raffle_token_block_check(&t) == RAFFLE_ERR_MANIFEST, "savings share above 1");
    t = b;
    t.target = RAFFLE_TOKEN_TARGET_NETWORK;
    CHECK(raffle_token_block_check(&t) == RAFFLE_ERR_MANIFEST, "fib index only for a local pot");
    t.local_fib_index = 0;
    CHECK(raffle_token_block_check(&t) == RAFFLE_OK, "network block");
    t = b;
    t.target = 2;
    CHECK(raffle_token_block_check(&t) == RAFFLE_ERR_MANIFEST, "unknown target");

    /* ---- T5: the deployment switch wins ---- */
    raffle_config_t c;
    raffle_player_t pl;
    uint8_t alice[32];
    memset(alice, 0xA1, sizeof alice);
    raffle_player_init(&pl, alice);
    uint64_t fee = 0, now = 200 * DAY;
    CHECK(raffle_token_config(&dep, &b, &c) == RAFFLE_ERR_DISABLED, "deployment off: refused");
    CHECK(raffle_token_set_rate(&dep, &b, &pl, (raffle_rate_t){1, 20}) == RAFFLE_ERR_DISABLED,
          "cannot opt in while off");
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_TRANSFER, &pl, 1000, now, 1, 0,
                             &fee) == RAFFLE_ERR_DISABLED,
          "slice refused while off");
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &pl, 1000, now, 1, &fee) ==
              RAFFLE_ERR_DISABLED,
          "savings refused while off");
    dep.jurisdiction_permits = true;

    /* ---- T3/T4: the effective config ---- */
    CHECK(raffle_token_config(&dep, &b, &c) == RAFFLE_OK && c.min_rate.num == 1 &&
              c.min_rate.den == 50 && c.max_rate.num == 1 && c.max_rate.den == 2 &&
              c.fib_index == 12 && c.threshold == 0,
          "issuer min 2%% beats deployment 1%%, local fib");
    CHECK(c.skim_num == dep.skim_num && c.skim_den == dep.skim_den &&
              c.grant_num == dep.grant_num && c.grant_den == dep.grant_den &&
              c.share_num == dep.share_num && c.jurisdiction_permits,
          "skim, grant, cap and switch are the deployment's");
    raffle_config_t dep2 = dep;
    dep2.max_rate = (raffle_rate_t){1, 100};
    CHECK(raffle_token_config(&dep2, &b, &c) == RAFFLE_ERR_RATE, "empty intersection");
    t = b;
    t.events = RAFFLE_TOKEN_EV_SAVINGS;
    t.slice_min = t.slice_max = (raffle_rate_t){0, 0};
    CHECK(raffle_token_config(&dep, &t, &c) == RAFFLE_ERR_EVENT, "no slice events");
    raffle_token_config(&dep, &b, &c);

    /* ---- G1 + T2: holder opt-in and events ---- */
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_TRANSFER, &pl, 1000, now, 1, 0,
                             &fee) == RAFFLE_ERR_NOT_IN,
          "holder out by default");
    CHECK(raffle_token_set_rate(&dep, &b, &pl, (raffle_rate_t){1, 100}) == RAFFLE_ERR_RATE,
          "1%% below the issuer's 2%%");
    CHECK(raffle_token_set_rate(&dep, &b, &pl, (raffle_rate_t){1, 20}) == RAFFLE_OK, "5%%");
    raffle_player_set_limits(&pol, &pl, 1000, 1000, now);
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_BURN, &pl, 1000, now, 1, 0, &fee) ==
              RAFFLE_ERR_EVENT,
          "burn not enabled");
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_TRANSFER | RAFFLE_TOKEN_EV_MINT, &pl,
                             1000, now, 1, 0, &fee) == RAFFLE_ERR_EVENT,
          "one event at a time");
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_SAVINGS, &pl, 1000, now, 1, 0, &fee) ==
              RAFFLE_ERR_EVENT,
          "savings is not a slice");
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_TRANSFER, &pl, 1000, now, 1, 0,
                             &fee) == RAFFLE_OK &&
              fee == 50,
          "5%% of 1000 on transfer");

    /* ---- T4: the issuer's local pot ---- */
    raffle_t pot;
    raffle_range_t rg[64];
    CHECK(raffle_init(&pot, &c, 1, NULL, rg, 64) == RAFFLE_OK, "local pot");
    CHECK(raffle_token_pot_check(&dep, &b, &pot) == RAFFLE_OK, "pot matches the deployment");
    raffle_t cheat;
    raffle_config_t cc = c;
    cc.skim_num = 1;
    raffle_init(&cheat, &cc, 1, NULL, rg, 64);
    CHECK(raffle_token_pot_check(&dep, &b, &cheat) == RAFFLE_ERR_CONFIG, "a lower skim refused");
    cc = c;
    cc.grant_den = 0;
    cc.grant_num = 0;
    raffle_init(&cheat, &cc, 1, NULL, rg, 64);
    CHECK(raffle_token_pot_check(&dep, &b, &cheat) == RAFFLE_ERR_CONFIG, "dropping grants refused");
    t = b;
    t.target = RAFFLE_TOKEN_TARGET_NETWORK;
    t.local_fib_index = 0;
    CHECK(raffle_token_pot_check(&dep, &t, &pot) == RAFFLE_ERR_MANIFEST, "network token, no pot");
    raffle_init(&pot, &c, 1, NULL, rg, 64);

    commit_one(&pot, 0x11);
    raffle_receipt_t rc;
    /* Three holders (the 8/21 share cap allows 54 of 144 tickets each). */
    raffle_player_t h[3];
    for (int i = 0; i < 3; i++) {
        uint8_t id[32];
        memset(id, 0xA1 + i, sizeof id);
        raffle_player_t *p = i == 0 ? &pl : &h[i];
        if (i) {
            raffle_player_init(p, id);
            raffle_token_set_rate(&dep, &b, p, (raffle_rate_t){1, 20});
            raffle_player_set_limits(&pol, p, 1000, 1000, now);
        }
        raffle_status_t st =
            raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_TRANSFER, p, 1000, now, 1, 0, &fee);
        CHECK(st == RAFFLE_OK, "slice %d", i);
        CHECK(raffle_enter(&pot, p->owner, 1000, &p->rate, &rc) == RAFFLE_OK && rc.fee == fee,
              "entered %d", i);
        raffle_guard_commit(&pol, p, now, 1, fee);
    }
    CHECK(pot.tickets == 150 && pot.phase == RAFFLE_CLOSED, "144 threshold reached at 150");
    CHECK(raffle_token_check(&pol, &dep, &b, RAFFLE_TOKEN_EV_MINT, &pl, 20000, now, 2, 0, &fee) ==
              RAFFLE_ERR_DAY_LIMIT,
          "guard limits apply to token slices");
    reveal_one(&pot, 0x11);
    raffle_settlement_t s;
    book_t bk = {{0, 0, 0}, 0};
    CHECK(raffle_draw(&pot, &s) == RAFFLE_OK && raffle_post(&s, credit, &bk), "draw + post");
    CHECK(bk.to[RAFFLE_DEST_RESERVE_FLOOR] == 150 * 987 / 61000 &&
              bk.to[RAFFLE_DEST_GRANT_POOL] == 150 * 5 / 55 &&
              bk.to[0] + bk.to[1] + bk.to[2] == 150,
          "local pot's skim goes to the main reserve floor");

    /* ---- T6: prize-linked savings ---- */
    raffle_player_t sv;
    uint8_t bob[32];
    memset(bob, 0xB0, sizeof bob);
    raffle_player_init(&sv, bob);
    raffle_rate_t none = {0, 1};
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 1000, now, 1, &fee) == RAFFLE_ERR_NOT_IN,
          "savings opt-in required");
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, (raffle_rate_t){3, 4}, none) ==
              RAFFLE_ERR_RATE,
          "75%% above the issuer's 50%%");
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, (raffle_rate_t){1, 4}, none) ==
              RAFFLE_ERR_CONFIRM,
          "25%% needs a confirmation at enrolment");
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, (raffle_rate_t){1, 4},
                                 (raffle_rate_t){2, 8}) == RAFFLE_ERR_CONFIRM,
          "the exact share must be confirmed");
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, (raffle_rate_t){1, 4},
                                 (raffle_rate_t){1, 4}) == RAFFLE_OK,
          "enrolled at 25%%");
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 1003, now, 1, &fee) ==
              RAFFLE_ERR_NO_LIMITS,
          "limits required for savings too");
    raffle_player_set_limits(&pol, &sv, 600, 5000, now);
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 1003, now, 1, &fee) == RAFFLE_OK &&
              fee == 250,
          "floor(1003 / 4) = 250, from realised profit only");
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 3, now, 1, &fee) == RAFFLE_ERR_DUST,
          "no profit, no tickets");
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 0, now, 1, &fee) == RAFFLE_ERR_DUST,
          "zero profit");
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, UINT64_MAX, now, 1, &fee) ==
              RAFFLE_ERR_DAY_LIMIT,
          "huge profit still capped by the day limit");
    uint64_t f_all = 0;
    raffle_token_block_t one = b;
    one.savings_max = (raffle_rate_t){1, 1};
    raffle_player_t sv1;
    raffle_player_init(&sv1, bob);
    raffle_token_set_share(&pol, &dep, &one, &sv1, (raffle_rate_t){1, 1}, (raffle_rate_t){1, 1});
    raffle_player_set_limits(&pol, &sv1, UINT64_MAX, UINT64_MAX, now);
    CHECK(raffle_token_savings_check(&pol, &dep, &one, &sv1, UINT64_MAX, now, 1, &f_all) ==
                  RAFFLE_OK &&
              f_all == UINT64_MAX,
          "share 1 buys exactly the profit, never more");

    /* Savings into the network ladder (fee-based entry). */
    raffle_ladder_t L;
    raffle_config_t lc;
    raffle_config_default(&lc);
    lc.jurisdiction_permits = true;
    CHECK(raffle_ladder_init(&L, &lc, LADDER_N, RAFFLE_LADDER_IDX_LO, RAFFLE_LADDER_IDX_HI, g_lv,
                             g_rg, 64, NULL, 0, NULL) == RAFFLE_OK,
          "ladder");
    for (uint32_t k = 0; k < LADDER_N; k++) commit_one(&g_lv[k], (uint8_t) (0x20 + k));
    raffle_ladder_receipt_t lr;
    raffle_token_savings_check(&pol, &dep, &b, &sv, 1003, now, 1, &fee);
    CHECK(raffle_ladder_enter_fee(&L, bob, 1003, &sv.rate, fee, NULL, &lr) == RAFFLE_OK &&
              lr.fee == 250 && lr.share[0] + lr.share[1] + lr.share[2] == 250,
          "savings fee spread over the ladder, conserved");
    CHECK(raffle_ladder_enter_fee(&L, bob, 1003, &sv.rate, 0, NULL, &lr) == RAFFLE_ERR_DUST,
          "zero fee refused by the ladder");
    raffle_guard_commit(&pol, &sv, now, 1, fee);
    CHECK(raffle_token_savings_check(&pol, &dep, &b, &sv, 1600, now, 1, &fee) ==
              RAFFLE_ERR_DAY_LIMIT,
          "250 + 400 passes the 600 day limit");

    /* A later manifest that lowers savings_max stops the old share. */
    raffle_token_block_t low = b;
    low.savings_max = (raffle_rate_t){1, 10};
    CHECK(raffle_token_savings_check(&pol, &dep, &low, &sv, 1003, now + DAY, 2, &fee) ==
              RAFFLE_ERR_RATE,
          "lowered savings_max: refused until re-enrolled");

    /* Leaving always works, even after the deployment switches off. */
    dep.jurisdiction_permits = false;
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, none, none) == RAFFLE_OK && sv.rate.num == 0,
          "leave while off");
    CHECK(raffle_token_set_share(&pol, &dep, &b, &sv, (raffle_rate_t){1, 20}, none) ==
              RAFFLE_ERR_DISABLED,
          "cannot enrol while off");
    dep.jurisdiction_permits = true;
    raffle_player_exclude(&sv1, now, 7 * DAY);
    CHECK(raffle_token_savings_check(&pol, &dep, &one, &sv1, 1000, now + DAY, 1, &fee) ==
              RAFFLE_ERR_EXCLUDED,
          "self-exclusion covers savings");

    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
