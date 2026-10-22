/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Forage's languages against the C its model runs (forage_rules.h).
 * Slots: @num.forage_net_after_fee, @logic.forage_claim_fits,
 * @num.forage_rate_rank, @logic.forage_bounty_rec_ok, and the parts it
 * uses: Fairground's @num.fairground_assurance_fee and Coffer's
 * @num.coffer_balance. */
#include "forage_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool fot_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

APP_TEST(forage, 17, 84)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* net_after_fee (FR1): Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(0, i < 32 ? INT64_C(100000000) : FAIRGROUND_FEE_WITNESSED);
    a[0] = 0;                        /* nothing offered */
    a[1] = FAIRGROUND_FEE_WITNESSED; /* the max both hold */
    a[2] = 1124;                     /* the most that pays no fee */
    a[3] = 1125;                     /* the least that pays 1: net 1124 again */
    a[4] = 10000000;                 /* net 9991111 */
    a[5] = 20000;                    /* the worked claim: net 19983 */
    a[6] = 9999999;                  /* 10^7 - 1: the low part at its most */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = forage_net_after_fee(a[i]);
    zxapp_arg_t na = {"amount", a, WAVE_ROWS};
    CHECK(wave_same(app, "net_after_fee", &na, 1, want, WAVE_ROWS, false),
          "Forage: net_after_fee, Sutra = Trebago = C");
    {
        /* max + 1: past Sutra's 9(15), refused there; Trebago answers */
        static const int64_t x[2] = {1, FAIRGROUND_FEE_WITNESSED + 1};
        zxapp_arg_t xa = {"amount", x, 2};
        CHECK(wave_lp(app, "net_after_fee", &xa, 1) == ZXAPP_BOTH,
              "Forage: an amount past 9(15): Sutra refuses, Trebago answers: BOTH");
        static const int64_t y[2] = {1, FAIRGROUND_AMOUNT_MAX};
        zxapp_arg_t ya = {"amount", y, 2};
        CHECK(wave_lp(app, "net_after_fee", &ya, 1) == ZXAPP_BOTH,
              "Forage: an amount of 2^53 - 1: BOTH");
        /* a negative amount and one past 2^53 - 1: both refuse */
        static const int64_t n[2] = {1, -1}, m[2] = {1, FAIRGROUND_AMOUNT_MAX + 1};
        zxapp_arg_t nn = {"amount", n, 2}, ma = {"amount", m, 2};
        CHECK(wave_lp(app, "net_after_fee", &nn, 1) == ZXAPP_NEITHER &&
                  fot_refused(app, "net_after_fee", &nn, 1),
              "Forage: a negative amount: both refuse: NEITHER");
        CHECK(wave_lp(app, "net_after_fee", &ma, 1) == ZXAPP_NEITHER,
              "Forage: an amount past 2^53 - 1: both refuse: NEITHER");
        CHECK(forage_net_after_fee(FAIRGROUND_AMOUNT_MAX) == INT64_C(8999192755323452) &&
                  forage_net_after_fee(-1) == FORAGE_REFUSED &&
                  forage_net_after_fee(FAIRGROUND_AMOUNT_MAX + 1) == FORAGE_REFUSED,
              "Forage: the C's net at its edges");
        /* net + fee(net) never passes the gross */
        bool under = true;
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            if (want[i] + fairground_assurance_fee(want[i]) > a[i]) under = false;
        CHECK(under, "Forage: a claim at its net, its fee on top, stays inside the gross");
    }

    /* claim_fits (FR2): Eve = Sutra = C, every value inside 9 digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(i < 32 ? -5 : 0, i < 32 ? 2000 : FORAGE_NINE);
        b[i] = wrand(i < 32 ? -5 : 0, i < 32 ? 5000 : FORAGE_NINE);
        c[i] = wrand(-50, i < 32 ? 2000 : FORAGE_NINE);
        d[i] = wrand(-50, i < 32 ? 5000 : FORAGE_NINE);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0; /* every least that fits */
    a[1] = FORAGE_NINE, b[1] = FORAGE_NINE, c[1] = FORAGE_NINE, d[1] = FORAGE_NINE; /* every max */
    a[2] = 0, b[2] = 0, c[2] = 0, d[2] = 0;            /* work that takes no tokens */
    a[3] = 5, b[3] = -1, c[3] = 0, d[3] = 0;           /* a negative draw */
    a[4] = 5, b[4] = 0, c[4] = -1, d[4] = 0;           /* past the swarm budget */
    a[5] = 5, b[5] = 0, c[5] = 0, d[5] = -1;           /* past the power cap */
    a[6] = -FORAGE_NINE, b[6] = 0, c[6] = 0, d[6] = 0; /* negative, inside 9 digits */
    a[7] = 1, b[7] = 0, c[7] = -FORAGE_NINE, d[7] = 0; /* the least room either holds */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(forage_claim_fits(a[i], b[i], c[i], d[i]));
    zxapp_arg_t ca[4] = {{"tokens", a, WAVE_ROWS},
                         {"mw", b, WAVE_ROWS},
                         {"tokroom", c, WAVE_ROWS},
                         {"mwroom", d, WAVE_ROWS}};
    CHECK(wave_same(app, "claim_fits", ca, 4, want, WAVE_ROWS, true),
          "Forage: claim_fits, Eve = Sutra = C");
    {
        /* max + 1 in each field: Eve answers no, Sutra refuses: BOTH */
        static const int64_t one[2] = {1, 1}, zero[2] = {0, 0}, big[2] = {1, FORAGE_NINE + 1},
                             bigz[2] = {0, FORAGE_NINE + 1}, neg[2] = {0, -FORAGE_NINE - 1};
        zxapp_arg_t ta[4] = {
            {"tokens", big, 2}, {"mw", zero, 2}, {"tokroom", zero, 2}, {"mwroom", zero, 2}};
        zxapp_arg_t wa[4] = {
            {"tokens", one, 2}, {"mw", bigz, 2}, {"tokroom", zero, 2}, {"mwroom", zero, 2}};
        zxapp_arg_t ra[4] = {
            {"tokens", one, 2}, {"mw", zero, 2}, {"tokroom", bigz, 2}, {"mwroom", zero, 2}};
        zxapp_arg_t pa[4] = {
            {"tokens", one, 2}, {"mw", zero, 2}, {"tokroom", zero, 2}, {"mwroom", bigz, 2}};
        zxapp_arg_t ga[4] = {
            {"tokens", one, 2}, {"mw", zero, 2}, {"tokroom", neg, 2}, {"mwroom", zero, 2}};
        CHECK(wave_lp(app, "claim_fits", ta, 4) == ZXAPP_BOTH,
              "Forage: tokens past 9 digits: Eve says no, Sutra refuses: BOTH");
        CHECK(wave_lp(app, "claim_fits", wa, 4) == ZXAPP_BOTH,
              "Forage: a draw past 9 digits: BOTH");
        CHECK(wave_lp(app, "claim_fits", ra, 4) == ZXAPP_BOTH,
              "Forage: a swarm room past 9 digits: BOTH");
        CHECK(wave_lp(app, "claim_fits", pa, 4) == ZXAPP_BOTH,
              "Forage: a power room past 9 digits: BOTH");
        CHECK(wave_lp(app, "claim_fits", ga, 4) == ZXAPP_BOTH,
              "Forage: a room below -999999999: BOTH");
    }

    /* rate_rank (FR3): Trebago = C, exact, ties sharing the higher place */
    for (uint32_t r = 0; r < 4u; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = r == 0 ? 600 : r == 1 ? wrand(0, 10) * 300 : wrand(0, FORAGE_NINE);
            b[i] = r == 0 ? 10 : r == 1 ? wrand(1, 6) * 5 : wrand(1, FORAGE_NINE);
        }
        if (r == 3) a[0] = FORAGE_NINE, b[0] = 1, a[1] = 0, b[1] = FORAGE_NINE; /* the edges */
        CHECK(forage_rate_rank(a, b, WAVE_ROWS, want), "Forage: the C ranks round %u", r);
        zxapp_arg_t ra[2] = {{"amount", a, WAVE_ROWS}, {"minutes", b, WAVE_ROWS}};
        CHECK(wave_same(app, "rate_rank", ra, 2, want, WAVE_ROWS, false),
              "Forage: rate_rank, round %u, Trebago = C", r);
    }
    {
        static const int64_t am[5] = {600, 300, 900, 0, 300}, mi[5] = {10, 5, 30, 1, 10};
        int64_t rk[5];
        CHECK(forage_rate_rank(am, mi, 5, rk) && rk[0] == 1 && rk[1] == 1 && rk[2] == 3 &&
                  rk[3] == 5 && rk[4] == 3 && !forage_rate_rank(am, mi, 0, rk),
              "Forage: 60, 60, 30, 0 and 30 a minute rank 1, 1, 3, 5, 3");
        static const int64_t one[2] = {1, 1}, negam[2] = {5, -1}, bigam[2] = {5, FORAGE_NINE + 1},
                             zmin[2] = {1, 0}, bigmin[2] = {1, FORAGE_NINE + 1};
        zxapp_arg_t xa[2] = {{"amount", negam, 2}, {"minutes", one, 2}};
        zxapp_arg_t ya[2] = {{"amount", bigam, 2}, {"minutes", one, 2}};
        zxapp_arg_t za[2] = {{"amount", one, 2}, {"minutes", zmin, 2}};
        zxapp_arg_t wa[2] = {{"amount", one, 2}, {"minutes", bigmin, 2}};
        CHECK(fot_refused(app, "rate_rank", xa, 2) && !forage_rate_rank(negam, one, 2, rk),
              "Forage: a negative value is refused");
        CHECK(fot_refused(app, "rate_rank", ya, 2) && !forage_rate_rank(bigam, one, 2, rk),
              "Forage: a value past 9 digits is refused");
        CHECK(fot_refused(app, "rate_rank", za, 2) && !forage_rate_rank(one, zmin, 2, rk),
              "Forage: work of 0 minutes is refused");
        CHECK(fot_refused(app, "rate_rank", wa, 2) && !forage_rate_rank(one, bigmin, 2, rk),
              "Forage: minutes past 9 digits are refused");
    }

    /* bounty_rec_ok (FR4): MYRA = C, digits sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, i < 32 ? 9999 : 10002);
        b[i] = wrand(0, i < 32 ? 3 : 11);
        c[i] = wrand(0, i < 32 ? 100000 : FORAGE_NINE + 2);
        d[i] = wrand(0, i < 32 ? 5000 : FORAGE_NINE + 2);
        e[i] = wrand(0, i < 32 ? 100000 : FAIRGROUND_FEE_WITNESSED + 2);
        f[i] = wrand(1, i < 32 ? 600 : FORAGE_NINE + 2);
        g[i] = wrand(0, i < 32 ? 3 : 11);
    }
    a[0] = 0, b[0] = 1, c[0] = 1, d[0] = 0, e[0] = 0, f[0] = 1, g[0] = 0; /* every least */
    a[1] = FORAGE_RECORD_MAX, b[1] = 9, c[1] = FORAGE_NINE, d[1] = FORAGE_NINE;
    e[1] = FAIRGROUND_FEE_WITNESSED, f[1] = FORAGE_NINE, g[1] = 9; /* every max */
    a[2] = FORAGE_RECORD_MAX + 1, b[2] = 1, c[2] = 1, d[2] = 0, e[2] = 0, f[2] = 1, g[2] = 0;
    a[3] = 0, b[3] = 10, c[3] = 1, d[3] = 0, e[3] = 0, f[3] = 1, g[3] = 0; /* kind past 1 digit */
    a[4] = 0, b[4] = 1, c[4] = FORAGE_NINE + 1, d[4] = 0, e[4] = 0, f[4] = 1, g[4] = 0;
    a[5] = 0, b[5] = 1, c[5] = 1, d[5] = 0, e[5] = FAIRGROUND_FEE_WITNESSED + 1, f[5] = 1, g[5] = 0;
    a[6] = 0, b[6] = 1, c[6] = 1, d[6] = 0, e[6] = 0, f[6] = 1, g[6] = 10;    /* status past 1 */
    a[7] = -9999, b[7] = 1, c[7] = 1, d[7] = 0, e[7] = 0, f[7] = 1, g[7] = 0; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(forage_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    zxapp_arg_t ba[7] = {{"bounty", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS},
                         {"tokens", c, WAVE_ROWS}, {"mw", d, WAVE_ROWS},
                         {"amount", e, WAVE_ROWS}, {"minutes", f, WAVE_ROWS},
                         {"status", g, WAVE_ROWS}};
    CHECK(wave_same(app, "bounty_rec_ok", ba, 7, want, WAVE_ROWS, true),
          "Forage: bounty_rec_ok, MYRA = C");

    /* fairground.assurance_fee and coffer.balance, used for a claim's fee and the wallet after */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, FORAGE_NINE);
        want[i] = fairground_assurance_fee(a[i]);
    }
    zxapp_arg_t fa = {"amount", a, WAVE_ROWS};
    CHECK(wave_same(app, "fairground.assurance_fee", &fa, 1, want, WAVE_ROWS, false),
          "Forage: a claim's fee, through fairground.assurance_fee");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, INT64_C(999999999999));
        c[i] = forage_net_after_fee(a[i]); /* paid at its net */
        want[i] = b[i] + c[i];
    }
    zxapp_arg_t wa2[2] = {{"before", b, WAVE_ROWS}, {"moved", c, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", wa2, 2, want, WAVE_ROWS, false),
          "Forage: the wallet after a claim is paid, through coffer.balance");
}
