/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fireglow's languages against the C its model runs (fireglow_rules.h).
 * Slots: @num.fireglow_speed_left_ms, @num.fireglow_chapter_at_pos,
 * @logic.fireglow_play_speed_ok, @logic.fireglow_episode_row_ok and,
 * used as they are, the Banker's @num.banker_share and Halloo's @num.halloo_hms. */
#include "fireglow_rules.h"

/* speed_left_ms over rows: every row the exact reduced FR1, Trebago = KRON */
static bool fg_left_same(zxapp_t *a, const int64_t *len, const int64_t *pos, const int64_t *rate,
                         uint32_t rows)
{
    zxapp_arg_t xa[3] = {{"length", len, rows}, {"pos", pos, rows}, {"rate", rate, rows}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "speed_left_ms", xa, 3, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    speed_left_ms: rc %d lp %d n %u (%s)\n", rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++) {
        int64_t num = 0, den = 0;
        if (!fireglow_speed_left(len[i], pos[i], rate[i], &num, &den) || o[i].num != num ||
            o[i].den != den) {
            printf("    speed_left_ms row %u: %lld/%lld, want %lld/%lld\n", i, (long long) o[i].num,
                   (long long) o[i].den, (long long) num, (long long) den);
            return false;
        }
    }
    return true;
}

static int32_t fg_left_lp(zxapp_t *a, int64_t len, int64_t pos, int64_t rate)
{
    /* a good row first, then the row under test: KRON never sees one element */
    int64_t l2[2] = {60000, len}, p2[2] = {0, pos}, r2[2] = {FIREGLOW_RATE_UNIT, rate};
    zxapp_arg_t xa[3] = {{"length", l2, 2}, {"pos", p2, 2}, {"rate", r2, 2}};
    return wave_lp(a, "speed_left_ms", xa, 3);
}

APP_TEST(fireglow, 19, 52)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* speed_left_ms (FR1): Trebago = KRON = C, exact, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FIREGLOW_LEN_MAX);
        b[i] = wrand(0, a[i]);
        c[i] = wrand(FIREGLOW_RATE_MIN, FIREGLOW_RATE_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = FIREGLOW_RATE_MIN;                /* nothing to hear */
    a[1] = FIREGLOW_LEN_MAX, b[1] = 0, c[1] = FIREGLOW_RATE_MIN; /* the longest, slowest */
    a[2] = FIREGLOW_LEN_MAX, b[2] = FIREGLOW_LEN_MAX, c[2] = FIREGLOW_RATE_MAX; /* at the end */
    a[3] = 60000, b[3] = 0, c[3] = FIREGLOW_RATE_UNIT;                          /* normal */
    a[4] = 1000, b[4] = 0, c[4] = 98304;                                        /* 1.5: 2000/3 */
    a[5] = 3600000, b[5] = 0, c[5] = FIREGLOW_RATE_MAX;
    {
        int64_t num = 0, den = 0;
        CHECK(fireglow_speed_left(1000, 0, 98304, &num, &den) && num == 2000 && den == 3 &&
                  fireglow_speed_left_floor(FIREGLOW_LEN_MAX, 0, FIREGLOW_RATE_MIN) ==
                      2 * FIREGLOW_LEN_MAX &&
                  fireglow_speed_left_floor(3600000, 0, FIREGLOW_RATE_MAX) == 1200000 &&
                  fireglow_speed_left_floor(1000, 1001, FIREGLOW_RATE_UNIT) == FIREGLOW_REFUSED &&
                  fireglow_speed_left_floor(1000, -1, FIREGLOW_RATE_UNIT) == FIREGLOW_REFUSED &&
                  fireglow_speed_left_floor(FIREGLOW_LEN_MAX + 1, 0, FIREGLOW_RATE_UNIT) ==
                      FIREGLOW_REFUSED &&
                  fireglow_speed_left_floor(-1, 0, FIREGLOW_RATE_UNIT) == FIREGLOW_REFUSED &&
                  fireglow_speed_left_floor(1000, 0, FIREGLOW_RATE_MIN - 1) == FIREGLOW_REFUSED &&
                  fireglow_speed_left_floor(1000, 0, FIREGLOW_RATE_MAX + 1) == FIREGLOW_REFUSED &&
                  !fireglow_speed_left(1000, 0, FIREGLOW_RATE_UNIT, NULL, &den),
              "Fireglow: the C time left's own edges (FR1)");
    }
    CHECK(fg_left_same(app, a, b, c, WAVE_ROWS), "Fireglow: speed_left_ms, Trebago = KRON = C");
    CHECK(fg_left_lp(app, 1000, 1001, FIREGLOW_RATE_UNIT) == ZXAPP_BOTH,
          "Fireglow: a place past the length: Trebago refuses, KRON answers: BOTH");
    CHECK(fg_left_lp(app, 1000, -1, FIREGLOW_RATE_UNIT) == ZXAPP_BOTH,
          "Fireglow: a negative place: Trebago refuses, KRON answers: BOTH");
    CHECK(fg_left_lp(app, FIREGLOW_LEN_MAX + 1, 0, FIREGLOW_RATE_UNIT) == ZXAPP_BOTH,
          "Fireglow: 100 hours or more: Trebago refuses, KRON answers: BOTH");
    {
        static const int64_t l2[2] = {1000, 1000}, p2[2] = {0, 0},
                             r2[2] = {FIREGLOW_RATE_UNIT, FIREGLOW_RATE_MIN - 1},
                             r3[2] = {FIREGLOW_RATE_UNIT, FIREGLOW_RATE_MAX + 1};
        zxapp_arg_t xa[3] = {{"length", l2, 2}, {"pos", p2, 2}, {"rate", r2, 2}};
        zxapp_arg_t ya[3] = {{"length", l2, 2}, {"pos", p2, 2}, {"rate", r3, 2}};
        CHECK(wave_lp(app, "speed_left_ms", xa, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "speed_left_ms", ya, 3) == ZXAPP_BOTH,
              "Fireglow: a speed past its limits: Trebago refuses, KRON answers: BOTH");
    }

    /* chapter_at_pos (FR3): 64 chapter lists, KRON = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t starts[FIREGLOW_CHAPTERS];
        uint32_t n = r < 2 ? FIREGLOW_CHAPTERS : (uint32_t) wrand(1, 20);
        starts[0] = 0;
        for (uint32_t i = 1; i < n; i++) starts[i] = starts[i - 1] + wrand(1, 3600000);
        int64_t pos = r == 0   ? 0
                      : r == 1 ? starts[n - 1] + 1
                      : r == 2 ? starts[n - 1]
                      : r == 3 ? -1
                               : wrand(0, starts[n - 1] + 600000);
        int64_t ch = fireglow_chapter_at_pos(starts, n, pos);
        zxapp_arg_t ca[2] = {{"starts", starts, n}, {"pos", &pos, 1}};
        CHECK(ch >= 0 && ch <= (int64_t) n &&
                  wave_same(app, "chapter_at_pos", ca, 2, &ch, 1, false),
              "Fireglow: chapter list %u, KRON = C", r);
    }
    {
        static const int64_t st[3] = {0, 60000, 120000};
        CHECK(
            fireglow_chapter_at_pos(st, 3, 0) == 1 && fireglow_chapter_at_pos(st, 3, 59999) == 1 &&
                fireglow_chapter_at_pos(st, 3, 60000) == 2 &&
                fireglow_chapter_at_pos(st, 3, 999999999) == 3 &&
                fireglow_chapter_at_pos(st, 3, -1) == 0 && fireglow_chapter_at_pos(st, 0, 5) == 0 &&
                fireglow_chapter_at_pos(NULL, 0, 5) == 0 &&
                fireglow_chapter_at_pos(NULL, 1, 5) == FIREGLOW_REFUSED,
            "Fireglow: the C chapter's own edges (FR3)");
    }

    /* play_speed_ok (FR4): Eve = C, swept across and past the limits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 2 * FIREGLOW_RATE_MAX);
    a[0] = FIREGLOW_RATE_MIN, a[1] = FIREGLOW_RATE_MAX, a[2] = FIREGLOW_RATE_MIN - 1;
    a[3] = FIREGLOW_RATE_MAX + 1, a[4] = 0, a[5] = -FIREGLOW_RATE_UNIT, a[6] = FIREGLOW_RATE_UNIT;
    a[7] = INT64_C(-1000000000000);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(fireglow_play_speed_ok(a[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE,
          "Fireglow: the C speed limits' own edges (FR4)");
    zxapp_arg_t sa = {"rate", a, WAVE_ROWS};
    CHECK(wave_same(app, "play_speed_ok", &sa, 1, want, WAVE_ROWS, true),
          "Fireglow: play_speed_ok, Eve = C");

    /* episode_row_ok (FR5): MYRA = Eve = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FIREGLOW_SHOWS - 1);
        b[i] = wrand(0, FIREGLOW_EPISODES - 1);
        c[i] = wrand(0, FIREGLOW_LEN_MAX);
        d[i] = wrand(0, FIREGLOW_LEN_MAX);
        e[i] = wrand(0, FIREGLOW_HEARD_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = FIREGLOW_SHOWS - 1, b[1] = FIREGLOW_EPISODES - 1, c[1] = FIREGLOW_LEN_MAX;
    d[1] = FIREGLOW_LEN_MAX, e[1] = FIREGLOW_HEARD_MAX; /* every max */
    a[2] = 100;                                         /* past MYRA's i2 too */
    b[3] = 1000;                                        /* past i3 */
    c[4] = 1000000000;                                  /* past i9 */
    d[5] = -1000000000;                                 /* past i9, negative */
    e[6] = FIREGLOW_HEARD_MAX + 1;                      /* past i10 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(fireglow_episode_row_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && !fireglow_episode_row_ok(FIREGLOW_SHOWS, 0, 0, 0, 0) &&
              !fireglow_episode_row_ok(0, FIREGLOW_EPISODES, 0, 0, 0) &&
              !fireglow_episode_row_ok(0, 0, -1, 0, 0) &&
              !fireglow_episode_row_ok(0, 0, 0, FIREGLOW_LEN_MAX + 1, 0) &&
              !fireglow_episode_row_ok(0, 0, 0, 0, -1),
          "Fireglow: the C episode record's own edges (FR5)");
    zxapp_arg_t ea[5] = {{"show", a, WAVE_ROWS},
                         {"episode", b, WAVE_ROWS},
                         {"pos", c, WAVE_ROWS},
                         {"length", d, WAVE_ROWS},
                         {"heard", e, WAVE_ROWS}};
    CHECK(wave_same(app, "episode_row_ok", ea, 5, want, WAVE_ROWS, true),
          "Fireglow: episode_row_ok, MYRA = Eve = C");
    {
        /* inside MYRA's digits, outside Eve's ranges: held */
        static const int64_t z2[2] = {0, 0}, s2[2] = {0, (int64_t) FIREGLOW_SHOWS},
                             p2[2] = {0, FIREGLOW_LEN_MAX + 1}, h2[2] = {0, -5};
        zxapp_arg_t xa[5] = {{"show", s2, 2},
                             {"episode", z2, 2},
                             {"pos", z2, 2},
                             {"length", z2, 2},
                             {"heard", z2, 2}};
        CHECK(wave_lp(app, "episode_row_ok", xa, 5) == ZXAPP_BOTH,
              "Fireglow: a show past the table: MYRA TRUE, Eve FALSE: BOTH");
        zxapp_arg_t ya[5] = {{"show", z2, 2},
                             {"episode", z2, 2},
                             {"pos", p2, 2},
                             {"length", z2, 2},
                             {"heard", z2, 2}};
        CHECK(wave_lp(app, "episode_row_ok", ya, 5) == ZXAPP_BOTH,
              "Fireglow: a place of 100 hours: MYRA TRUE, Eve FALSE: BOTH");
        zxapp_arg_t za[5] = {{"show", z2, 2},
                             {"episode", z2, 2},
                             {"pos", z2, 2},
                             {"length", z2, 2},
                             {"heard", h2, 2}};
        CHECK(wave_lp(app, "episode_row_ok", za, 5) == ZXAPP_BOTH,
              "Fireglow: a negative time heard: MYRA TRUE, Eve FALSE: BOTH");
    }

    /* banker.share (FR2): each show's minutes of the minutes in all, KRON = C++ = C */
    {
        int64_t total = 0;
        for (uint32_t i = 0; i < FIREGLOW_SHOWS; i++) {
            a[i] = i == 0 ? 0 : wrand(0, 100000);
            total += a[i];
        }
        for (uint32_t i = 0; i < FIREGLOW_SHOWS; i++) want[i] = fireglow_listen_share(a[i], total);
        zxapp_arg_t ba[2] = {{"size", a, FIREGLOW_SHOWS}, {"total", &total, 1}};
        CHECK(want[0] == 0 && fireglow_listen_share(total, total) == 10000 &&
                  fireglow_listen_share(1, 0) == 10000 && fireglow_listen_share(0, 0) == 0 &&
                  fireglow_listen_share(2, 1) == FIREGLOW_REFUSED &&
                  fireglow_listen_share(-1, 5) == FIREGLOW_REFUSED &&
                  wave_same(app, "banker.share", ba, 2, want, FIREGLOW_SHOWS, false),
              "Fireglow: the listening share per show through banker.share, KRON = C++ = C");
        static const int64_t sz[2] = {30, 121}, tt[1] = {120};
        zxapp_arg_t xa[2] = {{"size", sz, 2}, {"total", tt, 1}};
        CHECK(wave_lp(app, "banker.share", xa, 2) == ZXAPP_BOTH,
              "Fireglow: more minutes than in all: C++ refuses, KRON answers: BOTH");
    }

    /* halloo.hms (FR6): a time left shown, KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = fireglow_speed_left_floor(wrand(0, FIREGLOW_LEN_MAX), 0,
                                         wrand(FIREGLOW_RATE_MIN, FIREGLOW_RATE_MAX));
        want[i] = fireglow_left_hms(a[i]);
    }
    a[0] = 0, want[0] = 0;
    a[1] = 2 * FIREGLOW_LEN_MAX, want[1] = 1995959; /* the longest at half speed */
    a[2] = 1200000, want[2] = 2000;
    CHECK(fireglow_left_hms(a[1]) == want[1] && fireglow_left_hms(-1) == HALLOO_REFUSED,
          "Fireglow: the longest time left is still inside halloo.hms");
    zxapp_arg_t ha = {"ms", a, WAVE_ROWS};
    CHECK(wave_same(app, "halloo.hms", &ha, 1, want, WAVE_ROWS, false),
          "Fireglow: the time left through halloo.hms, KRON = Sutra = C");
}
