/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Understory's model against zxvd's real dispatcher (LINK), which does not
 * link the power group (zxvd_power.c), so power.status, power.top and
 * power.budget are refused there; then against a power ledger of its own
 * (US_LINK) that answers them, records what it was sent and can answer
 * badly. Water and land have no zxvd method: the owner enters them. */
#include "understory_model.h"

static zxm_link_t US_LINK;
static char us_sent[8][200]; /* the params of each call, in order */
static uint32_t us_nsent;
static int64_t us_uj = 7200000000; /* power.status total_uj: 2 kWh */
static bool us_bad_top;            /* power.top answers a negative energy */
static bool us_refuse_budget;      /* power.budget refused (feature off) */

static int32_t us_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *p = strstr(req, "\"params\":");
    uint32_t i = us_nsent++;
    (void) len;
    if (i < 8) snprintf(us_sent[i], sizeof us_sent[i], "%s", p ? p + 9 : "");
    if (strstr(req, "\"power.status\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"device\":\"laptop\","
                        "\"host\":\"linux\",\"total_uj\":%lld,\"total_mw\":4200,\"source\":"
                        "\"metered\",\"posture\":\"balanced\",\"token_scale_permille\":618,"
                        "\"battery\":null,\"meters\":[],\"consumers\":[]}}",
                        l->seq, (long long) us_uj);
    if (strstr(req, "\"power.top\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":[{\"id\":3,\"kind\":\"app\","
                        "\"name\":\"quillon\",\"parent\":null,\"uj\":%s,\"mw\":900,\"source\":"
                        "\"attributed\",\"lifetime_uj\":9000000,\"cpu_ns\":5},{\"id\":4,\"kind\":"
                        "\"app\",\"name\":\"glint\",\"parent\":null,\"uj\":1000,\"mw\":10,"
                        "\"source\":\"attributed\",\"lifetime_uj\":2000,\"cpu_ns\":1}]}",
                        l->seq, us_bad_top ? "-5" : "5000000");
    if (strstr(req, "\"power.budget\"") && !us_refuse_budget)
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true}}",
                        l->seq);
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32601,\"message\":"
                    "\"feature power is off\"}}",
                    l->seq);
}

APPS_TEST(understory)
{
    static understory_t U;
    understory_report_t r;

    /* the rules the model runs */
    int64_t out[4];
    static const int64_t am[4] = {5, 10, 20, 40};
    CHECK(understory_energy_cost(3600000000, 25) == 25 && understory_energy_cost(0, 999999) == 0 &&
              understory_energy_cost(1000000000000, 1) < 0 && understory_over_budget(1) &&
              !understory_over_budget(0) && !understory_over_budget(INT64_C(1000000000000000000)) &&
              understory_rolling(am, 4, 2, out) == 0 && out[0] == 5 && out[1] == 15 &&
              out[2] == 30 && out[3] == 60,
          "understory: cost, over budget and rolling totals at their edges (UR1-UR3)");

    /* UM1 on the real dispatcher: the power group is not linked here */
    understory_init(&U, &LINK);
    CHECK(understory_refresh(&U) == ZXM_E_REMOTE && !U.have && U.nsamples == 0,
          "understory: power.status refused by this zxvd (%d), nothing kept", LINK.err_code);

    /* UM1 on a ledger of its own */
    zxm_link_init(&US_LINK, us_send, &US_LINK);
    U.link = &US_LINK;
    us_nsent = 0;
    CHECK(understory_refresh(&U) == ZXM_OK && U.have && U.total_uj == 7200000000 &&
              U.total_mw == 4200 && !strcmp(U.source, "metered") &&
              !strcmp(U.posture, "balanced") && U.napps == 2 && !strcmp(U.app[0].name, "quillon") &&
              U.app[0].uj == 5000000 && U.app[1].mw == 10 && U.nsamples == 1 &&
              U.sample_mj[0] == 7200000 && us_nsent == 2 &&
              strstr(us_sent[1], "\"kind\":\"app\"") && strstr(us_sent[1], "\"n\":16"),
          "understory: refresh reads the ledger and the apps: %s", US_LINK.err);
    us_bad_top = true;
    us_uj = 1;
    CHECK(understory_refresh(&U) == ZXM_E_PROTOCOL && U.total_uj == 7200000000 && U.nsamples == 1,
          "understory: a negative energy from power.top changes nothing");
    us_bad_top = false;
    us_uj = 3600000000; /* 1 kWh */
    CHECK(understory_refresh(&U) == ZXM_OK && U.nsamples == 2 && U.sample_mj[1] == 3600000,
          "understory: a second reading");

    /* UM2: the last two readings, 3 kWh, at 25 per kWh, against 2 kWh */
    CHECK(understory_energy(&U, 2, 25, 10800000, &r) == ZXM_OK && r.total_mj == 10800000 &&
              r.cost == 0 && !r.over && r.used_x100 == 10000,
          "understory: 3 kWh at 25 per kWh floors to 0; all of its budget, not past it (%lld)",
          (long long) r.used_x100);
    CHECK(understory_energy(&U, 2, 250000, 7200000, &r) == ZXM_OK && r.cost == 750 && r.over &&
              r.used_x100 == 15000 && strstr(U.notice, "passed"),
          "understory: 3 kWh at 250000 per kWh is 750, 150%% of a 2 kWh budget: %s", U.notice);
    CHECK(understory_energy(&U, 1, 25, 3600000, &r) == ZXM_OK && r.total_mj == 3600000 && !r.over &&
              r.used_x100 == 10000 && strstr(U.notice, "100.00%"),
          "understory: exactly the budget is inside it: %s", U.notice);
    CHECK(understory_energy(&U, 3, 25, 1000, &r) == ZXM_E_ARG &&
              understory_energy(&U, 0, 25, 1000, &r) == ZXM_E_ARG &&
              understory_energy(&U, 1, -1, 1000, &r) == ZXM_E_ARG &&
              understory_energy(&U, 1, 25, 0, &r) == ZXM_E_ARG,
          "understory: more readings than kept, none, a negative tariff or no budget refused");
    for (uint32_t i = 0; i < UNDERSTORY_SAMPLES + 3u; i++) understory_refresh(&U);
    CHECK(U.nsamples == UNDERSTORY_SAMPLES && U.sample_mj[UNDERSTORY_SAMPLES - 1u] == 3600000,
          "understory: the readings stay bounded, the oldest dropped");

    /* UM3: proposed and held, sent only with the owner's word */
    us_nsent = 0;
    CHECK(understory_propose(&U, "quillon", 60000, 500, 1000) == ZXM_OK && U.held.pending &&
              us_nsent == 0 && strstr(U.notice, "confirm"),
          "understory: a budget for one app is held, nothing sent");
    CHECK(understory_propose(&U, NULL, 60000, 1000, 1000) == ZXM_E_STATE,
          "understory: one budget at a time");
    CHECK(understory_apply(&U, false) == ZXM_E_STATE && us_nsent == 0 && U.held.pending,
          "understory: without the owner's word nothing is sent");
    us_refuse_budget = true;
    CHECK(understory_apply(&U, true) == ZXM_E_REMOTE && U.held.pending &&
              strstr(U.notice, "refused"),
          "understory: power.budget refused (feature off): still held");
    us_refuse_budget = false;
    us_nsent = 0;
    CHECK(understory_apply(&U, true) == ZXM_OK && !U.held.pending && us_nsent == 1 &&
              !strcmp(us_sent[0], "{\"target\":\"app\",\"app\":\"quillon\",\"window_ms\":60000,"
                                  "\"max_uj\":500000,\"strict\":false}}"),
          "understory: the app's budget in microjoules, never strict: %s", us_sent[0]);
    CHECK(understory_apply(&U, true) == ZXM_E_STATE, "understory: the word is spent by one apply");
    CHECK(understory_propose(&U, NULL, 60000, 1000, 1000) == ZXM_OK, "understory: the device's");
    us_nsent = 0;
    CHECK(understory_apply(&U, true) == ZXM_OK &&
              !strcmp(us_sent[0], "{\"target\":\"device\",\"window_ms\":60000,\"max_uj\":1000000,"
                                  "\"strict\":false}}"),
          "understory: the device's budget: %s", us_sent[0]);
    CHECK(understory_propose(&U, "quillon", 60000, 1001, 1000) == ZXM_E_ARG &&
              understory_propose(&U, "quillon", 0, 500, 1000) == ZXM_E_ARG &&
              understory_propose(&U, "has space", 60000, 500, 1000) == ZXM_E_ARG &&
              understory_propose(&U, NULL, 60000, 500, 1000) == ZXM_E_ARG &&
              understory_propose(&U, NULL, 60000, 1000000000, 1000000000) == ZXM_E_ARG &&
              !U.held.pending,
          "understory: past the device's whole, no window, a bad name or a device budget that "
          "is not its whole refused (Keystone's KS2)");
    CHECK(understory_propose(&U, "glint", 1000, 5, 9) == ZXM_OK, "understory: proposed");
    understory_cancel(&U);
    CHECK(!U.held.pending && strstr(U.notice, "dropped"), "understory: cancelled");

    /* UM4: water and land, entered by the owner */
    int64_t litres = 0, area = 0;
    bool over = true;
    CHECK(understory_add_water(&U, 20000, UNDERSTORY_MAINS, 150) == ZXM_OK &&
              understory_add_water(&U, 20001, UNDERSTORY_RAIN, 80) == ZXM_OK &&
              understory_add_water(&U, 20005, UNDERSTORY_WELL, 40) == ZXM_OK &&
              understory_add_water(&U, 20001, 5, 1) == ZXM_E_ARG &&
              understory_add_water(&U, -1, 1, 1) == ZXM_E_ARG &&
              understory_add_water(&U, 1, 1, 1000000000) == ZXM_E_ARG && U.nwater == 3,
          "understory: water readings checked as they are entered");
    CHECK(understory_water(&U, 20000, 20001, 230, &litres, &over) == ZXM_OK && litres == 230 &&
              !over && understory_water(&U, 20000, 20005, 230, &litres, &over) == ZXM_OK &&
              litres == 270 && over && strstr(U.notice, "passed") &&
              understory_water(&U, 5, 4, 0, &litres, &over) == ZXM_E_ARG,
          "understory: water of days, against a budget");
    CHECK(understory_add_parcel(&U, 1, UNDERSTORY_GARDEN, 400) == ZXM_OK &&
              understory_add_parcel(&U, 2, UNDERSTORY_WILD, 2500) == ZXM_OK &&
              understory_add_parcel(&U, 3, UNDERSTORY_GARDEN, 100) == ZXM_OK &&
              understory_add_parcel(&U, 2, UNDERSTORY_CROP, 10) == ZXM_E_ARG &&
              understory_add_parcel(&U, 0, UNDERSTORY_CROP, 10) == ZXM_E_ARG &&
              understory_add_parcel(&U, 4, 6, 10) == ZXM_E_ARG && U.nland == 3,
          "understory: parcels checked, one record each");
    CHECK(understory_land(&U, UNDERSTORY_GARDEN, 500, &area, &over) == ZXM_OK && area == 500 &&
              !over && understory_land(&U, 0, 2999, &area, &over) == ZXM_OK && area == 3000 &&
              over && understory_land(&U, 6, 0, &area, &over) == ZXM_E_ARG,
          "understory: land of one kind and of all, against a budget");
    for (uint32_t i = U.nwater; i < UNDERSTORY_WATERS; i++)
        understory_add_water(&U, 30000, UNDERSTORY_MAINS, 1);
    CHECK(U.nwater == UNDERSTORY_WATERS &&
              understory_add_water(&U, 30000, UNDERSTORY_MAINS, 1) == ZXM_E_STATE,
          "understory: a full water table refuses the next reading");
    U.link = &LINK;
}
