/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Damper's languages, witnessed, row by row against damper_rules.h, the C
 * the model runs; the parts it composes (wake, brazier, homestead,
 * watchfire, conch) resolved and called against their own rules; and the
 * BOTH test: sensor 5 reports smoke 1 and smoke 0 in the same second, the
 * reading is BOTH and the alarm is still raised.
 * Slots: @logic.damper_fire_alarm_quiet, @num.damper_heat_rise_excess,
 * @logic.damper_heat_sensor_ok, @num.damper_room_alarm_sensors,
 * @logic.damper_building_room_ok; and, used, @num.wake_count,
 * @logic.brazier_heat_ok, @logic.brazier_heat_safe,
 * @num.brazier_minutes_left, @logic.homestead_home_device_ok,
 * @num.homestead_device_energy, @logic.watchfire_place_share_ok,
 * @num.watchfire_cancel_left, @logic.watchfire_sos_ring_ok,
 * @logic.watchfire_sos_contact_ok, @logic.conch_sound_alert_map_ok,
 * @logic.conch_hearing_level_ok. */
#include "conch_rules.h"
#include "damper_rules.h"
#include "homestead_rules.h"
#include "watchfire_rules.h"

/* One call of a one-language entry answering an exact fraction: rc, and
 * the answer in *num / *den. */
static int damper_rat_call(zxapp_t *a, const zxapp_arg_t *args, uint32_t nargs, int64_t *num,
                           int64_t *den)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "heat_rise_excess", args, nargs, o, WAVE_ROWS, &k, &lp);
    if (rc == ZXAPP_OK && (k != 1u || lp != ZXAPP_NEITHER)) return ZXAPP_E_RUNTIME;
    if (rc == ZXAPP_OK) *num = o[0].num, *den = o[0].den;
    return rc;
}

APP_TEST(damper, 36, 174)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS], alt[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[12][2] = {{"wake.count", "wake"},
                                            {"brazier.heat_ok", "brazier"},
                                            {"brazier.heat_safe", "brazier"},
                                            {"brazier.minutes_left", "brazier"},
                                            {"homestead.home_device_ok", "homestead"},
                                            {"homestead.device_energy", "homestead"},
                                            {"watchfire.place_share_ok", "watchfire"},
                                            {"watchfire.cancel_left", "watchfire"},
                                            {"watchfire.sos_ring_ok", "watchfire"},
                                            {"watchfire.sos_contact_ok", "watchfire"},
                                            {"conch.sound_alert_map_ok", "conch"},
                                            {"conch.hearing_level_ok", "conch"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 6 && app->n == 36,
          "Damper: composed of 5 parts, 6 own lines and 30 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 12u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Damper: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 6 && !org[0].from, "Damper: itself and five parts");

    /* DR1: fire_alarm_quiet, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(-200, DAMPER_MARGIN_MAX) : wrand(-1, 5);
        b[i] = i % 5u ? 0 : wrand(0, 1);
        c[i] = i % 7u ? 0 : wrand(0, 1);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0;                 /* at its limit: quiet */
        if (i == 1) a[i] = DAMPER_MARGIN_MAX, b[i] = 0, c[i] = 0; /* 150.0 C limit, -40.0 C */
        if (i == 2) a[i] = -1, b[i] = 0, c[i] = 0;                /* a tenth over: raised */
        if (i == 3) a[i] = 500, b[i] = 1, c[i] = 0;               /* smoke: raised */
        if (i == 4) a[i] = 500, b[i] = 0, c[i] = 1;               /* BOTH: raised */
        want[i] = T_(damper_quiet(a[i], b[i], c[i]));
    }
    zxapp_arg_t qa[3] = {
        {"margin", a, WAVE_ROWS}, {"smoke", b, WAVE_ROWS}, {"split", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "fire_alarm_quiet", qa, 3, want, WAVE_ROWS, true),
          "Damper: fire_alarm_quiet, Eve = KRON = C");
    {
        /* no reading: a margin past its range or a flag below 0. Eve refuses, KRON reads it
         * as quiet: BOTH, and BOTH is never quiet, so the alarm is raised */
        static const int64_t odd[3][3] = {{DAMPER_MARGIN_MAX + 1, 0, 0}, {5, -1, 0}, {5, 0, -1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t m2[2] = {5, odd[k][0]}, s2[2] = {0, odd[k][1]}, p2[2] = {0, odd[k][2]};
            zxapp_arg_t xa[3] = {{"margin", m2, 2}, {"smoke", s2, 2}, {"split", p2, 2}};
            CHECK(!damper_quiet(odd[k][0], odd[k][1], odd[k][2]) &&
                      wave_lp(app, "fire_alarm_quiet", xa, 3) == ZXAPP_BOTH,
                  "Damper: fire_alarm_quiet edge %u: Eve refuses, KRON quiet: BOTH", k);
        }
    }

    /* the BOTH test: sensor 5 (limit 57.0 C) reports 21.0 C with smoke 1 and 21.0 C with
     * smoke 0 in the same second. The smoke join is BOTH (DR6), so split is 1; the row is
     * not quiet in either language, and the sensor's and the building's fire state is BOTH:
     * the alarm is still raised. */
    {
        const int64_t temp = 210, limit = 570, one = 1, none = 0;
        lpres_state_t smoke = lpres_kjoin(lpres_kjoin(LPRES_STATE_NEITHER, damper_smoke_said(one)),
                                          damper_smoke_said(none));
        lpres_state_t fire = lpres_kjoin(damper_said(temp > limit || one == 1),
                                         damper_said(temp > limit || none == 1));
        lpres_state_t building =
            lpres_disjoin(lpres_disjoin(LPRES_STATE_FALSE, fire), LPRES_STATE_NEITHER);
        int64_t split = smoke == LPRES_STATE_BOTH ? 1 : 0;
        int64_t m2[2] = {limit - temp, limit - temp}, s2[2] = {one, none}, p2[2] = {split, 0};
        int64_t w2[2] = {T_(damper_quiet(m2[0], s2[0], p2[0])), T_(damper_quiet(m2[1], 0, 0))};
        zxapp_arg_t ba[3] = {{"margin", m2, 2}, {"smoke", s2, 2}, {"split", p2, 2}};
        CHECK(smoke == LPRES_STATE_BOTH && split == 1 && fire == LPRES_STATE_BOTH &&
                  building == LPRES_STATE_BOTH && damper_raised(building) && w2[0] == ZXAPP_FALSE &&
                  w2[1] == ZXAPP_TRUE && wave_same(app, "fire_alarm_quiet", ba, 3, w2, 2, true),
              "Damper: sensor 5's smoke 1 and smoke 0 in one second: BOTH, the alarm raised");
        CHECK(!damper_raised(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_NEITHER)) &&
                  damper_raised(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_TRUE)) &&
                  !damper_raised(LPRES_STATE_FALSE),
              "Damper: quiet sensors and a silent one raise nothing; one on fire does");
    }

    /* DR2: heat_rise_excess, Trebago = C, exact, one sensor's window a call */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t t[8], s[8], num = 0, den = 0;
        uint32_t n = (uint32_t) wrand(2, 8);
        int64_t rate = wrand(DAMPER_RATE_MIN, DAMPER_RATE_MAX);
        s[0] = wrand(0, 1800000000);
        t[0] = wrand(DAMPER_TEMP_MIN, DAMPER_TEMP_MAX);
        for (uint32_t k = 1; k < n; k++) {
            s[k] = s[k - 1u] + wrand(0, 90);
            t[k] = wrand(DAMPER_TEMP_MIN, DAMPER_TEMP_MAX);
        }
        if (s[n - 1u] == s[0]) s[n - 1u] = s[0] + 1; /* the window spans time */
        if (i == 0) {
            n = 3, rate = 83; /* 10.0 C a minute against 8.3 */
            s[0] = 0, s[1] = 30, s[2] = 60;
            t[0] = 200, t[1] = 250, t[2] = 300;
        }
        rat_t r = rat_zero();
        bool ok = damper_rise_excess(t, s, n, rate, &r);
        zxapp_arg_t ra[3] = {{"temp", t, n}, {"stamp", s, n}, {"rate", &rate, 1}};
        int rc = damper_rat_call(app, ra, 3, &num, &den);
        CHECK(ok && rc == ZXAPP_OK && num == r.num && den == r.den &&
                  (i || (num == 17 && den == 1)),
              "Damper: heat_rise_excess row %u, Trebago = C: %lld/%lld, want %lld/%lld", i,
              (long long) num, (long long) den, (long long) r.num, (long long) r.den);
    }
    {
        /* refused alike: one reading, a window with no time, a rate past DR3, a reading past
         * 150.0 C, a stamp past 12 digits */
        static const int64_t bad[5][5] = {{1, 0, 0, 83, 200},
                                          {2, 60, 60, 83, 200},
                                          {2, 0, 60, 301, 200},
                                          {2, 0, 60, 83, 1501},
                                          {2, 0, DAMPER_STAMP_MAX + 1, 83, 200}};
        for (uint32_t k = 0; k < 5u; k++) {
            int64_t t[2] = {200, bad[k][4]}, s[2] = {bad[k][1], bad[k][2]}, rate = bad[k][3],
                    num = 0, den = 0;
            uint32_t n = (uint32_t) bad[k][0];
            rat_t r = rat_zero();
            if (n == 1u) t[0] = 200, s[0] = 0;
            zxapp_arg_t ra[3] = {{"temp", t, n}, {"stamp", s, n}, {"rate", &rate, 1}};
            CHECK(!damper_rise_excess(t, s, n, rate, &r) &&
                      damper_rat_call(app, ra, 3, &num, &den) != ZXAPP_OK,
                  "Damper: heat_rise_excess edge %u refused by Trebago and C", k);
        }
    }

    /* DR3: heat_sensor_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(DAMPER_LIMIT_MIN - 50, DAMPER_LIMIT_MAX + 50);
        b[i] = wrand(DAMPER_RATE_MIN - 5, DAMPER_RATE_MAX + 5);
        if (i == 0) a[i] = DAMPER_LIMIT_MIN, b[i] = DAMPER_RATE_MIN;
        if (i == 1) a[i] = DAMPER_LIMIT_MAX, b[i] = DAMPER_RATE_MAX;
        if (i == 2) a[i] = DAMPER_LIMIT_MIN - 1, b[i] = 83;
        if (i == 3) a[i] = 570, b[i] = DAMPER_RATE_MAX + 1;
        want[i] = T_(damper_limits_ok(a[i], b[i]));
    }
    zxapp_arg_t la[2] = {{"limit", a, WAVE_ROWS}, {"rate", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "heat_sensor_ok", la, 2, want, WAVE_ROWS, true),
          "Damper: heat_sensor_ok, Eve = C");

    /* DR4: room_alarm_sensors, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 6), b[i] = wrand(0, 1);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = damper_room_cell(a, b, WAVE_ROWS, i);
    zxapp_arg_t ga[2] = {{"room", a, WAVE_ROWS}, {"raised", b, WAVE_ROWS}};
    CHECK(wave_same(app, "room_alarm_sensors", ga, 2, want, WAVE_ROWS, false),
          "Damper: room_alarm_sensors, KRON = C");

    /* DR5: building_room_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-1200, 1200);
        d[i] = wrand(-12, 12);
        if (i == 0) a[i] = 99, b[i] = 999, c[i] = 999, d[i] = 9; /* max */
        if (i == 1) a[i] = 1, b[i] = 12, c[i] = 0, d[i] = 10;    /* ten doors: 2 digits */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0;
        want[i] = T_(damper_room_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t na[4] = {{"floor", a, WAVE_ROWS},
                         {"room", b, WAVE_ROWS},
                         {"toward", c, WAVE_ROWS},
                         {"exits", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              wave_same(app, "building_room_ok", na, 4, want, WAVE_ROWS, true),
          "Damper: building_room_ok, MYRA = C");

    /* the parts, called through Damper as it exports them */
    {
        int64_t bar = 1, n1 = 0; /* warnings and errors (WAKE_WARN 1) */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 2);
            n1 += a[i] >= bar ? 1 : 0;
        }
        zxapp_arg_t wa[2] = {{"level", a, WAVE_ROWS}, {"bar", &bar, 1}};
        CHECK(wave_same(app, "wake.count", wa, 2, &n1, 1, false),
              "Damper: wake.count, %lld warnings and errors", (long long) n1);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-400, 800);
        if (i == 0) a[i] = 0;
        if (i == 1) a[i] = 450;
        if (i == 2) a[i] = 451;
        if (i == 3) a[i] = -201;
        want[i] = T_(a[i] >= 0 && a[i] <= 450);   /* brazier_heat_ok: 0.0..45.0 C */
        alt[i] = T_(a[i] >= -200 && a[i] <= 600); /* brazier_heat_safe: -20.0..60.0 C */
    }
    zxapp_arg_t ta[1] = {{"temp", a, WAVE_ROWS}};
    CHECK(wave_same(app, "brazier.heat_ok", ta, 1, want, WAVE_ROWS, true) &&
              wave_same(app, "brazier.heat_safe", ta, 1, alt, WAVE_ROWS, true),
          "Damper: brazier.heat_ok and heat_safe, the battery's heat");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999), b[i] = wrand(1, 999999999);
        want[i] = 60 * a[i] / b[i]; /* brazier_minutes_left */
    }
    zxapp_arg_t ma[2] = {{"charge", a, WAVE_ROWS}, {"draw", b, WAVE_ROWS}};
    CHECK(wave_same(app, "brazier.minutes_left", ma, 2, want, WAVE_ROWS, false),
          "Damper: brazier.minutes_left, minutes on battery");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(1, 9999) : wrand(10000, 10010), b[i] = wrand(1, 6),
        c[i] = wrand(-99999, 99999), d[i] = wrand(-99999, 99999), e[i] = wrand(-99999, 99999);
        want[i] = T_(homestead_device_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t ha[5] = {{"device", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"lo", c, WAVE_ROWS},
                         {"hi", d, WAVE_ROWS},
                         {"setpoint", e, WAVE_ROWS}};
    CHECK(wave_same(app, "homestead.home_device_ok", ha, 5, want, WAVE_ROWS, true),
          "Damper: homestead.home_device_ok, a heat source filed");
    {
        int64_t tot = 0, dev = 41;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(40, 43), c[i] = wrand(0, 999999999);
        CHECK(homestead_energy(a, c, WAVE_ROWS, dev, &tot) == 0, "Damper: HR4 answers");
        zxapp_arg_t ea[3] = {{"device", a, WAVE_ROWS}, {"want", &dev, 1}, {"wh", c, WAVE_ROWS}};
        CHECK(wave_same(app, "homestead.device_energy", ea, 3, &tot, 1, false),
              "Damper: homestead.device_energy, heater 41's energy %lld", (long long) tot);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? 1 : wrand(0, 1), b[i] = i % 3u ? 1 : wrand(0, 1),
        c[i] = i % 5u ? 1 : wrand(0, 1), d[i] = i % 7u ? 1 : wrand(0, 1);
        want[i] = T_(watchfire_place_share_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t pa[4] = {{"started", a, WAVE_ROWS},
                         {"optin", b, WAVE_ROWS},
                         {"listed", c, WAVE_ROWS},
                         {"placeok", d, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.place_share_ok", pa, 4, want, WAVE_ROWS, true),
          "Damper: watchfire.place_share_ok, where the machine is shared");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 99), b[i] = wrand(0, 200);
        want[i] = watchfire_cancel_left(a[i], b[i]);
    }
    zxapp_arg_t ca[2] = {{"window", a, WAVE_ROWS}, {"waited", b, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.cancel_left", ca, 2, want, WAVE_ROWS, false),
          "Damper: watchfire.cancel_left, the cancel window");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(5, 65), b[i] = i % 3u ? 0 : wrand(0, 30), c[i] = i % 4u ? 0 : wrand(0, 1);
        want[i] = T_(watchfire_ring_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ka[3] = {
        {"window", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}, {"cancelled", c, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.sos_ring_ok", ka, 3, want, WAVE_ROWS, true),
          "Damper: watchfire.sos_ring_ok, never before the window ran out");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 120), b[i] = wrand(1, 4294967295LL), c[i] = wrand(1, 120),
        d[i] = wrand(0, 1700000000000LL);
        want[i] = T_(watchfire_contact_fits(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t sa[4] = {{"seq", a, WAVE_ROWS},
                         {"peer", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(wave_same(app, "watchfire.sos_contact_ok", sa, 4, want, WAVE_ROWS, true),
          "Damper: watchfire.sos_contact_ok, an evacuation contact's record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? CONCH_SOUND_ALARM : wrand(1, CONCH_SOUNDS), b[i] = wrand(0, 3),
        c[i] = wrand(0, 3);
        want[i] = T_(conch_alert_map_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t xa[3] = {
        {"sound", a, WAVE_ROWS}, {"flash", b, WAVE_ROWS}, {"vibrate", c, WAVE_ROWS}};
    CHECK(wave_same(app, "conch.sound_alert_map_ok", xa, 3, want, WAVE_ROWS, true),
          "Damper: conch.sound_alert_map_ok, the alarm's flash pattern");
    {
        int64_t s2[2] = {CONCH_SOUND_ALARM, CONCH_SOUND_ALARM}, f2[2] = {1, 4}, v2[2] = {1, 1};
        zxapp_arg_t ya[3] = {{"sound", s2, 2}, {"flash", f2, 2}, {"vibrate", v2, 2}};
        CHECK(!conch_alert_map_ok(CONCH_SOUND_ALARM, 4, 1) &&
                  wave_lp(app, "conch.sound_alert_map_ok", ya, 3) == ZXAPP_BOTH,
              "Damper: four flashes a burst fit MYRA's digit, not Eve's limit: BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-300, 300), c[i] = wrand(0, 1200), b[i] = wrand(0, c[i]);
        want[i] = T_(conch_level_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t va[3] = {{"gain", a, WAVE_ROWS}, {"total", b, WAVE_ROWS}, {"cap", c, WAVE_ROWS}};
    CHECK(wave_same(app, "conch.hearing_level_ok", va, 3, want, WAVE_ROWS, true),
          "Damper: conch.hearing_level_ok, the alarm's tone under the owner's cap");
}
