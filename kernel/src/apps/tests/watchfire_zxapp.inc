/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Watchfire's languages against the C its model runs (watchfire_rules.h).
 * Slots: @logic.watchfire_place_share_ok, @num.watchfire_cancel_left,
 * @logic.watchfire_sos_ring_ok, @logic.watchfire_medical_id_ok,
 * @logic.watchfire_sos_contact_ok; Warren's
 * @logic.warren_approval_ok is used as is. */
#include "warren_rules.h"
#include "watchfire_rules.h"

APP_TEST(watchfire, 15, 143)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    static bool truth[WAVE_ROWS];

    /* place_share_ok (WF1): Eve = KRON = C, every flag swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = (int64_t) (i & 1u);
        b[i] = (int64_t) ((i >> 1) & 1u);
        c[i] = (int64_t) ((i >> 2) & 1u);
        d[i] = (int64_t) ((i >> 3) & 1u);
        if (i >= 16u) a[i] = b[i] = c[i] = d[i] = 1; /* the one yes, more often */
        if (i >= 40u) d[i] = wrand(0, 1);
        truth[i] = watchfire_place_share_ok(a[i], b[i], c[i], d[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(!truth[0] && truth[15] && !truth[7] && !truth[14] && truth[16] &&
              !watchfire_place_share_ok(2, 1, 1, 1) && !watchfire_place_share_ok(1, 1, 1, -1) &&
              !watchfire_place_share_ok(1, 0, 1, 1),
          "Watchfire: place_share_ok, the C's own edges");
    zxapp_arg_t pa[4] = {{"started", a, WAVE_ROWS},
                         {"optin", b, WAVE_ROWS},
                         {"listed", c, WAVE_ROWS},
                         {"placeok", d, WAVE_ROWS}};
    CHECK(wave_same(app, "place_share_ok", pa, 4, want, WAVE_ROWS, true),
          "Watchfire: place_share_ok, Eve = KRON = C");
    {
        /* a started of 2 and a placeok of 3: Eve refuses, KRON reads yes: BOTH */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2}, three[2] = {1, 3};
        zxapp_arg_t sa[4] = {
            {"started", two, 2}, {"optin", one, 2}, {"listed", one, 2}, {"placeok", one, 2}};
        CHECK(!watchfire_place_share_ok(2, 1, 1, 1) &&
                  wave_lp(app, "place_share_ok", sa, 4) == ZXAPP_BOTH,
              "Watchfire: a started of 2: Eve refuses, KRON shares: BOTH");
        zxapp_arg_t ka[4] = {
            {"started", one, 2}, {"optin", one, 2}, {"listed", one, 2}, {"placeok", three, 2}};
        CHECK(!watchfire_place_share_ok(1, 1, 1, 3) &&
                  wave_lp(app, "place_share_ok", ka, 4) == ZXAPP_BOTH,
              "Watchfire: a placeok of 3: Eve refuses, KRON shares: BOTH");
    }

    /* cancel_left (WF2): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(WATCHFIRE_WINDOW_MIN, WATCHFIRE_WINDOW_MAX);
        b[i] = i < 48u ? wrand(0, 70) : wrand(0, WATCHFIRE_WAIT_MAX);
    }
    a[0] = 30, b[0] = 0;                  /* just started: 30 */
    a[1] = 30, b[1] = 29;                 /* the last second: 1 */
    a[2] = 30, b[2] = 30;                 /* run out: 0 */
    a[3] = 10, b[3] = 0;                  /* the shortest window: 10 */
    a[4] = 60, b[4] = 0;                  /* the longest window: 60 */
    a[5] = 0, b[5] = 0;                   /* no window at all: 0 */
    a[6] = 99, b[6] = 0;                  /* the picture's max: 99 */
    a[7] = 30, b[7] = WATCHFIRE_WAIT_MAX; /* the longest wait: 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = watchfire_cancel_left(a[i], b[i]);
        CHECK(want[i] >= 0, "Watchfire: cancel_left row %u is inside the domain", i);
    }
    CHECK(want[0] == 30 && want[1] == 1 && want[2] == 0 && want[3] == 10 && want[4] == 60 &&
              want[5] == 0 && want[6] == 99 && want[7] == 0,
          "Watchfire: cancel_left, the C's own edges");
    zxapp_arg_t ca[2] = {{"window", a, WAVE_ROWS}, {"waited", b, WAVE_ROWS}};
    CHECK(wave_same(app, "cancel_left", ca, 2, want, WAVE_ROWS, false),
          "Watchfire: cancel_left, Sutra = KRON = C");
    {
        /* a wait of -1, a window of 100 (99 + 1): Sutra refuses, KRON answers: BOTH */
        static const int64_t w30[2] = {30, 30}, neg[2] = {0, -1}, w100[2] = {30, 100},
                             zero[2] = {0, 0};
        zxapp_arg_t na[2] = {{"window", w30, 2}, {"waited", neg, 2}};
        CHECK(watchfire_cancel_left(30, -1) == WATCHFIRE_REFUSED &&
                  wave_lp(app, "cancel_left", na, 2) == ZXAPP_BOTH,
              "Watchfire: a wait of -1: Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t wa[2] = {{"window", w100, 2}, {"waited", zero, 2}};
        CHECK(watchfire_cancel_left(100, 0) == WATCHFIRE_REFUSED &&
                  wave_lp(app, "cancel_left", wa, 2) == ZXAPP_BOTH,
              "Watchfire: a window of 100: Sutra refuses, KRON answers: BOTH");
    }

    /* sos_ring_ok (WF3): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 80);
        b[i] = i % 3 ? 0 : wrand(1, 60);
        c[i] = i % 5 ? 0 : 1;
        truth[i] = watchfire_ring_ok(a[i], b[i], c[i]);
    }
    a[0] = WATCHFIRE_WINDOW_MIN, b[0] = 0, c[0] = 0;     /* rings */
    a[1] = WATCHFIRE_WINDOW_MAX, b[1] = 0, c[1] = 0;     /* rings */
    a[2] = WATCHFIRE_WINDOW_MIN - 1, b[2] = 0, c[2] = 0; /* too short: skipped, no */
    a[3] = WATCHFIRE_WINDOW_MAX + 1, b[3] = 0, c[3] = 0; /* too long: no */
    a[4] = 30, b[4] = 1, c[4] = 0;                       /* a second left: no */
    a[5] = 30, b[5] = 0, c[5] = 1;                       /* cancelled: no */
    a[6] = 0, b[6] = 0, c[6] = 0;                        /* no window: no */
    for (uint32_t i = 0; i < 7u; i++) truth[i] = watchfire_ring_ok(a[i], b[i], c[i]);
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && !truth[5] && !truth[6],
          "Watchfire: sos_ring_ok, the C's own edges");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(truth[i]);
    zxapp_arg_t ra[3] = {
        {"window", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}, {"cancelled", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sos_ring_ok", ra, 3, want, WAVE_ROWS, true),
          "Watchfire: sos_ring_ok, Eve = KRON = C");
    {
        /* a left of -1 and a cancelled of -1: Eve refuses, KRON rings: BOTH */
        static const int64_t w[2] = {30, 30}, z[2] = {0, 0}, n[2] = {0, -1};
        zxapp_arg_t la[3] = {{"window", w, 2}, {"left", n, 2}, {"cancelled", z, 2}};
        CHECK(!watchfire_ring_ok(30, -1, 0) && wave_lp(app, "sos_ring_ok", la, 3) == ZXAPP_BOTH,
              "Watchfire: a left of -1: Eve refuses, KRON rings: BOTH");
        zxapp_arg_t xa[3] = {{"window", w, 2}, {"left", z, 2}, {"cancelled", n, 2}};
        CHECK(!watchfire_ring_ok(30, 0, -1) && wave_lp(app, "sos_ring_ok", xa, 3) == ZXAPP_BOTH,
              "Watchfire: a cancelled of -1: Eve refuses, KRON rings: BOTH");
    }

    /* medical_id_ok (WF4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 6);
        b[i] = wrand(0, 255);
        c[i] = wrand(0, 999999999999999LL);
    }
    a[0] = 1, b[0] = 0, c[0] = 0;                    /* every least */
    a[1] = 9, b[1] = 999, c[1] = 999999999999999;    /* every most */
    a[2] = 10;                                       /* past 1 digit */
    b[3] = 1000;                                     /* past 3 digits */
    c[4] = 1000000000000000;                         /* past 15 digits */
    a[5] = -9, b[5] = -999, c[5] = -999999999999999; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = watchfire_medical_fits(a[i], b[i], c[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && truth[5] && truth[6],
          "Watchfire: medical_id_ok, the C's own edges");
    zxapp_arg_t ma[3] = {{"field", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(wave_same(app, "medical_id_ok", ma, 3, want, WAVE_ROWS, true),
          "Watchfire: medical_id_ok, MYRA = C");

    /* sos_contact_ok (WF5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 9);
        b[i] = wrand(1, 4294967295LL);
        c[i] = wrand(1, 47);
        d[i] = wrand(0, 999999999999999LL);
    }
    a[0] = 1, b[0] = 1, c[0] = 1, d[0] = 0;                          /* every least */
    a[1] = 99, b[1] = 9999999999, c[1] = 99, d[1] = 999999999999999; /* every most */
    a[2] = 100;                                                      /* past 2 digits */
    b[3] = 10000000000;                                              /* past 10 digits */
    c[4] = 100;                                                      /* past 2 digits */
    d[5] = 1000000000000000;                                         /* past 15 digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = watchfire_contact_fits(a[i], b[i], c[i], d[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && !truth[5] && truth[6],
          "Watchfire: sos_contact_ok, the C's own edges");
    zxapp_arg_t ea[4] = {{"seq", a, WAVE_ROWS},
                         {"peer", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(wave_same(app, "sos_contact_ok", ea, 4, want, WAVE_ROWS, true),
          "Watchfire: sos_contact_ok, MYRA = C");

    /* warren.approval_ok, used for WM2: only the person's own session and yes */
    {
        static const int64_t ow[4] = {1, 1, 0, 0}, ap[4] = {1, 0, 1, 0};
        int64_t w[4];
        for (uint32_t i = 0; i < 4; i++) w[i] = T_(warren_approval_ok(ow[i], ap[i]));
        zxapp_arg_t wa[2] = {{"owner", ow, 4}, {"approved", ap, 4}};
        CHECK(wave_same(app, "warren.approval_ok", wa, 2, w, 4, true),
              "Watchfire: Warren's approval, used as is, Eve = KRON = C");
    }
}
