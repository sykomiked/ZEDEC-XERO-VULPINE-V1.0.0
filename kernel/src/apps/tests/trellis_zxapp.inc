/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Trellis's languages against the C its model runs (trellis_rules.h).
 * Slots: @logic.trellis_revoke_allowed, @num.trellis_invite_minutes_left,
 * @num.trellis_peers_in_state, @logic.trellis_peer_record_ok, and
 * Gatehouse's @logic.gatehouse_use_ok, whether a phone may post. */
#include "gatehouse_rules.h"
#include "trellis_rules.h"

APP_TEST(trellis, 12, 29)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* revoke_allowed (TL1): Eve = KRON = C over flags 0 and 1 and 0..8 admins left */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
        d[i] = wrand(0, (int64_t) TRELLIS_DEVICES);
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 1; /* one admin left: allowed */
    a[1] = 1, b[1] = 0, c[1] = 1, d[1] = 8; /* the most admins */
    a[2] = 1, b[2] = 0, c[2] = 1, d[2] = 0; /* the last admin */
    a[3] = 0, b[3] = 0, c[3] = 1, d[3] = 2; /* no admin session */
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = 2; /* this device */
    a[5] = 1, b[5] = 0, c[5] = 0, d[5] = 2; /* already revoked */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(trellis_revoke_allowed(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              !trellis_revoke_allowed(1, 0, 1, 9) && !trellis_revoke_allowed(2, 0, 1, 1) &&
              !trellis_revoke_allowed(1, -1, 1, 1) && !trellis_revoke_allowed(1, 0, 1, -1),
          "Trellis: the C rule's own edges (TL1)");
    zxapp_arg_t ra[4] = {{"sessionadmin", a, WAVE_ROWS},
                         {"isself", b, WAVE_ROWS},
                         {"isactive", c, WAVE_ROWS},
                         {"adminsleft", d, WAVE_ROWS}};
    CHECK(wave_same(app, "revoke_allowed", ra, 4, want, WAVE_ROWS, true),
          "Trellis: revoke_allowed, Eve = KRON = C");
    {
        /* max + 1 and negatives: Eve holds each to its range, KRON reads past them */
        static const int64_t one[2] = {1, 1}, zero[2] = {0, 0}, two[2] = {1, 2}, neg[2] = {0, -1},
                             nine[2] = {1, 9};
        zxapp_arg_t sa[4] = {{"sessionadmin", two, 2},
                             {"isself", zero, 2},
                             {"isactive", one, 2},
                             {"adminsleft", one, 2}};
        CHECK(wave_lp(app, "revoke_allowed", sa, 4) == ZXAPP_BOTH,
              "Trellis: an admin flag of 2: Eve false, KRON true: BOTH");
        zxapp_arg_t na[4] = {{"sessionadmin", one, 2},
                             {"isself", neg, 2},
                             {"isactive", one, 2},
                             {"adminsleft", one, 2}};
        CHECK(wave_lp(app, "revoke_allowed", na, 4) == ZXAPP_BOTH,
              "Trellis: a self flag of -1: Eve false, KRON true: BOTH");
        zxapp_arg_t xa[4] = {{"sessionadmin", one, 2},
                             {"isself", zero, 2},
                             {"isactive", one, 2},
                             {"adminsleft", nine, 2}};
        CHECK(wave_lp(app, "revoke_allowed", xa, 4) == ZXAPP_BOTH,
              "Trellis: 9 admins in a mesh of 8: Eve false, KRON true: BOTH");
    }

    /* invite_minutes_left (TL2): KRON = C over issued, ttl and now */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TRELLIS_MS_MAX - TRELLIS_INVITE_TTL_MS);
        b[i] = wrand(0, TRELLIS_INVITE_TTL_MS);
        c[i] = a[i] + wrand(-60000, TRELLIS_INVITE_TTL_MS + 60000);
        if (c[i] < 0) c[i] = 0;
        if (c[i] > TRELLIS_MS_MAX) c[i] = TRELLIS_MS_MAX;
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                                     /* 0 everywhere */
    a[1] = 0, b[1] = TRELLIS_INVITE_TTL_MS, c[1] = 0;                 /* fresh: 10 */
    a[2] = 0, b[2] = TRELLIS_INVITE_TTL_MS, c[2] = 1;                 /* 9:59.999 shows 10 */
    a[3] = 0, b[3] = TRELLIS_INVITE_TTL_MS, c[3] = 599999;            /* 1 ms shows 1 */
    a[4] = 0, b[4] = TRELLIS_INVITE_TTL_MS, c[4] = 600000;            /* lapsed: 0 */
    a[5] = 5000, b[5] = TRELLIS_INVITE_TTL_MS, c[5] = TRELLIS_MS_MAX; /* long past: 0 */
    a[6] = TRELLIS_MS_MAX, b[6] = TRELLIS_INVITE_TTL_MS, c[6] = TRELLIS_MS_MAX; /* the max */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = trellis_invite_minutes_left(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 10 && want[2] == 10 && want[3] == 1 && want[4] == 0 &&
              want[5] == 0 && want[6] == 10 &&
              trellis_invite_minutes_left(0, TRELLIS_INVITE_TTL_MS + 1, 0) == TRELLIS_REFUSED &&
              trellis_invite_minutes_left(-1, 0, 0) == TRELLIS_REFUSED &&
              trellis_invite_minutes_left(0, 0, TRELLIS_MS_MAX + 1) == TRELLIS_REFUSED,
          "Trellis: the C rule's own edges (TL2)");
    zxapp_arg_t ia[3] = {{"issued", a, WAVE_ROWS}, {"ttl", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "invite_minutes_left", ia, 3, want, WAVE_ROWS, false),
          "Trellis: invite_minutes_left, KRON = C");

    /* peers_in_state (TL3): KRON = Trebago = C over the states of 64 peers */
    for (uint32_t s = TRELLIS_ACTIVE; s <= TRELLIS_WAITING; s++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(TRELLIS_ACTIVE, TRELLIS_WAITING);
            b[i] = s;
        }
        want[0] = trellis_peers_in_state(a, b, WAVE_ROWS);
        CHECK(want[0] >= 0 && want[0] <= WAVE_ROWS, "Trellis: the C count of state %u", s);
        zxapp_arg_t pa[2] = {{"state", a, WAVE_ROWS}, {"want", b, WAVE_ROWS}};
        CHECK(wave_same(app, "peers_in_state", pa, 2, want, 1, false),
              "Trellis: peers_in_state %u, KRON = Trebago = C", s);
    }
    {
        static const int64_t all[3] = {1, 1, 1}, some[4] = {1, 2, 3, 3}, threes[4] = {3, 3, 3, 3},
                             past[2] = {1, 4}, ones[2] = {1, 1}, zero[2] = {0, 0};
        int64_t got1 = trellis_peers_in_state(all, all, 3), got2;
        got2 = trellis_peers_in_state(some, threes, 4);
        CHECK(got1 == 3 && got2 == 2 && trellis_peers_in_state(past, ones, 2) == TRELLIS_REFUSED &&
                  trellis_peers_in_state(ones, zero, 2) == TRELLIS_REFUSED &&
                  trellis_peers_in_state(ones, ones, 0) == TRELLIS_REFUSED,
              "Trellis: the C rule's own edges (TL3)");
        /* max + 1 and 0: Trebago refuses a state that is none; KRON counts it */
        zxapp_arg_t xa[2] = {{"state", past, 2}, {"want", ones, 2}};
        CHECK(wave_lp(app, "peers_in_state", xa, 2) == ZXAPP_BOTH,
              "Trellis: a state of 4: Trebago refuses, KRON answers: BOTH");
        zxapp_arg_t za[2] = {{"state", ones, 2}, {"want", zero, 2}};
        CHECK(wave_lp(app, "peers_in_state", za, 2) == ZXAPP_BOTH,
              "Trellis: a want of 0: Trebago refuses, KRON answers: BOTH");
    }

    /* peer_record_ok (TL4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 9), b[i] = wrand(0, 9), c[i] = wrand(0, 9);
        d[i] = wrand(0, 9), e[i] = wrand(0, 9), f[i] = wrand(0, INT64_C(999999999999999999));
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0; /* every least */
    a[1] = 9, b[1] = 9, c[1] = 9, d[1] = 9, e[1] = 9, f[1] = INT64_C(999999999999999999);
    a[2] = 10;                           /* peer max + 1 */
    b[3] = 10;                           /* state max + 1 */
    c[4] = 10;                           /* role max + 1 */
    e[5] = 10;                           /* held max + 1 */
    f[6] = INT64_C(1000000000000000000); /* since max + 1 */
    a[7] = -3, b[7] = -1, f[7] = -5;     /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(trellis_peer_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Trellis: the record's own edges (TL4)");
    zxapp_arg_t ka[6] = {{"peer", a, WAVE_ROWS},  {"state", b, WAVE_ROWS}, {"role", c, WAVE_ROWS},
                         {"admin", d, WAVE_ROWS}, {"held", e, WAVE_ROWS},  {"since", f, WAVE_ROWS}};
    CHECK(wave_same(app, "peer_record_ok", ka, 6, want, WAVE_ROWS, true),
          "Trellis: peer_record_ok, MYRA = C");

    /* gatehouse.use_ok (TL5): whether a phone may post, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
    }
    a[0] = 1, b[0] = 1, c[0] = 0; /* asked, allowed, not revoked */
    a[1] = 1, b[1] = 0, c[1] = 0; /* paired without allow_post */
    a[2] = 1, b[2] = 1, c[2] = 1; /* revoked */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gatehouse_use_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE,
          "Trellis: a phone posting, the C rule's own edges (TL5)");
    zxapp_arg_t ga[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", ga, 3, want, WAVE_ROWS, true),
          "Trellis: gatehouse.use_ok, Eve = KRON = C");
}
