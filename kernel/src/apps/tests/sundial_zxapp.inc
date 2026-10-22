/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sundial's languages against the C its model runs (sundial_rules.h).
 * Slots: @num.sundial_zone_minute, @num.sundial_alarm_wait,
 * @logic.sundial_clock_limits_ok, @num.sundial_clock_limits_fault,
 * @num.sundial_zone_offset, and the parts it uses, @num.solstice_weekday
 * and @num.halloo_hms. */
#include "halloo_rules.h"
#include "solstice_rules.h"
#include "sundial_rules.h"

APP_TEST(sundial, 29, 110)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* zone_minute: utc + offset wrapped into the day, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(-720, 840);
    }
    a[0] = 0, b[0] = 0;       /* midnight in UTC */
    a[1] = 0, b[1] = -1;      /* back past midnight */
    a[2] = 1439, b[2] = 1;    /* forward past midnight */
    a[3] = 1439, b[3] = -720; /* the last minute, the least offset */
    a[4] = 600, b[4] = 840;   /* exactly a day: 0 */
    a[5] = 0, b[5] = 840;     /* the greatest offset */
    a[6] = 0, b[6] = -720;    /* the least, from midnight */
    a[7] = 1439, b[7] = 840;  /* the most the sum can be */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = sundial_zone_minute(a[i], b[i]);
    zxapp_arg_t za[2] = {{"utc", a, WAVE_ROWS}, {"offset", b, WAVE_ROWS}};
    CHECK(wave_same(app, "zone_minute", za, 2, want, WAVE_ROWS, false),
          "Sundial: zone_minute, Trebago = KRON = C");
    {
        /* one minute past the day, and an offset past UTC+14:00: Trebago
         * refuses, KRON wraps the sum anyway */
        static const int64_t u[2] = {0, 1440}, o[2] = {0, 0}, u2[2] = {0, 0}, o2[2] = {0, 841},
                             u3[2] = {0, -1};
        zxapp_arg_t x1[2] = {{"utc", u, 2}, {"offset", o, 2}};
        zxapp_arg_t x2[2] = {{"utc", u2, 2}, {"offset", o2, 2}};
        zxapp_arg_t x3[2] = {{"utc", u3, 2}, {"offset", o, 2}};
        CHECK(wave_lp(app, "zone_minute", x1, 2) == ZXAPP_BOTH,
              "Sundial: utc 1440: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "zone_minute", x2, 2) == ZXAPP_BOTH,
              "Sundial: offset 841: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "zone_minute", x3, 2) == ZXAPP_BOTH,
              "Sundial: utc -1: Trebago refuses, KRON answers: BOTH");
    }

    /* alarm_wait: minutes to the next ring, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(1, 7);
        c[i] = wrand(1, 127);
        d[i] = wrand(0, 1439);
    }
    a[0] = 420, b[0] = 1, c[0] = 1, d[0] = 420;     /* rings this minute: 0 */
    a[1] = 420, b[1] = 1, c[1] = 1, d[1] = 419;     /* just gone: next week, the most */
    a[2] = 420, b[2] = 1, c[2] = 2, d[2] = 420;     /* tomorrow */
    a[3] = 600, b[3] = 7, c[3] = 1, d[3] = 420;     /* Sunday to Monday */
    a[4] = 1439, b[4] = 3, c[4] = 127, d[4] = 0;    /* every day, one minute on */
    a[5] = 0, b[5] = 7, c[5] = 64, d[5] = 0;        /* Sunday's only bit */
    a[6] = 0, b[6] = 1, c[6] = 64, d[6] = 1439;     /* the last minute of Sunday */
    a[7] = 419, b[7] = 5, c[7] = 96, d[7] = 420;    /* the weekend from Friday */
    a[8] = 1439, b[8] = 7, c[8] = 127, d[8] = 1439; /* the last minute of the week */
    a[9] = 0, b[9] = 4, c[9] = 8, d[9] = 0;         /* Thursday at midnight */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = sundial_alarm_wait(a[i], b[i], c[i], d[i]);
    zxapp_arg_t wa[4] = {{"now", a, WAVE_ROWS},
                         {"today", b, WAVE_ROWS},
                         {"mask", c, WAVE_ROWS},
                         {"at", d, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[1] == SUNDIAL_WEEK_MIN - 1 && want[2] == 1440 && want[3] == 1260 &&
              want[4] == 1 && want[5] == 0 && want[6] == 10079 && want[7] == 1441,
          "Sundial: the C's own waits at the edges");
    CHECK(wave_same(app, "alarm_wait", wa, 4, want, WAVE_ROWS, false),
          "Sundial: alarm_wait, Trebago = KRON = C");
    {
        /* a mask of no day, a mask past the week and a day past Sunday:
         * Trebago refuses each, KRON answers */
        static const int64_t n[2] = {420, 420}, t[2] = {1, 1}, at[2] = {420, 420};
        static const int64_t m0[2] = {1, 0}, m1[2] = {1, 128}, t8[2] = {1, 8}, m[2] = {1, 1};
        zxapp_arg_t x1[4] = {{"now", n, 2}, {"today", t, 2}, {"mask", m0, 2}, {"at", at, 2}};
        zxapp_arg_t x2[4] = {{"now", n, 2}, {"today", t, 2}, {"mask", m1, 2}, {"at", at, 2}};
        zxapp_arg_t x3[4] = {{"now", n, 2}, {"today", t8, 2}, {"mask", m, 2}, {"at", at, 2}};
        CHECK(wave_lp(app, "alarm_wait", x1, 4) == ZXAPP_BOTH,
              "Sundial: mask 0: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "alarm_wait", x2, 4) == ZXAPP_BOTH,
              "Sundial: mask 128: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "alarm_wait", x3, 4) == ZXAPP_BOTH,
              "Sundial: day 8: Trebago refuses, KRON answers: BOTH");
    }

    /* clock_limits_ok: inside the limits, Eve = Sutra = C (every value fits
     * Sutra's pictures: alarms and snooze 9(2), timer 9(4)) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 99);
        b[i] = wrand(0, 99);
        c[i] = wrand(0, 9999);
    }
    for (uint32_t i = 32; i < WAVE_ROWS; i++) { /* half of them near the inside */
        a[i] = wrand(0, 70);
        b[i] = wrand(0, 62);
        c[i] = wrand(5900, 6100);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;      /* every least */
    a[1] = 64, b[1] = 60, c[1] = 5999; /* every max */
    a[2] = 65, b[2] = 10, c[2] = 25;   /* alarms max + 1 */
    a[3] = 1, b[3] = 0, c[3] = 25;     /* no snooze */
    a[4] = 1, b[4] = 61, c[4] = 25;    /* snooze max + 1 */
    a[5] = 1, b[5] = 10, c[5] = 6000;  /* timer max + 1 */
    a[6] = 99, b[6] = 99, c[6] = 9999; /* every picture full */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(sundial_limits_ok(a[i], b[i], c[i]));
    zxapp_arg_t la[3] = {
        {"alarms", a, WAVE_ROWS}, {"snooze", b, WAVE_ROWS}, {"timer", c, WAVE_ROWS}};
    CHECK(wave_same(app, "clock_limits_ok", la, 3, want, WAVE_ROWS, true),
          "Sundial: clock_limits_ok, Eve = Sutra = C");
    {
        /* a timer below zero, and counts past Sutra's pictures: Sutra
         * refuses, Eve answers no */
        static const int64_t al[2] = {1, 1}, sn[2] = {1, 1}, tm[2] = {0, -1};
        static const int64_t al2[2] = {1, 100}, tm2[2] = {0, 0}, sn3[2] = {1, 100};
        static const int64_t tm4[2] = {0, 10000};
        zxapp_arg_t x1[3] = {{"alarms", al, 2}, {"snooze", sn, 2}, {"timer", tm, 2}};
        zxapp_arg_t x2[3] = {{"alarms", al2, 2}, {"snooze", sn, 2}, {"timer", tm2, 2}};
        zxapp_arg_t x3[3] = {{"alarms", al, 2}, {"snooze", sn3, 2}, {"timer", tm2, 2}};
        zxapp_arg_t x4[3] = {{"alarms", al, 2}, {"snooze", sn, 2}, {"timer", tm4, 2}};
        CHECK(wave_lp(app, "clock_limits_ok", x1, 3) == ZXAPP_BOTH,
              "Sundial: a timer below zero: Sutra refuses, Eve says no: BOTH");
        CHECK(wave_lp(app, "clock_limits_ok", x2, 3) == ZXAPP_BOTH,
              "Sundial: 100 alarms: Sutra refuses, Eve says no: BOTH");
        CHECK(wave_lp(app, "clock_limits_ok", x3, 3) == ZXAPP_BOTH,
              "Sundial: a snooze of 100: Sutra refuses, Eve says no: BOTH");
        CHECK(wave_lp(app, "clock_limits_ok", x4, 3) == ZXAPP_BOTH,
              "Sundial: a timer of 10000: Sutra refuses, Eve says no: BOTH");
    }

    /* clock_limits_fault: the first limit passed, Rust (wasm) = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 80);
        b[i] = wrand(0, 70);
        c[i] = wrand(5000, 7000);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;                 /* every least: 0 */
    a[1] = 64, b[1] = 60, c[1] = 5999;            /* every max: 0 */
    a[2] = 65, b[2] = 1, c[2] = 0;                /* 1 */
    a[3] = 1, b[3] = 0, c[3] = 0;                 /* 2 */
    a[4] = 1, b[4] = 61, c[4] = 0;                /* 2 */
    a[5] = 1, b[5] = 1, c[5] = 6000;              /* 3 */
    a[6] = 65, b[6] = 0, c[6] = 6000;             /* all three: the alarms first */
    a[7] = 999999999, b[7] = 999999999, c[7] = 0; /* far past */
    a[8] = 0, b[8] = 60, c[8] = INT64_MAX;        /* the greatest timer */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = sundial_limits_fault(a[i], b[i], c[i]);
    CHECK(want[2] == 1 && want[3] == 2 && want[5] == 3 && want[6] == 1 && want[8] == 3,
          "Sundial: the C's own faults at the edges");
    zxapp_arg_t fa[3] = {
        {"alarms", a, WAVE_ROWS}, {"snooze", b, WAVE_ROWS}, {"timer", c, WAVE_ROWS}};
    CHECK(wave_same(app, "clock_limits_fault", fa, 3, want, WAVE_ROWS, false),
          "Sundial: clock_limits_fault, Rust = KRON = C");
    {
        /* a count below zero: Rust traps (refused), KRON answers */
        static const int64_t al[2] = {1, 1}, sn[2] = {1, 1}, tm[2] = {0, -1}, al2[2] = {1, -1},
                             tm2[2] = {0, 0};
        zxapp_arg_t x1[3] = {{"alarms", al, 2}, {"snooze", sn, 2}, {"timer", tm, 2}};
        zxapp_arg_t x2[3] = {{"alarms", al2, 2}, {"snooze", sn, 2}, {"timer", tm2, 2}};
        CHECK(wave_lp(app, "clock_limits_fault", x1, 3) == ZXAPP_BOTH,
              "Sundial: a timer below zero: Rust refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "clock_limits_fault", x2, 3) == ZXAPP_BOTH,
              "Sundial: alarms below zero: Rust refuses, KRON answers: BOTH");
    }

    /* zone_offset: the table by index, KRON alone (NEITHER) = C */
    for (uint32_t i = 0; i < SUNDIAL_ZONES; i++) {
        a[i] = (int64_t) i;
        want[i] = sundial_zone_offset(a[i]);
    }
    CHECK(want[0] == SUNDIAL_OFFSET_MIN && want[SUNDIAL_ZONE_UTC] == 0 &&
              want[SUNDIAL_ZONES - 1] == SUNDIAL_OFFSET_MAX &&
              sundial_zone_offset(SUNDIAL_ZONES) == SUNDIAL_REFUSED &&
              sundial_zone_offset(-1) == SUNDIAL_REFUSED,
          "Sundial: the C's table ends at UTC-12:00 and UTC+14:00, UTC at 14");
    for (uint32_t i = 1; i < SUNDIAL_ZONES; i++)
        CHECK(want[i] > want[i - 1], "Sundial: zone offsets ascend at %u", i);
    zxapp_arg_t oa = {"zone", a, SUNDIAL_ZONES};
    CHECK(wave_same(app, "zone_offset", &oa, 1, want, SUNDIAL_ZONES, false),
          "Sundial: zone_offset, KRON = C");
    {
        /* an index past the table, or below it: refused */
        static const int64_t past[2] = {0, SUNDIAL_ZONES}, below[2] = {0, -1};
        zxapp_arg_t p1 = {"zone", past, 2}, p2 = {"zone", below, 2};
        zxapp_num_t out[2];
        uint32_t n = 0;
        CHECK(zxapp_call(app, &H, "zone_offset", &p1, 1, out, 2, &n) != ZXAPP_OK,
              "Sundial: zone 38 refused");
        CHECK(zxapp_call(app, &H, "zone_offset", &p2, 1, out, 2, &n) != ZXAPP_OK,
              "Sundial: zone -1 refused");
    }

    /* the parts it uses: a timer's time left through halloo.hms, today's
     * weekday through solstice.weekday */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SUNDIAL_TIMER_MAX) * 60000 - wrand(0, 59999);
        if (a[i] < 0) a[i] = 0;
        want[i] = halloo_hms(a[i]);
    }
    a[0] = 0, want[0] = 0;
    a[1] = (int64_t) SUNDIAL_TIMER_MAX * 60000, want[1] = 995900; /* 99:59:00 */
    zxapp_arg_t ha = {"ms", a, WAVE_ROWS};
    CHECK(wave_same(app, "halloo.hms", &ha, 1, want, WAVE_ROWS, false),
          "Sundial: a timer's time left through halloo.hms, KRON = Sutra = C");
    {
        int64_t y[1] = {2026}, m[1] = {10}, dd[1] = {22}, w[1] = {solstice_weekday(2026, 10, 22)};
        zxapp_arg_t da[3] = {{"year", y, 1}, {"month", m, 1}, {"day", dd, 1}};
        CHECK(w[0] == 4 && wave_same(app, "solstice.weekday", da, 3, w, 1, false),
              "Sundial: 2026-10-22 is a Thursday through solstice.weekday");
    }
}
