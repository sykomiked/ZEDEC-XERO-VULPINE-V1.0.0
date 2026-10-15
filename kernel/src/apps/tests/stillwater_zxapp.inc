/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Stillwater's languages against the C its model runs (stillwater_rules.h). */
#include "stillwater_rules.h"

APP_TEST(stillwater, 6, 144)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* quiet_now (SW1): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = i % 4 == 1 ? a[i] : wrand(0, 1439);             /* some windows are the whole day */
        c[i] = i % 3 ? wrand(0, 1439) : (i % 2 ? a[i] : b[i]); /* some minutes on an edge */
    }
    a[0] = 1320, b[0] = 420, c[0] = 1320; /* 22:00 to 07:00, at its start */
    a[1] = 1320, b[1] = 420, c[1] = 420;  /* its end is not inside */
    a[2] = 1320, b[2] = 420, c[2] = 419;  /* past midnight, inside */
    a[3] = 1320, b[3] = 420, c[3] = 1439; /* the day's last minute */
    a[4] = 1320, b[4] = 420, c[4] = 0;    /* the day's first minute */
    a[5] = 1320, b[5] = 420, c[5] = 1319; /* a minute before it */
    a[6] = 540, b[6] = 1020, c[6] = 1020; /* 09:00 to 17:00, its end */
    a[7] = 540, b[7] = 1020, c[7] = 539;
    a[8] = 0, b[8] = 0, c[8] = 1439; /* equal: the whole day */
    a[9] = 1439, b[9] = 1439, c[9] = 0;
    a[10] = 0, b[10] = 1439, c[10] = 1439; /* the widest window that is not the day */
    a[11] = 1439, b[11] = 0, c[11] = 1439; /* one minute, across midnight */
    a[12] = 1439, b[12] = 0, c[12] = 0;
    a[13] = 0, b[13] = 600, c[13] = 1440;   /* max + 1: a minute outside the day */
    a[14] = 1440, b[14] = 600, c[14] = 700; /* a window that does not start in the day */
    a[15] = 100, b[15] = 1440, c[15] = 100;
    a[16] = 9999, b[16] = 9999, c[16] = 9999; /* the largest Sutra holds */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(stillwater_window(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[8] == ZXAPP_TRUE && want[11] == ZXAPP_TRUE && want[12] == ZXAPP_FALSE &&
              want[13] == ZXAPP_FALSE && want[14] == ZXAPP_FALSE,
          "Stillwater: the C rule's own edges (SW1)");
    zxapp_arg_t qa[3] = {{"opens", a, WAVE_ROWS}, {"closes", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "quiet_now", qa, 3, want, WAVE_ROWS, true),
          "Stillwater: in a quiet window now, Sutra = KRON = C");
    {
        static const int64_t qf[2] = {1320, 1320}, qt[2] = {420, 420}, qn[2] = {0, -1};
        zxapp_arg_t qb[3] = {{"opens", qf, 2}, {"closes", qt, 2}, {"now", qn, 2}};
        CHECK(wave_lp(app, "quiet_now", qb, 3) == ZXAPP_BOTH,
              "Stillwater: a negative minute: Sutra refuses, KRON answers: BOTH, held");
    }

    /* let_through (SW2): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = i % 5 ? wrand(-1, 400) : -1;
        d[i] = i % 7 == 3 ? 1 : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = -1, d[0] = 1;  /* everything else says hold: the emergency goes */
    a[1] = 1, b[1] = 0, c[1] = -1, d[1] = 0;  /* everything says hold */
    a[2] = 1, b[2] = 0, c[2] = 180, d[2] = 0; /* the same caller at 3 minutes */
    a[3] = 1, b[3] = 0, c[3] = 181, d[3] = 0; /* a second past */
    a[4] = 1, b[4] = 0, c[4] = 0, d[4] = 0;   /* twice at once */
    a[5] = 1, b[5] = 1, c[5] = -1, d[5] = 0;  /* allowed */
    a[6] = 0, b[6] = 0, c[6] = -1, d[6] = 0;  /* not quiet */
    a[7] = 9, b[7] = 0, c[7] = 999999999, d[7] = 9; /* the largest Sutra holds */
    a[8] = 9, b[8] = 0, c[8] = -999999999, d[8] = 0;
    a[9] = 9, b[9] = 9, c[9] = 999999999, d[9] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(stillwater_through(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_TRUE && want[7] == ZXAPP_TRUE &&
              want[8] == ZXAPP_FALSE,
          "Stillwater: the C rule's own edges (SW2)");
    zxapp_arg_t la[4] = {{"quiet", a, WAVE_ROWS},
                         {"allowed", b, WAVE_ROWS},
                         {"gap", c, WAVE_ROWS},
                         {"emergency", d, WAVE_ROWS}};
    CHECK(wave_same(app, "let_through", la, 4, want, WAVE_ROWS, true),
          "Stillwater: let this notice through, Sutra = KRON = C");
    {
        static const int64_t lq[2] = {1, 1}, lal[2] = {0, 0}, lg[2] = {-1, 1000000000},
                             le[2] = {0, 0};
        zxapp_arg_t lb[4] = {
            {"quiet", lq, 2}, {"allowed", lal, 2}, {"gap", lg, 2}, {"emergency", le, 2}};
        CHECK(wave_lp(app, "let_through", lb, 4) == ZXAPP_BOTH,
              "Stillwater: a gap past s9(9): Sutra refuses, KRON answers: BOTH, held");
    }

    /* schedule_ok (SW3): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, 3);
        b[i] = wrand(-1, 128);
        c[i] = i % 2 ? wrand(0, 1439) : wrand(-1, 1440);
        d[i] = i % 3 ? wrand(0, 1439) : wrand(-1, 1440);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;
    a[1] = 2, b[1] = 127, c[1] = 1439, d[1] = 1439;
    a[2] = 3, b[2] = 127, c[2] = 1439, d[2] = 1439;
    a[3] = 2, b[3] = 128, c[3] = 1439, d[3] = 1439;
    a[4] = 2, b[4] = 127, c[4] = 1440, d[4] = 1439;
    a[5] = 2, b[5] = 127, c[5] = 1439, d[5] = 1440;
    a[6] = -1, b[6] = 0, c[6] = 0, d[6] = 0;
    a[7] = 1, b[7] = -1, c[7] = 0, d[7] = 0;
    a[8] = 1, b[8] = 31, c[8] = -1, d[8] = 420;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(stillwater_schedule_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE,
          "Stillwater: the C rule's own edges (SW3)");
    zxapp_arg_t sa[4] = {{"mode", a, WAVE_ROWS},
                         {"days", b, WAVE_ROWS},
                         {"opens", c, WAVE_ROWS},
                         {"closes", d, WAVE_ROWS}};
    CHECK(wave_same(app, "schedule_ok", sa, 4, want, WAVE_ROWS, true),
          "Stillwater: what a schedule may hold, Eve = C");

    /* record_ok (SW4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-1000, 1000);
        d[i] = wrand(-10000, 10000);
        e[i] = wrand(-10000, 10000);
        f[i] = wrand(-1000, 1000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;
    a[1] = 999, b[1] = 9, c[1] = 999, d[1] = 9999, e[1] = 9999, f[1] = 999;
    a[2] = 1000, b[2] = 9, c[2] = 999, d[2] = 9999, e[2] = 9999, f[2] = 999;
    a[3] = 999, b[3] = 10, c[3] = 999, d[3] = 9999, e[3] = 9999, f[3] = 999;
    a[4] = 999, b[4] = 9, c[4] = 999, d[4] = 10000, e[4] = 9999, f[4] = 999;
    a[5] = 999, b[5] = 9, c[5] = 999, d[5] = 9999, e[5] = 9999, f[5] = 1000;
    a[6] = -999, b[6] = -9, c[6] = -999, d[6] = -9999, e[6] = -9999, f[6] = -999;
    a[7] = 1, b[7] = 1, c[7] = 31, d[7] = 1320, e[7] = 420, f[7] = 2;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(stillwater_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[6] == ZXAPP_TRUE,
          "Stillwater: the C rule's own edges (SW4)");
    zxapp_arg_t ra[6] = {{"schedule", a, WAVE_ROWS}, {"mode", b, WAVE_ROWS},
                         {"days", c, WAVE_ROWS},     {"opens", d, WAVE_ROWS},
                         {"closes", e, WAVE_ROWS},   {"allowed", f, WAVE_ROWS}};
    CHECK(wave_same(app, "record_ok", ra, 6, want, WAVE_ROWS, true),
          "Stillwater: a schedule makes a ^ZXQUIET record, MYRA = C");
}
