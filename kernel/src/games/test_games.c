/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_games.c — the sea is a pure function of the tick; prove it.
 *
 * Anchors:
 *  (1) DETERMINISM/LOCKSTEP: identical seed + identical inputs => identical
 *      game_state_hash after N ticks; a different seed OR a different input
 *      diverges.
 *  (2) PURE REPLAY: replaying the same seed+inputs reproduces the exact final
 *      hash bit-for-bit across fresh runs.
 *  (3) COMPANION IS NOT RANDOM: the Chiglet's action is a deterministic
 *      function of the game features via chg_infer (known-answer + repeat),
 *      and an unbound runtime yields a TYPED not-available, not a fake move.
 *  (4) CID ROUND-TRIP: identical defs share a CID; a changed param changes it.
 *  (5) BOUNDS: heavy foraging never writes past the entity array (ASan).
 */
#include <stdio.h>
#include <string.h>
#include "games.h"

static int checks = 0, fails = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  [FAIL] %s\n", msg);                                                          \
        } else {                                                                                   \
            printf("  [ok] %s\n", msg);                                                            \
        }                                                                                          \
    } while (0)

/* a fixed, non-random input schedule — same on every node */
static uint32_t sched_input(uint32_t tick)
{
    return (tick * 7u + 3u) % 5u;
}

/* clear the board of food and drop exactly one food cell (for known-answer) */
static void set_single_food(game_t *g, int16_t x, int16_t y)
{
    for (uint32_t i = 0; i < g->count; i++)
        if (g->ents[i].kind == GAME_ENT_FOOD) {
            g->ents[i].alive = 0;
            g->ents[i].kind = GAME_ENT_EMPTY;
        }
    uint32_t slot = g->count < GAME_MAX_ENTITIES ? g->count++ : 0;
    g->ents[slot].kind = GAME_ENT_FOOD;
    g->ents[slot].alive = 1;
    g->ents[slot].x = x;
    g->ents[slot].y = y;
}

int main(void)
{
    printf("=== games — play alongside your Chiglet ===\n");

    /* build one companion model, shared by every bound Chiglet */
    chg_model_t model;
    game_companion_model(&model);

    /* ---------- (1) determinism / lockstep ---------- */
    {
        chiglet_t ca, cb;
        chg_init(&ca, CHG_CAP_INFER);
        chg_load_model(&ca, &model);
        chg_init(&cb, CHG_CAP_INFER);
        chg_load_model(&cb, &model);

        game_t a, b;
        game_init(&a, 0xC0FFEEu);
        game_bind_companion(&a, &ca);
        game_init(&b, 0xC0FFEEu);
        game_bind_companion(&b, &cb);
        for (uint32_t t = 0; t < 200; t++) {
            uint32_t in = sched_input(t);
            game_step(&a, in);
            game_step(&b, in);
        }
        uint32_t ha = game_state_hash(&a), hb = game_state_hash(&b);
        printf("       same seed+inputs: hashA=%08x hashB=%08x\n", ha, hb);
        CHECK(ha == hb, "same seed + same inputs => identical hash (P2P lockstep)");

        /* a different seed diverges */
        game_t c;
        chiglet_t cc;
        chg_init(&cc, CHG_CAP_INFER);
        chg_load_model(&cc, &model);
        game_init(&c, 0xC0FFEFu);
        game_bind_companion(&c, &cc);
        for (uint32_t t = 0; t < 200; t++) game_step(&c, sched_input(t));
        CHECK(game_state_hash(&c) != ha, "a different seed diverges");

        /* a different input stream diverges */
        game_t d;
        chiglet_t cd;
        chg_init(&cd, CHG_CAP_INFER);
        chg_load_model(&cd, &model);
        game_init(&d, 0xC0FFEEu);
        game_bind_companion(&d, &cd);
        for (uint32_t t = 0; t < 200; t++) {
            uint32_t in = sched_input(t);
            if (t == 50) in = GAME_MOVE_UP; /* one keystroke differs */
            game_step(&d, in);
        }
        CHECK(game_state_hash(&d) != ha, "a single different input diverges");
    }

    /* ---------- (2) pure replay, bit-for-bit ---------- */
    {
        uint32_t h[3];
        for (int run = 0; run < 3; run++) {
            chiglet_t c;
            chg_init(&c, CHG_CAP_INFER);
            chg_load_model(&c, &model);
            game_t g;
            game_init(&g, 0x5EED1234u);
            game_bind_companion(&g, &c);
            for (uint32_t t = 0; t < 137; t++) game_step(&g, sched_input(t));
            h[run] = game_state_hash(&g);
        }
        printf("       replays: %08x %08x %08x\n", h[0], h[1], h[2]);
        CHECK(h[0] == h[1] && h[1] == h[2],
              "same seed+inputs reproduce the exact final hash across runs");
    }

    /* ---------- (3) companion action is deterministic, not random ---------- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_load_model(&c, &model);
        game_t g;
        game_init(&g, 42u);
        game_bind_companion(&g, &c);

        /* companion sits at (7,8). Food strictly to the RIGHT => move RIGHT. */
        set_single_food(&g, 12, 8);
        uint8_t av1 = 0, av2 = 0;
        game_move_t m1 = game_companion_decide(&g, &av1);
        game_move_t m2 = game_companion_decide(&g, &av2);
        printf("       food east -> move=%d (available=%d)\n", (int) m1, (int) av1);
        CHECK(m1 == GAME_MOVE_RIGHT, "food to the east => companion decides RIGHT (known answer)");
        CHECK(av1 == 1, "bound+loaded runtime reports available");
        CHECK(m1 == m2, "same features => same action (NOT random)");

        /* different features => a different, feature-derived action */
        set_single_food(&g, 2, 8); /* food to the WEST */
        game_move_t m3 = game_companion_decide(&g, &av1);
        printf("       food west -> move=%d\n", (int) m3);
        CHECK(m3 == GAME_MOVE_LEFT, "food to the west => companion decides LEFT (feature-derived)");

        set_single_food(&g, 7, 2); /* food to the NORTH (smaller y) */
        game_move_t m4 = game_companion_decide(&g, &av1);
        CHECK(m4 == GAME_MOVE_UP, "food to the north => companion decides UP");

        /* UNBOUND runtime => typed not-available, never a fabricated move */
        game_t u;
        game_init(&u, 42u); /* no companion bound */
        set_single_food(&u, 12, 8);
        uint8_t av = 9;
        game_move_t mu = game_companion_decide(&u, &av);
        CHECK(mu == GAME_MOVE_STAY && av == 0,
              "no runtime bound => STAY + available=0 (typed not-available)");

        /* an UNLOADED model also fails closed */
        chiglet_t empty;
        chg_init(&empty, CHG_CAP_INFER); /* no model loaded */
        game_t e;
        game_init(&e, 42u);
        game_bind_companion(&e, &empty);
        set_single_food(&e, 12, 8);
        av = 9;
        game_move_t me = game_companion_decide(&e, &av);
        CHECK(me == GAME_MOVE_STAY && av == 0, "unloaded model => STAY + available=0");
    }

    /* ---------- (4) content-addressed game def round-trip ---------- */
    {
        game_def_t d1, d2;
        game_def_default(&d1);
        game_def_default(&d2);
        uint8_t cid1[IPFS_CID_LEN], cid2[IPFS_CID_LEN];
        CHECK(game_def_publish(&d1, cid1) == 0, "publish returns success");
        CHECK(game_def_publish(&d2, cid2) == 0, "publish (identical) returns success");
        CHECK(memcmp(cid1, cid2, IPFS_CID_LEN) == 0,
              "two identical defs share a CID (peers fetch the same ruleset)");

        d2.max_food += 1; /* change one rule */
        uint8_t cid3[IPFS_CID_LEN];
        game_def_publish(&d2, cid3);
        CHECK(memcmp(cid1, cid3, IPFS_CID_LEN) != 0,
              "a changed param changes the CID (a different ruleset)");

        /* NULL guarded */
        CHECK(game_def_publish(0, cid1) == -1, "NULL def rejected");
        CHECK(game_def_publish(&d1, 0) == -1, "NULL out_cid rejected");
    }

    /* ---------- (5) bounds: hammer the array, ASan is watching ---------- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_load_model(&c, &model);
        game_t g;
        game_init(&g, 7u);
        game_bind_companion(&g, &c);
        for (uint32_t t = 0; t < 5000; t++) game_step(&g, sched_input(t));
        CHECK(g.count <= GAME_MAX_ENTITIES, "entity count stays within bounds");
        uint32_t fc = 0;
        for (uint32_t i = 0; i < g.count; i++)
            if (g.ents[i].alive && g.ents[i].kind == GAME_ENT_FOOD) fc++;
        CHECK(fc <= GAME_MAX_FOOD, "food never exceeds GAME_MAX_FOOD");
        printf("       after 5000 ticks: count=%u food=%u score=%d\n", g.count, fc, g.score);
        CHECK(g.score > 0, "the captains actually forage over a long game");
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
