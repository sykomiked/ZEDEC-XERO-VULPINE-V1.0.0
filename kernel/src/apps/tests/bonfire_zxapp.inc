/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bonfire's languages against the C its model runs (bonfire_rules.h).
 * Slots: @logic.bonfire_guest_join_ok, @num.bonfire_seats_left,
 * @num.bonfire_rsvp_headcount, @logic.bonfire_event_record_ok,
 * @logic.bonfire_invite_record_ok and, used as they are,
 * @num.witness_kind_count and @num.used_x100. */
#include "bonfire_rules.h"
#include "witness_rules.h"

APP_TEST(bonfire, 27, 95)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* guest_join_ok (BR1): Eve = MYRA = C, every place on the list and every party around
     * the seats' edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t seats = wrand(1, BONFIRE_SEATS_MAX), going = wrand(0, seats);
        int64_t party = wrand(1, BONFIRE_PARTY_MAX);
        a[i] = i % 4 ? wrand(0, BONFIRE_GUESTS - 1) : -1;
        b[i] = i % 3 ? bonfire_seat_room(seats, going, party) : wrand(-5, 5);
    }
    a[0] = 0, b[0] = 0;                                  /* the first guest, the last seat */
    a[1] = BONFIRE_GUESTS - 1, b[1] = BONFIRE_SEATS_MAX; /* the last place, every seat */
    a[2] = -1, b[2] = 10;                                /* not on the list */
    a[3] = 5, b[3] = bonfire_seat_room(40, 39, 2);       /* a party of 2, one seat left */
    a[4] = 5, b[4] = bonfire_seat_room(40, 38, 2);       /* exactly fits */
    a[5] = 5, b[5] = bonfire_seat_room(40, 0, 0);        /* a party of none */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(bonfire_guest_join_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_TRUE && want[5] == ZXAPP_FALSE &&
              !bonfire_guest_join_ok(BONFIRE_GUESTS, 0) &&
              !bonfire_guest_join_ok(0, BONFIRE_SEATS_MAX + 1) &&
              bonfire_seat_room(40, 41, 1) == BONFIRE_NO_ROOM &&
              bonfire_seat_room(40, 0, BONFIRE_PARTY_MAX + 1) == BONFIRE_NO_ROOM &&
              bonfire_guest_may_join(0, 40, 38, 2) && !bonfire_guest_may_join(0, 40, 39, 2),
          "Bonfire: the C join rule's own edges (BR1)");
    zxapp_arg_t ja[2] = {{"listpos", a, WAVE_ROWS}, {"seatroom", b, WAVE_ROWS}};
    CHECK(wave_same(app, "guest_join_ok", ja, 2, want, WAVE_ROWS, true),
          "Bonfire: guest_join_ok, Eve = MYRA = C");
    {
        /* a margin past its meaning (max + 1) is a day to MYRA and no guest or seat to Eve */
        static const int64_t p[2][2] = {{0, BONFIRE_GUESTS}, {0, 0}};
        static const int64_t r[2][2] = {{0, 0}, {0, BONFIRE_SEATS_MAX + 1}};
        for (uint32_t k = 0; k < 2; k++) {
            zxapp_arg_t xa[2] = {{"listpos", p[k], 2}, {"seatroom", r[k], 2}};
            CHECK(wave_lp(app, "guest_join_ok", xa, 2) == ZXAPP_BOTH &&
                      !bonfire_guest_join_ok(p[k][1], r[k][1]),
                  "Bonfire: guest_join_ok case %u: MYRA says yes, Eve no: BOTH", k);
        }
    }

    /* seats_left (BR2): Trebago = KRON = C, one gathering per row */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, BONFIRE_SEATS_MAX);
        b[i] = wrand(0, a[i]);
    }
    a[0] = 0, b[0] = 0;                                 /* no seats */
    a[1] = BONFIRE_SEATS_MAX, b[1] = 0;                 /* every seat free */
    a[2] = BONFIRE_SEATS_MAX, b[2] = BONFIRE_SEATS_MAX; /* full */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = bonfire_seats_left(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == BONFIRE_SEATS_MAX && want[2] == 0 &&
              bonfire_seats_left(40, 41) == BONFIRE_REFUSED &&
              bonfire_seats_left(40, 45) == BONFIRE_REFUSED &&
              bonfire_seats_left(40, -1) == BONFIRE_REFUSED &&
              bonfire_seats_left(BONFIRE_SEATS_MAX + 1, 0) == BONFIRE_REFUSED,
          "Bonfire: the C seats-left rule's own edges (BR2)");
    zxapp_arg_t sa[2] = {{"seats", a, WAVE_ROWS}, {"going", b, WAVE_ROWS}};
    CHECK(wave_same(app, "seats_left", sa, 2, want, WAVE_ROWS, false),
          "Bonfire: seats_left, Trebago = KRON = C");
    {
        /* more coming than seats, below 0, past 9999 seats: Trebago refuses, KRON answers */
        static const int64_t s[3][2] = {{40, 40}, {40, 40}, {40, BONFIRE_SEATS_MAX + 1}};
        static const int64_t g[3][2] = {{0, 41}, {0, -1}, {0, 0}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[2] = {{"seats", s[k], 2}, {"going", g[k], 2}};
            CHECK(wave_lp(app, "seats_left", xa, 2) == ZXAPP_BOTH &&
                      bonfire_seats_left(s[k][1], g[k][1]) == BONFIRE_REFUSED,
                  "Bonfire: seats_left case %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* rsvp_headcount (BR3): Trebago = KRON = C, 1 to 100 guests */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t pa[BONFIRE_GUESTS], an[BONFIRE_GUESTS];
        uint32_t n = r == 0 ? BONFIRE_GUESTS : 2u + (uint32_t) wrand(0, BONFIRE_GUESTS - 2);
        for (uint32_t i = 0; i < n; i++) {
            pa[i] = r == 0 ? BONFIRE_PARTY_MAX : wrand(1, BONFIRE_PARTY_MAX);
            an[i] = r == 0 ? BONFIRE_YES : wrand(BONFIRE_NONE, BONFIRE_MAYBE);
        }
        int64_t w = bonfire_rsvp_headcount(pa, an, n);
        zxapp_arg_t ha[2] = {{"party", pa, n}, {"answer", an, n}};
        CHECK(w >= 0 && (r || w == BONFIRE_GUESTS * BONFIRE_PARTY_MAX) &&
                  wave_same(app, "rsvp_headcount", ha, 2, &w, 1, false),
              "Bonfire: the people coming of %u guests, Trebago = KRON = C", n);
    }
    {
        static const int64_t pa[5] = {1, 4, 2, 99, 3}, an[5] = {1, 1, 2, 1, 3};
        CHECK(bonfire_rsvp_headcount(pa, an, 5) == 104 && bonfire_rsvp_headcount(pa, an, 0) == 0,
              "Bonfire: the C headcount's own sum (BR3)");
        /* an answer of 4, a party of 0 or of 100: Trebago refuses the sum, KRON adds */
        static const int64_t p[3][2] = {{2, 2}, {2, 0}, {2, BONFIRE_PARTY_MAX + 1}};
        static const int64_t q[3][2] = {{1, 4}, {1, 1}, {1, 1}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[2] = {{"party", p[k], 2}, {"answer", q[k], 2}};
            CHECK(wave_lp(app, "rsvp_headcount", xa, 2) == ZXAPP_BOTH &&
                      bonfire_rsvp_headcount(p[k], q[k], 2) == BONFIRE_REFUSED,
                  "Bonfire: rsvp_headcount case %u: Trebago refuses, KRON adds: BOTH", k);
        }
    }

    /* event_record_ok (BR4): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100, 1100);
        b[i] = i % 2 ? wrand(1600000000, 2000000000) : wrand(-110000000000, 110000000000);
        c[i] = wrand(-11000, 11000);
        d[i] = wrand(-11000, 11000);
        e[i] = wrand(-1100, 1100);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;                           /* every least */
    a[1] = 999, b[1] = BONFIRE_START_MAX, c[1] = 9999, d[1] = 9999, e[1] = 999; /* most */
    a[2] = 1000, b[2] = 0, c[2] = 1, d[2] = 0, e[2] = 0;                  /* gathering past i3 */
    a[3] = 1, b[3] = BONFIRE_START_MAX + 1, c[3] = 1, d[3] = 0, e[3] = 0; /* start past i11 */
    a[4] = 1, b[4] = 0, c[4] = 10000, d[4] = 0, e[4] = 0;                 /* seats past i4 */
    a[5] = 1, b[5] = 0, c[5] = 1, d[5] = 10000, e[5] = 0;                 /* going past i4 */
    a[6] = 1, b[6] = 0, c[6] = 1, d[6] = 0, e[6] = 1000;                  /* venue past i3 */
    a[7] = 1, b[7] = 0, c[7] = 1, d[7] = 0, e[7] = -1;                    /* no venue: fits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(bonfire_event_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Bonfire: the gathering record's own edges (BR4)");
    zxapp_arg_t ga[5] = {{"gathering", a, WAVE_ROWS},
                         {"start", b, WAVE_ROWS},
                         {"seats", c, WAVE_ROWS},
                         {"going", d, WAVE_ROWS},
                         {"venue", e, WAVE_ROWS}};
    CHECK(wave_same(app, "event_record_ok", ga, 5, want, WAVE_ROWS, true),
          "Bonfire: event_record_ok, MYRA = C");

    /* invite_record_ok (BR5): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100, 1100);
        b[i] = wrand(-1100, 1100);
        c[i] = wrand(-11, 11);
        d[i] = wrand(-110, 110);
        e[i] = i % 3 ? wrand(1600000000000, 2000000000000) : wrand(-11000000000000, 11000000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;                         /* every least */
    a[1] = 999, b[1] = 999, c[1] = 9, d[1] = 99, e[1] = BONFIRE_STAMP_MAX;    /* every most */
    a[2] = 1000, b[2] = 0, c[2] = 0, d[2] = 1, e[2] = 0;                      /* gathering */
    a[3] = 1, b[3] = 1000, c[3] = 0, d[3] = 1, e[3] = 0;                      /* guest */
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1, e[4] = 0;                        /* answer */
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = 100, e[5] = 0;                       /* party */
    a[6] = 1, b[6] = 1, c[6] = 0, d[6] = 1, e[6] = BONFIRE_STAMP_MAX + 1;     /* stamp */
    a[7] = -999, b[7] = -1, c[7] = -9, d[7] = -99, e[7] = -BONFIRE_STAMP_MAX; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(bonfire_invite_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Bonfire: the invite record's own edges (BR5)");
    zxapp_arg_t ia[5] = {{"gathering", a, WAVE_ROWS},
                         {"guest", b, WAVE_ROWS},
                         {"answer", c, WAVE_ROWS},
                         {"party", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "invite_record_ok", ia, 5, want, WAVE_ROWS, true),
          "Bonfire: invite_record_ok, MYRA = C");

    /* witness.kind_count, used as it is: how many guests gave each answer */
    for (uint32_t r = 0; r < BONFIRE_ANSWERS; r++) {
        int64_t an[WAVE_ROWS], wv[WAVE_ROWS];
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            an[i] = wrand(BONFIRE_NONE, BONFIRE_MAYBE);
            wv[i] = r;
        }
        int64_t w = witness_kind_count(an, WAVE_ROWS, r);
        zxapp_arg_t ka[2] = {{"kind", an, WAVE_ROWS}, {"want", wv, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &w, 1, false),
              "Bonfire: guests who answered %u, witness.kind_count = C", r);
    }

    /* used_share.used_x100, used as it is: how full each gathering is (BR6) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, BONFIRE_SEATS_MAX);
        b[i] = bonfire_seats_left(a[i], wrand(0, a[i]));
        want[i] = bonfire_fill_x100(a[i], b[i]);
    }
    CHECK(bonfire_fill_x100(40, 30) == 2500 && bonfire_fill_x100(40, 0) == 10000 &&
              bonfire_fill_x100(0, 0) == BONFIRE_REFUSED &&
              bonfire_fill_x100(40, 41) == BONFIRE_REFUSED,
          "Bonfire: the C fill's own edges (BR6)");
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Bonfire: how full, used_share.used_x100 = C");
}
