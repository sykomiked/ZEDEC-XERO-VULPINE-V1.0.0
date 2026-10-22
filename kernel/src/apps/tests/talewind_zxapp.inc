/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talewind's languages against the C its model runs (talewind_rules.h).
 * Slots: @logic.talewind_choice_shown, @num.talewind_tale_flags_lacking,
 * @num.talewind_tale_flags_after, @logic.talewind_tale_passage_ok,
 * @logic.talewind_tale_choice_ok, and, used as it is,
 * @logic.sieve_suggestion_apply_ok. */
#include "sieve_rules.h"
#include "talewind_rules.h"

/* One Pascal call per row (zxapp_lingua.c L1): KRON, Pascal and the C on
 * one pair of masks. */
static bool tw_flags_same(zxapp_t *app, const char *entry, const char *x, const char *y, int64_t xv,
                          int64_t yv, int64_t want)
{
    int64_t a[1] = {xv}, b[1] = {yv}, w[1] = {want};
    zxapp_arg_t args[2] = {{x, a, 1}, {y, b, 1}};
    if (want >= 0 && wave_same(app, entry, args, 2, w, 1, false)) return true;
    printf("    %s %s=%lld %s=%lld\n", entry, x, (long long) xv, y, (long long) yv);
    return false;
}

static int32_t tw_flags_lp(zxapp_t *app, const char *entry, const char *x, const char *y,
                           int64_t xv, int64_t yv)
{
    int64_t a[1] = {xv}, b[1] = {yv};
    zxapp_arg_t args[2] = {{x, a, 1}, {y, b, 1}};
    return wave_lp(app, entry, args, 2);
}

APP_TEST(talewind, 16, 108)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges */
    CHECK(talewind_flags_lacking(0, 0) == 0 && talewind_flags_lacking(7, 0) == 3 &&
              talewind_flags_lacking(5, 4) == 1 && talewind_flags_lacking(65535, 0) == 16 &&
              talewind_flags_lacking(65535, 65535) == 0 &&
              talewind_flags_lacking(65536, 0) == TALEWIND_REFUSED &&
              talewind_flags_lacking(-1, 0) == TALEWIND_REFUSED &&
              talewind_flags_lacking(1, -1) == TALEWIND_REFUSED,
          "Talewind: the C flag count's own edges (TR2)");
    CHECK(talewind_flags_after(0, 0) == 0 && talewind_flags_after(1, 8) == 9 &&
              talewind_flags_after(5, 3) == 7 && talewind_flags_after(0, 65535) == 65535 &&
              talewind_flags_after(65536, 1) == TALEWIND_REFUSED &&
              talewind_flags_after(1, -1) == TALEWIND_REFUSED,
          "Talewind: the C flags after a choice's own edges (TR3)");
    CHECK(talewind_rating_room(4, 0) == 4 && talewind_rating_room(0, 0) == 0 &&
              talewind_rating_room(0, 4) == -4 && talewind_rating_room(5, 0) == TALEWIND_NO_ROOM &&
              talewind_rating_room(2, -1) == TALEWIND_NO_ROOM &&
              !talewind_choice_shown(0, talewind_rating_room(5, 0)),
          "Talewind: the C rating room's own edges (TR6)");

    /* choice_shown (TR1): every edge, then a sweep of real margins */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t need = wrand(0, TALEWIND_FLAGS_MAX), held = wrand(0, TALEWIND_FLAGS_MAX);
        if (i % 3 == 0) held = held | need; /* every flag held */
        a[i] = talewind_flags_lacking(need, held);
        b[i] = talewind_rating_room(wrand(0, 4), wrand(0, 4));
    }
    a[0] = 0, b[0] = 0;                /* the least that is shown */
    a[1] = 0, b[1] = 4;                /* the most */
    a[2] = 0, b[2] = -1;               /* rated one above the ceiling */
    a[3] = 1, b[3] = 0;                /* one flag lacking */
    a[4] = 16, b[4] = 4;               /* every flag lacking */
    a[5] = -1, b[5] = 0;               /* a count that is no count */
    a[6] = 0, b[6] = TALEWIND_NO_ROOM; /* a rating outside 0..4 */
    a[7] = 0, b[7] = -4;               /* adults only, under all ages */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(talewind_choice_shown(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Talewind: the C choice rule's own edges (TR1)");
    zxapp_arg_t sa[2] = {{"lacking", a, WAVE_ROWS}, {"ratingroom", b, WAVE_ROWS}};
    CHECK(wave_same(app, "choice_shown", sa, 2, want, WAVE_ROWS, true),
          "Talewind: choice_shown, Eve = KRON = C");
    {
        /* a room past 4 is no pair of ratings: Eve refuses it, KRON answers yes */
        static const int64_t none[2] = {0, 0}, room[2] = {4, 5}, wide[2] = {0, 1000000};
        zxapp_arg_t x1[2] = {{"lacking", none, 2}, {"ratingroom", room, 2}};
        zxapp_arg_t x2[2] = {{"lacking", none, 2}, {"ratingroom", wide, 2}};
        CHECK(wave_lp(app, "choice_shown", x1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "choice_shown", x2, 2) == ZXAPP_BOTH &&
                  !talewind_choice_shown(0, 5) && !talewind_choice_shown(0, 1000000),
              "Talewind: a room past 4: Eve no, KRON yes: BOTH");
    }

    /* tale_flags_lacking and tale_flags_after (TR2, TR3): 64 pairs of masks,
     * one Pascal call per pair */
    bool lack = true, after = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t x = wrand(0, TALEWIND_FLAGS_MAX), y = wrand(0, TALEWIND_FLAGS_MAX);
        if (r == 0) x = 0, y = 0;
        if (r == 1) x = TALEWIND_FLAGS_MAX, y = 0;
        if (r == 2) x = TALEWIND_FLAGS_MAX, y = TALEWIND_FLAGS_MAX;
        if (r == 3) x = 0, y = TALEWIND_FLAGS_MAX;
        if (r % 4 == 1) y |= x; /* every needed flag held */
        lack = lack && tw_flags_same(app, "tale_flags_lacking", "need", "held", x, y,
                                     talewind_flags_lacking(x, y));
        after = after && tw_flags_same(app, "tale_flags_after", "held", "sets", x, y,
                                       talewind_flags_after(x, y));
    }
    CHECK(lack, "Talewind: tale_flags_lacking, KRON = Pascal = C, 64 pairs");
    CHECK(after, "Talewind: tale_flags_after, KRON = Pascal = C, 64 pairs");
    {
        /* KRON counts the low 16 bits of any integer; Pascal refuses past 16 flags */
        static const int64_t over[][2] = {{65536, 0}, {-1, 0}, {1, -1}, {0, 1000000}};
        for (uint32_t k = 0; k < 4; k++) {
            CHECK(tw_flags_lp(app, "tale_flags_lacking", "need", "held", over[k][0], over[k][1]) ==
                          ZXAPP_BOTH &&
                      talewind_flags_lacking(over[k][0], over[k][1]) == TALEWIND_REFUSED,
                  "Talewind: tale_flags_lacking case %u: Pascal refuses, KRON answers: BOTH", k);
            CHECK(tw_flags_lp(app, "tale_flags_after", "held", "sets", over[k][0], over[k][1]) ==
                          ZXAPP_BOTH &&
                      talewind_flags_after(over[k][0], over[k][1]) == TALEWIND_REFUSED,
                  "Talewind: tale_flags_after case %u: Pascal refuses, KRON answers: BOTH", k);
        }
    }

    /* tale_passage_ok (TR4): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(1, TALEWIND_STORY_FIELD) : wrand(-1100000, 1100000);
        b[i] = wrand(-110, 110);
        c[i] = wrand(-11, 11);
        d[i] = i % 3 ? wrand(1, 1023) : wrand(-11000, 11000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;              /* every least */
    a[1] = 999999, b[1] = 99, c[1] = 9, d[1] = 9999;     /* every most */
    a[2] = 1000000, b[2] = 1, c[2] = 0, d[2] = 1;        /* story past i6 */
    a[3] = 1, b[3] = 100, c[3] = 0, d[3] = 1;            /* passage past i2 */
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1;             /* rating past i1 */
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = 10000;          /* bytes past i4 */
    a[6] = -999999, b[6] = -99, c[6] = -9, d[6] = -9999; /* sign aside */
    a[7] = -1000000, b[7] = 1, c[7] = 0, d[7] = 1;       /* and below */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(talewind_passage_ok(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE,
          "Talewind: the C passage record's own edges (TR4)");
    zxapp_arg_t pa[4] = {{"story", a, WAVE_ROWS},
                         {"passage", b, WAVE_ROWS},
                         {"rating", c, WAVE_ROWS},
                         {"bytes", d, WAVE_ROWS}};
    CHECK(wave_same(app, "tale_passage_ok", pa, 4, want, WAVE_ROWS, true),
          "Talewind: tale_passage_ok, MYRA = C");

    /* tale_choice_ok (TR5): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(1, TALEWIND_STORY_FIELD) : wrand(-1100000, 1100000);
        b[i] = wrand(-110, 110);
        c[i] = wrand(-11, 11);
        d[i] = wrand(-110, 110);
        e[i] = i % 3 ? wrand(0, TALEWIND_FLAGS_MAX) : wrand(-110000, 110000);
        f[i] = i % 3 ? wrand(0, TALEWIND_FLAGS_MAX) : wrand(-110000, 110000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;                 /* every least */
    a[1] = 999999, b[1] = 99, c[1] = 9, d[1] = 99, e[1] = 99999, f[1] = 99999;  /* every most */
    a[2] = 1, b[2] = 1, c[2] = 10, d[2] = 2, e[2] = 0, f[2] = 0;                /* choice past i1 */
    a[3] = 1, b[3] = 1, c[3] = 1, d[3] = 100, e[3] = 0, f[3] = 0;               /* target past i2 */
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = 2, e[4] = 100000, f[4] = 0;            /* need past i5 */
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 2, e[5] = 0, f[5] = -100000;           /* sets below -i5 */
    a[6] = 1000000, b[6] = 1, c[6] = 1, d[6] = 2, e[6] = 0, f[6] = 0;           /* story past i6 */
    a[7] = -1, b[7] = -99, c[7] = -9, d[7] = -99, e[7] = -99999, f[7] = -99999; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(talewind_choice_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Talewind: the C choice record's own edges (TR5)");
    zxapp_arg_t ca[6] = {{"story", a, WAVE_ROWS},  {"passage", b, WAVE_ROWS},
                         {"choice", c, WAVE_ROWS}, {"target", d, WAVE_ROWS},
                         {"need", e, WAVE_ROWS},   {"sets", f, WAVE_ROWS}};
    CHECK(wave_same(app, "tale_choice_ok", ca, 6, want, WAVE_ROWS, true),
          "Talewind: tale_choice_ok, MYRA = C");

    /* sieve.suggestion_apply_ok, as the model asks it of a held draft (TM3):
     * the author's accept, the seconds its hold has left, the story's drift */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t made = wrand(1700000000, 1800000000), ttl = wrand(1, SIEVE_HOLD_MAX);
        int64_t now = made + wrand(0, 2 * ttl);
        int64_t then = wrand(0, 1000), edits = i % 4 ? 0 : wrand(1, 3);
        a[i] = 1;
        b[i] = sieve_seconds_left(made, ttl, now);
        c[i] = sieve_drift(then + edits, then);
        want[i] = T_(sieve_apply_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t da[3] = {
        {"accepted", a, WAVE_ROWS}, {"secondsleft", b, WAVE_ROWS}, {"drift", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sieve.suggestion_apply_ok", da, 3, want, WAVE_ROWS, true),
          "Talewind: a held draft through sieve.suggestion_apply_ok, Eve = KRON = C");
}
