/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sundial's model against zxvd's real dispatcher: the time is sys.info's
 * time_ms, read for real (SM1). The rest is checked at a fixed time set on
 * the model after that read, so every answer below is known: 2026-10-22
 * 06:59:30 UTC, a Thursday (1792652370 s since 1970). */
#include "sundial_model.h"

#define SD_T0 INT64_C(1792652370000) /* 2026-10-22 06:59:30 UTC, ms */

APPS_TEST(sundial)
{
    static sundial_t S;
    sundial_local_t t;
    char text[SUNDIAL_CLOCK_TEXT];
    uint32_t idx = 99, n = 0;
    int64_t wait = -1;

    sundial_init(&S, &LINK);
    CHECK(S.zone == SUNDIAL_ZONE_UTC && S.snooze == SUNDIAL_SNOOZE_START && !S.have,
          "sundial: UTC and a snooze of 10 to begin with");
    CHECK(sundial_local(&S, SUNDIAL_ZONE_UTC, &t) == ZXM_E_STATE, "sundial: no time before a read");
    CHECK(sundial_next_alarm(&S, &idx, &wait) == ZXM_E_STATE &&
              sundial_snooze_now(&S) == ZXM_E_STATE && sundial_timer_start(&S, 5) == ZXM_E_STATE,
          "sundial: nothing timed before a read");

    /* SM1: zxvd's own clock, after this test was written */
    CHECK(sundial_refresh(&S) == ZXM_OK && S.have && S.now_ms >= SD_T0 - INT64_C(31536000000),
          "sundial: sys.info's time_ms read: %s", LINK.err);
    CHECK(sundial_local(&S, SUNDIAL_ZONE_UTC, &t) == ZXM_OK && t.minute >= 0 && t.minute < 1440 &&
              t.weekday >= 1 && t.weekday <= 7 && t.year >= 2025,
          "sundial: the local time now is a real one");

    /* SM2 at a known time */
    S.now_ms = SD_T0;
    CHECK(sundial_local(&S, SUNDIAL_ZONE_UTC, &t) == ZXM_OK && t.minute == 419 && t.weekday == 4 &&
              t.year == 2026 && t.month == 10 && t.day == 22 && t.offset == 0,
          "sundial: 06:59 UTC on Thursday 2026-10-22");
    CHECK(sundial_local(&S, 0, &t) == ZXM_OK && t.offset == -720 && t.minute == 1139 &&
              t.weekday == 3 && t.day == 21,
          "sundial: UTC-12:00 is still Wednesday the 21st, 18:59");
    CHECK(sundial_local(&S, SUNDIAL_ZONES - 1, &t) == ZXM_OK && t.offset == 840 &&
              t.minute == 1259 && t.weekday == 4 && t.day == 22,
          "sundial: UTC+14:00 is 20:59 Thursday");
    CHECK(sundial_local(&S, 23, &t) == ZXM_OK && t.offset == 345 && t.minute == 764,
          "sundial: UTC+05:45 is 12:44");
    CHECK(sundial_local(&S, SUNDIAL_ZONES, &t) == ZXM_E_ARG &&
              sundial_local(&S, -1, &t) == ZXM_E_ARG,
          "sundial: no zone past the table");
    CHECK(sundial_clock_text(&S, 23, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "UTC+05:45 Thursday 12:44"),
          "sundial: clock text '%s'", text);
    CHECK(sundial_clock_text(&S, 3, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "UTC-09:30 Wednesday 21:29"),
          "sundial: clock text '%s'", text);
    CHECK(sundial_clock_text(&S, 3, text, 10) == ZXM_E_ARG, "sundial: clock text needs room");

    /* world clocks */
    CHECK(sundial_world_add(&S, 23) == ZXM_OK && sundial_world_add(&S, 23) == ZXM_E_STATE &&
              sundial_world_add(&S, 38) == ZXM_E_ARG && S.nworld == 1,
          "sundial: a world clock once, from the table");
    for (int64_t z = 0; z < 7; z++)
        CHECK(sundial_world_add(&S, z) == ZXM_OK, "sundial: world %d", (int) z);
    CHECK(sundial_world_add(&S, 30) == ZXM_E_TOOBIG && S.nworld == SUNDIAL_WORLD,
          "sundial: eight world clocks at most");
    CHECK(sundial_world_remove(&S, 0) == ZXM_OK && S.world[0] == 0 && S.nworld == 7 &&
              sundial_world_remove(&S, 7) == ZXM_E_ARG,
          "sundial: a world clock removed by its place");

    /* SM3: alarms, at the owner's zone */
    CHECK(sundial_set_zone(&S, 38) == ZXM_E_ARG && sundial_set_zone(&S, SUNDIAL_ZONE_UTC) == ZXM_OK,
          "sundial: the owner's zone from the table");
    CHECK(sundial_next_alarm(&S, &idx, &wait) == ZXM_E_STATE, "sundial: no alarm, no next");
    CHECK(sundial_alarm_add(&S, 1440, 1, "x", &idx) == ZXM_E_ARG &&
              sundial_alarm_add(&S, 420, 0, "x", &idx) == ZXM_E_ARG &&
              sundial_alarm_add(&S, 420, 128, "x", &idx) == ZXM_E_ARG &&
              sundial_alarm_add(&S, 420, 1, "bad\nlabel", &idx) == ZXM_E_ARG &&
              sundial_alarm_add(&S, 420, 1, "a label of thirty-three bytes ok.", &idx) == ZXM_E_ARG,
          "sundial: an alarm out of its domain is refused");
    CHECK(sundial_alarm_add(&S, 420, 8, "Wake", &idx) == ZXM_OK && idx == 0,
          "sundial: Thursday 07:00");
    CHECK(sundial_alarm_add(&S, 400, 31, "", &idx) == ZXM_OK && idx == 1,
          "sundial: weekdays 06:40");
    CHECK(sundial_next_alarm(&S, &idx, &wait) == ZXM_OK && idx == 0 && wait == 1,
          "sundial: the next alarm in one minute, not the one just gone (%u, %lld)", idx,
          (long long) wait);
    CHECK(sundial_alarm_set_on(&S, 0, false) == ZXM_OK &&
              sundial_next_alarm(&S, &idx, &wait) == ZXM_OK && idx == 1 && wait == 1440 - 19,
          "sundial: off, the weekday alarm tomorrow at 06:40 (%lld)", (long long) wait);
    CHECK(sundial_alarm_set_on(&S, 0, true) == ZXM_OK &&
              sundial_alarm_set_on(&S, 2, true) == ZXM_E_ARG,
          "sundial: on again");

    /* due: nothing at 06:59, the Thursday alarm at 07:00 */
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 0 && !S.notice[0], "sundial: nothing due at 06:59");
    S.now_ms = SD_T0 + 30000;
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 1 && !strcmp(S.notice, "Alarm 07:00 Wake\n"),
          "sundial: due at 07:00: '%s'", S.notice);
    CHECK(sundial_next_alarm(&S, &idx, &wait) == ZXM_OK && idx == 0 && wait == 0,
          "sundial: ringing");

    /* SM4: snooze */
    CHECK(sundial_set_snooze(&S, 0) == ZXM_E_ARG && sundial_set_snooze(&S, 61) == ZXM_E_ARG &&
              sundial_set_snooze(&S, 5) == ZXM_OK && S.snooze == 5,
          "sundial: a snooze of 1..60 minutes");
    CHECK(sundial_snooze_now(&S) == ZXM_OK && S.snoozed_ms == SD_T0 + 30000 + 300000,
          "sundial: snoozed 5");
    S.now_ms = SD_T0 + 30000 + 299999;
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 0, "sundial: a millisecond early, nothing");
    S.now_ms = SD_T0 + 30000 + 300000;
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 1 && !strcmp(S.notice, "Snoozed alarm\n") &&
              !S.snoozed_ms,
          "sundial: the snoozed ring, once: '%s'", S.notice);
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 0, "sundial: told once");

    /* SM5: the timer, never below zero */
    CHECK(sundial_timer_left_ms(&S) == -1 && sundial_timer_hms(&S) == -1 &&
              sundial_timer_stop(&S) == ZXM_E_STATE,
          "sundial: no timer yet");
    CHECK(sundial_timer_start(&S, 0) == ZXM_E_ARG && sundial_timer_start(&S, 6000) == ZXM_E_ARG &&
              sundial_timer_start(&S, -1) == ZXM_E_ARG,
          "sundial: a timer of 1..5999 minutes");
    CHECK(sundial_timer_start(&S, SUNDIAL_TIMER_MAX) == ZXM_OK && sundial_timer_hms(&S) == 995900,
          "sundial: the longest timer shows 99:59:00");
    CHECK(sundial_timer_start(&S, 25) == ZXM_OK && sundial_timer_left_ms(&S) == 1500000 &&
              sundial_timer_hms(&S) == 2500,
          "sundial: 25 minutes, 0:25:00");
    int64_t t0 = S.now_ms;
    S.now_ms = t0 + 61500;
    CHECK(sundial_timer_left_ms(&S) == 1438500 && sundial_timer_hms(&S) == 2358,
          "sundial: 23:58 left after 61.5 s (%lld)", (long long) sundial_timer_hms(&S));
    S.now_ms = t0 + 1500000 + 5000;
    CHECK(sundial_timer_left_ms(&S) == 0 && sundial_timer_hms(&S) == 0,
          "sundial: never below zero");
    CHECK(sundial_due(&S, &n) == ZXM_OK && n == 1 &&
              !strcmp(S.notice, "Timer of 25 minutes done\n") && !S.timer_min &&
              sundial_timer_left_ms(&S) == -1,
          "sundial: the timer done, once: '%s'", S.notice);

    /* SD3: at most 64 alarms */
    for (uint32_t i = S.nalarm; i < SUNDIAL_ALARMS_MAX; i++)
        CHECK(sundial_alarm_add(&S, (int64_t) i, 127, "a", &idx) == ZXM_OK && idx == i,
              "sundial: alarm %u", i);
    CHECK(sundial_alarm_add(&S, 0, 127, "a", &idx) == ZXM_E_TOOBIG &&
              S.nalarm == SUNDIAL_ALARMS_MAX,
          "sundial: the 65th alarm is refused");
    CHECK(sundial_alarm_remove(&S, 64) == ZXM_E_ARG && sundial_alarm_remove(&S, 0) == ZXM_OK &&
              S.nalarm == 63 && S.alarm[0].at == 400,
          "sundial: an alarm removed by its place");

    /* a new read sets the clock again */
    int64_t keep = S.now_ms;
    CHECK(sundial_refresh(&S) == ZXM_OK && S.now_ms != keep,
          "sundial: read again, the time set from zxvd");
}
