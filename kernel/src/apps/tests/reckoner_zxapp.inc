/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Reckoner's languages against the C its model runs (reckoner_rules.h).
 * Slots: @num.reckoner_statement_close, @num.reckoner_band_tax,
 * @logic.reckoner_report_ok. */
#include "reckoner_rules.h"

APP_TEST(reckoner, 10, 83)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t M = RECKONER_MINOR_MAX, X = RECKONER_TAX_MAX;

    /* statement_close (RR1): Sutra = Trebago = C, 18 digits either way */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-M / 3, M / 3);
        b[i] = wrand(0, i < 32 ? 1000000 : M / 3);
        c[i] = wrand(0, i < 32 ? 1000000 : M / 3);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;     /* nothing at all */
    a[1] = M, b[1] = 0, c[1] = M;     /* the max both ways */
    a[2] = -M, b[2] = M, c[2] = 0;    /* the least, back to 0 */
    a[3] = 0, b[3] = M, c[3] = 0;     /* a credit total at the max */
    a[4] = 0, b[4] = 0, c[4] = M;     /* a debit total at the max: the least closing */
    a[5] = 100, b[5] = 0, c[5] = 250; /* below zero (an issuer's side) */
    a[6] = M - 1, b[6] = 1, c[6] = 0; /* the max reached exactly */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = reckoner_statement_close(a[i], b[i], c[i]);
        CHECK(want[i] != RECKONER_REFUSED, "Reckoner: statement row %u in range", i);
    }
    zxapp_arg_t sa[3] = {
        {"opening", a, WAVE_ROWS}, {"credits", b, WAVE_ROWS}, {"debits", c, WAVE_ROWS}};
    CHECK(wave_same(app, "statement_close", sa, 3, want, WAVE_ROWS, false),
          "Reckoner: statement_close, Sutra = Trebago = C");
    {
        /* max + 1: a credit total past Sutra's 9(18), refused there; Trebago answers */
        static const int64_t o[2] = {0, 0}, cr[2] = {0, M + 1}, db[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"opening", o, 2}, {"credits", cr, 2}, {"debits", db, 2}};
        CHECK(reckoner_statement_close(0, M + 1, 0) == RECKONER_REFUSED &&
                  wave_lp(app, "statement_close", xa, 3) == ZXAPP_BOTH,
              "Reckoner: credits past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* a closing past 18 digits from two in range: Sutra refuses, Trebago answers */
        static const int64_t o2[2] = {0, M}, cr2[2] = {0, 1}, db2[2] = {0, 0};
        zxapp_arg_t ya[3] = {{"opening", o2, 2}, {"credits", cr2, 2}, {"debits", db2, 2}};
        CHECK(reckoner_statement_close(M, 1, 0) == RECKONER_REFUSED &&
                  wave_lp(app, "statement_close", ya, 3) == ZXAPP_BOTH,
              "Reckoner: a closing past 18 digits: BOTH");
        /* a negative debit total: both refuse, nothing to show */
        static const int64_t o3[2] = {0, 5}, cr3[2] = {0, 0}, db3[2] = {0, -1};
        zxapp_arg_t za[3] = {{"opening", o3, 2}, {"credits", cr3, 2}, {"debits", db3, 2}};
        CHECK(reckoner_statement_close(5, 0, -1) == RECKONER_REFUSED &&
                  wave_lp(app, "statement_close", za, 3) == ZXAPP_NEITHER,
              "Reckoner: a debit total below 0: both refuse: NEITHER");
    }

    /* band_tax (RR2): Sutra = Trebago = C, every band shape */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t top = i < 32 ? 10000000 : X;
        a[i] = wrand(0, top);
        b[i] = wrand(0, top);
        c[i] = wrand(b[i], top);
        d[i] = wrand(0, RECKONER_RATE_ALL);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;                  /* nothing */
    a[1] = X, b[1] = 0, c[1] = X, d[1] = RECKONER_RATE_ALL;  /* all of the most */
    a[2] = 50000, b[2] = 10000, c[2] = 100000, d[2] = 2000;  /* inside the band */
    a[3] = 150000, b[3] = 10000, c[3] = 100000, d[3] = 2000; /* above it: the band's width */
    a[4] = 5000, b[4] = 10000, c[4] = 100000, d[4] = 2000;   /* below it: 0 */
    a[5] = 10000, b[5] = 10000, c[5] = 100000, d[5] = 2000;  /* at its foot: 0 */
    a[6] = 10001, b[6] = 0, c[6] = 100000, d[6] = 3333;      /* a floor: 3333.33 */
    a[7] = X, b[7] = X, c[7] = X, d[7] = 5000;               /* an empty band at the top */
    a[8] = X - 1, b[8] = 0, c[8] = X, d[8] = 9999;           /* floors at the max */
    a[9] = 1, b[9] = 0, c[9] = 1, d[9] = 1;                  /* 0.01% of 1: 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = reckoner_band_tax(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != RECKONER_REFUSED, "Reckoner: band row %u in range", i);
    }
    CHECK(want[2] == 8000 && want[3] == 18000 && want[4] == 0 && want[5] == 0 && want[6] == 3333 &&
              want[1] == X && want[9] == 0,
          "Reckoner: band_tax, the C's own edges");
    zxapp_arg_t ba[4] = {{"income", a, WAVE_ROWS},
                         {"lo", b, WAVE_ROWS},
                         {"hi", c, WAVE_ROWS},
                         {"rate", d, WAVE_ROWS}};
    CHECK(wave_same(app, "band_tax", ba, 4, want, WAVE_ROWS, false),
          "Reckoner: band_tax, Sutra = Trebago = C");
    {
        /* max + 1: an income past Sutra's 9(14), refused there; Trebago answers */
        static const int64_t in[2] = {1, X + 1}, lo[2] = {0, 0}, hi[2] = {1, X + 1},
                             rt[2] = {10000, 10000};
        zxapp_arg_t xa[4] = {{"income", in, 2}, {"lo", lo, 2}, {"hi", hi, 2}, {"rate", rt, 2}};
        CHECK(reckoner_band_tax(X + 1, 0, X + 1, 10000) == RECKONER_REFUSED &&
                  wave_lp(app, "band_tax", xa, 4) == ZXAPP_BOTH,
              "Reckoner: an income past 14 digits: Sutra refuses, Trebago answers: BOTH");
        /* a rate past 100%: both refuse */
        static const int64_t in2[2] = {100, 100}, lo2[2] = {0, 0}, hi2[2] = {100, 100},
                             rt2[2] = {10000, 10001};
        zxapp_arg_t ya[4] = {{"income", in2, 2}, {"lo", lo2, 2}, {"hi", hi2, 2}, {"rate", rt2, 2}};
        CHECK(reckoner_band_tax(100, 0, 100, 10001) == RECKONER_REFUSED &&
                  wave_lp(app, "band_tax", ya, 4) == ZXAPP_NEITHER,
              "Reckoner: a rate past 100%%: both refuse: NEITHER");
        /* a band upside down: both refuse */
        static const int64_t lo3[2] = {0, 50}, hi3[2] = {100, 49};
        zxapp_arg_t za[4] = {{"income", in2, 2}, {"lo", lo3, 2}, {"hi", hi3, 2}, {"rate", rt, 2}};
        CHECK(reckoner_band_tax(100, 50, 49, 10000) == RECKONER_REFUSED &&
                  wave_lp(app, "band_tax", za, 4) == ZXAPP_NEITHER,
              "Reckoner: a band upside down: both refuse: NEITHER");
        /* a negative income: both refuse */
        static const int64_t in4[2] = {100, -1};
        zxapp_arg_t na[4] = {{"income", in4, 2}, {"lo", lo2, 2}, {"hi", hi2, 2}, {"rate", rt, 2}};
        CHECK(wave_lp(app, "band_tax", na, 4) == ZXAPP_NEITHER,
              "Reckoner: a negative income: both refuse: NEITHER");
    }
    {
        /* the estimate over bands: the sum of each band's tax, as the languages give it */
        static const int64_t lo[3] = {0, 1000000, 5000000}, hi[3] = {1000000, 5000000, X},
                             rt[3] = {0, 2000, 4000};
        int64_t tax[3], inc[3] = {6000000, 6000000, 6000000}, got[3];
        int64_t total = reckoner_tax_estimate(6000000, lo, hi, rt, 3, tax);
        zxapp_arg_t ea[4] = {{"income", inc, 3}, {"lo", lo, 3}, {"hi", hi, 3}, {"rate", rt, 3}};
        got[0] = tax[0], got[1] = tax[1], got[2] = tax[2];
        CHECK(total == 1200000 && tax[0] == 0 && tax[1] == 800000 && tax[2] == 400000 &&
                  wave_same(app, "band_tax", ea, 4, got, 3, false),
              "Reckoner: a three-band estimate: %lld", (long long) total);
    }

    /* report_ok (RR3): Eve = C, each flag swept past its range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (int64_t) (i & 1u) : wrand(-2, 3);
        b[i] = i < 4 ? (int64_t) (i >> 1) : wrand(-2, 3);
        want[i] = T_(reckoner_report_ok(a[i], b[i]));
    }
    CHECK(want[3] == ZXAPP_TRUE && want[0] == ZXAPP_FALSE && want[1] == ZXAPP_FALSE &&
              want[2] == ZXAPP_FALSE,
          "Reckoner: report_ok, the C's own edges");
    zxapp_arg_t ra[2] = {{"closed", a, WAVE_ROWS}, {"books", b, WAVE_ROWS}};
    CHECK(wave_same(app, "report_ok", ra, 2, want, WAVE_ROWS, true),
          "Reckoner: report_ok, Eve = C");
    {
        static const int64_t cl[2] = {1, 2}, bk[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"closed", cl, 2}, {"books", bk, 2}};
        CHECK(!reckoner_report_ok(2, 1) && wave_lp(app, "report_ok", xa, 2) == ZXAPP_NEITHER,
              "Reckoner: a flag of 2 is no flag: not allowed (one language: NEITHER)");
    }
}
