/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fieldguide's languages against the C its model runs (fieldguide_rules.h),
 * with Assay's score_rank it uses for the match ranking. Slots:
 * @num.fieldguide_species_caution, @num.fieldguide_key_next_lead,
 * @num.fieldguide_match_order_key, @logic.fieldguide_article_row_ok;
 * and, used, @num.assay_score_rank. */
#include "assay_rules.h"
#include "fieldguide_rules.h"

APP_TEST(fieldguide, 12, 102)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* FR1: species_caution, KRON = Trebago = C, over many sets of marks */
    {
        uint32_t seen[3] = {0, 0, 0};
        for (uint32_t s = 0; s < 48u; s++) {
            uint32_t n = 2u + s % 7u;
            for (uint32_t i = 0; i < n; i++) a[i] = wrand(-1, 1);
            if (s % 3u == 0) /* sets with no toxic mark, so unknown and none both come up */
                for (uint32_t i = 0; i < n; i++) a[i] = a[i] == 1 ? 0 : a[i];
            if (s % 6u == 0)
                for (uint32_t i = 0; i < n; i++) a[i] = 0;
            int64_t w = fieldguide_caution(a, n);
            if (w >= 0 && w <= 2) seen[w]++;
            zxapp_arg_t ma = {"mark", a, n};
            CHECK(
                w != FIELDGUIDE_REFUSED && wave_same(app, "species_caution", &ma, 1, &w, 1, false),
                "Fieldguide: species_caution set %u = %lld, KRON = Trebago = C", s, (long long) w);
        }
        CHECK(seen[0] && seen[1] && seen[2], "Fieldguide: every caution level came up (%u %u %u)",
              seen[0], seen[1], seen[2]);
    }
    {
        static const int64_t none[3] = {0, 0, 0}, blank[3] = {0, -1, 0}, marked[3] = {-1, 1, 0},
                             nothing[2] = {-1, -1};
        CHECK(fieldguide_caution(none, 3) == FIELDGUIDE_CAUTION_NONE &&
                  fieldguide_caution(blank, 3) == FIELDGUIDE_CAUTION_UNKNOWN &&
                  fieldguide_caution(marked, 3) == FIELDGUIDE_CAUTION_MARKED &&
                  fieldguide_caution(nothing, 2) == FIELDGUIDE_CAUTION_UNKNOWN &&
                  fieldguide_caution(none, 0) == FIELDGUIDE_CAUTION_UNKNOWN &&
                  fieldguide_caution(NULL, 3) == FIELDGUIDE_REFUSED,
              "Fieldguide: the caution's own edges (FR1); no article is unknown, never none");
        /* a mark past 1 or below -1: Trebago refuses, KRON clamps it: BOTH */
        static const int64_t over[2] = {0, 2}, under[2] = {-2, 0}, far[2] = {1000000, 1};
        zxapp_arg_t oa = {"mark", over, 2}, ua = {"mark", under, 2}, fa = {"mark", far, 2};
        CHECK(fieldguide_caution(over, 2) == FIELDGUIDE_REFUSED &&
                  fieldguide_caution(under, 2) == FIELDGUIDE_REFUSED &&
                  wave_lp(app, "species_caution", &oa, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "species_caution", &ua, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "species_caution", &fa, 1) == ZXAPP_BOTH,
              "Fieldguide: species_caution, a mark outside -1..1: Trebago refuses, KRON answers: "
              "BOTH");
    }

    /* FR2: key_next_lead, BASIC (Hatchling) = KRON = C, one row a call (Lingua reads one row) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t an = wrand(-1, 1), y = wrand(-FIELDGUIDE_LEAD_MAX, FIELDGUIDE_LEAD_MAX),
                no = wrand(-FIELDGUIDE_LEAD_MAX, FIELDGUIDE_LEAD_MAX);
        if (i == 0) y = FIELDGUIDE_LEAD_MAX, no = -FIELDGUIDE_LEAD_MAX, an = 1; /* max */
        if (i == 1) y = FIELDGUIDE_LEAD_MAX, no = -FIELDGUIDE_LEAD_MAX, an = 0;
        if (i == 2) y = 0, no = 0, an = -1; /* zero */
        int64_t w = fieldguide_key_next(an, y, no);
        zxapp_arg_t ka[3] = {{"answer", &an, 1}, {"yes", &y, 1}, {"no", &no, 1}};
        CHECK(w != FIELDGUIDE_REFUSED && wave_same(app, "key_next_lead", ka, 3, &w, 1, false),
              "Fieldguide: key_next_lead %lld %lld %lld = %lld, BASIC = KRON = C", (long long) an,
              (long long) y, (long long) no, (long long) w);
    }
    CHECK(fieldguide_key_next(1, 5, -7) == 5 && fieldguide_key_next(0, 5, -7) == -7 &&
              fieldguide_key_next(-1, 5, -7) == 0 && fieldguide_key_next(-1, 0, 0) == 0,
          "Fieldguide: yes, no, and not sure never guesses (FR2)");
    {
        /* an answer outside -1..1 or a lead past nine digits: BASIC refuses, KRON answers */
        static const int64_t bad[5][3] = {{2, 5, -7},
                                          {-2, 5, -7},
                                          {0, 5, 1000000000},
                                          {1, -1000000000, 3},
                                          {1, 1000000000, 1000000000}};
        for (uint32_t k = 0; k < 5u; k++) {
            int64_t an = bad[k][0], y = bad[k][1], no = bad[k][2];
            zxapp_arg_t ka[3] = {{"answer", &an, 1}, {"yes", &y, 1}, {"no", &no, 1}};
            CHECK(fieldguide_key_next(an, y, no) == FIELDGUIDE_REFUSED &&
                      wave_lp(app, "key_next_lead", ka, 3) == ZXAPP_BOTH,
                  "Fieldguide: key_next_lead edge %u: BASIC refuses, KRON answers: BOTH", k);
        }
    }

    /* FR3: match_order_key, KRON = Trebago = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FIELDGUIDE_SCORE_MAX);
        b[i] = wrand(0, 1);
    }
    a[0] = 0, b[0] = 0;                    /* zero */
    a[1] = FIELDGUIDE_SCORE_MAX, b[1] = 0; /* max, not keyed */
    a[2] = 0, b[2] = 1;                    /* keyed, above every score */
    a[3] = FIELDGUIDE_SCORE_MAX, b[3] = 1; /* max */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = fieldguide_match_key(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == FIELDGUIDE_SCORE_MAX && want[2] > want[1] &&
              want[3] == FIELDGUIDE_KEYED + FIELDGUIDE_SCORE_MAX,
          "Fieldguide: the order key's own edges (FR3): keyed first");
    zxapp_arg_t sa[2] = {{"score", a, WAVE_ROWS}, {"keyed", b, WAVE_ROWS}};
    CHECK(wave_same(app, "match_order_key", sa, 2, want, WAVE_ROWS, false),
          "Fieldguide: match_order_key, KRON = Trebago = C");
    {
        static const int64_t sc[3][2] = {{FIELDGUIDE_SCORE_MAX + 1, 5}, {-1, 5}, {7, 5}};
        static const int64_t ky[3][2] = {{0, 0}, {0, 0}, {2, 0}};
        for (uint32_t k = 0; k < 3u; k++) {
            zxapp_arg_t xa[2] = {{"score", sc[k], 2}, {"keyed", ky[k], 2}};
            CHECK(fieldguide_match_key(sc[k][0], ky[k][0]) == FIELDGUIDE_REFUSED &&
                      wave_lp(app, "match_order_key", xa, 2) == ZXAPP_BOTH,
                  "Fieldguide: match_order_key edge %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }
    /* the ranking is Assay's score_rank of the keys: the keyed match first */
    {
        static const int64_t k3[3] = {5, 4294967301, 5}, r3[3] = {2, 1, 2};
        zxapp_arg_t ra = {"score", k3, 3};
        CHECK(assay_score_rank(k3, 3, 0) == 2 && assay_score_rank(k3, 3, 1) == 1 &&
                  wave_same(app, "assay.score_rank", &ra, 1, r3, 3, false),
              "Fieldguide: match ranking, assay.score_rank of the keys");
    }

    /* FR4: article_row_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(1, 9999) : wrand(-20000, 20000);
        b[i] = i % 4 ? wrand(0, 999999999) : wrand(-2000000000, 2000000000);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-1, 1);
        e[i] = i % 5 ? wrand(0, 999999999999) : wrand(-2000000000000, 2000000000000);
    }
    a[0] = 9999, b[0] = 999999999, c[0] = 3, d[0] = -1, e[0] = 999999999999; /* every max */
    a[1] = 10000, b[1] = 1, c[1] = 1, d[1] = 0, e[1] = 1;                    /* place max + 1 */
    a[2] = 1, b[2] = 1000000000, c[2] = 1, d[2] = 0, e[2] = 1;               /* taxon max + 1 */
    a[3] = 1, b[3] = 1, c[3] = 10, d[3] = 0, e[3] = 1;                       /* kind past 1 digit */
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = 1, e[4] = 1000000000000;            /* mtime max + 1 */
    a[5] = 0, b[5] = 0, c[5] = 0, d[5] = 0, e[5] = 0;                        /* zero */
    a[6] = -9999, b[6] = -999999999, c[6] = -9, d[6] = -1, e[6] = -999999999999; /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(fieldguide_article_row_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_TRUE,
          "Fieldguide: the article record's own edges (FR4)");
    zxapp_arg_t aa[5] = {{"article", a, WAVE_ROWS},
                         {"taxon", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"mark", d, WAVE_ROWS},
                         {"mtime", e, WAVE_ROWS}};
    CHECK(wave_same(app, "article_row_ok", aa, 5, want, WAVE_ROWS, true),
          "Fieldguide: article_row_ok, MYRA = C");
}
