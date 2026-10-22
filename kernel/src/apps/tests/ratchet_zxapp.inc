/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ratchet's languages, witnessed, row by row against ratchet_rules.h, the C
 * the model runs; the parts it composes (tidewatch, treadle, homestead,
 * mockingbird) resolved and called against their own rules; and the BOTH
 * of part 4, within its limits on vibration and over them on temperature.
 * Slots: @logic.ratchet_machine_limits_ok, @num.ratchet_machine_repair_step,
 * @num.ratchet_machine_wear, @num.ratchet_service_hours_left,
 * @num.ratchet_repair_line_cost, @num.ratchet_sensor_hour_faults,
 * @logic.ratchet_machine_part_ok; and, used, @num.tidewatch_cpu_x100,
 * @num.tidewatch_peak, @num.treadle_next_run, @logic.treadle_budget_ok,
 * @logic.treadle_task_ok, @logic.homestead_device_cmd_ok,
 * @num.homestead_device_energy, @logic.mockingbird_boot_passed,
 * @logic.mockingbird_emu_device_ok. */
#include "homestead_rules.h"
#include "mockingbird_rules.h"
#include "ratchet_rules.h"
#include "treadle_rules.h"

/* One call of an entry answering exact fractions: every row the reduced
 * num[i] / den[i], and the join lp. */
static bool ratchet_rat_rows(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
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

APP_TEST(ratchet, 36, 152)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS], den[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[9][2] = {{"tidewatch.cpu_x100", "tidewatch"},
                                           {"tidewatch.peak", "tidewatch"},
                                           {"treadle.next_run", "treadle"},
                                           {"treadle.budget_ok", "treadle"},
                                           {"treadle.task_ok", "treadle"},
                                           {"homestead.device_cmd_ok", "homestead"},
                                           {"homestead.device_energy", "homestead"},
                                           {"mockingbird.boot_passed", "mockingbird"},
                                           {"mockingbird.emu_device_ok", "mockingbird"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 10 && app->n == 36,
          "Ratchet: composed of 4 parts, 10 own lines and 26 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Ratchet: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 5 && !org[0].from, "Ratchet: itself and four parts");

    /* RR1: machine_limits_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(-1000, RATCHET_MARGIN_MAX) : wrand(-1, 1000);
        b[i] = i % 4u ? wrand(-1000, RATCHET_MARGIN_MAX) : wrand(-1, 1000);
        if (i == 0) a[i] = 0, b[i] = 0;                                   /* at both limits */
        if (i == 1) a[i] = RATCHET_MARGIN_MAX, b[i] = RATCHET_MARGIN_MAX; /* max */
        if (i == 2) a[i] = -1, b[i] = 5;                                  /* under lo */
        if (i == 3) a[i] = 5, b[i] = -1;                                  /* over hi */
        want[i] = T_(ratchet_limits_ok(a[i], b[i]));
    }
    zxapp_arg_t la[2] = {{"above", a, WAVE_ROWS}, {"below", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "machine_limits_ok", la, 2, want, WAVE_ROWS, true),
          "Ratchet: machine_limits_ok, Eve = KRON = C");
    {
        int64_t ab[2] = {5, RATCHET_MARGIN_MAX + 1}, be[2] = {5, 5};
        zxapp_arg_t xa[2] = {{"above", ab, 2}, {"below", be, 2}};
        CHECK(!ratchet_limits_ok(RATCHET_MARGIN_MAX + 1, 5) &&
                  wave_lp(app, "machine_limits_ok", xa, 2) == ZXAPP_BOTH,
              "Ratchet: a margin past 999999: Eve refuses, KRON reads inside: BOTH");
    }

    /* RR2: machine_repair_step, BASIC (Hatchling) = KRON = C, every flag combination,
     * one row a call (Lingua reads one row) */
    for (int64_t k = 0; k < 16; k++) {
        int64_t f = k & 1, p = (k >> 1) & 1, x = (k >> 2) & 1, t = (k >> 3) & 1;
        int64_t w = ratchet_repair_step(f, p, x, t);
        zxapp_arg_t ra[4] = {
            {"fault", &f, 1}, {"isolated", &p, 1}, {"fixed", &x, 1}, {"tested", &t, 1}};
        CHECK(w >= RATCHET_STEP_NONE && w <= RATCHET_STEP_RECORD &&
                  wave_same(app, "machine_repair_step", ra, 4, &w, 1, false),
              "Ratchet: machine_repair_step %lld%lld%lld%lld = %lld, BASIC = KRON = C",
              (long long) f, (long long) p, (long long) x, (long long) t, (long long) w);
    }
    CHECK(ratchet_repair_step(0, 1, 1, 1) == RATCHET_STEP_NONE &&
              ratchet_repair_step(1, 0, 0, 0) == RATCHET_STEP_ISOLATE &&
              ratchet_repair_step(1, 1, 0, 1) == RATCHET_STEP_REPAIR &&
              ratchet_repair_step(1, 0, 1, 0) == RATCHET_STEP_RESTORE &&
              ratchet_repair_step(1, 0, 1, 1) == RATCHET_STEP_RECORD &&
              ratchet_step_touches(RATCHET_STEP_ISOLATE) &&
              ratchet_step_touches(RATCHET_STEP_RESTORE) &&
              !ratchet_step_touches(RATCHET_STEP_RECORD),
          "Ratchet: the first step that applies");
    {
        /* a flag that is no flag: BASIC refuses, KRON reads it as 0 or 1: BOTH */
        static const int64_t bad[4][4] = {{2, 0, 0, 0}, {1, -1, 0, 0}, {1, 1, 2, 0}, {1, 0, 1, -5}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t f = bad[k][0], p = bad[k][1], x = bad[k][2], t = bad[k][3];
            zxapp_arg_t ra[4] = {
                {"fault", &f, 1}, {"isolated", &p, 1}, {"fixed", &x, 1}, {"tested", &t, 1}};
            CHECK(ratchet_repair_step(f, p, x, t) == RATCHET_REFUSED &&
                      wave_lp(app, "machine_repair_step", ra, 4) == ZXAPP_BOTH,
                  "Ratchet: machine_repair_step edge %u: BASIC refuses, KRON answers: BOTH", k);
        }
    }

    /* RR3: machine_wear, Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(0, RATCHET_HOURS_MAX), b[i] = wrand(0, RATCHET_LOAD_FULL);
    a[0] = RATCHET_HOURS_MAX, b[0] = RATCHET_LOAD_FULL; /* max */
    a[1] = 0, b[1] = 0;                                 /* idle */
    want[0] = ratchet_wear(a, b, WAVE_ROWS);
    zxapp_arg_t wa[2] = {{"hours", a, WAVE_ROWS}, {"load", b, WAVE_ROWS}};
    CHECK(want[0] != RATCHET_REFUSED && wave_same(app, "machine_wear", wa, 2, want, 1, false),
          "Ratchet: machine_wear %lld, Trebago = C", (long long) want[0]);
    {
        static const int64_t bad[4][2] = {
            {RATCHET_HOURS_MAX + 1, 1}, {-1, 1}, {1, RATCHET_LOAD_FULL + 1}, {1, -1}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t h[2] = {10, bad[k][0]}, ld[2] = {5000, bad[k][1]};
            zxapp_arg_t xa[2] = {{"hours", h, 2}, {"load", ld, 2}};
            CHECK(ratchet_wear(h, ld, 2) == RATCHET_REFUSED &&
                      wave_lp(app, "machine_wear", xa, 2) == ZXAPP_NEITHER,
                  "Ratchet: machine_wear edge %u refused", k);
        }
    }

    /* RR4: service_hours_left, Trebago = C, exact fractions */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = wrand(1, RATCHET_INTERVAL_MAX);
        d[i] = i % 2u ? wrand(0, RATCHET_WORN_MAX) : wrand(0, c[i] * RATCHET_LOAD_FULL);
        e[i] = wrand(1, RATCHET_LOAD_FULL);
        if (i == 0) c[i] = 1000, d[i] = 4000000, e[i] = 5000;                       /* 1200 hours */
        if (i == 1) c[i] = 500, d[i] = 6000000, e[i] = 3;                           /* past due */
        if (i == 2) c[i] = RATCHET_INTERVAL_MAX, d[i] = RATCHET_WORN_MAX, e[i] = 1; /* max */
        CHECK(ratchet_service_left(c[i], d[i], e[i], &want[i], &den[i]), "Ratchet: RR4 row %u", i);
    }
    zxapp_arg_t sa[3] = {
        {"interval", c, WAVE_ROWS}, {"worn", d, WAVE_ROWS}, {"load", e, WAVE_ROWS}};
    CHECK(
        want[0] == 1200 && den[0] == 1 && want[1] == -1000000 && den[1] == 3 &&
            ratchet_rat_rows(app, "service_hours_left", sa, 3, want, den, WAVE_ROWS, ZXAPP_NEITHER),
        "Ratchet: service_hours_left, Trebago = C");
    {
        static const int64_t bad[4][3] = {
            {0, 0, 1}, {1, -1, 1}, {1, 0, 0}, {1, 0, RATCHET_LOAD_FULL + 1}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t iv[2] = {10, bad[k][0]}, wo[2] = {0, bad[k][1]}, ld[2] = {100, bad[k][2]},
                    n = 0, q = 0;
            zxapp_arg_t xa[3] = {{"interval", iv, 2}, {"worn", wo, 2}, {"load", ld, 2}};
            CHECK(!ratchet_service_left(bad[k][0], bad[k][1], bad[k][2], &n, &q) &&
                      wave_lp(app, "service_hours_left", xa, 3) == ZXAPP_NEITHER,
                  "Ratchet: service_hours_left edge %u refused", k);
        }
    }

    /* RR5: repair_line_cost, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RATCHET_QTY_MAX);
        b[i] = i % 2u ? wrand(0, 100000) : wrand(0, RATCHET_PRICE_MAX);
        c[i] = wrand(0, RATCHET_MINUTES_MAX);
        d[i] = wrand(0, RATCHET_RATE_MAX);
        if (i == 0) a[i] = 2, b[i] = 1250, c[i] = 90, d[i] = 75; /* two parts and 90 minutes */
        if (i == 1)
            a[i] = RATCHET_QTY_MAX, b[i] = RATCHET_PRICE_MAX, c[i] = RATCHET_MINUTES_MAX,
            d[i] = RATCHET_RATE_MAX;                        /* max */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0; /* nothing */
        want[i] = ratchet_line_cost(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t ca[4] = {{"qty", a, WAVE_ROWS},
                         {"price", b, WAVE_ROWS},
                         {"minutes", c, WAVE_ROWS},
                         {"rate", d, WAVE_ROWS}};
    CHECK(want[0] == 9250 && want[1] == INT64_C(10998989890002) && want[2] == 0 &&
              wave_same(app, "repair_line_cost", ca, 4, want, WAVE_ROWS, false),
          "Ratchet: repair_line_cost, Sutra = Trebago = C");
    {
        int64_t q[2] = {1, -1}, pr[2] = {1, 1}, mi[2] = {0, 0}, ra[2] = {0, 0};
        zxapp_arg_t xa[4] = {{"qty", q, 2}, {"price", pr, 2}, {"minutes", mi, 2}, {"rate", ra, 2}};
        CHECK(ratchet_line_cost(-1, 1, 0, 0) == RATCHET_REFUSED &&
                  ratchet_line_cost_held(-1, 1, 0, 0) == RATCHET_REFUSED &&
                  wave_lp(app, "repair_line_cost", xa, 4) == ZXAPP_NEITHER,
              "Ratchet: a negative quantity: both refuse");
        static const int64_t big[3][4] = {{RATCHET_QTY_MAX + 1, 1, 0, 0},
                                          {1, 1, RATCHET_MINUTES_MAX + 1, 1},
                                          {1, 1, 1, RATCHET_RATE_MAX + 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t q2[2] = {1, big[k][0]}, p2[2] = {1, big[k][1]}, m2[2] = {0, big[k][2]},
                    r2[2] = {0, big[k][3]};
            zxapp_arg_t ya[4] = {
                {"qty", q2, 2}, {"price", p2, 2}, {"minutes", m2, 2}, {"rate", r2, 2}};
            CHECK(ratchet_line_cost(big[k][0], big[k][1], big[k][2], big[k][3]) ==
                          RATCHET_REFUSED &&
                      ratchet_line_cost_held(big[k][0], big[k][1], big[k][2], big[k][3]) !=
                          RATCHET_REFUSED &&
                      wave_lp(app, "repair_line_cost", ya, 4) == ZXAPP_BOTH,
                  "Ratchet: repair_line_cost edge %u past Sutra's picture: Sutra refuses, Trebago "
                  "answers: BOTH",
                  k);
        }
    }

    /* RR6: sensor_hour_faults, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, 4) * 10 + wrand(0, 1), b[i] = wrand(490000, 490003), c[i] = wrand(0, 1);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = ratchet_grid_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {{"sensor", a, WAVE_ROWS}, {"hour", b, WAVE_ROWS}, {"fault", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sensor_hour_faults", ga, 3, want, WAVE_ROWS, false),
          "Ratchet: sensor_hour_faults, KRON = C");

    /* RR7: machine_part_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(-1200000, 1200000);
        d[i] = wrand(-120, 120);
        e[i] = wrand(-1200000000, 1200000000);
        if (i == 0)
            a[i] = 9999, b[i] = 999999, c[i] = 999999, d[i] = 99, e[i] = 999999999; /* max */
        if (i == 1) a[i] = 1, b[i] = 4, c[i] = 1, d[i] = 100, e[i] = 0; /* a kind past 2 digits */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0;
        want[i] = T_(ratchet_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"machine", a, WAVE_ROWS},
                         {"part", b, WAVE_ROWS},
                         {"parent", c, WAVE_ROWS},
                         {"kind", d, WAVE_ROWS},
                         {"hours", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              wave_same(app, "machine_part_ok", na, 5, want, WAVE_ROWS, true),
          "Ratchet: machine_part_ok, MYRA = C");

    /* the parts, called through Ratchet as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 99999999), b[i] = wrand(1, 100000);
        want[i] = a[i] * 10 / b[i];
    }
    zxapp_arg_t ta[2] = {{"cpu", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tidewatch.cpu_x100", ta, 2, want, WAVE_ROWS, false),
          "Ratchet: tidewatch.cpu_x100, the CPU share between two reads");
    {
        int64_t pk = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-500, 10000);
            if (a[i] > pk) pk = a[i];
        }
        zxapp_arg_t pa[1] = {{"share", a, WAVE_ROWS}};
        CHECK(wave_same(app, "tidewatch.peak", pa, 1, &pk, 1, false),
              "Ratchet: tidewatch.peak %lld", (long long) pk);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TREADLE_DAY_MIN - 1), b[i] = wrand(1, TREADLE_DAY_MIN),
        c[i] = wrand(0, TREADLE_DAY_MIN - 1);
        want[i] = treadle_next_run(a[i], b[i], c[i]);
    }
    zxapp_arg_t xa[3] = {{"start", a, WAVE_ROWS}, {"period", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "treadle.next_run", xa, 3, want, WAVE_ROWS, false),
          "Ratchet: treadle.next_run, a maintenance task's next run");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, TREADLE_TOKENS_MAX), b[i] = wrand(-1, TREADLE_TOKENS_MAX);
        want[i] = T_(treadle_budget_ok(a[i], b[i]));
    }
    zxapp_arg_t ba[2] = {{"cost", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "treadle.budget_ok", ba, 2, want, WAVE_ROWS, true),
          "Ratchet: treadle.budget_ok, a run within the owner's budget");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200, 1200), b[i] = wrand(0, 12), c[i] = wrand(0, 12000),
        d[i] = wrand(1, 12000), e[i] = wrand(0, 1);
        want[i] = T_(treadle_record_fits(a[i], b[i], c[i], d[i], 500, e[i]));
    }
    static int64_t cost500[WAVE_ROWS];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) cost500[i] = 500;
    zxapp_arg_t ka[6] = {{"task", a, WAVE_ROWS},       {"kind", b, WAVE_ROWS},
                         {"start", c, WAVE_ROWS},      {"period", d, WAVE_ROWS},
                         {"cost", cost500, WAVE_ROWS}, {"approved", e, WAVE_ROWS}};
    CHECK(wave_same(app, "treadle.task_ok", ka, 6, want, WAVE_ROWS, true),
          "Ratchet: treadle.task_ok, a task's record");
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
          "Ratchet: homestead.device_cmd_ok, restoring a device's power (shown only)");
    {
        int64_t tot = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            a[i] = wrand(1, 4), b[i] = 2, c[i] = wrand(0, 1000000000);
        CHECK(homestead_energy(a, c, WAVE_ROWS, 2, &tot) == 0, "Ratchet: HR4 answers");
        zxapp_arg_t ea[3] = {{"device", a, WAVE_ROWS}, {"want", b, 1}, {"wh", c, WAVE_ROWS}};
        CHECK(wave_same(app, "homestead.device_energy", ea, 3, &tot, 1, false),
              "Ratchet: homestead.device_energy, a meter domain's energy %lld", (long long) tot);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t code[MOCK_STAGES];
        for (uint32_t s = 0; s < MOCK_STAGES; s++) code[s] = i % 3u ? 0 : wrand(1, MOCK_CODE_MAX);
        if (i % 7u == 1) code[2] = 4, code[3] = MOCK_NOT_RUN, code[4] = MOCK_NOT_RUN;
        a[i] = code[0], b[i] = code[1], c[i] = code[2], d[i] = code[3], e[i] = code[4];
        want[i] = T_(mockingbird_boot_passed(code));
    }
    zxapp_arg_t ma[5] = {{"rom", a, WAVE_ROWS},
                         {"loader", b, WAVE_ROWS},
                         {"kernel", c, WAVE_ROWS},
                         {"drivers", d, WAVE_ROWS},
                         {"apps", e, WAVE_ROWS}};
    CHECK(wave_same(app, "mockingbird.boot_passed", ma, 5, want, WAVE_ROWS, true),
          "Ratchet: mockingbird.boot_passed, the self-check after a repair");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120), b[i] = wrand(-120, 120),
        c[i] = wrand(0, INT64_C(1200000000000000)), d[i] = wrand(0, INT64_C(1200000000000000)),
        e[i] = wrand(-1200, 1200);
        want[i] = T_(mockingbird_device_record_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t da[5] = {{"device", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"base", c, WAVE_ROWS},
                         {"size", d, WAVE_ROWS},
                         {"irq", e, WAVE_ROWS}};
    CHECK(wave_same(app, "mockingbird.emu_device_ok", da, 5, want, WAVE_ROWS, true),
          "Ratchet: mockingbird.emu_device_ok, a part's place on the map");

    /* RR8, the BOTH test: part 4 read against its limits, vibration 0..710 hundredths of a
     * mm/s (it reads 450) and temperature 0..850 tenths of a degree (it reads 912). The
     * witnessed rule says the vibration is inside and the temperature outside; the part is
     * BOTH, never TRUE, and the machine with it is BOTH: an inspection is raised. */
    {
        int64_t ab[2], be[2];
        CHECK(ratchet_margins(450, 0, 710, &ab[0], &be[0]) &&
                  ratchet_margins(912, 0, 850, &ab[1], &be[1]),
              "Ratchet: part 4's margins");
        int64_t w2[2] = {ZXAPP_TRUE, ZXAPP_FALSE};
        zxapp_arg_t pa[2] = {{"above", ab, 2}, {"below", be, 2}};
        CHECK(wave_same(app, "machine_limits_ok", pa, 2, w2, 2, true),
              "Ratchet: part 4: vibration within limits, temperature over them (Eve = KRON)");
        lpres_state_t says[2] = {ratchet_measure_says(ab[0], be[0]),
                                 ratchet_measure_says(ab[1], be[1])};
        lpres_state_t s = ratchet_part_state(says, 2);
        CHECK(s == LPRES_STATE_BOTH && (int) s == 3 && s != LPRES_STATE_TRUE &&
                  lpres_conjoin(s, LPRES_STATE_TRUE) == LPRES_STATE_BOTH,
              "Ratchet: part 4 is BOTH, never sound, and so is the machine");
        lpres_state_t ok2[2] = {LPRES_STATE_TRUE, LPRES_STATE_TRUE},
                      bad2[2] = {LPRES_STATE_FALSE, LPRES_STATE_FALSE};
        CHECK(ratchet_part_state(ok2, 2) == LPRES_STATE_TRUE &&
                  ratchet_part_state(bad2, 2) == LPRES_STATE_FALSE &&
                  ratchet_part_state(says, 0) == LPRES_STATE_NEITHER,
              "Ratchet: measures that agree give TRUE or FALSE; none gives NEITHER");
    }
}
