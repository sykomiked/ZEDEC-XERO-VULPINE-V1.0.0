/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Levin's model against zxvd's real dispatcher (LINK): the plan is a file
 * in the Hoard read with files.read and written with files.write, only on
 * the owner's word; power.status, which this in-process zxvd does not link,
 * is refused here and answered on a link of its own (LV_LINK). The circuit
 * check, the kitchen's 2 kW planned on circuit 3 and read on circuit 4
 * (BOTH, miswired), a test run, the panel's heat, energy and permits. */
#include "levin_model.h"

static levin_t LV, LV2;
static zxm_link_t LV_LINK;
static uint32_t lv_sent;

static int32_t lv_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) len;
    lv_sent++;
    if (!strstr(req, "\"power.status\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32601,"
                        "\"message\":\"no such method\"}}",
                        l->seq);
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"device\":\"desktop\","
                    "\"total_mw\":14090,\"source\":\"metered\",\"meters\":[]}}",
                    l->seq);
}

static bool lv_file_is(const char *want)
{
    static uint8_t buf[LEVIN_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(LEVIN_FILE, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

APPS_TEST(levin)
{
    uint64_t size = 0;
    levin_report_t r;
    rat_t q = rat_zero();

    /* the rules, as the blocks run them */
    CHECK(levin_within_breaker(0, 0, 0) && !levin_within_breaker(-1, 0, 0) &&
              !levin_within_breaker(0, -1, 0) && !levin_within_breaker(0, 0, -1) &&
              !levin_within_breaker(LEVIN_SPARE_MAX + 1, 0, 0),
          "levin: LR1, three margins each in its range");
    CHECK(levin_load_ma(2000, 230, &q) && q.num == 200000 && q.den == 23 &&
              levin_ma_up(q) == 8696 && !levin_load_ma(2000, 0, &q) &&
              !levin_load_ma(LEVIN_WATTS_MAX + 1, 230, &q),
          "levin: LR2, 2 kW at 230 V is 200000/23 mA, rounded up 8696");
    CHECK(levin_drop_uv(20, 8696, 7) == 2434880 && levin_drop_uv(-1, 1, 1) == LEVIN_REFUSED &&
              levin_spare(LEVIN_BREAKER_MAX + 1, q) == LEVIN_REFUSED,
          "levin: LR3, the drop; a breaker past 200 A gives no margin");
    CHECK(levin_load_ma(2000, 230, &q) && levin_spare(20000, q) == 36521 &&
              levin_load_ma(3680, 230, &q) && levin_spare(20000, q) == 0 &&
              levin_load_ma(3681, 230, &q) && levin_spare(20000, q) == -22 &&
              levin_spare(20000, rat_make(1, 0)) == LEVIN_REFUSED,
          "levin: LR1, 80%% of 20 A exactly: 3680 W within, 3681 W not; no current, no margin");
    CHECK(levin_permit_fee(15000, 2, 7500) == 30000 &&
              levin_permit_fee(LEVIN_BASE_MAX, LEVIN_VISITS_MAX, LEVIN_VISIT_MAX) == LEVIN_REFUSED,
          "levin: LR5, a permit's fee; an overflow refused");

    /* nothing is written before the plan is read, nor without the owner's word */
    CHECK(zxf_use_space(NULL) == 0, "home");
    levin_init(&LV, &LINK);
    levin_circuit_t c3 = {3, 0, 20000, 27000, 230, 20, 7, 6900000};
    int before = sent;
    CHECK(levin_add_circuit(&LV, &c3, true) == ZXM_E_STATE && sent == before && LV.nc == 0,
          "levin: not loaded: nothing sent");
    CHECK(levin_load(&LV) == ZXM_OK && LV.loaded && LV.nc == 0 && LV.nld == 0,
          "levin: no plan yet is an empty one: %s", LINK.err);
    before = sent;
    CHECK(levin_add_circuit(&LV, &c3, false) == ZXM_E_STATE && sent == before && LV.nc == 0,
          "levin: a circuit without the owner's word: nothing sent, the plan as it was");
    levin_circuit_t bad[6] = {{0, 0, 20000, 27000, 230, 20, 7, 1},
                              {3, 9, 20000, 27000, 230, 20, 7, 1},
                              {3, 0, LEVIN_BREAKER_MAX + 1, 999999, 230, 20, 7, 1},
                              {3, 0, 20000, 27000, 0, 20, 7, 1},
                              {3, 0, 20000, 27000, 230, LEVIN_METRES_MAX + 1, 7, 1},
                              {3, 0, 20000, 1000000, 230, 20, 7, 1}};
    for (uint32_t i = 0; i < 6u; i++)
        CHECK(levin_add_circuit(&LV, &bad[i], true) == ZXM_E_ARG && sent == before,
              "levin: circuit %u refused before anything is sent", i);

    /* LM2, LM3: the plan, on the owner's word */
    levin_circuit_t c4 = {4, 0, 16000, 20000, 230, 15, 12, 6900000},
                    c5 = {5, 0, 6000, 10000, 230, 30, 12, 6900000},
                    c6 = {6, 4, 16000, 13000, 230, 40, 18, 2000000};
    CHECK(levin_add_circuit(&LV, &c5, true) == ZXM_OK &&
              levin_add_circuit(&LV, &c3, true) == ZXM_OK &&
              levin_add_circuit(&LV, &c4, true) == ZXM_OK &&
              levin_add_circuit(&LV, &c6, true) == ZXM_OK && LV.nc == 4 && LV.c[0].id == 3 &&
              LV.c[3].id == 6 && levin_add_circuit(&LV, &c4, true) == ZXM_E_ARG,
          "levin: four circuits, ids rising, one twice refused: %s", LINK.err);
    before = sent;
    CHECK(levin_add_load(&LV, 3, 2, 2000, "Kitchen", false) == ZXM_E_STATE && sent == before &&
              LV.nld == 0 && levin_add_load(&LV, 9, 2, 2000, "Kitchen", true) == ZXM_E_ARG &&
              levin_add_load(&LV, 3, 0, 2000, "Kitchen", true) == ZXM_E_ARG &&
              levin_add_load(&LV, 3, 2, LEVIN_WATTS_MAX + 1, "Kitchen", true) == ZXM_E_ARG &&
              levin_add_load(&LV, 3, 2, 2000, "", true) == ZXM_E_ARG && sent == before,
          "levin: a load without the owner's word, on no circuit, in no room, too large or "
          "unnamed: refused, nothing sent");
    CHECK(levin_add_load(&LV, 3, 2, 2000, "Kitchen", true) == ZXM_OK &&
              levin_add_load(&LV, 5, 1, 60, "Hall lights", true) == ZXM_OK &&
              levin_add_load(&LV, 6, 3, 3000, "Workshop", true) == ZXM_OK &&
              levin_add_load(&LV, 3, 2, 500, "Kettle", true) == ZXM_OK && LV.nld == 4,
          "levin: four loads: %s", LINK.err);
    CHECK(lv_file_is("levin 1\n"
                     "circuit 3 0 20000 27000 230 20 7 6900000\n"
                     "circuit 4 0 16000 20000 230 15 12 6900000\n"
                     "circuit 5 0 6000 10000 230 30 12 6900000\n"
                     "circuit 6 4 16000 13000 230 40 18 2000000\n"
                     "load 3 2 2000 Kitchen\nload 5 1 60 Hall lights\nload 6 3 3000 Workshop\n"
                     "load 3 2 500 Kettle\n"),
          "levin: the plan file, as LM1 says");

    /* LM4: the circuit check */
    CHECK(levin_check(&LV, 3, &r) == ZXM_OK && r.watts == 2500 && r.load.num == 250000 &&
              r.load.den == 23 && r.ma == 10870 && r.drop == 2 * 20 * 10870 * 7 &&
              r.spare == (4 * 20000 * 23 - 5 * 250000) / 23 && r.cable == 7000 &&
              r.slack == 6900000 - 3043600 && r.within && strstr(LV.notice, "within"),
          "levin: circuit 3, 2500 W, within its breaker: %s", LV.notice);
    CHECK(levin_check(&LV, 6, &r) == ZXM_OK && r.watts == 3000 && !r.within && r.spare < 0 &&
              r.cable == -3000 && r.slack < 0 && strstr(LV.notice, "past its breaker") &&
              strstr(LV.notice, "80%") && strstr(LV.notice, "cable") && strstr(LV.notice, "drop") &&
              strstr(LV.notice, "electrician"),
          "levin: circuit 6, past its breaker three ways: %s", LV.notice);
    CHECK(levin_check(&LV, 4, &r) == ZXM_OK && r.watts == 0 && r.ma == 0 && r.within &&
              levin_check(&LV, 7, &r) == ZXM_E_ARG,
          "levin: an idle circuit is within; no circuit 7");

    /* LM5: the circuit matrix */
    int64_t cell[LEVIN_LOADS];
    uint32_t nc = 0;
    CHECK(levin_matrix(&LV, cell, LEVIN_LOADS, &nc) == ZXM_OK && nc == 4 && cell[0] == 2500 &&
              cell[1] == 60 && cell[2] == 3000 && cell[3] == 2500 &&
              levin_matrix(&LV, cell, 3, &nc) == ZXM_E_ARG,
          "levin: the circuit matrix, circuit 3's kitchen 2500 W");

    /* LM1: the plan reads back; a file that is not Levin's is never overwritten */
    levin_init(&LV2, &LINK);
    CHECK(levin_load(&LV2) == ZXM_OK && LV2.nc == 4 && LV2.nld == 4 && LV2.c[3].parent == 4 &&
              !strcmp(LV2.ld[1].name, "Hall lights"),
          "levin: the plan reads back");
    static const char *const junk[] = {
        "levin 2\n",
        "levin 1\ncircuit 3 0 20000 27000 230 20 7 6900000",
        "levin 1\ncircuit 03 0 20000 27000 230 20 7 6900000\n",
        "levin 1\ncircuit 4 0 20000 27000 230 20 7 1\ncircuit 3 0 20000 27000 230 20 7 1\n",
        "levin 1\ncircuit 3 4 20000 27000 230 20 7 1\n",
        "levin 1\ncircuit 3 0 20000 27000 230 20 7 1\nload 4 1 60 Hall\n",
        "levin 1\ncircuit 3 0 20000 27000 230 20 7 1\nload 3 1 60 \n",
        "levin 1\nload 3 1 60 Hall\ncircuit 3 0 20000 27000 230 20 7 1\n",
        "levin 1\ncircuit 3 0 20000 27000 230 20 7 -0\n",
    };
    for (uint32_t i = 0; i < sizeof junk / sizeof junk[0]; i++)
        CHECK(zxf_write(LEVIN_FILE, (const uint8_t *) junk[i], (uint32_t) strlen(junk[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  levin_load(&LV2) == ZXM_E_PROTOCOL && !LV2.loaded,
              "levin: not Levin's file: %u", i);
    CHECK(levin_add_load(&LV2, 3, 1, 60, "Hall", true) == ZXM_E_STATE &&
              lv_file_is(junk[sizeof junk / sizeof junk[0] - 1u]),
          "levin: refused, left as it was");

    /* LM6, the BOTH test: the kitchen's 2 kW planned on circuit 3, read on circuit 4 */
    static const char plan[] = "levin 1\n"
                               "circuit 3 0 20000 27000 230 20 7 6900000\n"
                               "circuit 4 0 20000 27000 230 20 7 6900000\n"
                               "circuit 5 0 6000 10000 230 30 12 6900000\n"
                               "load 3 2 2000 Kitchen\nload 5 1 60 Hall lights\n";
    lpres_state_t st = LPRES_STATE_TRUE;
    uint32_t mis = 9;
    CHECK(zxf_write(LEVIN_FILE, (const uint8_t *) plan, (uint32_t) strlen(plan), ZXF_W_REPLACE,
                    false, &size) == 0 &&
              levin_load(&LV2) == ZXM_OK && LV2.nc == 3,
          "levin: the kitchen's plan");
    CHECK(levin_verify(&LV2, &st, &mis) == ZXM_OK && st == LPRES_STATE_NEITHER && mis == 0,
          "levin: no reading yet: NEITHER, nothing proven");
    CHECK(levin_reading(&LV2, 3, 0) == ZXM_OK && levin_reading(&LV2, 4, 2000) == ZXM_OK &&
              levin_reading(&LV2, 5, 60) == ZXM_OK && levin_reading(&LV2, 9, 1) == ZXM_E_ARG &&
              levin_reading(&LV2, 3, -1) == ZXM_E_ARG,
          "levin: the person's readings");
    CHECK(levin_verify(&LV2, &st, &mis) == ZXM_OK && (int) st == 3 && st == LPRES_STATE_BOTH &&
              mis == 2 && LV2.state[0] == LPRES_STATE_BOTH && LV2.state[1] == LPRES_STATE_BOTH &&
              LV2.state[2] == LPRES_STATE_TRUE && strstr(LV2.notice, "Miswired") &&
              strstr(LV2.notice, "circuit 3") && strstr(LV2.notice, "circuit 4"),
          "levin: circuit 3 planned, circuit 4 carrying: the plan is BOTH (3), miswired: %s",
          LV2.notice);
    CHECK(levin_reading(&LV2, 3, 1990) == ZXM_OK && levin_reading(&LV2, 4, 0) == ZXM_OK &&
              levin_verify(&LV2, &st, &mis) == ZXM_OK && st == LPRES_STATE_TRUE && mis == 0,
          "levin: wired as planned: TRUE");

    /* LM7: a test run, Mockingbird's boot rule over five steps */
    int64_t pass[LEVIN_STEPS] = {0, 0, 0, 0, 0}, fail[LEVIN_STEPS] = {0, 0, 3, 0, 0},
            skip[LEVIN_STEPS] = {0, 0, 0, MOCK_NOT_RUN, 0},
            nonsense[LEVIN_STEPS] = {0, 0, 0, 0, 256};
    bool ok = false;
    uint32_t at = 0;
    CHECK(levin_test_run(&LV2, 3, pass, &ok, &at) == ZXM_OK && ok && at == LEVIN_STEPS &&
              LV2.tested[0] == 1 && strstr(LV2.notice, "passed"),
          "levin: every step 0 in order: passed");
    CHECK(levin_test_run(&LV2, 3, fail, &ok, &at) == ZXM_OK && !ok && at == 2 &&
              LV2.tested[0] == 0 && strstr(LV2.notice, "polarity") &&
              strstr(LV2.notice, "do not energise"),
          "levin: polarity failed: %s", LV2.notice);
    CHECK(levin_test_run(&LV2, 3, skip, &ok, &at) == ZXM_OK && !ok && at == 3 &&
              levin_test_run(&LV2, 3, nonsense, &ok, &at) == ZXM_E_ARG &&
              levin_test_run(&LV2, 8, pass, &ok, &at) == ZXM_E_ARG,
          "levin: a step that never ran fails; a code past 255 or no circuit refused");

    /* LM8: the panel's temperature, Brazier's limits */
    CHECK(levin_panel_heat(&LV2, 250) == BRAZIER_HEAT_OK &&
              levin_panel_heat(&LV2, 500) == BRAZIER_HEAT_WARN && strstr(LV2.notice, "looked at") &&
              levin_panel_heat(&LV2, 700) == BRAZIER_HEAT_CRITICAL &&
              strstr(LV2.notice, "critical"),
          "levin: 25.0 C ok, 50.0 C a warning, 70.0 C critical");

    /* LM9: energy, a device's command shown, permits, the machine's draw */
    int64_t wh = 0;
    CHECK(levin_energy(&LV2, 3, 1200) == ZXM_OK && levin_energy(&LV2, 4, 700) == ZXM_OK &&
              levin_energy(&LV2, 3, 800) == ZXM_OK && levin_energy(&LV2, 3, -1) == ZXM_E_ARG &&
              levin_circuit_energy(&LV2, 3, &wh) == ZXM_OK && wh == 2000 &&
              levin_circuit_energy(&LV2, 5, &wh) == ZXM_OK && wh == 0,
          "levin: circuit 3's energy (homestead.device_energy)");
    CHECK(levin_device_cmd_shown(HOMESTEAD_PLUG, 0, 1, 0, 0, 0) &&
              !levin_device_cmd_shown(HOMESTEAD_HEATER, 50, 250, 50, 1, 0) &&
              levin_device_cmd_shown(HOMESTEAD_HEATER, 50, 250, 50, 1, 1) &&
              !levin_device_cmd_shown(HOMESTEAD_PLUG, 0, 1, 2, 0, 0),
          "levin: a plug off shown allowed, a heater only with the owner's approval");
    int64_t pb[2] = {15000, 9999999}, pi[2] = {2, 0}, pv[2] = {7500, 0}, tot = 0;
    CHECK(levin_permits(pb, pi, pv, 2, &tot) == ZXM_OK && tot == 10029999, "levin: two permits");
    pi[1] = 99, pv[1] = LEVIN_VISIT_MAX;
    CHECK(levin_permits(pb, pi, pv, 2, &tot) == ZXM_E_ARG && tot == 10029999,
          "levin: a permit past 9(8) refused, the total as it was");
    CHECK(levin_refresh_draw(&LV2) == ZXM_E_REMOTE &&
              (LINK.err_code == -32601 || LINK.err_code == -32095) && LV2.draw_mw == -1,
          "levin: the real zxvd: power.status refused (%d), nothing kept", LINK.err_code);
    zxm_link_init(&LV_LINK, lv_send, &LV_LINK);
    LV2.link = &LV_LINK;
    lv_sent = 0;
    CHECK(levin_refresh_draw(&LV2) == ZXM_OK && LV2.draw_mw == 14090 && lv_sent == 1,
          "levin: the machine's own draw, 14.09 W: %s", LV_LINK.err);
    LV2.link = &LINK;

    /* a full plan: every list at its limit is still one file that reads back */
    CHECK(zxf_write(LEVIN_FILE, (const uint8_t *) "levin 1\n", 8, ZXF_W_REPLACE, false, &size) ==
                  0 &&
              levin_load(&LV) == ZXM_OK && LV.nc == 0,
          "levin: an empty plan");
    bool all = true;
    for (int64_t id = 1; all && id <= (int64_t) LEVIN_CIRCUITS; id++) {
        levin_circuit_t c = {LEVIN_CIRCUIT_MAX - 32 + id,
                             id > 1 ? LEVIN_CIRCUIT_MAX - 32 + 1 : 0,
                             LEVIN_BREAKER_MAX,
                             LEVIN_CABLE_MAX,
                             LEVIN_VOLTS_MAX,
                             LEVIN_METRES_MAX,
                             LEVIN_MOHM_MAX,
                             LEVIN_SLACK_MAX};
        all = levin_add_circuit(&LV, &c, true) == ZXM_OK;
    }
    static char longest[LEVIN_NAME_MAX + 1];
    memset(longest, 'n', LEVIN_NAME_MAX);
    for (uint32_t i = 0; all && i < LEVIN_LOADS; i++)
        all = levin_add_load(&LV, LV.c[i % LEVIN_CIRCUITS].id, LEVIN_ROOM_MAX, LEVIN_WATTS_MAX / 64,
                             longest, true) == ZXM_OK;
    levin_circuit_t extra = {1, 0, 1, 1, 1, 0, 0, 0};
    CHECK(all && LV.nc == LEVIN_CIRCUITS && LV.nld == LEVIN_LOADS &&
              levin_add_circuit(&LV, &extra, true) == ZXM_E_STATE &&
              levin_add_load(&LV, LV.c[0].id, 1, 1, "x", true) == ZXM_E_STATE,
          "levin: a full plan: %s", LINK.err);
    levin_init(&LV2, &LINK);
    CHECK(levin_load(&LV2) == ZXM_OK && LV2.nc == LEVIN_CIRCUITS && LV2.nld == LEVIN_LOADS &&
              levin_check(&LV2, LV2.c[0].id, &r) == ZXM_OK,
          "levin: the full plan reads back and checks");
}
