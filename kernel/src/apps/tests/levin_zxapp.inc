/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Levin's languages, witnessed, row by row against levin_rules.h, the C the
 * model runs; the parts it composes (homestead, brazier, mockingbird,
 * tally) resolved and called against their own rules; and the BOTH of the
 * kitchen's 2 kW, planned on circuit 3 and read on circuit 4.
 * Slots: @logic.levin_circuit_breaker_ok, @num.levin_circuit_load_ma,
 * @num.levin_circuit_drop_uv, @num.levin_circuit_room_watts,
 * @num.levin_wiring_permit_fee, @logic.levin_panel_circuit_ok; and, used,
 * @logic.homestead_device_cmd_ok, @num.homestead_device_energy,
 * @logic.brazier_heat_ok, @logic.brazier_heat_safe,
 * @logic.mockingbird_boot_passed, @num.tally_ratio_div. */
#include "homestead_rules.h"
#include "levin_rules.h"
#include "mockingbird_rules.h"

/* One call of an entry answering exact fractions: every row the reduced
 * num[i] / den[i], and the join lp. */
static bool levin_rat_rows(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
                           const int64_t *num, const int64_t *den, uint32_t rows, int32_t want_lp)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != want_lp || k != rows) {
        printf("    %s: rc %d lp %d n %u (%s)\n", entry, rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++)
        if (o[i].num != num[i] || o[i].den != den[i]) {
            printf("    %s row %u: %lld/%lld, want %lld/%lld\n", entry, i, (long long) o[i].num,
                   (long long) o[i].den, (long long) num[i], (long long) den[i]);
            return false;
        }
    return true;
}

APP_TEST(levin, 46, 236)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS], den[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[6][2] = {{"homestead.device_cmd_ok", "homestead"},
                                           {"homestead.device_energy", "homestead"},
                                           {"brazier.heat_ok", "brazier"},
                                           {"brazier.heat_safe", "brazier"},
                                           {"mockingbird.boot_passed", "mockingbird"},
                                           {"tally.ratio_div", "tally"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 9 && app->n == 46,
          "Levin: composed of 4 parts, 9 own lines and 37 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 6u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Levin: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 5 && !org[0].from, "Levin: itself and four parts");

    /* LR1: circuit_breaker_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(-1000, LEVIN_SPARE_MAX) : wrand(-1, 1000);
        b[i] = i % 4u ? wrand(-1000, LEVIN_CABLE_MAX) : wrand(-1, 1000);
        c[i] = i % 5u ? wrand(-1000, LEVIN_SLACK_MAX) : wrand(-1, 1000);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0; /* at every limit */
        if (i == 1) a[i] = LEVIN_SPARE_MAX, b[i] = LEVIN_CABLE_MAX, c[i] = LEVIN_SLACK_MAX;
        if (i == 2) a[i] = -1, b[i] = 5, c[i] = 5; /* load over 80% */
        if (i == 3) a[i] = 5, b[i] = -1, c[i] = 5; /* cable under the breaker */
        if (i == 4) a[i] = 5, b[i] = 5, c[i] = -1; /* drop over the limit */
        want[i] = T_(levin_within_breaker(a[i], b[i], c[i]));
    }
    zxapp_arg_t wa[3] = {{"spare", a, WAVE_ROWS}, {"cable", b, WAVE_ROWS}, {"slack", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "circuit_breaker_ok", wa, 3, want, WAVE_ROWS, true),
          "Levin: circuit_breaker_ok, Eve = KRON = C");
    {
        /* a margin past its range: Eve refuses it, KRON reads it as within: BOTH */
        static const int64_t big[3][3] = {
            {LEVIN_SPARE_MAX + 1, 5, 5}, {5, LEVIN_CABLE_MAX + 1, 5}, {5, 5, LEVIN_SLACK_MAX + 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t sp[2] = {5, big[k][0]}, cb[2] = {5, big[k][1]}, sl[2] = {5, big[k][2]};
            zxapp_arg_t xa[3] = {{"spare", sp, 2}, {"cable", cb, 2}, {"slack", sl, 2}};
            CHECK(!levin_within_breaker(big[k][0], big[k][1], big[k][2]) &&
                      wave_lp(app, "circuit_breaker_ok", xa, 3) == ZXAPP_BOTH,
                  "Levin: circuit_breaker_ok margin %u past its range: Eve refuses, KRON "
                  "within: BOTH",
                  k);
        }
    }

    /* LR2: circuit_load_ma, Trebago = KRON = C (tally.ratio_div), exact fractions */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        rat_t r = rat_zero();
        a[i] = wrand(0, LEVIN_WATTS_MAX);
        b[i] = i % 2u ? wrand(1, LEVIN_VOLTS_MAX) : wrand(100, 250);
        if (i == 0) a[i] = 2000, b[i] = 230;          /* the kitchen */
        if (i == 1) a[i] = 0, b[i] = 230;             /* nothing on */
        if (i == 2) a[i] = LEVIN_WATTS_MAX, b[i] = 1; /* max */
        if (i == 3) a[i] = 1, b[i] = LEVIN_VOLTS_MAX; /* the smallest current */
        CHECK(levin_load_ma(a[i], b[i], &r), "Levin: LR2 row %u", i);
        want[i] = r.num, den[i] = r.den;
    }
    zxapp_arg_t la[2] = {{"watts", a, WAVE_ROWS}, {"volts", b, WAVE_ROWS}};
    CHECK(want[0] == 200000 && den[0] == 23 && want[1] == 0 && den[1] == 1 &&
              levin_rat_rows(app, "circuit_load_ma", la, 2, want, den, WAVE_ROWS, ZXAPP_TRUE),
          "Levin: circuit_load_ma, Trebago = KRON = C");
    {
        rat_t r = rat_zero();
        int64_t w0[2] = {2000, 2000}, v0[2] = {230, 0};
        zxapp_arg_t za[2] = {{"watts", w0, 2}, {"volts", v0, 2}};
        CHECK(!levin_load_ma(2000, 0, &r) &&
                  wave_lp(app, "circuit_load_ma", za, 2) == ZXAPP_NEITHER,
              "Levin: circuit_load_ma at 0 V: both refuse");
        static const int64_t bad[4][2] = {
            {2000, LEVIN_VOLTS_MAX + 1}, {2000, -230}, {LEVIN_WATTS_MAX + 1, 230}, {-1, 230}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t w2[2] = {2000, bad[k][0]}, v2[2] = {230, bad[k][1]};
            zxapp_arg_t xa[2] = {{"watts", w2, 2}, {"volts", v2, 2}};
            CHECK(!levin_load_ma(bad[k][0], bad[k][1], &r) &&
                      wave_lp(app, "circuit_load_ma", xa, 2) == ZXAPP_BOTH,
                  "Levin: circuit_load_ma edge %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* LR3: circuit_drop_uv, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, LEVIN_METRES_MAX), b[i] = wrand(0, LEVIN_MA_MAX),
        c[i] = wrand(0, LEVIN_MOHM_MAX);
        if (i == 0) a[i] = 20, b[i] = 8696, c[i] = 7; /* the kitchen run, 2.5 mm2 */
        if (i == 1) a[i] = LEVIN_METRES_MAX, b[i] = LEVIN_MA_MAX, c[i] = LEVIN_MOHM_MAX;
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0;
        want[i] = levin_drop_uv(a[i], b[i], c[i]);
    }
    zxapp_arg_t da[3] = {{"metres", a, WAVE_ROWS}, {"ma", b, WAVE_ROWS}, {"mohm", c, WAVE_ROWS}};
    CHECK(want[0] == 2434880 && want[1] == INT64_C(1999778002219998) && want[2] == 0 &&
              wave_same(app, "circuit_drop_uv", da, 3, want, WAVE_ROWS, false),
          "Levin: circuit_drop_uv, Trebago = KRON = C");
    {
        static const int64_t bad[6][3] = {{LEVIN_METRES_MAX + 1, 1, 1}, {-1, 1, 1},
                                          {1, LEVIN_MA_MAX + 1, 1},     {1, -1, 1},
                                          {1, 1, LEVIN_MOHM_MAX + 1},   {1, 1, -1}};
        for (uint32_t k = 0; k < 6u; k++) {
            int64_t m2[2] = {20, bad[k][0]}, i2[2] = {1, bad[k][1]}, r2[2] = {1, bad[k][2]};
            zxapp_arg_t xa[3] = {{"metres", m2, 2}, {"ma", i2, 2}, {"mohm", r2, 2}};
            CHECK(levin_drop_uv(bad[k][0], bad[k][1], bad[k][2]) == LEVIN_REFUSED &&
                      wave_lp(app, "circuit_drop_uv", xa, 3) == ZXAPP_BOTH,
                  "Levin: circuit_drop_uv edge %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* LR4: circuit_room_watts, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, 6), b[i] = wrand(1, 4), c[i] = wrand(0, LEVIN_WATTS_MAX);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = levin_room_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ma[3] = {
        {"circuit", a, WAVE_ROWS}, {"room", b, WAVE_ROWS}, {"watts", c, WAVE_ROWS}};
    CHECK(wave_same(app, "circuit_room_watts", ma, 3, want, WAVE_ROWS, false),
          "Levin: circuit_room_watts, KRON = C");

    /* LR5: wiring_permit_fee, Sutra = C, refused on overflow */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, LEVIN_BASE_MAX), b[i] = wrand(0, LEVIN_VISITS_MAX), c[i] = wrand(0, 900000);
        if (i == 0) a[i] = 15000, b[i] = 2, c[i] = 7500;
        if (i == 1) a[i] = 0, b[i] = 0, c[i] = 0;
        if (i == 2) a[i] = 1000000, b[i] = 99, c[i] = 999999;              /* 99999901: fits 9(8) */
        if (levin_permit_fee(a[i], b[i], c[i]) == LEVIN_REFUSED) b[i] = 1; /* stay in 9(8) */
        want[i] = levin_permit_fee(a[i], b[i], c[i]);
    }
    zxapp_arg_t pa[3] = {
        {"base", a, WAVE_ROWS}, {"inspections", b, WAVE_ROWS}, {"visit", c, WAVE_ROWS}};
    CHECK(want[0] == 30000 && want[1] == 0 && want[2] == 99999901 &&
              wave_same(app, "wiring_permit_fee", pa, 3, want, WAVE_ROWS, false),
          "Levin: wiring_permit_fee, Sutra = C");
    {
        static const int64_t bad[5][3] = {{LEVIN_BASE_MAX, LEVIN_VISITS_MAX, LEVIN_VISIT_MAX},
                                          {LEVIN_BASE_MAX + 1, 0, 0},
                                          {0, LEVIN_VISITS_MAX + 1, 1},
                                          {0, 1, LEVIN_VISIT_MAX + 1},
                                          {-1, 0, 0}};
        for (uint32_t k = 0; k < 5u; k++) {
            int64_t b2[2] = {1, bad[k][0]}, i2[2] = {0, bad[k][1]}, v2[2] = {0, bad[k][2]};
            zxapp_arg_t xa[3] = {{"base", b2, 2}, {"inspections", i2, 2}, {"visit", v2, 2}};
            CHECK(levin_permit_fee(bad[k][0], bad[k][1], bad[k][2]) == LEVIN_REFUSED &&
                      wave_lp(app, "wiring_permit_fee", xa, 3) == ZXAPP_NEITHER,
                  "Levin: wiring_permit_fee edge %u refused (an overflow, never wrapped)", k);
        }
    }

    /* LR6: panel_circuit_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-1200, 1200);
        d[i] = wrand(-1200000, 1200000);
        e[i] = wrand(-1200000, 1200000);
        if (i == 0) a[i] = 99, b[i] = 999, c[i] = 999, d[i] = 999999, e[i] = 999999; /* max */
        if (i == 1) a[i] = 1, b[i] = 3, c[i] = 0, d[i] = 1000000, e[i] = 1;          /* 7 digits */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0;
        want[i] = T_(levin_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"panel", a, WAVE_ROWS},
                         {"circuit", b, WAVE_ROWS},
                         {"parent", c, WAVE_ROWS},
                         {"breaker", d, WAVE_ROWS},
                         {"cable", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              wave_same(app, "panel_circuit_ok", na, 5, want, WAVE_ROWS, true),
          "Levin: panel_circuit_ok, MYRA = C");

    /* the parts, called through Levin as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, HOMESTEAD_MARGIN_MAX), b[i] = wrand(-1, HOMESTEAD_MARGIN_MAX),
        c[i] = wrand(-1, 0), d[i] = i % 5u ? 0 : 1;
        want[i] = T_(homestead_command_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ha[4] = {{"above", a, WAVE_ROWS},
                         {"below", b, WAVE_ROWS},
                         {"owed", c, WAVE_ROWS},
                         {"ai", d, WAVE_ROWS}};
    CHECK(wave_same(app, "homestead.device_cmd_ok", ha, 4, want, WAVE_ROWS, true),
          "Levin: homestead.device_cmd_ok, a device's command shown, never sent");
    {
        int64_t tot = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            a[i] = wrand(1, 6), b[i] = 3, c[i] = wrand(0, 999999999);
        CHECK(homestead_energy(a, c, WAVE_ROWS, 3, &tot) == 0, "Levin: HR4 answers");
        zxapp_arg_t ea[3] = {{"device", a, WAVE_ROWS}, {"want", b, 1}, {"wh", c, WAVE_ROWS}};
        CHECK(wave_same(app, "homestead.device_energy", ea, 3, &tot, 1, false),
              "Levin: homestead.device_energy, circuit 3's energy %lld", (long long) tot);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-400, 800);
        if (i == 0) a[i] = 0;
        if (i == 1) a[i] = 450;
        if (i == 2) a[i] = 451;
        if (i == 3) a[i] = -1;
        want[i] = T_(a[i] >= 0 && a[i] <= 450);   /* brazier_heat_ok: 0.0..45.0 C */
        den[i] = T_(a[i] >= -200 && a[i] <= 600); /* brazier_heat_safe: -20.0..60.0 C */
    }
    zxapp_arg_t ta[1] = {{"temp", a, WAVE_ROWS}};
    CHECK(wave_same(app, "brazier.heat_ok", ta, 1, want, WAVE_ROWS, true) &&
              wave_same(app, "brazier.heat_safe", ta, 1, den, WAVE_ROWS, true),
          "Levin: brazier.heat_ok and heat_safe, the panel's temperature");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t code[MOCK_STAGES];
        for (uint32_t s = 0; s < MOCK_STAGES; s++) code[s] = i % 3u ? 0 : wrand(1, MOCK_CODE_MAX);
        if (i % 7u == 1) code[2] = 4, code[3] = MOCK_NOT_RUN, code[4] = MOCK_NOT_RUN;
        a[i] = code[0], b[i] = code[1], c[i] = code[2], d[i] = code[3], e[i] = code[4];
        want[i] = T_(mockingbird_boot_passed(code));
    }
    zxapp_arg_t ka[5] = {{"rom", a, WAVE_ROWS},
                         {"loader", b, WAVE_ROWS},
                         {"kernel", c, WAVE_ROWS},
                         {"drivers", d, WAVE_ROWS},
                         {"apps", e, WAVE_ROWS}};
    CHECK(wave_same(app, "mockingbird.boot_passed", ka, 5, want, WAVE_ROWS, true),
          "Levin: mockingbird.boot_passed, a test run's five steps in order");
    {
        static int64_t one[WAVE_ROWS];
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            rat_t r = rat_zero();
            a[i] = wrand(0, LEVIN_WATTS_MAX) * 1000, b[i] = wrand(1, LEVIN_VOLTS_MAX), one[i] = 1;
            CHECK(tally_ratio(TALLY_DIV, a[i], 1, b[i], 1, &r) &&
                      levin_load_ma(a[i] / 1000, b[i], &r),
                  "Levin: tally.ratio_div row %u", i);
            want[i] = r.num, den[i] = r.den;
        }
        zxapp_arg_t ra[4] = {{"an", a, WAVE_ROWS},
                             {"ad", one, WAVE_ROWS},
                             {"bn", b, WAVE_ROWS},
                             {"bd", one, WAVE_ROWS}};
        CHECK(levin_rat_rows(app, "tally.ratio_div", ra, 4, want, den, WAVE_ROWS, ZXAPP_TRUE),
              "Levin: tally.ratio_div, a load's current as Tally divides it");
    }

    /* LR7, the BOTH test: the plan puts the kitchen's 2 kW on circuit 3 (room 2), the reading
     * shows circuit 3 idle and circuit 4 carrying it. The circuit matrix of each source says
     * so (KRON), and of each circuit the plan and the reading disagree: BOTH, never TRUE; the
     * plan is BOTH and the circuits are flagged miswired. */
    {
        int64_t ci[3] = {3, 4, 5}, rm[3] = {2, 2, 1}, plan_w[3] = {2000, 0, 60},
                read_w[3] = {0, 2000, 60}, pw[3], rw[3];
        zxapp_arg_t pa2[3] = {{"circuit", ci, 3}, {"room", rm, 3}, {"watts", plan_w, 3}};
        zxapp_arg_t ra2[3] = {{"circuit", ci, 3}, {"room", rm, 3}, {"watts", read_w, 3}};
        for (uint32_t i = 0; i < 3u; i++) {
            pw[i] = levin_room_cell(ci, rm, plan_w, 3, i);
            rw[i] = levin_room_cell(ci, rm, read_w, 3, i);
        }
        CHECK(wave_same(app, "circuit_room_watts", pa2, 3, pw, 3, false) &&
                  wave_same(app, "circuit_room_watts", ra2, 3, rw, 3, false) && pw[0] == 2000 &&
                  rw[0] == 0 && rw[1] == 2000,
              "Levin: the kitchen planned on circuit 3, read on circuit 4 (KRON)");
        lpres_state_t s3 = levin_circuit_state(pw[0], true, rw[0]),
                      s4 = levin_circuit_state(pw[1], true, rw[1]),
                      s5 = levin_circuit_state(pw[2], true, rw[2]);
        lpres_state_t plan =
            lpres_conjoin(lpres_conjoin(levin_wired(s3), levin_wired(s4)), levin_wired(s5));
        bool miswired = s3 == LPRES_STATE_BOTH || s4 == LPRES_STATE_BOTH;
        CHECK(s3 == LPRES_STATE_BOTH && s4 == LPRES_STATE_BOTH && s5 == LPRES_STATE_TRUE &&
                  (int) plan == 3 && plan != LPRES_STATE_TRUE && miswired,
              "Levin: circuit 3 and 4 BOTH, the plan state 3 and flagged miswired");
        CHECK(levin_circuit_state(2000, true, 1990) == LPRES_STATE_TRUE &&
                  levin_circuit_state(0, true, LEVIN_IDLE_W) == LPRES_STATE_FALSE &&
                  levin_wired(LPRES_STATE_FALSE) == LPRES_STATE_TRUE &&
                  levin_circuit_state(2000, false, 0) == LPRES_STATE_NEITHER &&
                  lpres_conjoin(levin_wired(LPRES_STATE_TRUE), levin_wired(LPRES_STATE_FALSE)) ==
                      LPRES_STATE_TRUE,
              "Levin: plan and reading that agree are TRUE; no reading is NEITHER");
    }
}
