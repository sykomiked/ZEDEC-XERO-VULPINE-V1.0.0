/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kitling's languages against the C its model runs (kitling_rules.h).
 * Slots: @num.kitling_stage, @logic.kitling_grown, @num.kitling_zeck,
 * @num.kitling_gene_strength, @logic.kitling_memory_ok, and the companion's
 * budget used through the used_share block, @num.used_x100. */
#include "kitling_rules.h"

APP_TEST(kitling, 11, 62)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* stage (KR1): Trebago = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, i < 40 ? 60 : KITLING_GEN_MAX);
    a[0] = 0, a[1] = 1, a[2] = 2, a[3] = 4, a[4] = 5, a[5] = 12, a[6] = 13, a[7] = 33, a[8] = 34;
    a[9] = KITLING_GEN_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = kitling_stage(a[i]);
        CHECK(want[i] >= 0, "Kitling: stage row %u is inside the domain", i);
    }
    CHECK(want[0] == 0 && want[1] == 1 && want[2] == 2 && want[3] == 2 && want[4] == 3 &&
              want[5] == 3 && want[6] == 4 && want[7] == 4 && want[8] == 5 && want[9] == 5,
          "Kitling: stage, the C's own edges");
    zxapp_arg_t sa[1] = {{"generation", a, WAVE_ROWS}};
    CHECK(wave_same(app, "stage", sa, 1, want, WAVE_ROWS, false),
          "Kitling: stage, Trebago = KRON = C");
    {
        /* max + 1 and a negative: Trebago refuses (-1) as the C does, KRON counts steps */
        static const int64_t over[2] = {0, KITLING_GEN_MAX + 1}, neg[2] = {0, -1};
        zxapp_arg_t oa[1] = {{"generation", over, 2}}, na[1] = {{"generation", neg, 2}};
        CHECK(kitling_stage(KITLING_GEN_MAX + 1) == KITLING_REFUSED &&
                  wave_lp(app, "stage", oa, 1) == ZXAPP_BOTH,
              "Kitling: a generation past 32 bits: Trebago refuses, KRON answers 5: BOTH");
        CHECK(kitling_stage(-1) == KITLING_REFUSED && wave_lp(app, "stage", na, 1) == ZXAPP_BOTH,
              "Kitling: a negative generation: Trebago refuses, KRON answers 0: BOTH");
    }

    /* grown (KR2): Eve = KRON = C, the same generations and the negatives */
    a[10] = -1, a[11] = -KITLING_GEN_MAX, a[12] = 3;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(kitling_grown(a[i]));
    CHECK(want[3] == ZXAPP_FALSE && want[4] == ZXAPP_TRUE && want[9] == ZXAPP_TRUE &&
              want[10] == ZXAPP_FALSE,
          "Kitling: grown, the C's own edges");
    CHECK(wave_same(app, "grown", sa, 1, want, WAVE_ROWS, true), "Kitling: grown, Eve = KRON = C");
    {
        static const int64_t over[2] = {KITLING_GROWN_GEN, KITLING_GEN_MAX + 1};
        zxapp_arg_t oa[1] = {{"generation", over, 2}};
        CHECK(!kitling_grown(KITLING_GEN_MAX + 1) && wave_lp(app, "grown", oa, 1) == ZXAPP_BOTH,
              "Kitling: a generation past 32 bits: Eve FALSE, KRON TRUE: BOTH");
    }

    /* zeck (KR3): FORTRAN 77 = BASIC = Lingua IR = C, one amount per call, 64 calls */
    static const int64_t ZEDGE[] = {0, 1, 2, 3, 4, 7, 8, 12, 100, 233, 3524576, KITLING_ZECK32_MAX};
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t v[1], w[1];
        v[0] = r < sizeof ZEDGE / sizeof ZEDGE[0] ? ZEDGE[r]
                                                  : wrand(0, r % 2 ? 1000 : KITLING_ZECK32_MAX);
        w[0] = kitling_zeck(v[0]);
        zxapp_arg_t za[1] = {{"charge", v, 1}};
        if (w[0] < 0 || (w[0] & (w[0] >> 1)) || !wave_same(app, "zeck", za, 1, w, 1, false)) {
            all = false;
            printf("    zeck charge %lld\n", (long long) v[0]);
        }
    }
    CHECK(all,
          "Kitling: zeck, FORTRAN 77 = BASIC = Lingua IR = C, 64 amounts, no two tiers adjacent");
    CHECK(kitling_zeck(0) == 0 && kitling_zeck(12) == 21 && kitling_zeck(100) == 532 &&
              kitling_zeck(KITLING_ZECK32_MAX) == INT64_C(1431655765) &&
              kitling_zeck(KITLING_ZECK_MAX) == INT64_C(6148914691236517205) &&
              kitling_zeck(KITLING_ZECK_MAX + 1) == KITLING_REFUSED &&
              kitling_zeck(-1) == KITLING_REFUSED,
          "Kitling: zeck, the C's own edges");
    {
        /* past 31 tiers: FORTRAN 77 and its IR refuse, BASIC answers: held */
        static const int64_t past[1] = {KITLING_ZECK32_MAX + 1}, top[1] = {KITLING_ZECK_MAX},
                             over[1] = {KITLING_ZECK_MAX + 1}, neg[1] = {-1};
        zxapp_arg_t pa[1] = {{"charge", past, 1}}, ta[1] = {{"charge", top, 1}},
                    oa[1] = {{"charge", over, 1}}, na[1] = {{"charge", neg, 1}};
        CHECK(kitling_zeck(KITLING_ZECK32_MAX + 1) == INT64_C(2147483648) &&
                  wave_lp(app, "zeck", pa, 1) == ZXAPP_BOTH,
              "Kitling: zeck past 31 tiers: FORTRAN 77 refuses, BASIC answers: BOTH");
        CHECK(wave_lp(app, "zeck", ta, 1) == ZXAPP_BOTH,
              "Kitling: zeck at the 63rd tier: FORTRAN 77 refuses, BASIC answers: BOTH");
        CHECK(wave_lp(app, "zeck", oa, 1) == ZXAPP_NEITHER &&
                  wave_lp(app, "zeck", na, 1) == ZXAPP_NEITHER,
              "Kitling: zeck past 63 tiers or negative: every language refuses: NEITHER");
    }

    /* strength (KR4): KRON = C, one language (NEITHER), every gene byte in four calls */
    for (uint32_t q = 0; q < 4; q++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = (int64_t) (q * WAVE_ROWS + i);
            want[i] = kitling_strength(a[i]);
        }
        zxapp_arg_t ga[1] = {{"gene", a, WAVE_ROWS}};
        CHECK(wave_same(app, "gene_strength", ga, 1, want, WAVE_ROWS, false),
              "Kitling: gene_strength, KRON = C, genes %u..%u", q * WAVE_ROWS, q * WAVE_ROWS + 63u);
    }
    CHECK(kitling_strength(0) == 0 && kitling_strength(255) == 1000 &&
              kitling_strength(128) == 502 && kitling_strength(256) == KITLING_REFUSED &&
              kitling_strength(-1) == KITLING_REFUSED,
          "Kitling: gene_strength, the C's own edges");

    /* memory_ok (KR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = wrand(0, 999999999) * 1000;
        c[i] = wrand(0, 7);
        d[i] = wrand(0, 5);
        e[i] = wrand(0, i % 2 ? 450 : 999999999);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = 999999999, b[1] = INT64_C(999999999999999), c[1] = 9, d[1] = 9, e[1] = 999999999;
    a[2] = 1000000000;                /* seq max + 1 */
    b[3] = INT64_C(1000000000000000); /* stamp max + 1 */
    c[4] = 10;                        /* an emotion of two digits */
    d[5] = 10;                        /* an intensity of two digits */
    e[6] = 1000000000;                /* tokens max + 1 */
    e[7] = -999999999;                /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(kitling_memory_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Kitling: memory_ok, the C's own edges");
    zxapp_arg_t ma[5] = {{"seq", a, WAVE_ROWS},
                         {"stamp", b, WAVE_ROWS},
                         {"emotion", c, WAVE_ROWS},
                         {"intensity", d, WAVE_ROWS},
                         {"tokens", e, WAVE_ROWS}};
    CHECK(wave_same(app, "memory_ok", ma, 5, want, WAVE_ROWS, true),
          "Kitling: memory_ok, MYRA = C");

    /* the companion's budget used this cycle: used_share, total = allotted, left = remaining */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i < 32 ? 1000 : 999999999);
        b[i] = wrand(0, a[i]);
        want[i] = (a[i] - b[i]) * 10000 / a[i];
    }
    a[0] = 450, b[0] = 450, want[0] = 0;    /* nothing spent */
    a[1] = 450, b[1] = 0, want[1] = 10000;  /* all spent */
    a[2] = 450, b[2] = 225, want[2] = 5000; /* half */
    a[3] = 3, b[3] = 2, want[3] = 3333;     /* 33.33.. floored */
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Kitling: budget used, Sutra = KRON = C");
}
