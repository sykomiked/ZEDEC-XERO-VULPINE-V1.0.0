/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Assay's languages against the C its model runs (assay_rules.h).
 * Slots: @num.assay_pass_share_x100, @logic.assay_meets_bar,
 * @num.assay_score_rank, and the shared block it uses, @prob.join (with
 * its verdict). */
#include "assay_rules.h"

/* one pce_prob entry through the app: the first answer, den 0 for a truth */
static bool ayt_prob(zxapp_t *a, const char *entry, const zxapp_arg_t *args, zxapp_num_t *out,
                     uint32_t cap, uint32_t want)
{
    uint32_t got = 0;
    return zxapp_call(a, &H, entry, args, 2, out, cap, &got) == ZXAPP_OK && got == want;
}

APP_TEST(assay, 8, 72)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* pass_share_x100 (AY1): Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 3 == 0 ? wrand(1, 64) : wrand(1, ASSAY_COUNT_MAX);
        a[i] = wrand(0, b[i]);
    }
    a[0] = 0, b[0] = 1;                             /* none passed */
    a[1] = 1, b[1] = 1;                             /* every one passed */
    a[2] = ASSAY_COUNT_MAX, b[2] = ASSAY_COUNT_MAX; /* max on max */
    a[3] = 0, b[3] = ASSAY_COUNT_MAX;
    a[4] = 1, b[4] = 3; /* 3333, never rounded up */
    a[5] = 2, b[5] = 3; /* 6666 */
    a[6] = 1, b[6] = ASSAY_COUNT_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = assay_pass_share_x100(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == ASSAY_SHARE_MAX && want[2] == ASSAY_SHARE_MAX &&
              want[3] == 0 && want[4] == 3333 && want[5] == 6666 && want[6] == 0 &&
              assay_pass_share_x100(4, 3) == ASSAY_REFUSED &&
              assay_pass_share_x100(-1, 3) == ASSAY_REFUSED &&
              assay_pass_share_x100(0, 0) == ASSAY_REFUSED &&
              assay_pass_share_x100(1, ASSAY_COUNT_MAX + 1) == ASSAY_REFUSED,
          "Assay: the C rule's own edges (AY1)");
    zxapp_arg_t pa[2] = {{"passes", a, WAVE_ROWS}, {"runs", b, WAVE_ROWS}};
    CHECK(wave_same(app, "pass_share_x100", pa, 2, want, WAVE_ROWS, false),
          "Assay: pass_share_x100, Trebago = KRON = C");
    {
        /* max + 1 and a negative: Trebago refuses the whole call, KRON divides */
        static const int64_t p1[2] = {1, 4}, r1[2] = {3, 3}, p2[2] = {1, -1},
                             r3[2] = {3, ASSAY_COUNT_MAX + 1}, p3[2] = {1, 1};
        zxapp_arg_t x1[2] = {{"passes", p1, 2}, {"runs", r1, 2}};
        zxapp_arg_t x2[2] = {{"passes", p2, 2}, {"runs", r1, 2}};
        zxapp_arg_t x3[2] = {{"passes", p3, 2}, {"runs", r3, 2}};
        CHECK(wave_lp(app, "pass_share_x100", x1, 2) == ZXAPP_BOTH,
              "Assay: 4 passes of 3: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "pass_share_x100", x2, 2) == ZXAPP_BOTH,
              "Assay: -1 passes: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "pass_share_x100", x3, 2) == ZXAPP_BOTH,
              "Assay: runs past 999999999: Trebago refuses, KRON answers: BOTH");
    }

    /* meets_bar (AY2): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, ASSAY_SHARE_MAX);
        b[i] = wrand(-5, ASSAY_COUNT_MAX);
        c[i] = wrand(-5, ASSAY_COUNT_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0; /* just met */
    a[1] = ASSAY_SHARE_MAX, b[1] = ASSAY_COUNT_MAX, c[1] = ASSAY_COUNT_MAX;
    a[2] = -1, b[2] = 0, c[2] = 0; /* below the pass bar */
    a[3] = 0, b[3] = -1, c[3] = 0; /* one refusal too many */
    a[4] = 0, b[4] = 0, c[4] = -1; /* one run short */
    a[5] = -1, b[5] = -1, c[5] = -1;
    a[6] = 5, b[6] = 5, c[6] = 5;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(assay_meets_bar(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && !assay_meets_bar(ASSAY_SHARE_MAX + 1, 0, 0) &&
              !assay_meets_bar(0, ASSAY_COUNT_MAX + 1, 0) &&
              !assay_meets_bar(0, 0, ASSAY_COUNT_MAX + 1),
          "Assay: the C rule's own edges (AY2)");
    zxapp_arg_t ba[3] = {
        {"passroom", a, WAVE_ROWS}, {"refusalroom", b, WAVE_ROWS}, {"runroom", c, WAVE_ROWS}};
    CHECK(wave_same(app, "meets_bar", ba, 3, want, WAVE_ROWS, true),
          "Assay: meets_bar, Eve = KRON = C");
    {
        /* a margin past its range: Eve refuses it, KRON reads it as met */
        static const int64_t z[2] = {0, 0}, pp[2] = {0, ASSAY_SHARE_MAX + 1},
                             cp[2] = {0, ASSAY_COUNT_MAX + 1};
        zxapp_arg_t y1[3] = {{"passroom", pp, 2}, {"refusalroom", z, 2}, {"runroom", z, 2}};
        zxapp_arg_t y2[3] = {{"passroom", z, 2}, {"refusalroom", cp, 2}, {"runroom", z, 2}};
        zxapp_arg_t y3[3] = {{"passroom", z, 2}, {"refusalroom", z, 2}, {"runroom", cp, 2}};
        CHECK(wave_lp(app, "meets_bar", y1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "meets_bar", y2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "meets_bar", y3, 3) == ZXAPP_BOTH,
              "Assay: a margin past its range: Eve refuses, KRON answers: BOTH");
    }

    /* score_rank (AY3): KRON = C, ties sharing the higher place */
    for (uint32_t r = 0; r < 4u; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            a[i] = r == 0 ? 7 : r == 1 ? wrand(0, 20) * 500 : wrand(0, ASSAY_SHARE_MAX);
        if (r == 3) a[0] = ASSAY_SHARE_MAX, a[1] = 0; /* the edges of a share */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = assay_score_rank(a, WAVE_ROWS, i);
        zxapp_arg_t ra[1] = {{"score", a, WAVE_ROWS}};
        CHECK(wave_same(app, "score_rank", ra, 1, want, WAVE_ROWS, false),
              "Assay: score_rank, round %u, KRON = C", r);
    }
    {
        static const int64_t s[5] = {10000, 5000, 5000, 0, 7500}, neg[3] = {-5, 0, -1};
        CHECK(assay_score_rank(s, 5, 0) == 1 && assay_score_rank(s, 5, 1) == 3 &&
                  assay_score_rank(s, 5, 2) == 3 && assay_score_rank(s, 5, 3) == 5 &&
                  assay_score_rank(s, 5, 4) == 2 && assay_score_rank(s, 5, 5) == ASSAY_REFUSED &&
                  assay_score_rank(NULL, 5, 0) == ASSAY_REFUSED,
              "Assay: the C rule's own edges (AY3)");
        int64_t w3[3];
        for (uint32_t i = 0; i < 3u; i++) w3[i] = assay_score_rank(neg, 3, i);
        zxapp_arg_t na[1] = {{"score", neg, 3}};
        CHECK(w3[0] == 3 && w3[1] == 1 && w3[2] == 2 &&
                  wave_same(app, "score_rank", na, 1, w3, 3, false),
              "Assay: negative scores rank too, KRON = C");
    }

    /* the run's verdict and two runs joined (AY4, AY5): pce_prob through the app */
    {
        static const int64_t th[4] = {1, 2, 1, 4};
        static const int64_t m[6][4] = {{3, 1, 0, 0}, {1, 3, 0, 0}, {0, 0, 0, 0},
                                        {1, 1, 2, 0}, {2, 0, 1, 1}, {0, 0, 0, 5}};
        static const int64_t lp[6] = {ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_NEITHER,
                                      ZXAPP_BOTH, ZXAPP_BOTH,  ZXAPP_NEITHER};
        uint32_t wrong = 0;
        for (uint32_t i = 0; i < 6u; i++) {
            zxapp_arg_t va[2] = {{"m", m[i], 4}, {"th", th, 4}};
            zxapp_num_t o[2];
            if (!ayt_prob(app, "pce_prob.verdict", va, o, 2, 1) || o[0].den != 0 ||
                o[0].num != lp[i])
                wrong++;
        }
        CHECK(wrong == 0, "Assay: pce_prob.verdict on a run's four counts (%u wrong)", wrong);
        static const int64_t pass[4] = {1, 0, 0, 0}, fail[4] = {0, 1, 0, 0};
        zxapp_arg_t ja[2] = {{"a", pass, 4}, {"b", fail, 4}};
        zxapp_num_t o[4];
        CHECK(ayt_prob(app, "pce_prob.join", ja, o, 4, 4) && o[0].num == 0 && o[1].num == 0 &&
                  o[2].num == 1 && o[2].den == 1 && o[3].num == 0,
              "Assay: a pass joined with a fail is all conflict (pce_prob.join)");
    }
}
