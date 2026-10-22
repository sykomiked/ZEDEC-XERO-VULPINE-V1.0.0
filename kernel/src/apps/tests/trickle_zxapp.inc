/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Trickle's languages against the C its model runs (trickle_rules.h).
 * Slots: @num.trickle_stream_due, @num.trickle_tithe_due,
 * @num.trickle_tithe_carry, @logic.trickle_stream_pay_ok. */
#include "trickle_rules.h"

/* The four inputs a and b (rows 0..1) as one two-row argument set. */
static int32_t trickle_lp2(zxapp_t *app, const char *entry, const char *const name[4],
                           const int64_t v[4][2])
{
    zxapp_arg_t xa[4];
    for (uint32_t k = 0; k < 4; k++) xa[k] = (zxapp_arg_t){name[k], v[k], 2};
    return wave_lp(app, entry, xa, 4);
}

APP_TEST(trickle, 13, 77)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS];
    static int64_t want[WAVE_ROWS], rest[WAVE_ROWS];
    static const char *const sn[4] = {"total", "tick", "ticks", "paid"};
    static const char *const tn[4] = {"income", "tnum", "tden", "carry"};

    /* stream_due (TK1): Sutra = Trebago = C over every size of stream */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 100000 : TRK_TOTAL_MAX);
        c[i] = wrand(1, i < 32 ? 12 : TRK_TICKS_MAX);
        b[i] = wrand(0, c[i]);
        d[i] = wrand(0, a[i] * b[i] / c[i]);
    }
    a[0] = 0, b[0] = 0, c[0] = 1, d[0] = 0;                                     /* nothing */
    a[1] = TRK_TOTAL_MAX, b[1] = TRK_TICKS_MAX, c[1] = TRK_TICKS_MAX, d[1] = 0; /* all, at once */
    a[2] = 1000, b[2] = 1, c[2] = 3, d[2] = 0;   /* the first third, floored */
    a[3] = 1000, b[3] = 2, c[3] = 3, d[3] = 333; /* the second */
    a[4] = 1000, b[4] = 3, c[4] = 3, d[4] = 666; /* the last takes the held-back unit */
    a[5] = TRK_TOTAL_MAX, b[5] = 1, c[5] = TRK_TICKS_MAX, d[5] = 0; /* one tick of the most */
    a[6] = 7, b[6] = 5, c[6] = 5, d[6] = 7;                         /* paid in full: nothing more */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = trk_stream_due(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != TRK_REFUSED, "Trickle: stream row %u in range", i);
    }
    CHECK(want[0] == 0 && want[1] == TRK_TOTAL_MAX && want[2] == 333 && want[3] == 333 &&
              want[4] == 334 && want[5] == 10000100 && want[6] == 0,
          "Trickle: stream_due, the C's own edges");
    zxapp_arg_t sa[4] = {{"total", a, WAVE_ROWS},
                         {"tick", b, WAVE_ROWS},
                         {"ticks", c, WAVE_ROWS},
                         {"paid", d, WAVE_ROWS}};
    CHECK(wave_same(app, "stream_due", sa, 4, want, WAVE_ROWS, false),
          "Trickle: stream_due, Sutra = Trebago = C");
    {
        /* max + 1: ticks past Sutra's 9(5): Sutra refuses, Trebago answers */
        static const int64_t v1[4][2] = {{1000, 1000}, {1, 1}, {3, TRK_TICKS_MAX + 1}, {0, 0}};
        CHECK(trk_stream_due(1000, 1, TRK_TICKS_MAX + 1, 0) == TRK_REFUSED &&
                  trickle_lp2(app, "stream_due", sn, v1) == ZXAPP_BOTH,
              "Trickle: ticks past 9(5): Sutra refuses, Trebago answers: BOTH");
        /* a total past 9(12): both refuse */
        static const int64_t v2[4][2] = {{1, TRK_TOTAL_MAX + 1}, {1, 1}, {1, 1}, {0, 0}};
        CHECK(trk_stream_due(TRK_TOTAL_MAX + 1, 1, 1, 0) == TRK_REFUSED &&
                  trickle_lp2(app, "stream_due", sn, v2) == ZXAPP_NEITHER,
              "Trickle: a total past 9(12): both refuse: NEITHER");
        /* paid past what is owed: it would claw back, both refuse */
        static const int64_t v3[4][2] = {{1000, 1000}, {1, 1}, {3, 3}, {0, 334}};
        CHECK(trk_stream_due(1000, 1, 3, 334) == TRK_REFUSED &&
                  trickle_lp2(app, "stream_due", sn, v3) == ZXAPP_NEITHER,
              "Trickle: paid past what is owed: NEITHER");
        /* a tick past the last, a ticks of 0, a negative total: both refuse */
        static const int64_t v4[4][2] = {{1000, 1000}, {1, 4}, {3, 3}, {0, 0}};
        static const int64_t v5[4][2] = {{1000, 1000}, {1, 0}, {3, 0}, {0, 0}};
        static const int64_t v6[4][2] = {{1000, -1}, {1, 1}, {3, 3}, {0, 0}};
        CHECK(trk_stream_due(1000, 4, 3, 0) == TRK_REFUSED &&
                  trk_stream_due(1000, 0, 0, 0) == TRK_REFUSED &&
                  trk_stream_due(-1, 1, 3, 0) == TRK_REFUSED &&
                  trickle_lp2(app, "stream_due", sn, v4) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "stream_due", sn, v5) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "stream_due", sn, v6) == ZXAPP_NEITHER,
              "Trickle: a tick past the last, no ticks, a negative total: NEITHER");
    }

    /* tithe_due and tithe_carry (TK2): Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 100000 : TRK_TOTAL_MAX);
        c[i] = wrand(1, i < 32 ? 100 : TRK_TICKS_MAX);
        b[i] = wrand(0, c[i]);
        d[i] = wrand(0, c[i] - 1);
    }
    a[0] = 0, b[0] = 0, c[0] = 1, d[0] = 0;                           /* nothing */
    a[1] = TRK_TOTAL_MAX, b[1] = TRK_TICKS_MAX, c[1] = TRK_TICKS_MAX; /* the whole of the most */
    d[1] = TRK_TICKS_MAX - 1;
    a[2] = 1000, b[2] = 1, c[2] = 10, d[2] = 9; /* a tenth, 9 tenths carried in */
    a[3] = 999, b[3] = 1, c[3] = 10, d[3] = 9;  /* 1008 tenths: 100, 8 carried on */
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 9;    /* the carry makes a unit */
    a[5] = 7, b[5] = 2, c[5] = 3, d[5] = 2;     /* 16 thirds: 5, 1 carried on */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = trk_tithe_due(a[i], b[i], c[i], d[i]);
        rest[i] = trk_tithe_carry(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != TRK_REFUSED && rest[i] >= 0 && rest[i] < c[i] &&
                  want[i] * c[i] + rest[i] == a[i] * b[i] + d[i],
              "Trickle: tithe row %u exact", i);
    }
    CHECK(want[0] == 0 && rest[0] == 0 && want[1] == TRK_TOTAL_MAX && rest[1] == d[1] &&
              want[2] == 100 && rest[2] == 9 && want[3] == 100 && rest[3] == 8 && want[4] == 1 &&
              rest[4] == 0 && want[5] == 5 && rest[5] == 1,
          "Trickle: tithe_due and tithe_carry, the C's own edges");
    zxapp_arg_t ta[4] = {{"income", a, WAVE_ROWS},
                         {"tnum", b, WAVE_ROWS},
                         {"tden", c, WAVE_ROWS},
                         {"carry", d, WAVE_ROWS}};
    CHECK(wave_same(app, "tithe_due", ta, 4, want, WAVE_ROWS, false),
          "Trickle: tithe_due, Sutra = Trebago = C");
    CHECK(wave_same(app, "tithe_carry", ta, 4, rest, WAVE_ROWS, false),
          "Trickle: tithe_carry, Sutra = Trebago = C");
    {
        /* max + 1: tden past Sutra's 9(5): Sutra refuses, Trebago answers */
        static const int64_t v1[4][2] = {{1000, 1000}, {1, 1}, {10, TRK_TICKS_MAX + 1}, {0, 0}};
        CHECK(trk_tithe_due(1000, 1, TRK_TICKS_MAX + 1, 0) == TRK_REFUSED &&
                  trickle_lp2(app, "tithe_due", tn, v1) == ZXAPP_BOTH &&
                  trickle_lp2(app, "tithe_carry", tn, v1) == ZXAPP_BOTH,
              "Trickle: tden past 9(5): Sutra refuses, Trebago answers: BOTH");
        /* a share past the whole, a carry of a whole unit, tden 0, a
         * negative income, an income past 9(12): both refuse */
        static const int64_t v2[4][2] = {{1000, 1000}, {1, 11}, {10, 10}, {0, 0}};
        static const int64_t v3[4][2] = {{1000, 1000}, {1, 1}, {10, 10}, {0, 10}};
        static const int64_t v4[4][2] = {{1000, 1000}, {1, 0}, {10, 0}, {0, 0}};
        static const int64_t v5[4][2] = {{1000, -1}, {1, 1}, {10, 10}, {0, 0}};
        static const int64_t v6[4][2] = {{1000, TRK_TOTAL_MAX + 1}, {1, 1}, {10, 10}, {0, 0}};
        CHECK(trk_tithe_due(1000, 11, 10, 0) == TRK_REFUSED &&
                  trk_tithe_carry(1000, 1, 10, 10) == TRK_REFUSED &&
                  trk_tithe_due(1000, 0, 0, 0) == TRK_REFUSED &&
                  trk_tithe_carry(-1, 1, 10, 0) == TRK_REFUSED &&
                  trk_tithe_due(TRK_TOTAL_MAX + 1, 1, 10, 0) == TRK_REFUSED &&
                  trickle_lp2(app, "tithe_due", tn, v2) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "tithe_carry", tn, v3) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "tithe_due", tn, v4) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "tithe_carry", tn, v5) == ZXAPP_NEITHER &&
                  trickle_lp2(app, "tithe_due", tn, v6) == ZXAPP_NEITHER,
              "Trickle: a tithe outside its domain: both refuse: NEITHER");
    }

    /* stream_pay_ok (TK3): Eve = Sutra = C, each field swept past its range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 32 ? wrand(-2, 1000000) : wrand(TRK_AMOUNT_MAX - 3, TRK_AMOUNT_MAX + 3);
        b[i] = i < 32 ? wrand(-1000, 1000) : wrand(TRK_MINOR_MAX - 3, TRK_MINOR_MAX);
        c[i] = i < 32 ? wrand(-1000, 1000) : wrand(TRK_MINOR_MAX - 3, TRK_MINOR_MAX);
        d[i] = i < 32 ? wrand(-1000, 1000) : wrand(0, TRK_MINOR_MAX);
        e[i] = wrand(0, 2);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 1;                  /* the least, at the edge */
    a[1] = TRK_AMOUNT_MAX, b[1] = TRK_MINOR_MAX, c[1] = TRK_MINOR_MAX; /* the most */
    d[1] = TRK_MINOR_MAX, e[1] = 1;
    a[2] = 0, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 1;                  /* nothing due */
    a[3] = 5, b[3] = -1, c[3] = 0, d[3] = 0, e[3] = 1;                 /* one unit short */
    a[4] = 5, b[4] = 0, c[4] = -1, d[4] = 0, e[4] = 1;                 /* past the cap */
    a[5] = 5, b[5] = 0, c[5] = 0, d[5] = -1, e[5] = 1;                 /* past the total */
    a[6] = 5, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = 0;                  /* stopped */
    a[7] = TRK_AMOUNT_MAX + 1, b[7] = 0, c[7] = 0, d[7] = 0, e[7] = 1; /* no posting carries it */
    a[8] = 5, b[8] = 0, c[8] = 0, d[8] = 0, e[8] = 9;                  /* no approval */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(trk_stream_pay_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE,
          "Trickle: stream_pay_ok, the C's own edges");
    zxapp_arg_t pa[5] = {{"due", a, WAVE_ROWS},
                         {"after", b, WAVE_ROWS},
                         {"room", c, WAVE_ROWS},
                         {"left", d, WAVE_ROWS},
                         {"approved", e, WAVE_ROWS}};
    CHECK(wave_same(app, "stream_pay_ok", pa, 5, want, WAVE_ROWS, true),
          "Trickle: stream_pay_ok, Eve = Sutra = C");
    {
        /* max + 1: a margin past 18 digits, an approval past one digit:
         * Eve says FALSE, Sutra refuses */
        static const int64_t du[2] = {5, 5}, z[2] = {0, 0}, on[2] = {1, 1};
        static const int64_t big[2] = {0, TRK_MINOR_MAX + 1}, ten[2] = {1, 10};
        zxapp_arg_t xa[5] = {
            {"due", du, 2}, {"after", big, 2}, {"room", z, 2}, {"left", z, 2}, {"approved", on, 2}};
        zxapp_arg_t ya[5] = {
            {"due", du, 2}, {"after", z, 2}, {"room", z, 2}, {"left", z, 2}, {"approved", ten, 2}};
        CHECK(!trk_stream_pay_ok(5, TRK_MINOR_MAX + 1, 0, 0, 1) &&
                  wave_lp(app, "stream_pay_ok", xa, 5) == ZXAPP_BOTH,
              "Trickle: a margin past 18 digits: Eve FALSE, Sutra refuses: BOTH");
        CHECK(!trk_stream_pay_ok(5, 0, 0, 0, 10) &&
                  wave_lp(app, "stream_pay_ok", ya, 5) == ZXAPP_BOTH,
              "Trickle: an approval past one digit: Eve FALSE, Sutra refuses: BOTH");
    }
}
