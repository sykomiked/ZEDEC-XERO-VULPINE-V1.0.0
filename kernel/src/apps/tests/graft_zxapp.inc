/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Graft's languages against the C its model runs (graft_rules.h).
 * Slots: @logic.graft_merge_line_clash, @logic.graft_merge_untouched,
 * @num.graft_diff_lines_added, @num.graft_diff_lines_removed,
 * @logic.graft_diff_size_ok, @logic.graft_commit_record_ok, and a
 * history's links through Witness, @logic.witness_chain_ok. */
#include "graft_rules.h"

APP_TEST(graft, 13, 124)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* GR2 in C: the four values of a line, joined with lpres_kjoin */
    CHECK(graft_line_state(0, 0, 0) == LPRES_STATE_NEITHER &&
              graft_line_state(1, 0, 0) == LPRES_STATE_TRUE &&
              graft_line_state(0, 1, 0) == LPRES_STATE_TRUE &&
              graft_line_state(1, 1, 0) == LPRES_STATE_TRUE &&
              graft_line_state(1, 1, 1) == LPRES_STATE_BOTH &&
              graft_line_state(2, 1, 1) == LPRES_STATE_NEITHER,
          "Graft: a line's four values (GR2)");
    for (int64_t o = 0; o <= 1; o++)
        for (int64_t t = 0; t <= 1; t++)
            for (int64_t p = 0; p <= 1; p++) {
                lpres_state_t s = graft_line_state(o, t, o && t ? p : 0);
                CHECK((s == LPRES_STATE_BOTH) == graft_line_clash(o, t, o && t ? p : 0) &&
                          (s == LPRES_STATE_NEITHER) == graft_line_untouched(o, t),
                      "Graft: the two truths are the line's BOTH and NEITHER (%lld %lld %lld)",
                      (long long) o, (long long) t, (long long) p);
            }
    {
        /* the file joined with lpres_conjoin across the lines that changed */
        static const lpres_state_t clean[4] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE,
                                               LPRES_STATE_NEITHER, LPRES_STATE_TRUE};
        static const lpres_state_t held[4] = {LPRES_STATE_TRUE, LPRES_STATE_NEITHER,
                                              LPRES_STATE_BOTH, LPRES_STATE_TRUE};
        static const lpres_state_t none[3] = {LPRES_STATE_NEITHER, LPRES_STATE_NEITHER,
                                              LPRES_STATE_NEITHER};
        CHECK(graft_merge_join(clean, 4) == LPRES_STATE_TRUE &&
                  graft_merge_join(held, 4) == LPRES_STATE_BOTH &&
                  graft_merge_join(none, 3) == LPRES_STATE_NEITHER &&
                  graft_merge_join(NULL, 3) == LPRES_STATE_NEITHER &&
                  graft_merge_join(held, 0) == LPRES_STATE_NEITHER,
              "Graft: a file's merge, an untouched line never hiding a BOTH");
    }

    /* merge_line_clash (GR2): Eve = KRON = C, every flag pattern, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 8 ? (i >> 2) & 1 : wrand(0, 1);
        b[i] = i < 8 ? (i >> 1) & 1 : wrand(0, 1);
        c[i] = i < 8 ? i & 1 : wrand(0, 1);
        want[i] = T_(graft_line_clash(a[i], b[i], c[i]));
    }
    CHECK(graft_line_clash(1, 1, 1) && !graft_line_clash(1, 1, 0) && !graft_line_clash(0, 1, 1) &&
              !graft_line_clash(2, 1, 1) && !graft_line_clash(1, -1, 1) &&
              !graft_line_clash(1, 1, 2),
          "Graft: the C rule's own edges (GR2 clash)");
    zxapp_arg_t ka[3] = {{"ours", a, WAVE_ROWS}, {"theirs", b, WAVE_ROWS}, {"apart", c, WAVE_ROWS}};
    CHECK(wave_same(app, "merge_line_clash", ka, 3, want, WAVE_ROWS, true),
          "Graft: merge_line_clash, Eve = KRON = C");
    {
        /* a flag of 2 is no flag: Eve's 1 .. 1 refuses it, KRON reads above 0 as set */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2};
        zxapp_arg_t xa[3] = {{"ours", two, 2}, {"theirs", one, 2}, {"apart", one, 2}};
        CHECK(wave_lp(app, "merge_line_clash", xa, 3) == ZXAPP_BOTH,
              "Graft: ours 2: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t ya[3] = {{"ours", one, 2}, {"theirs", one, 2}, {"apart", two, 2}};
        CHECK(wave_lp(app, "merge_line_clash", ya, 3) == ZXAPP_BOTH,
              "Graft: apart 2: Eve FALSE, KRON TRUE: BOTH");
    }

    /* merge_untouched (GR2): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (i >> 1) & 1 : wrand(0, 1);
        b[i] = i < 4 ? i & 1 : wrand(0, 1);
        want[i] = T_(graft_line_untouched(a[i], b[i]));
    }
    CHECK(graft_line_untouched(0, 0) && !graft_line_untouched(1, 0) &&
              !graft_line_untouched(0, 1) && !graft_line_untouched(-1, 0) &&
              !graft_line_untouched(0, 2),
          "Graft: the C rule's own edges (GR2 untouched)");
    zxapp_arg_t ua[2] = {{"ours", a, WAVE_ROWS}, {"theirs", b, WAVE_ROWS}};
    CHECK(wave_same(app, "merge_untouched", ua, 2, want, WAVE_ROWS, true),
          "Graft: merge_untouched, Eve = KRON = C");
    {
        /* -1 is no flag: Eve's 0 .. 0 refuses it, KRON reads below 1 as clear */
        static const int64_t zero[2] = {0, 0}, neg[2] = {0, -1};
        zxapp_arg_t xa[2] = {{"ours", zero, 2}, {"theirs", neg, 2}};
        CHECK(wave_lp(app, "merge_untouched", xa, 2) == ZXAPP_BOTH,
              "Graft: theirs -1: Eve FALSE, KRON TRUE: BOTH");
    }

    /* diff_lines_added and diff_lines_removed (GR3): Trebago = C, exact, every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GRAFT_LINES_MAX);
        b[i] = wrand(0, a[i]);
    }
    a[0] = 0, b[0] = 0;                             /* two empty files */
    a[1] = GRAFT_LINES_MAX, b[1] = 0;               /* every line new */
    a[2] = GRAFT_LINES_MAX, b[2] = GRAFT_LINES_MAX; /* nothing changed */
    a[3] = 1, b[3] = 1;
    bool fits = true;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) fits = graft_lines_added(a[i], b[i], &want[i]) && fits;
    CHECK(fits && want[0] == 0 && want[1] == GRAFT_LINES_MAX && want[2] == 0,
          "Graft: the C rule's own edges (GR3)");
    zxapp_arg_t aa[2] = {{"after", a, WAVE_ROWS}, {"kept", b, WAVE_ROWS}};
    CHECK(wave_same(app, "diff_lines_added", aa, 2, want, WAVE_ROWS, false),
          "Graft: diff_lines_added, Trebago = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        fits = graft_lines_removed(a[i], b[i], &want[i]) && fits;
    zxapp_arg_t ra[2] = {{"before", a, WAVE_ROWS}, {"kept", b, WAVE_ROWS}};
    CHECK(fits && wave_same(app, "diff_lines_removed", ra, 2, want, WAVE_ROWS, false),
          "Graft: diff_lines_removed, Trebago = C");
    {
        /* refused by Trebago and by C alike: kept past the side, a side past
         * 65536 (max + 1), a negative count */
        static const int64_t side[3][2] = {{5, 5}, {5, GRAFT_LINES_MAX + 1}, {5, -1}};
        static const int64_t kept[3][2] = {{1, 6}, {1, 0}, {1, -1}};
        int64_t got = 0;
        zxapp_num_t o[2];
        uint32_t k = 0;
        for (uint32_t r = 0; r < 3u; r++) {
            zxapp_arg_t xa[2] = {{"after", side[r], 2}, {"kept", kept[r], 2}};
            zxapp_arg_t xr[2] = {{"before", side[r], 2}, {"kept", kept[r], 2}};
            CHECK(zxapp_call(app, &H, "diff_lines_added", xa, 2, o, 2, &k) != ZXAPP_OK &&
                      zxapp_call(app, &H, "diff_lines_removed", xr, 2, o, 2, &k) != ZXAPP_OK &&
                      !graft_lines_added(side[r][1], kept[r][1], &got) &&
                      !graft_lines_removed(side[r][1], kept[r][1], &got),
                  "Graft: refused case %u, by Trebago and C", r);
        }
    }

    /* diff_size_ok (GR4): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, GRAFT_BYTES_MAX + 2);
        b[i] = wrand(-2, GRAFT_LINES_MAX + 2);
    }
    a[0] = 0, b[0] = 0;
    a[1] = GRAFT_BYTES_MAX, b[1] = GRAFT_LINES_MAX;
    a[2] = GRAFT_BYTES_MAX + 1, b[2] = 1;
    a[3] = 1, b[3] = GRAFT_LINES_MAX + 1;
    a[4] = -1, b[4] = 1;
    a[5] = 1, b[5] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(graft_diff_fits(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Graft: the C rule's own edges (GR4)");
    zxapp_arg_t la[2] = {{"bytes", a, WAVE_ROWS}, {"lines", b, WAVE_ROWS}};
    CHECK(wave_same(app, "diff_size_ok", la, 2, want, WAVE_ROWS, true),
          "Graft: diff_size_ok, Eve = C");

    /* commit_record_ok (GR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = a[i] - 1;
        c[i] = wrand(1, GRAFT_AUTHOR);
        d[i] = wrand(0, GRAFT_STAMP);
        e[i] = wrand(1, GRAFT_MSG_MAX);
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 0, e[0] = 1; /* every least */
    a[1] = 999999999, b[1] = 999999998, c[1] = GRAFT_AUTHOR, d[1] = GRAFT_STAMP, e[1] = 9999;
    a[2] = 1000000000, b[2] = 0, c[2] = 1, d[2] = 1, e[2] = 1;       /* commit max + 1 */
    a[3] = 2, b[3] = 1, c[3] = GRAFT_AUTHOR + 1, d[3] = 1, e[3] = 1; /* author max + 1 */
    a[4] = 2, b[4] = 1, c[4] = 1, d[4] = GRAFT_STAMP + 1, e[4] = 1;  /* stamp max + 1 */
    a[5] = 2, b[5] = 1, c[5] = 1, d[5] = 1, e[5] = 10000;            /* a message of 5 digits */
    a[6] = -2, b[6] = -3, c[6] = -1, d[6] = -1, e[6] = -5;           /* sign aside, they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(graft_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE,
          "Graft: the record's own edges (GR5)");
    zxapp_arg_t ca[5] = {{"commit", a, WAVE_ROWS},
                         {"parent", b, WAVE_ROWS},
                         {"author", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS},
                         {"msglen", e, WAVE_ROWS}};
    CHECK(wave_same(app, "commit_record_ok", ca, 5, want, WAVE_ROWS, true),
          "Graft: commit_record_ok, MYRA = C");

    /* GR1: a history's links through witness.chain_ok, KRON = Sutra = C */
    int64_t last = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = last + (i % 11 == 5 ? 0 : 1);  /* some commits not past the last */
        b[i] = i % 13 == 7 ? last + 1 : last; /* some parents pointing elsewhere */
        c[i] = last;
        want[i] = T_(graft_linked(a[i], b[i], c[i]));
        last = a[i] > last ? a[i] : last;
    }
    CHECK(want[0] == ZXAPP_TRUE && want[5] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE &&
              graft_linked(1, 0, 0) && !graft_linked(0, 0, 0),
          "Graft: the history's own links (GR1)");
    zxapp_arg_t ha[3] = {{"seq", a, WAVE_ROWS}, {"prev", b, WAVE_ROWS}, {"last", c, WAVE_ROWS}};
    CHECK(wave_same(app, "witness.chain_ok", ha, 3, want, WAVE_ROWS, true),
          "Graft: a history through witness.chain_ok, KRON = Sutra = C");
    {
        static const int64_t ix[2] = {2, -1}, pv[2] = {1, 1}, ls[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"seq", ix, 2}, {"prev", pv, 2}, {"last", ls, 2}};
        CHECK(wave_lp(app, "witness.chain_ok", xa, 3) == ZXAPP_BOTH,
              "Graft: a negative commit: Sutra refuses, KRON answers: BOTH");
    }
}
