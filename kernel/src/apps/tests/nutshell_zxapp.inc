/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Nutshell's languages against the C its model runs (nutshell_rules.h).
 * Slots: @num.nutshell_leitner_next_box, @num.nutshell_leitner_due_day,
 * @num.nutshell_flash_due_count, @logic.nutshell_flash_deck_ok,
 * @logic.nutshell_flash_review_ok, and what it uses:
 * @num.witness_kind_count. */
#include "nutshell_rules.h"

/* One Lingua call per row (zxapp_lingua.c L1): BASIC and KRON against the C. */
static bool nut_box_same(zxapp_t *app, int64_t box, int64_t right)
{
    int64_t bv[1] = {box}, rv[1] = {right}, w[1] = {nutshell_next_box(box, right)};
    zxapp_arg_t a[2] = {{"box", bv, 1}, {"right", rv, 1}};
    if (w[0] >= 0 && wave_same(app, "leitner_next_box", a, 2, w, 1, false)) return true;
    printf("    leitner_next_box %lld %lld\n", (long long) box, (long long) right);
    return false;
}

static int32_t nut_box_lp(zxapp_t *app, int64_t box, int64_t right)
{
    int64_t bv[1] = {box}, rv[1] = {right};
    zxapp_arg_t a[2] = {{"box", bv, 1}, {"right", rv, 1}};
    return wave_lp(app, "leitner_next_box", a, 2);
}

static bool nut_due_same(zxapp_t *app, int64_t box, int64_t right, int64_t today)
{
    int64_t bv[1] = {box}, rv[1] = {right}, tv[1] = {today};
    int64_t w[1] = {nutshell_due_day(box, right, today)};
    zxapp_arg_t a[3] = {{"box", bv, 1}, {"right", rv, 1}, {"today", tv, 1}};
    if (w[0] >= 0 && wave_same(app, "leitner_due_day", a, 3, w, 1, false)) return true;
    printf("    leitner_due_day %lld %lld %lld\n", (long long) box, (long long) right,
           (long long) today);
    return false;
}

static int32_t nut_due_lp(zxapp_t *app, int64_t box, int64_t right, int64_t today)
{
    int64_t bv[1] = {box}, rv[1] = {right}, tv[1] = {today};
    zxapp_arg_t a[3] = {{"box", bv, 1}, {"right", rv, 1}, {"today", tv, 1}};
    return wave_lp(app, "leitner_due_day", a, 3);
}

APP_TEST(nutshell, 12, 106)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* the C's own rules (NS1, NS2) at their edges */
    CHECK(nutshell_next_box(1, 1) == 2 && nutshell_next_box(4, 1) == 5 &&
              nutshell_next_box(5, 1) == 5 && nutshell_next_box(5, 0) == 1 &&
              nutshell_next_box(1, 0) == 1 && nutshell_next_box(0, 1) == NUTSHELL_REFUSED &&
              nutshell_next_box(6, 0) == NUTSHELL_REFUSED &&
              nutshell_next_box(3, 2) == NUTSHELL_REFUSED &&
              nutshell_next_box(3, -1) == NUTSHELL_REFUSED,
          "Nutshell: leitner_next_box, the C's own edges");
    CHECK(nutshell_due_day(1, 0, 0) == 1 && nutshell_due_day(1, 1, 0) == 2 &&
              nutshell_due_day(2, 1, 0) == 4 && nutshell_due_day(3, 1, 0) == 8 &&
              nutshell_due_day(4, 1, 0) == 16 && nutshell_due_day(5, 1, 0) == 16 &&
              nutshell_due_day(4, 1, SOLSTICE_DAY_MAX - 16) == SOLSTICE_DAY_MAX &&
              nutshell_due_day(4, 1, SOLSTICE_DAY_MAX - 15) == NUTSHELL_REFUSED &&
              nutshell_due_day(1, 0, -1) == NUTSHELL_REFUSED &&
              nutshell_due_day(1, 0, SOLSTICE_DAY_MAX + 1) == NUTSHELL_REFUSED &&
              nutshell_due_day(0, 0, 5) == NUTSHELL_REFUSED,
          "Nutshell: leitner_due_day, the C's own edges");

    /* leitner_next_box (NS1): BASIC = KRON = C, every box and answer, then 64 rows */
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t box = r < 10 ? 1 + (int64_t) (r / 2) : wrand(1, NUTSHELL_BOX_MAX);
        int64_t right = r < 10 ? (int64_t) (r % 2) : wrand(0, 1);
        all = nut_box_same(app, box, right) && all;
    }
    CHECK(all, "Nutshell: leitner_next_box, BASIC = KRON = C, 64 answers");
    CHECK(nut_box_lp(app, 6, 1) == ZXAPP_BOTH && nut_box_lp(app, 0, 0) == ZXAPP_BOTH,
          "Nutshell: a box outside 1..5: BASIC refuses, KRON answers: BOTH");
    CHECK(nut_box_lp(app, 3, 2) == ZXAPP_BOTH && nut_box_lp(app, 3, -1) == ZXAPP_BOTH,
          "Nutshell: an answer that is no flag: BASIC refuses, KRON answers: BOTH");

    /* leitner_due_day (NS2): BASIC = KRON = C, 64 answers on days of every size */
    static const int64_t DS[][3] = {{1, 0, 0},
                                    {1, 1, 0},
                                    {5, 1, 0},
                                    {5, 0, 20745},
                                    {4, 1, 20745},
                                    {4, 1, SOLSTICE_DAY_MAX - 16},
                                    {1, 0, SOLSTICE_DAY_MAX - 1}};
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        bool fixed = r < sizeof DS / sizeof DS[0];
        int64_t box = fixed ? DS[r][0] : wrand(1, NUTSHELL_BOX_MAX);
        int64_t right = fixed ? DS[r][1] : wrand(0, 1);
        int64_t today = fixed ? DS[r][2] : wrand(0, r % 2 ? 40000 : SOLSTICE_DAY_MAX - 16);
        all = nut_due_same(app, box, right, today) && all;
    }
    CHECK(all, "Nutshell: leitner_due_day, BASIC = KRON = C, 64 answers");
    CHECK(nut_due_lp(app, 5, 1, SOLSTICE_DAY_MAX) == ZXAPP_BOTH &&
              nutshell_due_day(5, 1, SOLSTICE_DAY_MAX) == NUTSHELL_REFUSED,
          "Nutshell: due past 9999-12-31: BASIC refuses, KRON answers: BOTH");
    CHECK(nut_due_lp(app, 0, 0, 100) == ZXAPP_BOTH && nut_due_lp(app, 2, 1, -1) == ZXAPP_BOTH,
          "Nutshell: a box of 0 or a day before 1970: BASIC refuses, KRON answers: BOTH");

    /* flash_due_count (NS3): KRON = C, decks of 1 to 64 cards */
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = 1u + r % WAVE_ROWS;
        int64_t today = r == 0 ? 0 : wrand(0, 40000);
        for (uint32_t i = 0; i < n; i++) {
            a[i] = i % 3 ? wrand(today > 20 ? today - 20 : 0, today + 20) : wrand(0, 40000);
            b[i] = today;
        }
        if (r == 1) a[0] = today; /* due today counts */
        int64_t w = nutshell_due_count(a, n, today);
        zxapp_arg_t ca[2] = {{"due", a, n}, {"today", b, n}};
        if (!wave_same(app, "flash_due_count", ca, 2, &w, 1, false)) {
            printf("    flash_due_count deck of %u on %lld\n", n, (long long) today);
            all = false;
        }
    }
    CHECK(all, "Nutshell: flash_due_count, KRON = C, 64 decks");
    {
        static const int64_t due[4] = {20745, 20746, 20750, 0},
                             td[4] = {20746, 20746, 20746, 20746};
        int64_t w = 3;
        zxapp_arg_t ca[2] = {{"due", due, 4}, {"today", td, 4}};
        CHECK(nutshell_due_count(due, 4, 20746) == 3 &&
                  wave_same(app, "flash_due_count", ca, 2, &w, 1, false),
              "Nutshell: due yesterday, today and long ago count; next week does not");
    }

    /* flash_deck_ok (NS4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999);
        b[i] = wrand(0, 9999);
        c[i] = wrand(0, SOLSTICE_DAY_MAX);
        d[i] = i % 3 ? c[i] + wrand(0, 30) : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0;                     /* least */
    a[1] = 999999, b[1] = 9999, c[1] = 9999999, d[1] = 9999999; /* most */
    a[2] = 1000000;                                             /* deck max + 1 */
    b[3] = 10000;                                               /* cards max + 1 */
    c[4] = 10000000;                                            /* made max + 1 */
    d[5] = 10000000;                                            /* reviewed max + 1 */
    b[6] = -9999;                                               /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(nutshell_deck_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE,
          "Nutshell: flash_deck_ok, the C's own edges");
    zxapp_arg_t da[4] = {{"deck", a, WAVE_ROWS},
                         {"cards", b, WAVE_ROWS},
                         {"made", c, WAVE_ROWS},
                         {"reviewed", d, WAVE_ROWS}};
    CHECK(wave_same(app, "flash_deck_ok", da, 4, want, WAVE_ROWS, true),
          "Nutshell: flash_deck_ok, MYRA = C");

    /* flash_review_ok (NS5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999);
        b[i] = wrand(1, 9999);
        c[i] = wrand(1, NUTSHELL_BOX_MAX);
        d[i] = wrand(0, SOLSTICE_DAY_MAX);
        e[i] = wrand(0, i % 2 ? 50 : 999999);
        f[i] = wrand(0, i % 2 ? 50 : 999999);
    }
    a[0] = 1, b[0] = 1, c[0] = 1, d[0] = 0, e[0] = 0, f[0] = 0;                         /* least */
    a[1] = 999999, b[1] = 9999, c[1] = 9, d[1] = 9999999, e[1] = 999999, f[1] = 999999; /* most */
    a[2] = 1000000;  /* deck max + 1 */
    b[3] = 10000;    /* card max + 1 */
    c[4] = 10;       /* box max + 1 */
    d[5] = 10000000; /* due max + 1 */
    e[6] = 1000000;  /* right max + 1 */
    f[7] = 1000000;  /* wrong max + 1 */
    c[8] = -9;       /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(nutshell_review_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE,
          "Nutshell: flash_review_ok, the C's own edges");
    zxapp_arg_t ra[6] = {{"deck", a, WAVE_ROWS}, {"card", b, WAVE_ROWS},  {"box", c, WAVE_ROWS},
                         {"due", d, WAVE_ROWS},  {"right", e, WAVE_ROWS}, {"wrong", f, WAVE_ROWS}};
    CHECK(wave_same(app, "flash_review_ok", ra, 6, want, WAVE_ROWS, true),
          "Nutshell: flash_review_ok, MYRA = C");

    /* the cards in each box, through witness.kind_count (WR2) */
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t boxes[64], wb[1];
        uint32_t n = 1u + r % 64u;
        for (uint32_t i = 0; i < n; i++) boxes[i] = wrand(1, NUTSHELL_BOX_MAX);
        wb[0] = r == 0 ? NUTSHELL_BOX_MAX + 1 : wrand(1, NUTSHELL_BOX_MAX); /* no card there */
        int64_t cnt = witness_kind_count(boxes, n, wb[0]);
        zxapp_arg_t ka[2] = {{"kind", boxes, n}, {"want", wb, 1}};
        all = wave_same(app, "witness.kind_count", ka, 2, &cnt, 1, false) && all;
    }
    CHECK(all, "Nutshell: the cards in a box, witness.kind_count, KRON = Trebago = C, 64 decks");
    {
        static const int64_t k2[2] = {1, 2}, wants[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"kind", k2, 2}, {"want", wants, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ba, 2) == ZXAPP_BOTH,
              "Nutshell: two boxes asked at once: Trebago refuses, KRON sums them: BOTH");
    }
}
