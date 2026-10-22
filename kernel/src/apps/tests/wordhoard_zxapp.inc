/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Wordhoard's languages against the C its model runs (wordhoard_rules.h).
 * Slots: @num.wordhoard_edit_distance, @num.wordhoard_shared_senses,
 * @logic.wordhoard_headword_row_ok, and, used as they are, Kindred's
 * @num.kindred_name_prefix_hit, Assay's @num.assay_score_rank and
 * Warren's @logic.warren_approval_ok. */
#include "wordhoard_rules.h"

/* a code point of one of five scripts' ranges, never a surrogate */
static uint32_t wh_cp(void)
{
    switch (wrand(0, 4)) {
    case 0:
        return (uint32_t) wrand('a', 'd'); /* a small alphabet, so words share letters */
    case 1:
        return (uint32_t) wrand(0xC0, 0xC3);
    case 2:
        return (uint32_t) wrand(0x4E00, 0x4E02);
    case 3:
        return (uint32_t) wrand(0x1F600, 0x1F601);
    default:
        return (uint32_t) wrand(0x10FFFE, 0x10FFFF);
    }
}

/* one edit_distance call with eight packed numbers, one row */
static bool wh_distance_same(zxapp_t *app, const int64_t *a, const int64_t *b, int64_t want)
{
    static const char *const NAMES[8] = {"a1", "a2", "a3", "a4", "b1", "b2", "b3", "b4"};
    int64_t v[8][1];
    zxapp_arg_t args[8];
    for (uint32_t k = 0; k < 8u; k++) {
        v[k][0] = k < 4u ? a[k] : b[k - 4u];
        args[k].name = NAMES[k];
        args[k].v = v[k];
        args[k].n = 1;
    }
    return wave_same(app, "edit_distance", args, 8, &want, 1, false);
}

static int32_t wh_distance_lp(zxapp_t *app, const int64_t *a, const int64_t *b)
{
    static const char *const NAMES[8] = {"a1", "a2", "a3", "a4", "b1", "b2", "b3", "b4"};
    int64_t v[8][1];
    zxapp_arg_t args[8];
    for (uint32_t k = 0; k < 8u; k++) {
        v[k][0] = k < 4u ? a[k] : b[k - 4u];
        args[k].name = NAMES[k];
        args[k].v = v[k];
        args[k].n = 1;
    }
    return wave_lp(app, "edit_distance", args, 8);
}

/* the code points of a C string of ASCII */
static uint32_t wh_ascii(const char *s, uint32_t *cp)
{
    uint32_t n = 0;
    while (s[n]) {
        cp[n] = (unsigned char) s[n];
        n++;
    }
    return n;
}

APP_TEST(wordhoard, 25, 46)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];
    uint32_t x[WORDHOARD_PACKED_CPS], y[WORDHOARD_PACKED_CPS], z[WORDHOARD_PACKED_CPS];
    int64_t pa[4], pb[4];

    /* the C rules' own edges (WH1-WH6) */
    {
        uint32_t nx = wh_ascii("kitten", x), ny = wh_ascii("sitting", y);
        CHECK(wordhoard_distance(x, nx, y, ny) == 3 && wordhoard_distance(x, nx, x, nx) == 0 &&
                  wordhoard_distance(x, 0, y, ny) == 7 && wordhoard_distance(x, nx, y, 0) == 6 &&
                  wordhoard_distance(x, wh_ascii("flaw", x), y, wh_ascii("lawn", y)) == 2,
              "Wordhoard: edit distances of known pairs (WH2)");
        x[0] = 0xE9, x[1] = 'a';
        y[0] = 'e', y[1] = 'a';
        CHECK(wordhoard_distance(x, 2, y, 2) == 1, "Wordhoard: one code point is one edit (WH2)");
        x[0] = 0xD800;
        CHECK(wordhoard_distance(x, 1, y, 1) == WORDHOARD_REFUSED &&
                  wordhoard_distance(y, 1, x, 1) == WORDHOARD_REFUSED &&
                  wordhoard_distance(x, WORDHOARD_WORD_CPS + 1u, y, 1) == WORDHOARD_REFUSED,
              "Wordhoard: a surrogate, or a word past 32 code points, refused (WH2)");
    }
    {
        uint32_t back[WORDHOARD_PACKED_CPS];
        bool round = true;
        for (uint32_t n = 0; n <= WORDHOARD_PACKED_CPS; n++) {
            for (uint32_t i = 0; i < n; i++) x[i] = wh_cp();
            round = round && wordhoard_pack(x, n, pa) &&
                    wordhoard_unpack(pa, back) == (int64_t) n && !memcmp(back, x, n * sizeof x[0]);
        }
        CHECK(round, "Wordhoard: 0..12 code points pack and unpack the same (WH1)");
        int64_t gap[4] = {(INT64_C(97) << 42) | 98, 0, 0, 0}; /* a, nothing, b */
        int64_t neg[4] = {-1, 0, 0, 0}, past[4] = {INT64_C(0x110000) << 42, 0, 0, 0},
                late[4] = {INT64_C(97) << 42, 0, 0, 97};
        x[0] = 0xDFFF;
        CHECK(wordhoard_unpack(gap, back) == WORDHOARD_REFUSED &&
                  wordhoard_unpack(neg, back) == WORDHOARD_REFUSED &&
                  wordhoard_unpack(past, back) == WORDHOARD_REFUSED &&
                  wordhoard_unpack(late, back) == WORDHOARD_REFUSED && !wordhoard_pack(x, 1, pa) &&
                  !wordhoard_pack(y, WORDHOARD_PACKED_CPS + 1u, pa),
              "Wordhoard: a gap, a negative, a field past U+10FFFF or a surrogate refused (WH1)");
    }
    CHECK(wordhoard_suggest_limit(0) == 0 && wordhoard_suggest_limit(1) == 1 &&
              wordhoard_suggest_limit(4) == 1 && wordhoard_suggest_limit(5) == 2 &&
              wordhoard_suggest_limit(8) == 2 && wordhoard_suggest_limit(9) == 3 &&
              wordhoard_suggest_limit(32) == 3,
          "Wordhoard: the did-you-mean limit (WH3)");
    {
        static const int64_t h[3] = {10, 20, 30}, s[4] = {20, 30, 40, 50}, bad[2] = {0, 2},
                             big[2] = {WORDHOARD_SENSE_MAX + 1, 5};
        CHECK(wordhoard_shared_senses(h, 3, s, 4) == 2 &&
                  wordhoard_shared_senses(h, 3, s, 0) == 0 &&
                  wordhoard_shared_senses(h, 3, bad, 2) == WORDHOARD_REFUSED &&
                  wordhoard_shared_senses(big, 2, s, 4) == WORDHOARD_REFUSED,
              "Wordhoard: shared senses at their edges (WH4)");
    }
    CHECK(wordhoard_record_ok(KINDRED_NAME_CODE_MAX, WORDHOARD_SENSE_MAX, 999, 9, 999999999999) &&
              wordhoard_record_ok(-1, -1, -1, -1, -1) &&
              !wordhoard_record_ok(KINDRED_NAME_CODE_MAX + 1, 1, 1, 1, 1) &&
              !wordhoard_record_ok(1, WORDHOARD_SENSE_MAX + 1, 1, 1, 1) &&
              !wordhoard_record_ok(1, 1, 1000, 1, 1) && !wordhoard_record_ok(1, 1, 1, 10, 1) &&
              !wordhoard_record_ok(1, 1, 1, 1, 1000000000000),
          "Wordhoard: the headword record's own edges (WH6)");

    /* edit_distance (WH1, WH2): C++ = BASIC = C, 64 pairs of words one call each */
    {
        bool all = true;
        uint32_t near = 0;
        for (uint32_t r = 0; r < WAVE_ROWS; r++) {
            uint32_t nx = (uint32_t) wrand(0, WORDHOARD_PACKED_CPS), ny;
            for (uint32_t i = 0; i < nx; i++) x[i] = wh_cp();
            if (r % 2u) { /* the other word an edit or two away */
                ny = nx;
                memcpy(y, x, nx * sizeof x[0]);
                for (int64_t k = wrand(1, 2); k > 0; k--) {
                    int64_t op = wrand(0, 2);
                    if (op == 0 && ny < WORDHOARD_PACKED_CPS) {
                        uint32_t at = (uint32_t) wrand(0, ny);
                        memmove(y + at + 1, y + at, (ny - at) * sizeof y[0]);
                        y[at] = wh_cp();
                        ny++;
                    } else if (op == 1 && ny > 0u) {
                        uint32_t at = (uint32_t) wrand(0, ny - 1u);
                        memmove(y + at, y + at + 1, (ny - at - 1u) * sizeof y[0]);
                        ny--;
                    } else if (ny > 0u) {
                        y[wrand(0, ny - 1u)] = wh_cp();
                    }
                }
            } else {
                ny = (uint32_t) wrand(0, WORDHOARD_PACKED_CPS);
                for (uint32_t i = 0; i < ny; i++) y[i] = wh_cp();
            }
            if (r == 0) nx = ny = 0;
            if (r == 2) { /* twelve code points each, the last of all */
                nx = ny = WORDHOARD_PACKED_CPS;
                for (uint32_t i = 0; i < nx; i++) x[i] = 0x10FFFF, y[i] = 'a';
            }
            if (!wordhoard_pack(x, nx, pa) || !wordhoard_pack(y, ny, pb)) {
                all = false;
                continue;
            }
            int64_t w = wordhoard_packed_distance(pa, pb);
            if (r == 2 && w != 12) all = false;
            near += r % 2u && w >= 0 && w <= 2;
            if (w < 0 || !wh_distance_same(app, pa, pb, w)) {
                all = false;
                printf("    edit_distance pair %u (%u and %u code points)\n", r, nx, ny);
            }
        }
        CHECK(all && near == WAVE_ROWS / 2u,
              "Wordhoard: edit_distance, C++ = BASIC = C, 64 pairs of 0..12 code points");
    }
    {
        /* a surrogate: C++ refuses, BASIC holds fields as numbers only and answers: BOTH */
        z[0] = 'a';
        CHECK(wordhoard_pack(z, 1, pb), "Wordhoard: a packs");
        int64_t sur[4] = {INT64_C(0xD800) << 42, 0, 0, 0},
                end[4] = {INT64_C(0xDFFF) << 42, 0, 0, 0};
        CHECK(wh_distance_lp(app, sur, pb) == ZXAPP_BOTH &&
                  wh_distance_lp(app, pb, end) == ZXAPP_BOTH,
              "Wordhoard: a surrogate: C++ refuses, BASIC answers: BOTH");
        int64_t neg[4] = {-1, 0, 0, 0}, gap[4] = {(INT64_C(97) << 42) | 98, 0, 0, 0},
                past[4] = {INT64_C(0x110000) << 42, 0, 0, 0};
        CHECK(wh_distance_lp(app, neg, pb) == ZXAPP_NEITHER &&
                  wh_distance_lp(app, gap, pb) == ZXAPP_NEITHER &&
                  wh_distance_lp(app, pb, past) == ZXAPP_NEITHER,
              "Wordhoard: a negative, a gap or a field past U+10FFFF: both refuse: NEITHER");
    }

    /* shared_senses (WH4): KRON = Trebago = C, 64 pairs of sense lists */
    {
        bool all = true;
        uint32_t some = 0;
        for (uint32_t r = 0; r < WAVE_ROWS; r++) {
            static int64_t h[24], s[24];
            uint32_t m = (uint32_t) wrand(1, 24), n = (uint32_t) wrand(1, 24);
            int64_t lo = r % 3u ? 1 : WORDHOARD_SENSE_MAX - 60;
            /* distinct within each list, from one small range so they meet */
            int64_t step = wrand(1, 2);
            int64_t base = lo + wrand(0, 10);
            for (uint32_t j = 0; j < m; j++) h[j] = base + (int64_t) j * step;
            base = lo + wrand(0, 10);
            for (uint32_t i = 0; i < n; i++) s[i] = base + (int64_t) i * (3 - step);
            if (r == 1) h[0] = 1, s[0] = 1, m = n = 1;
            if (r == 2) h[0] = WORDHOARD_SENSE_MAX, s[0] = 1, m = n = 1;
            int64_t w = wordhoard_shared_senses(h, m, s, n);
            some += w > 0;
            zxapp_arg_t sa[2] = {{"head", h, m}, {"sense", s, n}};
            if (w < 0 || !wave_same(app, "shared_senses", sa, 2, &w, 1, false)) {
                all = false;
                printf("    shared_senses pair %u (%u and %u senses)\n", r, m, n);
            }
        }
        CHECK(all && some > 8u && some < WAVE_ROWS,
              "Wordhoard: shared_senses, KRON = Trebago = C (%u pairs share a sense)", some);
    }
    {
        /* a synset number of 0 or past 9 digits: Trebago refuses, KRON counts it: BOTH */
        static const int64_t h1[2] = {1, 2}, s1[2] = {0, 2}, h2[2] = {WORDHOARD_SENSE_MAX + 1, 5},
                             s2[2] = {WORDHOARD_SENSE_MAX + 1, 5}, s3[2] = {-7, 3};
        zxapp_arg_t x1[2] = {{"head", h1, 2}, {"sense", s1, 2}};
        zxapp_arg_t x2[2] = {{"head", h2, 2}, {"sense", s2, 2}};
        zxapp_arg_t x3[2] = {{"head", h1, 2}, {"sense", s3, 2}};
        CHECK(
            wave_lp(app, "shared_senses", x1, 2) == ZXAPP_BOTH &&
                wave_lp(app, "shared_senses", x2, 2) == ZXAPP_BOTH &&
                wave_lp(app, "shared_senses", x3, 2) == ZXAPP_BOTH,
            "Wordhoard: a synset number outside 1..999999999: Trebago refuses, KRON counts: BOTH");
    }

    /* headword_row_ok (WH6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(-KINDRED_NAME_CODE_MAX, KINDRED_NAME_CODE_MAX)
                     : wrand(0, KINDRED_NAME_CODE_MAX / 8) * 12;
        b[i] = wrand(-1200000000, 1200000000);
        c[i] = wrand(-1100, 1100);
        d[i] = wrand(-11, 11);
        e[i] = wrand(-1100000000000, 1100000000000);
        if (i % 3 == 0)
            b[i] = wrand(1, WORDHOARD_SENSE_MAX), c[i] = wrand(0, 999), d[i] = wrand(0, 2);
        if (i % 3 == 0) e[i] = wrand(0, WORDHOARD_STAMP_MAX);
    }
    a[0] = 102030000000000000, b[0] = 20840710, c[0] = 1, d[0] = 1, e[0] = 1760000000;
    a[1] = KINDRED_NAME_CODE_MAX, b[1] = WORDHOARD_SENSE_MAX, c[1] = 999, d[1] = 9,
    e[1] = WORDHOARD_STAMP_MAX;
    a[2] = KINDRED_NAME_CODE_MAX + 1, b[2] = 1, c[2] = 0, d[2] = 0, e[2] = 1;
    a[3] = 1, b[3] = WORDHOARD_SENSE_MAX + 1, c[3] = 0, d[3] = 0, e[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 1000, d[4] = 0, e[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = 10, e[5] = 1;
    a[6] = 1, b[6] = 1, c[6] = 0, d[6] = 0, e[6] = WORDHOARD_STAMP_MAX + 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(wordhoard_record_ok(a[i], b[i], c[i], d[i], e[i]));
    {
        uint32_t yes = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) yes += want[i] == ZXAPP_TRUE;
        zxapp_arg_t ra[5] = {{"word", a, WAVE_ROWS},
                             {"sense", b, WAVE_ROWS},
                             {"lang", c, WAVE_ROWS},
                             {"list", d, WAVE_ROWS},
                             {"stamp", e, WAVE_ROWS}};
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
                  want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
                  want[6] == ZXAPP_FALSE && yes > 8u && yes < WAVE_ROWS - 8u &&
                  wave_same(app, "headword_row_ok", ra, 5, want, WAVE_ROWS, true),
              "Wordhoard: a headword row makes a ^ZXWORDHOARD record, MYRA = C (%u fit)", yes);
    }

    /* the prefix walk (WH7) through kindred.name_prefix_hit, over real headwords */
    {
        static const char *const WORDS[8] = {
            "dog", "dogma", "do", "cat", "Dove", "doghouse", "\xc3\xa9t\xc3\xa9", "a b"};
        static const char *const TYPED[4] = {"do", "dog", "\xc3\xa9", "ab"};
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            int64_t len = 0;
            const char *wd = WORDS[i % 8u], *t = TYPED[(i / 8u) % 4u];
            a[i] = kindred_name_code(wd, (uint32_t) strlen(wd), NULL);
            b[i] = kindred_name_code(t, (uint32_t) strlen(t), &len);
            c[i] = len;
            want[i] = kindred_prefix_hit(a[i], b[i], c[i]);
        }
        zxapp_arg_t ka[3] = {
            {"name", a, WAVE_ROWS}, {"prefix", b, WAVE_ROWS}, {"len", c, WAVE_ROWS}};
        CHECK(want[0] == 1 && want[3] == 0 && want[4] == 1 && want[7 + 24] == 1 &&
                  wave_same(app, "kindred.name_prefix_hit", ka, 3, want, WAVE_ROWS, false),
              "Wordhoard: the prefix walk through kindred.name_prefix_hit");
    }

    /* synonym order (WH5): shared counts placed by assay.score_rank */
    {
        static int64_t shared[WAVE_ROWS], place[WAVE_ROWS];
        for (uint32_t i = 0; i < WAVE_ROWS; i++) shared[i] = wrand(1, 8);
        for (uint32_t i = 0; i < WAVE_ROWS; i++) place[i] = assay_score_rank(shared, WAVE_ROWS, i);
        zxapp_arg_t sa[1] = {{"score", shared, WAVE_ROWS}};
        CHECK(wave_same(app, "assay.score_rank", sa, 1, place, WAVE_ROWS, false),
              "Wordhoard: synonym order through assay.score_rank");
    }

    /* a definition kept only on the owner's word (WH8) through warren.approval_ok */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        want[i] = T_(warren_approval_ok(a[i], b[i]));
    }
    zxapp_arg_t oa[2] = {{"owner", a, WAVE_ROWS}, {"approved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "warren.approval_ok", oa, 2, want, WAVE_ROWS, true),
          "Wordhoard: the owner's word through warren.approval_ok");
}
