/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Conch's languages against the C its model runs (conch_rules.h):
 * @logic.conch_hearing_level_ok, @logic.conch_hearing_balance_ok and
 * @logic.conch_sound_alert_map_ok. */
#include "conch_rules.h"

/* a value inside lo..hi, or one past either end */
static int64_t cn_edge(int64_t lo, int64_t hi, uint32_t i)
{
    switch (i % 6) {
    case 0:
        return lo;
    case 1:
        return hi;
    case 2:
        return lo - 1;
    case 3:
        return hi + 1;
    default:
        return wrand(lo, hi);
    }
}

APP_TEST(conch, 5, 141)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* hearing_level_ok (CN1): Eve = KRON = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(CONCH_GAIN_MIN, CONCH_GAIN_MAX)
                     : cn_edge(CONCH_GAIN_MIN, CONCH_GAIN_MAX, i);
        c[i] = i % 4 ? wrand(0, CONCH_SPL_MAX) : cn_edge(0, CONCH_SPL_MAX, i);
        b[i] = i % 5 ? wrand(0, c[i] >= 0 && c[i] <= CONCH_SPL_MAX ? c[i] : CONCH_SPL_MAX)
                     : cn_edge(0, CONCH_SPL_MAX, i);
        if (b[i] > c[i] && a[i] >= CONCH_GAIN_MIN && a[i] <= CONCH_GAIN_MAX && b[i] >= 0 &&
            b[i] <= CONCH_SPL_MAX && c[i] >= 0 && c[i] <= CONCH_SPL_MAX)
            b[i] = c[i]; /* past the cap is held, below */
    }
    a[0] = 0, b[0] = 650, c[0] = 900;                         /* speech at the start */
    a[1] = CONCH_GAIN_MAX, b[1] = 950, c[1] = 950;            /* max gain, at the cap */
    a[2] = CONCH_GAIN_MAX + 1, b[2] = 951, c[2] = 1000;       /* max + 1 */
    a[3] = CONCH_GAIN_MIN, b[3] = 350, c[3] = 900;            /* the deepest cut */
    a[4] = CONCH_GAIN_MIN - 1, b[4] = 349, c[4] = 900;        /* past it */
    a[5] = 0, b[5] = 0, c[5] = 0;                             /* silence under a cap of 0 */
    a[6] = 0, b[6] = CONCH_SPL_MAX, c[6] = CONCH_SPL_MAX;     /* the loudest there is */
    a[7] = 0, b[7] = CONCH_SPL_MAX + 1, c[7] = CONCH_SPL_MAX; /* past it */
    a[8] = 0, b[8] = -1, c[8] = 900;                          /* negative level */
    a[9] = 0, b[9] = 0, c[9] = -1;                            /* negative cap */
    a[10] = 0, b[10] = 0, c[10] = CONCH_SPL_MAX + 1;          /* cap past 120 dB SPL */
    a[11] = -1000000000000, b[11] = 5, c[11] = 1000000000000; /* far out */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(conch_level_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE && want[10] == ZXAPP_FALSE && want[11] == ZXAPP_FALSE &&
              !conch_level_ok(0, 901, 900),
          "Conch: the C level rule's own edges (CN1)");
    zxapp_arg_t la[3] = {{"gain", a, WAVE_ROWS}, {"total", b, WAVE_ROWS}, {"cap", c, WAVE_ROWS}};
    CHECK(wave_same(app, "hearing_level_ok", la, 3, want, WAVE_ROWS, true),
          "Conch: hearing_level_ok, Eve = KRON = C");
    {
        /* a level past the cap: Eve holds each range alone (TRUE), KRON relates them */
        static const int64_t g2[2] = {0, 100}, t2[2] = {650, 951}, c2[2] = {900, 950};
        zxapp_arg_t xa[3] = {{"gain", g2, 2}, {"total", t2, 2}, {"cap", c2, 2}};
        CHECK(wave_lp(app, "hearing_level_ok", xa, 3) == ZXAPP_BOTH,
              "Conch: a level past the owner's cap: Eve TRUE, KRON FALSE: BOTH, held");
    }

    /* hearing_balance_ok (CN2): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(-CONCH_BALANCE_MAX, CONCH_BALANCE_MAX)
                     : cn_edge(-CONCH_BALANCE_MAX, CONCH_BALANCE_MAX, i / 2);
        b[i] = i % 3 ? wrand(0, 1) : cn_edge(0, 1, i / 3);
    }
    a[0] = -CONCH_BALANCE_MAX, b[0] = 0;
    a[1] = CONCH_BALANCE_MAX, b[1] = 1;
    a[2] = CONCH_BALANCE_MAX + 1, b[2] = 0;
    a[3] = -CONCH_BALANCE_MAX - 1, b[3] = 1;
    a[4] = 0, b[4] = 2;
    a[5] = 0, b[5] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(conch_balance_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Conch: the C balance rule's own edges (CN2)");
    zxapp_arg_t ba[2] = {{"balance", a, WAVE_ROWS}, {"mono", b, WAVE_ROWS}};
    CHECK(wave_same(app, "hearing_balance_ok", ba, 2, want, WAVE_ROWS, true),
          "Conch: hearing_balance_ok, Eve = C");
    CHECK(conch_ear_pct(0, CONCH_LEFT) == 100 && conch_ear_pct(0, CONCH_RIGHT) == 100 &&
              conch_ear_pct(30, CONCH_LEFT) == 70 && conch_ear_pct(30, CONCH_RIGHT) == 100 &&
              conch_ear_pct(-100, CONCH_LEFT) == 100 && conch_ear_pct(-100, CONCH_RIGHT) == 0 &&
              conch_ear_pct(-40, CONCH_BOTH_EARS) == 60 && conch_ear_pct(101, CONCH_LEFT) == -1 &&
              conch_ear_pct(0, CONCH_SIDES) == -1 && conch_ear_pct(0, -1) == -1,
          "Conch: what each ear plays at a balance (CN4)");

    /* sound_alert_map_ok (CN3): MYRA = Eve = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        bool fits = i % 3 != 0;
        a[i] = fits ? wrand(1, CONCH_SOUNDS) : wrand(-30, 30);
        b[i] = fits ? wrand(0, CONCH_PULSES_MAX) : wrand(-30, 30);
        c[i] = fits ? wrand(0, CONCH_PULSES_MAX) : wrand(-30, 30);
        if (!fits && conch_alert_digits_ok(a[i], b[i], c[i]))
            b[i] = i % 2 ? 10 : -10; /* inside MYRA's digits but outside Eve's ranges is held */
    }
    a[0] = 1, b[0] = 0, c[0] = 0;                 /* the least */
    a[1] = CONCH_SOUNDS, b[1] = 3, c[1] = 3;      /* max */
    a[2] = 10, b[2] = 0, c[2] = 0;                /* a sound past its digit */
    a[3] = 1, b[3] = 10, c[3] = 0;                /* a flash past its digit */
    a[4] = 1, b[4] = 0, c[4] = -10;               /* a negative past its digit */
    a[5] = CONCH_SOUND_ALARM, b[5] = 3, c[5] = 0; /* an alarm flashes, no vibration */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(conch_alert_map_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              !conch_alert_map_ok(1, 4, 0) && conch_alert_digits_ok(1, 4, 0) &&
              CONCH_PULSES_MAX * CONCH_PULSE_MS - CONCH_PULSE_MS < 1000,
          "Conch: the C alert rule's own edges, three flashes inside a second (CN3)");
    zxapp_arg_t ma[3] = {
        {"sound", a, WAVE_ROWS}, {"flash", b, WAVE_ROWS}, {"vibrate", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sound_alert_map_ok", ma, 3, want, WAVE_ROWS, true),
          "Conch: sound_alert_map_ok, MYRA = Eve = C");
    {
        /* four flashes: one digit in MYRA, past Eve's three a second: held */
        static const int64_t s2[2] = {1, 1}, f2[2] = {1, 4}, v2[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"sound", s2, 2}, {"flash", f2, 2}, {"vibrate", v2, 2}};
        CHECK(wave_lp(app, "sound_alert_map_ok", xa, 3) == ZXAPP_BOTH,
              "Conch: four flashes: MYRA TRUE, Eve FALSE: BOTH, held");
    }
}
