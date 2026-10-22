/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Coffer's languages, witnessed, row by row against the rules of
 * coffer_model.c (written out here, since this file cannot include the
 * model's header: it reaches zxm.h). Slots: @num.coffer_balance,
 * @logic.coffer_transfer_ok, @num.coffer_holdings. */

#define CF_MINOR_MAX  INT64_C(999999999999999999) /* COFFER_MINOR_MAX */
#define CF_AMOUNT_MAX INT64_C(9007199254740991)   /* COFFER_AMOUNT_MAX */

static bool cf_transfer_ok(int64_t amount, int64_t after, int64_t room, int64_t gap)
{
    return amount >= 1 && amount <= CF_AMOUNT_MAX && after >= 0 && after <= CF_MINOR_MAX &&
           room >= 0 && room <= CF_MINOR_MAX && gap == 0; /* coffer_transfer_ok */
}

APP_TEST(coffer, 5, 73)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* balance: before + moved, 18 digits either way, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-CF_MINOR_MAX / 2, CF_MINOR_MAX / 2);
        b[i] = wrand(-CF_MINOR_MAX / 2, CF_MINOR_MAX / 2);
    }
    a[0] = 0, b[0] = 0;                         /* nothing */
    a[1] = CF_MINOR_MAX - 1, b[1] = 1;          /* the max */
    a[2] = -CF_MINOR_MAX + 1, b[2] = -1;        /* the least */
    a[3] = CF_MINOR_MAX, b[3] = -CF_MINOR_MAX;  /* both ends at once */
    a[4] = 100, b[4] = -250;                    /* below zero (an issuer's side) */
    a[5] = CF_AMOUNT_MAX, b[5] = CF_AMOUNT_MAX; /* two of libzxv's largest */
    a[6] = 1, b[6] = -1;                        /* back to zero */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = a[i] + b[i];
    zxapp_arg_t ba[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "balance", ba, 2, want, WAVE_ROWS, false),
          "Coffer: balance, Sutra = Trebago = C");
    {
        /* max + 1: past Sutra's s9(18), refused there; Trebago answers */
        static const int64_t x[2] = {1, CF_MINOR_MAX}, y[2] = {0, 1};
        zxapp_arg_t xa[2] = {{"before", x, 2}, {"moved", y, 2}};
        CHECK(wave_lp(app, "balance", xa, 2) == ZXAPP_BOTH,
              "Coffer: a balance past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* a negative past 18 digits too */
        static const int64_t n[2] = {0, -CF_MINOR_MAX - 1}, z[2] = {0, 0};
        zxapp_arg_t na[2] = {{"before", n, 2}, {"moved", z, 2}};
        CHECK(wave_lp(app, "balance", na, 2) == ZXAPP_BOTH,
              "Coffer: a debt past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* past 64 bits: both refuse, nothing to show */
        static const int64_t m[2] = {INT64_MAX, INT64_MAX}, o[2] = {1, 1};
        zxapp_arg_t ma[2] = {{"before", m, 2}, {"moved", o, 2}};
        CHECK(wave_lp(app, "balance", ma, 2) == ZXAPP_NEITHER,
              "Coffer: past 64 bits: both refuse: NEITHER");
    }

    /* transfer_ok: amount, after, room, gap, Eve = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, i < 32 ? 1000000 : CF_AMOUNT_MAX + 5);
        b[i] = wrand(-5, i < 32 ? 1000000 : CF_MINOR_MAX);
        c[i] = wrand(-5, i < 32 ? 100000 : CF_MINOR_MAX);
        d[i] = i % 4 == 3 ? wrand(-2, 2) : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0;                         /* every least */
    a[1] = CF_AMOUNT_MAX, b[1] = CF_MINOR_MAX, c[1] = CF_MINOR_MAX; /* every max */
    d[1] = 0;
    a[2] = 0, b[2] = 10, c[2] = 10, d[2] = 0;                 /* nothing to move */
    a[3] = CF_AMOUNT_MAX + 1, b[3] = 10, c[3] = 10, d[3] = 0; /* amount max + 1 */
    a[4] = 5, b[4] = -1, c[4] = 10, d[4] = 0;                 /* below zero by one */
    a[5] = 5, b[5] = 10, c[5] = -1, d[5] = 0;                 /* past the day's cap by one */
    a[6] = 5, b[6] = 10, c[6] = 10, d[6] = 1;                 /* not balanced */
    a[7] = 5, b[7] = 10, c[7] = 10, d[7] = -1;                /* not balanced the other way */
    a[8] = -5, b[8] = 10, c[8] = 10, d[8] = 0;                /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(cf_transfer_ok(a[i], b[i], c[i], d[i]));
    zxapp_arg_t ta[4] = {{"amount", a, WAVE_ROWS},
                         {"after", b, WAVE_ROWS},
                         {"room", c, WAVE_ROWS},
                         {"gap", d, WAVE_ROWS}};
    CHECK(wave_same(app, "transfer_ok", ta, 4, want, WAVE_ROWS, true),
          "Coffer: transfer_ok, Eve = Sutra = C");
    {
        /* after max + 1: Eve says FALSE, Sutra's s9(18) refuses it: BOTH */
        static const int64_t m[2] = {5, 5}, af[2] = {0, CF_MINOR_MAX + 1}, r[2] = {0, 0},
                             g[2] = {0, 0};
        zxapp_arg_t xa[4] = {{"amount", m, 2}, {"after", af, 2}, {"room", r, 2}, {"gap", g, 2}};
        CHECK(wave_lp(app, "transfer_ok", xa, 4) == ZXAPP_BOTH,
              "Coffer: a balance past 18 digits: Eve FALSE, Sutra refuses: BOTH");
    }

    /* holdings: 64 sets of accounts, each an exact sum, Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t h[8], s = 0;
        for (uint32_t i = 0; i < 8; i++)
            s += h[i] = r == 0   ? 0
                        : r == 1 ? CF_AMOUNT_MAX
                        : r == 2 ? -CF_AMOUNT_MAX
                                 : wrand(-1000000, CF_AMOUNT_MAX);
        zxapp_arg_t ha = {"balance", h, 8};
        CHECK(wave_same(app, "holdings", &ha, 1, &s, 1, false), "Coffer: holdings %u, Trebago = C",
              r);
    }
    {
        static const int64_t top[2] = {INT64_MAX - 5, 5}, tw[1] = {INT64_MAX};
        zxapp_arg_t ta1 = {"balance", top, 2};
        CHECK(wave_same(app, "holdings", &ta1, 1, tw, 1, false), "Coffer: holdings at INT64_MAX");
        /* a partial sum past 64 bits is refused, never wrapped: one language, NEITHER */
        static const int64_t over[3] = {INT64_MAX, 1, -1};
        zxapp_arg_t oa = {"balance", over, 3};
        CHECK(wave_lp(app, "holdings", &oa, 1) == ZXAPP_NEITHER,
              "Coffer: holdings past 64 bits: refused");
    }
}
