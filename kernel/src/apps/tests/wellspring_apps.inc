/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Wellspring's model against zxvd's real dispatcher (LINK): the log is a
 * file in the Banker, read with files.read and written with files.write. */
#include "wellspring_model.h"

static wellspring_t WS, WS2;

static bool ws_file_is(const char *want)
{
    static uint8_t buf[WELLSPRING_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(WELLSPRING_LOG, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

APPS_TEST(wellspring)
{
    /* the rules */
    CHECK(wellspring_band(39, 40, 100) == -1 && wellspring_band(40, 40, 100) == 0 &&
              wellspring_band(100, 40, 100) == 0 && wellspring_band(101, 40, 100) == 1 &&
              wellspring_band(INT64_MIN, INT64_MIN, INT64_MAX) == 0 &&
              wellspring_band(5, 5, 5) == 0 && wellspring_band(0, 1, 0) == WELLSPRING_NO_RANGE,
          "band: edges, and a range upside down");
    CHECK(wellspring_safe(40, 420, 1500) && wellspring_safe(100, 540, 4000) &&
              !wellspring_safe(39, 480, 2000) && !wellspring_safe(60, 541, 2000) &&
              !wellspring_safe(60, 480, 1499) && !wellspring_safe(-60, 480, 2000),
          "safe: the ranges' ends are inside");
    int64_t wk[3] = {30, 0, 45}, top[2] = {INT64_MAX - 5, 5}, over[2] = {INT64_MAX, 1},
            neg[2] = {5, -1};
    CHECK(wellspring_week_total(wk, 3) == 75 && wellspring_week_total(wk, 0) == 0 &&
              wellspring_week_total(top, 2) == INT64_MAX && wellspring_week_total(over, 2) == -1 &&
              wellspring_week_total(neg, 2) == -1 && wellspring_week_total(NULL, 1) == -1,
          "week total: exact, refused past 64 bits or negative");
    CHECK(wellspring_progress_x100(0, 150) == 0 && wellspring_progress_x100(150, 150) == 10000 &&
              wellspring_progress_x100(149, 150) == 9933 &&
              wellspring_progress_x100(225, 150) == 15000 && wellspring_progress_x100(1, 0) == -1 &&
              wellspring_progress_x100(-1, 150) == -1 &&
              wellspring_progress_x100(INT64_MAX / 10000 + 1, 1) == -1,
          "progress x100: floored, refused without a goal");
    CHECK(wellspring_log_ok(0, 1, 0) && wellspring_log_ok(999999, 4, 999999999) &&
              !wellspring_log_ok(1000000, 1, 0) && !wellspring_log_ok(-1, 1, 0) &&
              !wellspring_log_ok(0, 0, 0) && !wellspring_log_ok(0, 5, 0) &&
              !wellspring_log_ok(0, 1, 1000000000) && !wellspring_log_ok(0, 1, -1),
          "log entry: its record's edges");

    /* nothing is written before the log is read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    wellspring_init(&WS, &LINK);
    int before = sent;
    CHECK(wellspring_add(&WS, 20000, WELLSPRING_WATER, 2000) == ZXM_E_STATE && sent == before &&
              !WS.loaded,
          "not loaded: nothing written");
    CHECK(wellspring_add(&WS, 20000, 5, 1) == ZXM_E_ARG &&
              wellspring_set_goal(&WS, 4, 1) == ZXM_E_ARG &&
              wellspring_remove(&WS, 0) == ZXM_E_STATE && sent == before,
          "bad entries are refused before anything is asked");

    /* a log not made yet is empty */
    CHECK(wellspring_load(&WS) == ZXM_OK && WS.loaded && WS.n == 0 &&
              WS.goal[WELLSPRING_ACTIVITY] == WELLSPRING_GOAL_ACTIVITY &&
              WS.goal[WELLSPRING_SLEEP] == WELLSPRING_GOAL_SLEEP &&
              WS.goal[WELLSPRING_WATER] == WELLSPRING_GOAL_WATER,
          "no log yet: %s", LINK.err);

    /* a week of activity, and one day's readings */
    for (int64_t d = 0; d < 7; d++)
        CHECK(wellspring_add(&WS, 20000 + d, WELLSPRING_ACTIVITY, 20 + d) == ZXM_OK, "add %lld: %s",
              (long long) d, LINK.err);
    CHECK(wellspring_add(&WS, 20000, WELLSPRING_PULSE, 120) == ZXM_OK &&
              wellspring_add(&WS, 20000, WELLSPRING_SLEEP, 200) == ZXM_OK &&
              wellspring_add(&WS, 20000, WELLSPRING_SLEEP, 100) == ZXM_OK &&
              wellspring_add(&WS, 20000, WELLSPRING_WATER, 2000) == ZXM_OK && WS.n == 11,
          "readings: %s", LINK.err);
    CHECK(ws_file_is("wellspring 1\ngoal 1 150\ngoal 2 3360\ngoal 3 14000\n20000 1 20\n"
                     "20001 1 21\n20002 1 22\n20003 1 23\n20004 1 24\n20005 1 25\n20006 1 26\n"
                     "20000 4 120\n20000 2 200\n20000 2 100\n20000 3 2000\n"),
          "the log in the Banker (WL1-WL3)");
    int64_t total = 0, x100 = 0;
    CHECK(wellspring_week(&WS, 20000, WELLSPRING_ACTIVITY, &total) == ZXM_OK && total == 161,
          "the week's activity: %lld", (long long) total);
    CHECK(wellspring_week(&WS, 20001, WELLSPRING_ACTIVITY, &total) == ZXM_OK && total == 141,
          "a week from the next day leaves the first out: %lld", (long long) total);
    CHECK(wellspring_progress(&WS, 20000, WELLSPRING_ACTIVITY, &x100) == ZXM_OK && x100 == 10733,
          "goal passed: %lld", (long long) x100);
    CHECK(wellspring_week(&WS, 20000, WELLSPRING_PULSE, &total) == ZXM_E_ARG &&
              wellspring_week(&WS, -1, WELLSPRING_WATER, &total) == ZXM_E_ARG,
          "a heart rate is not summed; no day before 1970");

    wellspring_day_t day;
    CHECK(wellspring_day(&WS, 20000, &day) == ZXM_OK && day.has[WELLSPRING_PULSE] &&
              day.value[WELLSPRING_SLEEP] == 300 && day.band[WELLSPRING_PULSE] == 1 &&
              day.band[WELLSPRING_SLEEP] == -1 && day.band[WELLSPRING_WATER] == 0 &&
              day.outside == 2 && !day.safe,
          "day 20000: heart rate above, sleep below");
    CHECK(strstr(WS.notice, "resting heart rate 120 bpm is above the usual 40-100 range") &&
              strstr(WS.notice, "sleep 300 minutes is below the usual 420-540 range") &&
              strstr(WS.notice, "not a diagnosis") && strstr(WS.notice, "clinician"),
          "advice, never a diagnosis: %s", WS.notice);
    CHECK(wellspring_day(&WS, 20001, &day) == ZXM_OK && !day.outside && !day.safe &&
              strstr(WS.notice, "no heart rate, sleep or water logged"),
          "a day with activity only: %s", WS.notice);
    CHECK(wellspring_add(&WS, 20001, WELLSPRING_PULSE, 62) == ZXM_OK &&
              wellspring_add(&WS, 20001, WELLSPRING_SLEEP, 480) == ZXM_OK &&
              wellspring_add(&WS, 20001, WELLSPRING_WATER, 1500) == ZXM_OK &&
              wellspring_day(&WS, 20001, &day) == ZXM_OK && day.safe && !day.outside &&
              strstr(WS.notice, "inside its usual range"),
          "a day inside every range: %s", WS.notice);

    /* goals and removal are kept in the file; a second model reads it all */
    CHECK(wellspring_set_goal(&WS, WELLSPRING_ACTIVITY, 300) == ZXM_OK &&
              wellspring_set_goal(&WS, WELLSPRING_WATER, 0) == ZXM_OK &&
              wellspring_progress(&WS, 20000, WELLSPRING_ACTIVITY, &x100) == ZXM_OK &&
              x100 == 5366 &&
              wellspring_progress(&WS, 20000, WELLSPRING_WATER, &x100) == ZXM_E_STATE,
          "goals: %lld", (long long) x100);
    CHECK(wellspring_remove(&WS, 14) == ZXM_E_ARG && wellspring_remove(&WS, 7) == ZXM_OK &&
              WS.n == 13 && WS.e[7].kind == WELLSPRING_SLEEP,
          "remove the first heart rate");
    wellspring_init(&WS2, &LINK);
    CHECK(wellspring_load(&WS2) == ZXM_OK && WS2.n == WS.n &&
              !memcmp(WS2.e, WS.e, WS.n * sizeof WS.e[0]) && WS2.goal[WELLSPRING_ACTIVITY] == 300 &&
              WS2.goal[WELLSPRING_SLEEP] == WELLSPRING_GOAL_SLEEP && !WS2.goal[WELLSPRING_WATER],
          "read back: %s", LINK.err);

    /* a long log goes in parts (begin, add, end); a full one refuses more */
    uint32_t n = WS.n;
    bool ok = true;
    while (ok && WS.n < WELLSPRING_ENTRIES)
        ok = wellspring_add(&WS, 999999, WELLSPRING_WATER, 999999999) == ZXM_OK;
    CHECK(ok && WS.n == WELLSPRING_ENTRIES && n < WS.n, "full: %u %s", WS.n, LINK.err);
    before = sent;
    CHECK(wellspring_add(&WS, 1, 1, 1) == ZXM_E_TOOBIG && sent == before, "no room");
    wellspring_init(&WS2, &LINK);
    CHECK(wellspring_load(&WS2) == ZXM_OK && WS2.n == WELLSPRING_ENTRIES &&
              !memcmp(WS2.e, WS.e, WS.n * sizeof WS.e[0]),
          "a full log read back: %s", LINK.err);
    CHECK(wellspring_week(&WS, 999999, WELLSPRING_WATER, &total) == ZXM_OK &&
              total == (int64_t) (WELLSPRING_ENTRIES - n) * 999999999,
          "the largest week: %lld", (long long) total);

    /* a file that is not a log is never overwritten (WL4) */
    uint64_t size;
    CHECK(zxf_write(WELLSPRING_LOG, (const uint8_t *) "wellspring 1\n12 1 07\n", 21, ZXF_W_REPLACE,
                    false, &size) == 0,
          "a leading zero");
    CHECK(wellspring_load(&WS2) == ZXM_E_PROTOCOL && !WS2.loaded &&
              wellspring_add(&WS2, 1, 1, 1) == ZXM_E_STATE && ws_file_is("wellspring 1\n12 1 07\n"),
          "not a log: refused, left as it was");
    static const char *const bad[] = {"notes\n",
                                      "wellspring 1\n1 1 1",
                                      "wellspring 1\n1 9 1\n",
                                      "wellspring 1\n1 1 -1\n",
                                      "wellspring 1\n1 1 1\ngoal 1 5\n",
                                      "wellspring 1\ngoal 4 5\n",
                                      "wellspring 1\ngoal 1 5\ngoal 1 6\n",
                                      "wellspring 1\n1000000 1 1\n"};
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(zxf_write(WELLSPRING_LOG, (const uint8_t *) bad[i], (uint32_t) strlen(bad[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  wellspring_load(&WS2) == ZXM_E_PROTOCOL,
              "not a log: %u", i);
    CHECK(zxf_write(WELLSPRING_LOG, (const uint8_t *) "wellspring 1\n", 13, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              wellspring_load(&WS2) == ZXM_OK && WS2.loaded && WS2.n == 0 &&
              !WS2.goal[WELLSPRING_ACTIVITY],
          "a log with no goals and no entries");
}
