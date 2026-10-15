/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_bond.c — host tests for the chiglet's model of its person.
 *
 *  B1  the ISF interaction parameter against a float reference
 *  B2  a person's routine is learned and offered at the right moment
 *  B3  independence: duplicated evidence never adds confidence
 *  B4  feedback, decay and eviction
 *  B5  the owner's controls: pause, forget, share, export, import
 *  B6  random operations keep every bound
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "chg_bond.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x0ddba11c0ffee123ull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545f4914f6cdd1dull;
}

static uint8_t OWNER[32], STRANGER[32];
static chgb_t g_b, g_b2;

enum { WAKE = 100, INBOX = 200, MUSIC = 300, MAIL_APP = 7, PLAYER_APP = 9 };

static chgb_ctx_t ctx(uint16_t slot, uint16_t app, uint16_t prev)
{
    chgb_ctx_t c = {slot, app, prev};
    return c;
}

/* ===== B1 ===== */
static void b1_interaction(void)
{
    printf("B1 ISF interaction\n");
    uint16_t a[CHGB_ACTIONS] = {0}, b[CHGB_ACTIONS] = {0}, z[CHGB_ACTIONS] = {0};
    a[0] = 500, a[1] = 200;
    CHECK(chgb_interaction(a, a, CHGB_ACTIONS) == 0);
    b[2] = 700;
    CHECK(chgb_interaction(a, b, CHGB_ACTIONS) == 1000);
    CHECK(chgb_interaction(a, z, CHGB_ACTIONS) == 0);
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) b[i] = (uint16_t) (2 * a[i]);
    CHECK(chgb_interaction(a, b, CHGB_ACTIONS) == 0); /* scale-invariant */
    uint32_t worst = 0;
    for (uint32_t t = 0; t < 5000; t++) {
        double dot = 0, na = 0, nb = 0;
        for (uint32_t i = 0; i < CHGB_ACTIONS; i++) {
            a[i] = (uint16_t) (rnd() % 4 ? 0 : rnd() % (CHGB_WMAX + 1));
            b[i] = (uint16_t) (rnd() % 4 ? 0 : rnd() % (CHGB_WMAX + 1));
            dot += (double) a[i] * b[i];
            na += (double) a[i] * a[i];
            nb += (double) b[i] * b[i];
        }
        if (na == 0 || nb == 0) continue;
        double ref = 1000.0 * (1.0 - dot * dot / (na * nb));
        double d = fabs(ref - (double) chgb_interaction(a, b, CHGB_ACTIONS));
        if (d > worst) worst = (uint32_t) ceil(d);
    }
    CHECK(worst <= 1); /* floor of cos^2 permille: at most one permille off */
}

/* ===== B2 ===== */
static void live_four_weeks(chgb_t *b)
{
    for (uint32_t week = 0; week < 4; week++)
        for (uint32_t day = 0; day < 7; day++) {
            uint16_t morning = (uint16_t) (day * 24 + 8), evening = (uint16_t) (day * 24 + 21);
            chgb_ctx_t m = ctx(morning, MAIL_APP, WAKE);
            if (day < 5) chgb_observe(b, &m, INBOX);
            chgb_ctx_t e = ctx(evening, PLAYER_APP, INBOX);
            chgb_observe(b, &e, MUSIC);
            /* noise: random things at random hours */
            for (uint32_t k = 0; k < 3; k++) {
                chgb_ctx_t r =
                    ctx((uint16_t) (rnd() % CHGB_SLOTS), (uint16_t) (20 + rnd() % 5), CHGB_NONE);
                chgb_observe(b, &r, (uint16_t) (1000 + rnd() % 6));
            }
        }
}

static void b2_routine(void)
{
    printf("B2 learning a routine\n");
    memset(OWNER, 0x36, 32);
    memset(STRANGER, 0x99, 32);
    chgb_init(&g_b, OWNER);
    chgb_suggestion_t s;
    chgb_ctx_t mon8 = ctx(8, MAIL_APP, WAKE);
    CHECK(chgb_suggest(&g_b, &mon8, &s) == CHGB_OK && s.state == CHGB_NOTHING);

    /* seen once: not enough to bother the person */
    chgb_observe(&g_b, &mon8, INBOX);
    chgb_suggest(&g_b, &mon8, &s);
    CHECK(s.state == CHGB_UNSURE && s.action == INBOX);

    chgb_init(&g_b, OWNER);
    live_four_weeks(&g_b);
    chgb_suggest(&g_b, &mon8, &s);
    CHECK(s.state == CHGB_SUGGEST && s.action == INBOX && s.share >= 600);
    chgb_ctx_t fri21 = ctx(4 * 24 + 21, PLAYER_APP, INBOX);
    chgb_suggest(&g_b, &fri21, &s);
    CHECK(s.state == CHGB_SUGGEST && s.action == MUSIC);
    /* Saturday morning has no inbox habit, but mail app + waking still do */
    chgb_ctx_t sat8 = ctx(5 * 24 + 8, MAIL_APP, WAKE);
    chgb_suggest(&g_b, &sat8, &s);
    CHECK(s.action == INBOX && s.experts >= 2);
    /* an hour with only noise: no confident offer */
    chgb_ctx_t odd = ctx(3 * 24 + 3, CHGB_NONE, CHGB_NONE);
    chgb_suggest(&g_b, &odd, &s);
    CHECK(s.state != CHGB_SUGGEST);
    CHECK(chgb_app_affinity(&g_b, MAIL_APP) > 0 && chgb_app_affinity(&g_b, 4242) == 0);
    chgb_ctx_t once = ctx(50, 600, CHGB_NONE);
    chgb_observe(&g_b, &once, 1001);
    CHECK(chgb_app_affinity(&g_b, MAIL_APP) > 4 * chgb_app_affinity(&g_b, 600));
    CHECK(g_b.observed > 0);
}

/* ===== B3 ===== */
static void b3_independence(void)
{
    printf("B3 independent evidence\n");
    chgb_t *b = &g_b2;
    chgb_suggestion_t s1, s2;
    /* time, app and prev all saw exactly the same thing: one voice */
    chgb_init(b, OWNER);
    chgb_ctx_t c = ctx(10, 1, CHGB_NONE);
    chgb_observe(b, &c, 50); /* interns 50 so it can be a prev */
    chgb_init(b, OWNER);
    for (uint32_t i = 0; i < 6; i++) {
        chgb_ctx_t p = ctx(11, 2, CHGB_NONE);
        chgb_observe(b, &p, 50);
    }
    for (uint32_t i = 0; i < 6; i++) {
        chgb_ctx_t q = ctx(10, 1, 50);
        chgb_observe(b, &q, 60);
        chgb_observe(b, &q, 61);
    }
    chgb_ctx_t q = ctx(10, 1, 50);
    chgb_suggest(b, &q, &s1);
    CHECK(s1.experts == 3 && s1.distinct == 1 && s1.R_permille == 1000);
    CHECK(s1.u[0][1] < CHGB_U_MERGE && s1.u[1][2] < CHGB_U_MERGE);

    /* an independent second reason raises R above one */
    for (uint32_t i = 0; i < 8; i++) {
        chgb_ctx_t r = ctx(12, 1, CHGB_NONE);
        chgb_observe(b, &r, 70);
    }
    chgb_suggest(b, &q, &s2);
    CHECK(s2.distinct == 2 && s2.R_permille > 1000 && s2.R_permille <= 2000);

    /* duplication cannot add confidence: the same evidence repeated through
     * all three experts gives the same share as through one */
    chgb_t one;
    chgb_init(&one, OWNER);
    for (uint32_t i = 0; i < 6; i++) {
        chgb_ctx_t r = ctx(10, CHGB_NONE, CHGB_NONE);
        chgb_observe(&one, &r, 60);
        chgb_observe(&one, &r, 61);
        chgb_observe(&one, &r, 60);
    }
    chgb_init(b, OWNER);
    for (uint32_t i = 0; i < 6; i++) {
        chgb_ctx_t r = ctx(9, CHGB_NONE, CHGB_NONE);
        chgb_observe(b, &r, 99);
        chgb_ctx_t t = ctx(10, 1, 99);
        chgb_observe(b, &t, 60);
        chgb_observe(b, &t, 61);
        chgb_observe(b, &t, 60);
    }
    chgb_ctx_t a1 = ctx(10, CHGB_NONE, CHGB_NONE), a3 = ctx(10, 1, 99);
    chgb_suggest(&one, &a1, &s1);
    chgb_suggest(b, &a3, &s2);
    CHECK(s1.distinct == 1 && s2.distinct == 1);
    CHECK(s1.share == s2.share && s1.action == s2.action);
}

/* ===== B4 ===== */
static void b4_feedback(void)
{
    printf("B4 feedback, decay, eviction\n");
    chgb_init(&g_b2, OWNER);
    chgb_t *b = &g_b2;
    chgb_ctx_t c = ctx(30, 3, CHGB_NONE);
    for (uint32_t i = 0; i < 4; i++) chgb_observe(b, &c, 5);
    chgb_suggestion_t s;
    chgb_suggest(b, &c, &s);
    CHECK(s.state == CHGB_SUGGEST && s.action == 5);
    CHECK(chgb_feedback(b, &c, 5, true) == CHGB_OK && b->accepted == 1);
    uint16_t before = b->w_time[30][0];
    for (uint32_t i = 0; i < 4; i++) chgb_feedback(b, &c, 5, false);
    CHECK(b->rejected == 4 && b->w_time[30][0] < before);
    chgb_suggest(b, &c, &s);
    CHECK(s.state != CHGB_SUGGEST); /* "no thanks" four times: it stops offering */
    CHECK(chgb_feedback(b, &c, 777, true) == CHGB_ERR_ARG);

    for (uint32_t i = 0; i < 40; i++) chgb_observe(b, &c, 5);
    CHECK(b->w_time[30][0] <= CHGB_WMAX);
    for (uint32_t i = 0; i < 2000; i++) chgb_decay_all(b);
    CHECK(b->w_time[30][0] == 0 && chgb_app_affinity(b, 3) == 0); /* old habits fade */

    /* vocabulary: 40 actions into 32 places keeps the most used */
    chgb_init(b, OWNER);
    chgb_ctx_t d = ctx(40, CHGB_NONE, CHGB_NONE);
    for (uint32_t i = 0; i < 10; i++) chgb_observe(b, &d, 1);
    for (uint16_t a = 2; a < 42; a++) {
        chgb_ctx_t e = ctx((uint16_t) (a % 100), CHGB_NONE, CHGB_NONE);
        chgb_observe(b, &e, a);
    }
    CHECK(b->n_act == CHGB_ACTIONS);
    bool has1 = false;
    for (uint32_t i = 0; i < b->n_act; i++) has1 |= b->act[i] == 1;
    CHECK(has1);
    for (uint16_t a = 0; a < 60; a++) {
        chgb_ctx_t e = ctx(1, (uint16_t) (500 + a), CHGB_NONE);
        chgb_observe(b, &e, 1);
    }
    CHECK(b->n_app == CHGB_APPS);
    CHECK(chgb_observe(b, &d, CHGB_NONE) == CHGB_ERR_ARG);
    chgb_ctx_t bad = ctx(CHGB_SLOTS, 1, 1);
    CHECK(chgb_observe(b, &bad, 3) == CHGB_ERR_ARG);
}

/* ===== B5 ===== */
static uint8_t g_exp[40000];

static void b5_owner(void)
{
    printf("B5 the owner's controls\n");
    chgb_suggestion_t s, s2;
    chgb_ctx_t mon8 = ctx(8, MAIL_APP, WAKE);

    /* pause: nothing is stored */
    memcpy(&g_b2, &g_b, sizeof(g_b));
    chgb_set_learning(&g_b2, false);
    CHECK(chgb_observe(&g_b2, &mon8, MUSIC) == CHGB_PAUSED);
    CHECK(chgb_feedback(&g_b2, &mon8, INBOX, false) == CHGB_PAUSED);
    chgb_set_learning(&g_b2, true);
    CHECK(memcmp(&g_b2, &g_b, sizeof(g_b)) == 0);

    /* forget one action */
    chgb_forget_action(&g_b2, INBOX);
    chgb_suggest(&g_b2, &mon8, &s);
    CHECK(s.action != INBOX);
    for (uint32_t i = 0; i < g_b2.n_act; i++) CHECK(g_b2.act[i] != INBOX);
    chgb_ctx_t fri21 = ctx(4 * 24 + 21, PLAYER_APP, CHGB_NONE);
    chgb_suggest(&g_b2, &fri21, &s);
    CHECK(s.state == CHGB_SUGGEST && s.action == MUSIC); /* the rest is untouched */
    chgb_forget_action(&g_b2, 31337);                    /* unknown: no-op */

    /* forget an app */
    chgb_forget_app(&g_b2, MAIL_APP);
    CHECK(chgb_app_affinity(&g_b2, MAIL_APP) == 0 && chgb_app_affinity(&g_b2, PLAYER_APP) > 0);

    /* forget everything: the owner stays */
    chgb_forget_all(&g_b2);
    chgb_suggest(&g_b2, &fri21, &s);
    CHECK(s.state == CHGB_NOTHING && g_b2.n_act == 0 && memcmp(g_b2.owner, OWNER, 32) == 0);

    /* sharing is off by default and only the owner can turn it on */
    CHECK(chgb_export(&g_b, OWNER, g_exp, sizeof(g_exp)) == CHGB_DENIED);
    CHECK(chgb_set_share(&g_b, STRANGER, true) == CHGB_DENIED);
    CHECK(chgb_set_share(&g_b, OWNER, true) == CHGB_OK);
    CHECK(chgb_export(&g_b, STRANGER, g_exp, sizeof(g_exp)) == CHGB_DENIED);
    CHECK(chgb_export(&g_b, OWNER, g_exp, 100) == CHGB_ERR_SPACE);
    int32_t n = chgb_export(&g_b, OWNER, g_exp, sizeof(g_exp));
    CHECK(n == (int32_t) chgb_export_size());

    /* the owner's new device */
    CHECK(chgb_import(&g_b2, STRANGER, g_exp, (uint32_t) n) == CHGB_DENIED);
    CHECK(chgb_import(&g_b2, OWNER, g_exp, (uint32_t) n - 1) == CHGB_ERR_PARSE);
    CHECK(chgb_import(&g_b2, OWNER, g_exp, (uint32_t) n) == CHGB_OK);
    CHECK(!g_b2.share_opt_in);
    CHECK(memcmp(g_b2.w_time, g_b.w_time, sizeof(g_b.w_time)) == 0 &&
          memcmp(g_b2.w_app, g_b.w_app, sizeof(g_b.w_app)) == 0 &&
          memcmp(g_b2.w_prev, g_b.w_prev, sizeof(g_b.w_prev)) == 0);
    chgb_suggest(&g_b, &mon8, &s);
    chgb_suggest(&g_b2, &mon8, &s2);
    CHECK(memcmp(&s, &s2, sizeof(s)) == 0);

    /* any changed byte is refused, and nothing is half-written */
    for (uint32_t t = 0; t < 300; t++) {
        uint32_t at = (uint32_t) (rnd() % (uint32_t) n);
        g_exp[at] ^= (uint8_t) (1u << (rnd() % 8));
        chgb_t probe;
        memset(&probe, 0x5a, sizeof(probe));
        chgb_status_t st = chgb_import(&probe, OWNER, g_exp, (uint32_t) n);
        CHECK(st == CHGB_ERR_PARSE || st == CHGB_DENIED);
        CHECK(probe.n_act == 0x5a5a);
        n = chgb_export(&g_b, OWNER, g_exp, sizeof(g_exp)); /* restore the bytes */
    }
}

/* ===== B6 ===== */
static void b6_random(void)
{
    printf("B6 random operations\n");
    chgb_t *b = &g_b2;
    chgb_init(b, OWNER);
    bool ok = true;
    for (uint32_t i = 0; i < 60000; i++) {
        chgb_ctx_t c =
            ctx((uint16_t) (rnd() % CHGB_SLOTS), (uint16_t) (rnd() % 3 ? rnd() % 70 : CHGB_NONE),
                (uint16_t) (rnd() % 3 ? rnd() % 50 : CHGB_NONE));
        uint16_t a = (uint16_t) (rnd() % 50);
        chgb_suggestion_t s;
        switch (rnd() % 8) {
        case 0:
            chgb_feedback(b, &c, a, rnd() & 1);
            break;
        case 1:
            if (rnd() % 64 == 0) chgb_forget_action(b, a);
            break;
        case 2:
            if (rnd() % 64 == 0) chgb_forget_app(b, c.app);
            break;
        case 3:
            if (rnd() % 256 == 0) chgb_decay_all(b);
            break;
        case 4:
            chgb_suggest(b, &c, &s);
            if (s.share > 1000 || s.second > s.share || s.distinct > s.experts) ok = false;
            if (s.experts && (s.R_permille < 1000 || s.R_permille > 1000u * s.distinct)) ok = false;
            break;
        default:
            chgb_observe(b, &c, a);
            break;
        }
        if (b->n_act > CHGB_ACTIONS || b->n_app > CHGB_APPS) ok = false;
    }
    for (uint32_t s = 0; s < CHGB_SLOTS; s++)
        for (uint32_t a = 0; a < CHGB_ACTIONS; a++) {
            if (b->w_time[s][a] > CHGB_WMAX) ok = false;
            if (a >= b->n_act && b->w_time[s][a]) ok = false; /* no ghost weights */
        }
    CHECK(ok);
}

int main(void)
{
    b1_interaction();
    b2_routine();
    b3_independence();
    b4_feedback();
    b5_owner();
    b6_random();
    printf("chg_bond: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
