/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Quarry's languages against the C its model runs (quarry_rules.h), with
 * Witness's kind_count it uses for sources per topic. Slots:
 * @logic.quarry_claim_sourced, @logic.quarry_summary_length_ok,
 * @logic.quarry_cite_record_ok, @num.witness_kind_count. */
#include "quarry_rules.h"

APP_TEST(quarry, 9, 69)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* claim_sourced (QR1): Eve = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4 ? wrand(1, 9999) : wrand(-20000, 20000);
        b[i] = wrand(0, 1);
    }
    a[0] = 1, b[0] = 1;     /* the least number, held */
    a[1] = 9999, b[1] = 1;  /* max */
    a[2] = 10000, b[2] = 1; /* max + 1 */
    a[3] = 0, b[3] = 1;     /* names no citation */
    a[4] = -1, b[4] = 1;    /* negative */
    a[5] = 5, b[5] = 0;     /* a citation not held */
    a[6] = -9999, b[6] = 0; /* nothing at all */
    a[7] = 20000, b[7] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(quarry_sourced(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Quarry: the C rule's own edges (QR1)");
    zxapp_arg_t ca[2] = {{"cite", a, WAVE_ROWS}, {"held", b, WAVE_ROWS}};
    CHECK(wave_same(app, "claim_sourced", ca, 2, want, WAVE_ROWS, true),
          "Quarry: this claim has a source, Eve = KRON = C");
    {
        /* two citations under one number: Eve takes exactly one, KRON any: held */
        static const int64_t cn[2] = {5, 5}, two[2] = {1, 2};
        zxapp_arg_t cb[2] = {{"cite", cn, 2}, {"held", two, 2}};
        CHECK(wave_lp(app, "claim_sourced", cb, 2) == ZXAPP_BOTH,
              "Quarry: one number on two citations: Eve FALSE, KRON TRUE: BOTH, held");
        /* no claim of them has a source: both say so */
        static const int64_t none[2] = {0, 10000}, one[2] = {1, 1};
        zxapp_arg_t cc[2] = {{"cite", none, 2}, {"held", one, 2}};
        static const int64_t no[2] = {ZXAPP_FALSE, ZXAPP_FALSE};
        CHECK(wave_same(app, "claim_sourced", cc, 2, no, 2, true),
              "Quarry: no number and one past four digits: Eve = KRON = FALSE");
    }

    /* summary_length_ok (QR2): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(25, 500) : wrand(-10, 700);
        b[i] = i % 3 ? wrand(1, 32) : wrand(-2, 40);
    }
    a[0] = 25, b[0] = 1;   /* every least */
    a[1] = 500, b[1] = 32; /* every max */
    a[2] = 24, b[2] = 1;   /* words one below */
    a[3] = 501, b[3] = 1;  /* one above */
    a[4] = 100, b[4] = 0;  /* no source */
    a[5] = 100, b[5] = 33; /* one past */
    a[6] = -100, b[6] = 1; /* negative */
    a[7] = 0, b[7] = 0;    /* nothing */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(quarry_length_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Quarry: the C rule's own edges (QR2)");
    zxapp_arg_t sa[2] = {{"words", a, WAVE_ROWS}, {"sources", b, WAVE_ROWS}};
    CHECK(wave_same(app, "summary_length_ok", sa, 2, want, WAVE_ROWS, true),
          "Quarry: a summary's length limits, Eve = C");

    /* cite_record_ok (QR3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-12, 12);
        d[i] = i % 2 ? wrand(0, 2000000000) : wrand(-1, 1) * INT64_C(999999999999) + wrand(-2, 2);
        e[i] = wrand(-120000, 120000);
    }
    a[0] = 1, b[0] = 1, c[0] = 1, d[0] = 1760000000, e[0] = 0;
    a[1] = 9999, b[1] = 999, c[1] = 9, d[1] = 999999999999, e[1] = 99999; /* every max */
    a[2] = 10000, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 0;                 /* max + 1, each */
    a[3] = 1, b[3] = 1000, c[3] = 1, d[3] = 1, e[3] = 0;
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1, e[4] = 0;
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 1000000000000, e[5] = 0;
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = 100000;
    a[7] = -9999, b[7] = -999, c[7] = -9, d[7] = -999999999999, e[7] = -99999; /* sign aside */
    a[8] = 0, b[8] = 0, c[8] = 0, d[8] = 0, e[8] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(quarry_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Quarry: the C rule's own edges (QR3)");
    zxapp_arg_t ra[5] = {{"cite", a, WAVE_ROWS},
                         {"topic", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS},
                         {"quote", e, WAVE_ROWS}};
    CHECK(wave_same(app, "cite_record_ok", ra, 5, want, WAVE_ROWS, true),
          "Quarry: a citation makes a ^ZXQUARRY record, MYRA = C");

    /* sources per topic (QR4) through witness.kind_count: KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t col[32], one[1];
        uint32_t n = 1u + r % 32u;
        for (uint32_t i = 0; i < n; i++) col[i] = wrand(1, 1 + (int64_t) (r % 5u));
        one[0] = wrand(1, 3);
        int64_t count = quarry_count(col, n, one[0]);
        zxapp_arg_t wa[2] = {{"kind", col, n}, {"want", one, 1}};
        CHECK(wave_same(app, "witness.kind_count", wa, 2, &count, 1, false),
              "Quarry: %u citations, those of topic %lld through witness.kind_count", n,
              (long long) one[0]);
    }
}
