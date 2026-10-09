/* test_bombsquad.c — preemptive debugging tests.
 *
 * The headline test is the whole thesis: a fault is DETECTED AND CORRECTED
 * while the margin is still positive — caught during the burn, never allowed to
 * detonate. A linear watchdog on the same signal would have seen nothing until
 * the breach.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/bombsquad \
 *       src/bombsquad/test_bombsquad.c src/bombsquad/bombsquad.c \
 *       -o /tmp/test_bs && /tmp/test_bs
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "bombsquad.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

/* A resource leaking a little every tick: the classic slow fuse. */
typedef struct { int32_t margin; int32_t burn; int defused; } leak_t;
static int32_t leak_margin(void *c) { leak_t *l = c; l->margin -= l->burn; return l->margin; }
static int32_t leak_peek(void *c)   { return ((leak_t *)c)->margin; }
static int leak_defuse(void *c) { leak_t *l = c; l->burn = 0; l->margin += 400; l->defused++; return 0; }

/* Steady: never moves. */
static int32_t steady_margin(void *c) { (void)c; return 1000; }

/* No run-up: fine until it is suddenly gone. */
typedef struct { uint32_t t; } cliff_t;
static int32_t cliff_margin(void *c) {
    cliff_t *k = c; k->t++;
    return (k->t < 20u) ? 1000 : -1;
}

int main(void) {
    printf("Bomb squad (preemptive debugging)\n");

    /* ---- THE THESIS: caught and corrected during the burn ---- */
    printf("the fuse is seen and cut before detonation:\n");
    {   bs_squad_t s; bs_init(&s, 32);
        leak_t leak = { 1000, 25, 0 };            /* 40 ticks of fuse */
        int idx = bs_watch(&s, "handle-pool", leak_margin, leak_defuse, &leak);
        CHECK(idx == 0, "invariant registered");

        uint32_t armed_at = 0, ticks = 0;
        for (uint32_t t = 0; t < 60; t++) {
            bs_tick(&s);
            ticks++;
            if (!armed_at && bs_state(&s, 0, 0) == BS_ARMED) armed_at = ticks;
            if (bs_state(&s, 0, 0) == BS_BREACHED) break;
        }
        CHECK(armed_at > 0, "the burn was detected (ARMED) before anything failed");
        CHECK(leak.defused == 1, "a defusal was attempted DURING the burn");
        CHECK(leak_peek(&leak) > 0, "margin never reached zero — it did not go off");
        CHECK(s.total_breached == 0, "NO detonation occurred");
        CHECK(bs_state(&s, 0, 0) == BS_SAFE,
              "after defusal the invariant returns to SAFE (the next sample confirms it)");
        printf("       (armed at tick %u, margin held at %d)\n", armed_at, leak_peek(&leak));
    }

    /* ---- the estimate is a fuse length, in the system's own time unit ---- */
    printf("fuse length is reported, not just a warning:\n");
    {   bs_squad_t s; bs_init(&s, 100);
        leak_t leak = { 1000, 10, 0 };            /* ~100 ticks */
        bs_watch(&s, "slow-burn", leak_margin, 0, &leak);   /* observe only */
        for (int i = 0; i < 5; i++) bs_tick(&s);
        uint32_t ttz = 0;
        bs_state_t st = bs_state(&s, 0, &ttz);
        CHECK(st == BS_ARMED || st == BS_WATCH, "a slow burn is tracked");
        CHECK(ttz > 60 && ttz < 130,
              "estimated ticks-to-detonation is in the right neighbourhood");
        printf("       (estimated fuse: %u ticks)\n", ttz);
    }

    /* ---- WATCH vs ARMED: distance matters, not just direction ---- */
    printf("distant burns are not treated as emergencies:\n");
    {   bs_squad_t s; bs_init(&s, 10);            /* only 10 ticks counts as soon */
        leak_t slow = { 100000, 1, 0 };
        bs_watch(&s, "century-leak", leak_margin, 0, &slow);
        for (int i = 0; i < 4; i++) bs_tick(&s);
        CHECK(bs_state(&s, 0, 0) == BS_WATCH,
              "a burn beyond the horizon is WATCH, not ARMED (no alarm fatigue)");
    }

    /* ---- a steady invariant is never disturbed ---- */
    printf("no false alarms on healthy signals:\n");
    {   bs_squad_t s; bs_init(&s, 32);
        bs_watch(&s, "steady", steady_margin, 0, 0);
        for (int i = 0; i < 30; i++) bs_tick(&s);
        CHECK(bs_state(&s, 0, 0) == BS_SAFE, "a flat margin stays SAFE for 30 ticks");
        CHECK(s.total_armed == 0, "never armed");
    }

    /* ---- the honest limit: no run-up means no fuse to see ---- */
    printf("the limit, tested rather than hidden:\n");
    {   bs_squad_t s; bs_init(&s, 32);
        cliff_t k = { 0 };
        bs_watch(&s, "cliff", cliff_margin, 0, &k);
        int armed = 0;
        for (int i = 0; i < 25; i++) { bs_tick(&s); if (bs_state(&s,0,0) == BS_ARMED) armed = 1; }
        CHECK(!armed, "a fault with NO run-up is never armed — there was no fuse");
        CHECK(bs_state(&s, 0, 0) == BS_BREACHED, "it is still reported once it breaches");
        CHECK(s.total_breached == 1,
              "a persistent breach counts ONCE, not once per tick"); }

    /* ---- triage across several simultaneous burns ---- */
    printf("triage:\n");
    {   bs_squad_t s; bs_init(&s, 200);
        leak_t a = { 1000, 5, 0 }, b = { 1000, 50, 0 }, c = { 1000, 1, 0 };
        bs_watch(&s, "slow",   leak_margin, 0, &a);
        bs_watch(&s, "urgent", leak_margin, 0, &b);
        bs_watch(&s, "creep",  leak_margin, 0, &c);
        for (int i = 0; i < 5; i++) bs_tick(&s);
        int u = bs_most_urgent(&s);
        CHECK(u == 1, "the SOONEST detonation is identified, not the largest drop");
        uint32_t t0 = 0, t1 = 0;
        bs_state(&s, 0, &t0); bs_state(&s, 1, &t1);
        CHECK(t1 < t0, "fuse lengths order the queue"); }

    printf("\n%s bombsquad: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
