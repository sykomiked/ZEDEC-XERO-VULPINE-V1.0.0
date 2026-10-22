/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Whetstone's languages against the C its model runs (whetstone_rules.h).
 * Slots: @num.whetstone_suite_verdict, @logic.whetstone_suite_passed,
 * @num.whetstone_test_result_sum, @num.whetstone_test_ms_total,
 * @logic.whetstone_test_retry_ok, @logic.whetstone_test_record_ok, and
 * the parts it uses, @num.witness_kind_count and @num.assay_pass_share_x100. */
#include "whetstone_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool wst_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

APP_TEST(whetstone, 19, 125)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    static int64_t e[WAVE_ROWS], f[WAVE_ROWS], g[WAVE_ROWS];

    /* WR1 in C: the fold is lpres_conjoin's, BOTH or NEITHER never TRUE */
    {
        static const int64_t all_true[3] = {1, 1, 1}, held[3] = {1, 3, 1};
        static const int64_t gap[3] = {3, 0, 1}, fail[4] = {0, 2, 3, 1}, bad[2] = {1, 4};
        CHECK(whetstone_suite_verdict(all_true, 3) == LPRES_STATE_TRUE &&
                  whetstone_suite_verdict(held, 3) == LPRES_STATE_BOTH &&
                  whetstone_suite_verdict(gap, 3) == LPRES_STATE_NEITHER &&
                  whetstone_suite_verdict(fail, 4) == LPRES_STATE_FALSE &&
                  whetstone_suite_verdict(bad, 2) == WHETSTONE_REFUSED &&
                  whetstone_suite_verdict(NULL, 2) == WHETSTONE_REFUSED &&
                  whetstone_suite_verdict(all_true, 0) == LPRES_STATE_NEITHER,
              "Whetstone: the C rule's own edges (WR1)");
        for (int64_t x = 0; x < 4; x++)
            for (int64_t y = 0; y < 4; y++) {
                int64_t v[2] = {x, y};
                CHECK(whetstone_suite_verdict(v, 2) ==
                          lpres_conjoin((lpres_state_t) x, (lpres_state_t) y),
                      "Whetstone: WR1 of %lld and %lld is lpres_conjoin's", (long long) x,
                      (long long) y);
            }
    }

    /* suite_verdict (WR1): KRON = Trebago = C, every pair, then swept vectors */
    for (int64_t x = 0; x < 4; x++)
        for (int64_t y = 0; y < 4; y++) {
            int64_t v[2] = {x, y}, w = whetstone_suite_verdict(v, 2);
            zxapp_arg_t va = {"outcome", v, 2};
            CHECK(wave_same(app, "suite_verdict", &va, 1, &w, 1, false),
                  "Whetstone: suite_verdict of %lld %lld, KRON = Trebago = C", (long long) x,
                  (long long) y);
        }
    for (uint32_t k = 0; k < 8; k++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = k < 4 ? (int64_t) k : wrand(0, 3);
        if (k >= 4) a[0] = LPRES_STATE_TRUE; /* every sweep has a TRUE among it */
        int64_t w = whetstone_suite_verdict(a, WAVE_ROWS);
        zxapp_arg_t va = {"outcome", a, WAVE_ROWS};
        CHECK(wave_same(app, "suite_verdict", &va, 1, &w, 1, false),
              "Whetstone: suite_verdict of %u results (sweep %u), KRON = Trebago = C", WAVE_ROWS,
              k);
    }
    {
        /* a verdict that is none: Trebago refuses, KRON ranks it as FALSE */
        static const int64_t four[2] = {1, 4}, neg[2] = {1, -1};
        zxapp_arg_t p1 = {"outcome", four, 2}, p2 = {"outcome", neg, 2};
        CHECK(wave_lp(app, "suite_verdict", &p1, 1) == ZXAPP_BOTH,
              "Whetstone: a verdict of 4: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "suite_verdict", &p2, 1) == ZXAPP_BOTH,
              "Whetstone: a verdict of -1: Trebago refuses, KRON answers: BOTH");
    }

    /* suite_passed (WR2): Eve = KRON = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(0, 2) == 0 ? wrand(0, 9) : 0;
        c[i] = wrand(0, 2) == 0 ? wrand(0, 9) : 0;
        d[i] = wrand(0, 2) == 0 ? wrand(0, 9) : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0;
    a[1] = WHETSTONE_COUNT_MAX, b[1] = 0, c[1] = 0, d[1] = 0;
    a[2] = 0, b[2] = 0, c[2] = 0, d[2] = 0; /* nothing passed: no TRUE suite */
    a[3] = 5, b[3] = 1, c[3] = 0, d[3] = 0;
    a[4] = 5, b[4] = 0, c[4] = 1, d[4] = 0;
    a[5] = 5, b[5] = 0, c[5] = 0, d[5] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(whetstone_suite_passed(a[i], b[i], c[i], d[i]));
    CHECK(whetstone_suite_passed(1, 0, 0, 0) && whetstone_suite_passed(999999999, 0, 0, 0) &&
              !whetstone_suite_passed(0, 0, 0, 0) && !whetstone_suite_passed(1000000000, 0, 0, 0) &&
              !whetstone_suite_passed(1, 1, 0, 0) && !whetstone_suite_passed(1, 0, 1, 0) &&
              !whetstone_suite_passed(1, 0, 0, 1) && !whetstone_suite_passed(1, -1, 0, 0) &&
              !whetstone_suite_passed(-1, 0, 0, 0),
          "Whetstone: the C rule's own edges (WR2)");
    zxapp_arg_t sp[4] = {{"passed", a, WAVE_ROWS},
                         {"failed", b, WAVE_ROWS},
                         {"held", c, WAVE_ROWS},
                         {"unknown", d, WAVE_ROWS}};
    CHECK(wave_same(app, "suite_passed", sp, 4, want, WAVE_ROWS, true),
          "Whetstone: suite_passed, Eve = KRON = C");
    {
        /* WR2 is WR1's TRUE: the counts of every pair of verdicts */
        for (int64_t x = 0; x < 4; x++)
            for (int64_t y = 0; y < 4; y++) {
                int64_t v[2] = {x, y};
                bool p = whetstone_suite_passed(whetstone_kind_count(v, 2, LPRES_STATE_TRUE),
                                                whetstone_kind_count(v, 2, LPRES_STATE_FALSE),
                                                whetstone_kind_count(v, 2, LPRES_STATE_BOTH),
                                                whetstone_kind_count(v, 2, LPRES_STATE_NEITHER));
                CHECK(p == (whetstone_suite_verdict(v, 2) == LPRES_STATE_TRUE),
                      "Whetstone: WR2 agrees with WR1 for %lld %lld", (long long) x, (long long) y);
            }
        /* past nine digits or a negative count: Eve refuses, KRON reads it */
        static const int64_t one[2] = {1, 1}, big[2] = {1, 1000000000}, zero[2] = {0, 0};
        static const int64_t neg[2] = {0, -1};
        zxapp_arg_t x1[4] = {
            {"passed", big, 2}, {"failed", zero, 2}, {"held", zero, 2}, {"unknown", zero, 2}};
        zxapp_arg_t x2[4] = {
            {"passed", one, 2}, {"failed", neg, 2}, {"held", zero, 2}, {"unknown", zero, 2}};
        zxapp_arg_t x3[4] = {
            {"passed", one, 2}, {"failed", zero, 2}, {"held", neg, 2}, {"unknown", neg, 2}};
        CHECK(wave_lp(app, "suite_passed", x1, 4) == ZXAPP_BOTH,
              "Whetstone: ten digits passed: Eve FALSE, KRON TRUE: BOTH");
        CHECK(wave_lp(app, "suite_passed", x2, 4) == ZXAPP_BOTH,
              "Whetstone: -1 failed: Eve FALSE, KRON TRUE: BOTH");
        CHECK(wave_lp(app, "suite_passed", x3, 4) == ZXAPP_BOTH,
              "Whetstone: -1 held and unknown: Eve FALSE, KRON TRUE: BOTH");
    }

    /* test_result_sum (WR3): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 333333333);
        b[i] = wrand(0, 333333333);
        c[i] = wrand(0, 333333333);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;
    a[1] = WHETSTONE_COUNT_MAX, b[1] = 0, c[1] = 0;
    a[2] = 0, b[2] = WHETSTONE_COUNT_MAX, c[2] = 0;
    a[3] = 0, b[3] = 0, c[3] = WHETSTONE_COUNT_MAX;
    a[4] = 333333333, b[4] = 333333333, c[4] = 333333333;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = whetstone_results_total(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == WHETSTONE_COUNT_MAX && want[4] == 999999999 &&
              whetstone_results_total(WHETSTONE_COUNT_MAX, 1, 0) == WHETSTONE_REFUSED &&
              whetstone_results_total(-1, 0, 0) == WHETSTONE_REFUSED &&
              whetstone_results_total(0, 0, 1000000000) == WHETSTONE_REFUSED,
          "Whetstone: the C rule's own edges (WR3)");
    zxapp_arg_t ra[3] = {
        {"passed", a, WAVE_ROWS}, {"failed", b, WAVE_ROWS}, {"held", c, WAVE_ROWS}};
    CHECK(wave_same(app, "test_result_sum", ra, 3, want, WAVE_ROWS, false),
          "Whetstone: test_result_sum, Sutra = KRON = C");
    {
        /* past 9(9) or below zero: Sutra refuses, KRON adds as it is */
        static const int64_t p1[2] = {1, WHETSTONE_COUNT_MAX}, f1[2] = {0, 1};
        static const int64_t pn[2] = {1, -1}, z[2] = {0, 0};
        zxapp_arg_t y1[3] = {{"passed", p1, 2}, {"failed", f1, 2}, {"held", z, 2}};
        zxapp_arg_t y2[3] = {{"passed", pn, 2}, {"failed", z, 2}, {"held", z, 2}};
        CHECK(wave_lp(app, "test_result_sum", y1, 3) == ZXAPP_BOTH,
              "Whetstone: a total of ten digits: Sutra refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "test_result_sum", y2, 3) == ZXAPP_BOTH,
              "Whetstone: -1 passed: Sutra refuses, KRON answers: BOTH");
    }

    /* test_ms_total (WR4): Trebago = C, one total for the vector */
    for (uint32_t k = 0; k < 4; k++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, WHETSTONE_RUN_MS_MAX);
        a[0] = 0, a[1] = WHETSTONE_RUN_MS_MAX;
        int64_t total = whetstone_ms_total(a, WAVE_ROWS);
        zxapp_arg_t ma = {"ms", a, WAVE_ROWS};
        CHECK(total >= WHETSTONE_RUN_MS_MAX &&
                  wave_same(app, "test_ms_total", &ma, 1, &total, 1, false),
              "Whetstone: test_ms_total (sweep %u), Trebago = C", k);
    }
    {
        static const int64_t over[2] = {5, WHETSTONE_RUN_MS_MAX + 1}, neg[2] = {-1, 5};
        static const int64_t ok[2] = {5, 7};
        CHECK(whetstone_ms_total(over, 2) == WHETSTONE_REFUSED &&
                  whetstone_ms_total(neg, 2) == WHETSTONE_REFUSED &&
                  whetstone_ms_total(ok, 2) == 12 && whetstone_ms_total(ok, 0) == 0,
              "Whetstone: the C rule's own edges (WR4)");
        zxapp_arg_t m1 = {"ms", over, 2}, m2 = {"ms", neg, 2};
        CHECK(wst_refused(app, "test_ms_total", &m1, 1) &&
                  wst_refused(app, "test_ms_total", &m2, 1),
              "Whetstone: a run past four hours or below zero: Trebago refuses the total");
    }

    /* test_retry_ok (WR5): Eve = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 5);
        b[i] = wrand(-2, 3602);
    }
    a[0] = 0, b[0] = 1;
    a[1] = 3, b[1] = 3600;
    a[2] = 4, b[2] = 60;
    a[3] = -1, b[3] = 60;
    a[4] = 0, b[4] = 0;
    a[5] = 0, b[5] = 3601;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(whetstone_retry_ok(a[i], b[i]));
    CHECK(whetstone_retry_ok(0, 1) && whetstone_retry_ok(3, 3600) && !whetstone_retry_ok(4, 60) &&
              !whetstone_retry_ok(-1, 60) && !whetstone_retry_ok(0, 0) &&
              !whetstone_retry_ok(0, 3601),
          "Whetstone: the C rule's own edges (WR5)");
    zxapp_arg_t ta[2] = {{"retries", a, WAVE_ROWS}, {"timeout", b, WAVE_ROWS}};
    CHECK(wave_same(app, "test_retry_ok", ta, 2, want, WAVE_ROWS, true),
          "Whetstone: test_retry_ok, Eve = C");

    /* test_record_ok (WR6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-12, 12);
        d[i] = wrand(0, 999999999);
        e[i] = wrand(0, 999999999);
        f[i] = wrand(0, 999999999);
        g[i] = wrand(0, 120000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 1, d[0] = 0, e[0] = 0, f[0] = 0, g[0] = 0;
    a[1] = 9999, b[1] = 999, c[1] = 3, d[1] = 999999999, e[1] = 999999999, f[1] = 999999999,
    g[1] = 99999999;
    a[2] = 10000, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1, f[2] = 1, g[2] = 1;
    a[3] = 1, b[3] = 1000, c[3] = 1, d[3] = 1, e[3] = 1, f[3] = 1, g[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1, e[4] = 1, f[4] = 1, g[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 2, d[5] = 1, e[5] = 1, f[5] = 1, g[5] = 100000000;
    a[6] = 1, b[6] = 1, c[6] = 2, d[6] = 1000000000, e[6] = 1, f[6] = 1, g[6] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(whetstone_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == T_(true) && want[1] == T_(true) && want[2] == T_(false) &&
              want[3] == T_(false) && want[4] == T_(false) && want[5] == T_(false) &&
              want[6] == T_(false),
          "Whetstone: the C rule's own edges (WR6)");
    zxapp_arg_t rec[7] = {{"run", a, WAVE_ROWS},     {"target", b, WAVE_ROWS},
                          {"verdict", c, WAVE_ROWS}, {"passed", d, WAVE_ROWS},
                          {"failed", e, WAVE_ROWS},  {"held", f, WAVE_ROWS},
                          {"ms", g, WAVE_ROWS}};
    CHECK(wave_same(app, "test_record_ok", rec, 7, want, WAVE_ROWS, true),
          "Whetstone: test_record_ok, MYRA = C");

    /* the runs counted by verdict (WR8), through witness.kind_count = C */
    for (int64_t k = 0; k < 4; k++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 3);
        int64_t n = whetstone_kind_count(a, WAVE_ROWS, k);
        zxapp_arg_t ka[2] = {{"kind", a, WAVE_ROWS}, {"want", &k, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &n, 1, false),
              "Whetstone: runs of verdict %lld, witness.kind_count = C", (long long) k);
    }

    /* the share of the results that passed (WR7), through assay.pass_share_x100 = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, 999999999);
        a[i] = wrand(0, b[i]);
    }
    a[0] = 0, b[0] = 4;
    a[1] = 3, b[1] = 4;
    a[2] = 12, b[2] = 12;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = whetstone_pass_x100(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 7500 && want[2] == 10000 &&
              whetstone_pass_x100(1, 0) == WHETSTONE_REFUSED &&
              whetstone_pass_x100(5, 4) == WHETSTONE_REFUSED,
          "Whetstone: the C rule's own edges (WR7)");
    zxapp_arg_t pa[2] = {{"passes", a, WAVE_ROWS}, {"runs", b, WAVE_ROWS}};
    CHECK(wave_same(app, "assay.pass_share_x100", pa, 2, want, WAVE_ROWS, false),
          "Whetstone: the pass share, assay.pass_share_x100 = C");
}
