/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Heirloom's languages against the C its model runs (heirloom_rules.h).
 * Slots: @num.heirloom_estate_share_gap, @logic.heirloom_heir_release_due,
 * @num.heirloom_heir_release_due_n, and the entries it uses:
 * @num.crown_allot, @num.tally_ratio_add and @num.coffer_holdings. */
#include "heirloom_rules.h"
#include "crown_rules.h"

static const char *const HL_DUE[5] = {"reported", "k", "spare", "over", "vetoed"};

APP_TEST(heirloom, 32, 81)
{
    static int64_t an[WAVE_ROWS], ad[WAVE_ROWS], bn[WAVE_ROWS], bd[WAVE_ROWS], want[WAVE_ROWS];
    static int64_t f[5][WAVE_ROWS];

    /* estate_share_gap (HR1): Sutra = Trebago = C, shares swept, a third of them whole */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        ad[i] = wrand(1, i < 32 ? 1000 : HEIRLOOM_NINE);
        bd[i] = wrand(1, i < 32 ? 1000 : HEIRLOOM_NINE);
        an[i] = wrand(0, ad[i]);
        bn[i] = wrand(0, bd[i]);
        if (i % 3 == 0) { /* exactly one whole: bn / bd = 1 - an / ad */
            bd[i] = ad[i];
            bn[i] = ad[i] - an[i];
        }
    }
    an[0] = 0, ad[0] = 1, bn[0] = 0, bd[0] = 1;                         /* nothing: -1 */
    an[1] = HEIRLOOM_NINE, ad[1] = HEIRLOOM_NINE;                       /* the max, twice */
    bn[1] = HEIRLOOM_NINE, bd[1] = HEIRLOOM_NINE;                       /* a whole past */
    an[2] = 0, ad[2] = HEIRLOOM_NINE, bn[2] = 0, bd[2] = HEIRLOOM_NINE; /* the most short */
    an[4] = 1, ad[4] = 2, bn[4] = 1, bd[4] = 3;                         /* 5/6: short by 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = heirloom_share_gap(an[i], ad[i], bn[i], bd[i]);
        CHECK(want[i] != HEIRLOOM_REFUSED, "Heirloom: gap row %u in range", i);
    }
    CHECK(want[0] == -1 && want[1] == INT64_C(999999998000000001) &&
              want[2] == -INT64_C(999999998000000001) && want[3] == 0 && want[4] == -1,
          "Heirloom: the gap's own edges");
    zxapp_arg_t ga[4] = {
        {"an", an, WAVE_ROWS}, {"ad", ad, WAVE_ROWS}, {"bn", bn, WAVE_ROWS}, {"bd", bd, WAVE_ROWS}};
    CHECK(wave_same(app, "estate_share_gap", ga, 4, want, WAVE_ROWS, false),
          "Heirloom: estate_share_gap, Sutra = Trebago = C");
    {
        /* a share past its whole (an = ad + 1): Sutra refuses, Trebago answers */
        static const int64_t n1[2] = {1, 3}, d1[2] = {2, 2}, n2[2] = {1, 1}, d2[2] = {2, 2};
        zxapp_arg_t xa[4] = {{"an", n1, 2}, {"ad", d1, 2}, {"bn", n2, 2}, {"bd", d2, 2}};
        CHECK(heirloom_share_gap(3, 2, 1, 2) == HEIRLOOM_REFUSED &&
                  wave_lp(app, "estate_share_gap", xa, 4) == ZXAPP_BOTH,
              "Heirloom: a share past its whole: Sutra refuses, Trebago answers: BOTH");
        /* a negative part */
        static const int64_t n3[2] = {1, -1};
        zxapp_arg_t na[4] = {{"an", n3, 2}, {"ad", d1, 2}, {"bn", n2, 2}, {"bd", d2, 2}};
        CHECK(heirloom_share_gap(-1, 2, 1, 2) == HEIRLOOM_REFUSED &&
                  wave_lp(app, "estate_share_gap", na, 4) == ZXAPP_BOTH,
              "Heirloom: a negative part: Sutra refuses, Trebago answers: BOTH");
        /* max + 1: a denominator past nine digits */
        static const int64_t n4[2] = {1, 1}, d4[2] = {2, HEIRLOOM_NINE + 1};
        zxapp_arg_t ma[4] = {{"an", n4, 2}, {"ad", d4, 2}, {"bn", n2, 2}, {"bd", d2, 2}};
        CHECK(heirloom_share_gap(1, HEIRLOOM_NINE + 1, 1, 2) == HEIRLOOM_REFUSED &&
                  wave_lp(app, "estate_share_gap", ma, 4) == ZXAPP_BOTH,
              "Heirloom: a denominator past 9(9): Sutra refuses, Trebago answers: BOTH");
        /* a denominator of 0: both refuse, never TRUE */
        static const int64_t d0[2] = {2, 0};
        zxapp_arg_t za[4] = {{"an", n2, 2}, {"ad", d0, 2}, {"bn", n2, 2}, {"bd", d2, 2}};
        CHECK(heirloom_share_gap(1, 0, 1, 2) == HEIRLOOM_REFUSED &&
                  wave_lp(app, "estate_share_gap", za, 4) != ZXAPP_TRUE,
              "Heirloom: a denominator of 0 is refused by both");
    }

    /* HR2: a manifest of three heirs, folded with Tally's rule, then the gap */
    {
        static const int64_t num[3] = {1, 1, 1}, den[3] = {2, 3, 6}, short_[3] = {1, 1, 1},
                             sden[3] = {2, 3, 7};
        int64_t gap = 7, gap2 = 0;
        CHECK(heirloom_manifest_gap(num, den, 3, &gap) == 0 && gap == 0 &&
                  heirloom_manifest_gap(short_, sden, 3, &gap2) == 0 && gap2 < 0,
              "Heirloom: 1/2 + 1/3 + 1/6 is whole; 1/2 + 1/3 + 1/7 is short");
        /* the last step in the languages: 5/6 (tally's sum) and 1/6 */
        static const int64_t sa[2] = {5, 5}, sd[2] = {6, 6}, la[2] = {1, 1}, ld[2] = {6, 7};
        int64_t w2[2] = {heirloom_share_gap(5, 6, 1, 6), heirloom_share_gap(5, 6, 1, 7)};
        zxapp_arg_t ma[4] = {{"an", sa, 2}, {"ad", sd, 2}, {"bn", la, 2}, {"bd", ld, 2}};
        CHECK(w2[0] == 0 && w2[1] == -1 && wave_same(app, "estate_share_gap", ma, 4, w2, 2, false),
              "Heirloom: the last heir against the sum: whole, then short by 1/42");
        /* Tally's sum of two heirs that make a whole is 1 exactly, as the gap says 0 */
        static const int64_t ra[2] = {1, 1}, rd[2] = {2, 3}, rb[2] = {1, 2}, rbd[2] = {2, 3},
                             sum[2] = {1, 1};
        zxapp_arg_t ta[4] = {{"an", ra, 2}, {"ad", rd, 2}, {"bn", rb, 2}, {"bd", rbd, 2}};
        CHECK(heirloom_share_gap(1, 2, 1, 2) == 0 && heirloom_share_gap(1, 3, 2, 3) == 0 &&
                  wave_same(app, "tally.ratio_add", ta, 4, sum, 2, false),
              "Heirloom: 1/2 + 1/2 and 1/3 + 2/3 through tally.ratio_add: 1, gap 0");
    }

    /* heir_release_due (HR3): Eve = Sutra = C; heir_release_due_n: Trebago = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        f[0][i] = i % 7 ? 1 : 0;                /* a report filed */
        f[1][i] = wrand(1, HEIRLOOM_GUARDIANS); /* k */
        f[2][i] = wrand(-3, 4);                 /* approvals - k */
        f[3][i] = i < 32 ? wrand(-100, 100) : wrand(-HEIRLOOM_NINE, HEIRLOOM_EIGHTEEN);
        f[4][i] = i % 11 ? 0 : 1; /* a living veto */
    }
    f[0][1] = 1, f[1][1] = 1, f[2][1] = 0, f[3][1] = 0, f[4][1] = 0;  /* due at the first second */
    f[0][2] = 1, f[1][2] = 5, f[2][2] = 0, f[3][2] = -1, f[4][2] = 0; /* one second early */
    f[0][3] = 1, f[1][3] = 5, f[2][3] = -1, f[3][3] = 9, f[4][3] = 0; /* one approval short */
    f[0][4] = 1, f[1][4] = 1, f[2][4] = 4, f[3][4] = HEIRLOOM_EIGHTEEN; /* the max */
    f[4][4] = 0;
    f[0][5] = 1, f[1][5] = 0, f[2][5] = 0, f[3][5] = 5, f[4][5] = 0; /* no quorum: never due */
    f[0][6] = 1, f[1][6] = 2, f[2][6] = 0, f[3][6] = 5, f[4][6] = 1; /* vetoed */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(heirloom_release_due(f[0][i], f[1][i], f[2][i], f[3][i], f[4][i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[3] == ZXAPP_FALSE &&
              want[4] == ZXAPP_TRUE && want[5] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE &&
              want[0] == ZXAPP_FALSE,
          "Heirloom: release due, the C's own edges");
    zxapp_arg_t da[5];
    for (uint32_t k = 0; k < 5; k++) da[k] = (zxapp_arg_t){HL_DUE[k], f[k], WAVE_ROWS};
    CHECK(wave_same(app, "heir_release_due", da, 5, want, WAVE_ROWS, true),
          "Heirloom: heir_release_due, Eve = Sutra = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = heirloom_release_due_n(f[0][i], f[1][i], f[2][i], f[3][i], f[4][i]);
    CHECK(wave_same(app, "heir_release_due_n", da, 5, want, WAVE_ROWS, false),
          "Heirloom: heir_release_due_n, Trebago = Sutra = C");
    {
        /* max + 1: a wait past 18 digits: Sutra refuses, Eve FALSE and Trebago 1 */
        static const int64_t one[2] = {1, 1}, two[2] = {2, 2}, zero[2] = {0, 0},
                             over[2] = {0, HEIRLOOM_EIGHTEEN + 1};
        zxapp_arg_t oa[5] = {{"reported", one, 2},
                             {"k", two, 2},
                             {"spare", zero, 2},
                             {"over", over, 2},
                             {"vetoed", zero, 2}};
        CHECK(!heirloom_release_due(1, 2, 0, HEIRLOOM_EIGHTEEN + 1, 0) &&
                  wave_lp(app, "heir_release_due", oa, 5) == ZXAPP_BOTH,
              "Heirloom: a wait past 18 digits: Sutra refuses, Eve FALSE: BOTH");
        CHECK(wave_lp(app, "heir_release_due_n", oa, 5) == ZXAPP_BOTH,
              "Heirloom: a wait past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* ten approvals beyond k: past s9(1) */
        static const int64_t ten[2] = {0, 10};
        zxapp_arg_t sa2[5] = {{"reported", one, 2},
                              {"k", two, 2},
                              {"spare", ten, 2},
                              {"over", zero, 2},
                              {"vetoed", zero, 2}};
        CHECK(wave_lp(app, "heir_release_due", sa2, 5) == ZXAPP_BOTH &&
                  wave_lp(app, "heir_release_due_n", sa2, 5) == ZXAPP_BOTH,
              "Heirloom: spare 10: Sutra refuses, Eve and Trebago say no: BOTH");
        /* a plain no in every language: too few approvals */
        static const int64_t lack[2] = {0, -1};
        zxapp_arg_t la2[5] = {{"reported", one, 2},
                              {"k", two, 2},
                              {"spare", lack, 2},
                              {"over", zero, 2},
                              {"vetoed", zero, 2}};
        CHECK(wave_lp(app, "heir_release_due", la2, 5) == ZXAPP_TRUE,
              "Heirloom: one approval short is a no every language agrees on");
    }

    /* the split preview is Crown's: crown.allot, against zxvd's own example (estate.h E2) */
    {
        static const int64_t units[1] = {1000}, weight[3] = {3, 2, 1}, total[1] = {6},
                             extra[3] = {0, 0, 1};
        int64_t share[3], ex[3], w = 0;
        CHECK(crown_allot(1000, weight, 3, share, ex, &w) == 0 && w == 6 && ex[2] == 1 &&
                  share[0] == 500 && share[1] == 333 && share[2] == 167,
              "Heirloom: 1000 by 3/6, 2/6, 1/6 is 500, 333, 167");
        zxapp_arg_t ca[4] = {
            {"units", units, 1}, {"weight", weight, 3}, {"total", total, 1}, {"extra", extra, 3}};
        CHECK(wave_same(app, "crown.allot", ca, 4, share, 3, false),
              "Heirloom: the preview through crown.allot, Sutra = Trebago = C");
    }

    /* coffer.holdings: what is split */
    {
        static const int64_t bal[3] = {100, 250, 0}, sum[1] = {350};
        zxapp_arg_t ha = {"balance", bal, 3};
        CHECK(wave_same(app, "coffer.holdings", &ha, 1, sum, 1, false),
              "Heirloom: the amount, through coffer.holdings");
    }
}
