/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Retort's model against zxvd's real dispatcher (LINK): model.info and
 * swarm.ask as zxvd answers them. power.status, play.run and aipi.execute
 * are not linked in this in-process zxvd, so they are answered on a link
 * of its own (RTA_LINK), which passes every other request to the real
 * dispatcher. The clock is the test's: each read moves it RTA_STEP ms. */
#include "retort_model.h"

static retort_t RTA;
static zxm_link_t RTA_LINK;
static const char *RTA_POWER; /* power.status's result, when set */
static const char *RTA_PLAY;  /* play.run's result, when set */
static const char *RTA_AIPI;  /* aipi.execute's result, when set */
static int64_t RTA_NOW, RTA_STEP;

static int32_t rta_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"power.status\"")) r = RTA_POWER;
    if (strstr(req, "\"play.run\"")) r = RTA_PLAY;
    if (strstr(req, "\"aipi.execute\"")) r = RTA_AIPI;
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

static int64_t rta_clock(void *ctx)
{
    (void) ctx;
    int64_t now = RTA_NOW;
    RTA_NOW += RTA_STEP;
    return now;
}

APPS_TEST(retort)
{
    static const char *CID = "zQmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG";
    retort_standing_t s[RETORT_CONTESTANTS];
    int before;

    /* RT1-RT6 through the model's own C */
    {
        int64_t m[4] = {0, 1, 0, 2}, sc[4] = {100, 40, 60, 0}, bad[2] = {0, 101}, n = 0, d = 0;
        CHECK(retort_model_score_total(m, 0, sc, 4) == 160 &&
                  retort_model_score_total(m, 2, sc, 4) == 0 &&
                  retort_model_score_total(m, 0, bad, 2) == RETORT_REFUSED &&
                  retort_latency_fastest(RETORT_LATENCY_MAX, 120) == 120 &&
                  retort_latency_slowest(0, 120) == 120 &&
                  retort_latency_fastest(-1, 0) == RETORT_REFUSED &&
                  retort_run_limits_ok(4095, 1, 3600) && !retort_run_limits_ok(4096, 1, 1) &&
                  retort_energy_per_answer(12, 8, &n, &d) && n == 3 && d == 2 &&
                  retort_run_record_fits(0, 7, 2, 12, 5, 120, -1, 3) &&
                  !retort_run_record_fits(0, 7, 2, 12, 5, 10000000, 0, 3) &&
                  retort_share_x100(160, 200) == 8000,
              "retort: the rules (RT1-RT6)");
    }

    /* RM1: the slot as the real dispatcher answers it */
    retort_init(&RTA, &LINK, rta_clock, NULL);
    CHECK(retort_add(&RTA, RETORT_KIND_ASK, NULL, 0) == ZXM_E_STATE && RTA.ncontestants == 0,
          "retort: the slot's model is no contestant before the slot is read");
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\",\"arch\":\"llama\","
            "\"n_layers\":22,\"n_vocab\":32000,\"status\":\"ready\"}";
    CHECK(retort_read_slot(&RTA) == ZXM_OK && RTA.known && RTA.can_generate &&
              !strcmp(RTA.name, "tinyllama") && !strcmp(RTA.arch, "llama") && RTA.layers == 22 &&
              RTA.vocab == 32000 && strstr(RTA.notice, "tinyllama"),
          "retort: model.info: %s (%s)", RTA.notice, LINK.err);
    MODEL = "{\"loaded\":false,\"name\":\"\"}";
    retort_init(&RTA, &LINK, rta_clock, NULL);
    CHECK(retort_read_slot(&RTA) == ZXM_OK && RTA.known && !RTA.loaded && !RTA.can_generate &&
              strstr(RTA.notice, "shorthand"),
          "retort: an empty slot: %s", RTA.notice);
    MODEL = "{\"name\":\"x\"}";
    retort_init(&RTA, &LINK, rta_clock, NULL);
    CHECK(retort_read_slot(&RTA) == ZXM_E_PROTOCOL && !RTA.known,
          "retort: a slot without loaded is not zxvd's answer");
    MODEL = "{\"loaded\":true,\"name\":\"x\",\"n_layers\":-2}";
    CHECK(retort_read_slot(&RTA) == ZXM_E_PROTOCOL && !RTA.known,
          "retort: negative layers are not zxvd's answer");

    /* RM2: limits */
    retort_init(&RTA, &RTA_LINK, rta_clock, NULL);
    zxm_link_init(&RTA_LINK, rta_send, &RTA_LINK);
    CHECK(RTA.lim_prompt == RETORT_DEFAULT_PROMPT && RTA.lim_fuel == RETORT_DEFAULT_FUEL &&
              RTA.lim_seconds == RETORT_DEFAULT_SECONDS,
          "retort: the default limits");
    CHECK(retort_set_limits(&RTA, 0, 1, 1) == ZXM_E_ARG &&
              retort_set_limits(&RTA, 4096, 1, 1) == ZXM_E_ARG &&
              retort_set_limits(&RTA, 10, 0, 1) == ZXM_E_ARG &&
              retort_set_limits(&RTA, 10, 1, 3601) == ZXM_E_ARG &&
              RTA.lim_prompt == RETORT_DEFAULT_PROMPT,
          "retort: limits outside RT3 are refused, the old ones stay");
    CHECK(retort_set_limits(&RTA, 64, 5000, 2) == ZXM_OK && RTA.lim_prompt == 64 &&
              RTA.lim_fuel == 5000 && RTA.lim_seconds == 2,
          "retort: 64 bytes, 5000 fuel, 2 seconds: %s", RTA.notice);

    /* RM3: contestants */
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\",\"arch\":\"llama\","
            "\"n_layers\":22,\"n_vocab\":32000}";
    CHECK(retort_read_slot(&RTA) == ZXM_OK, "retort: the slot through its own link");
    CHECK(retort_add(&RTA, RETORT_KIND_ASK, NULL, 0) == 0 &&
              retort_add(&RTA, RETORT_KIND_ASK, NULL, 0) == ZXM_E_STATE &&
              !strcmp(RTA.contestant[0].name, "tinyllama"),
          "retort: the slot's model once");
    CHECK(retort_add(&RTA, RETORT_KIND_PLAY, "shares", 0) == 1 &&
              retort_add(&RTA, RETORT_KIND_AIPI, CID, 4000) == 2 &&
              retort_add(&RTA, RETORT_KIND_AIPI, CID, 6000) == 3,
          "retort: a template and two functions");
    CHECK(retort_add(&RTA, RETORT_KIND_PLAY, "", 0) == ZXM_E_ARG &&
              retort_add(&RTA, RETORT_KIND_PLAY, "x", 5) == ZXM_E_ARG &&
              retort_add(&RTA, RETORT_KIND_AIPI, "Qm0OIl", 5) == ZXM_E_ARG &&
              retort_add(&RTA, RETORT_KIND_AIPI, "z0OIl", 5) == ZXM_E_ARG &&
              retort_add(&RTA, RETORT_KIND_AIPI, CID, 0) == ZXM_E_ARG &&
              retort_add(&RTA, 7, "x", 0) == ZXM_E_ARG && RTA.ncontestants == 4,
          "retort: contestants that are none");

    /* RM4: runs, refused before anything is asked */
    RTA_NOW = 1000, RTA_STEP = 250;
    RTA_POWER = "{\"device\":\"laptop\",\"total_uj\":9000,\"total_mw\":36}";
    before = sent;
    CHECK(retort_ask(&RTA, 0, "") == ZXM_E_ARG &&
              retort_ask(&RTA, 0,
                         "a prompt much longer than the sixty-four bytes the owner "
                         "allows for one run") == ZXM_E_ARG &&
              retort_ask(&RTA, 1, "hi") == ZXM_E_ARG && retort_play(&RTA, 0) == ZXM_E_ARG &&
              retort_execute(&RTA, 3, (const int64_t[]){1}, 1, 2) == ZXM_E_ARG &&
              retort_execute(&RTA, 2, NULL, 1, 2) == ZXM_E_ARG && sent == before && RTA.nruns == 0,
          "retort: an empty or long prompt, a wrong kind, fuel past its limit: nothing asked");

    /* an asked run: timed, its energy read, waiting for the owner's grade */
    ANSWER = "Four.";
    CHECK(retort_ask(&RTA, 0, "What is two and two?") == ZXM_OK && RTA.nruns == 1 &&
              RTA.run[0].ms == 250 && RTA.run[0].uj == 9000 && RTA.run[0].prompt == 20 &&
              RTA.run[0].score == RETORT_UNGRADED && !strcmp(RTA.answer, "Four.") &&
              !strcmp(last_prompt, "What is two and two?") && strstr(RTA.notice, "waits"),
          "retort: asked: %s (%s)", RTA.notice, RTA_LINK.err);
    CHECK(retort_grade(&RTA, 0, 101) == ZXM_E_ARG && retort_grade(&RTA, 0, 90) == ZXM_OK &&
              retort_grade(&RTA, 0, 80) == ZXM_E_STATE && RTA.run[0].score == 90 &&
              retort_grade(&RTA, 9, 50) == ZXM_E_STATE,
          "retort: graded once by the owner: %s", RTA.notice);

    /* a template: its verdict grades it */
    RTA_STEP = 100;
    RTA_PLAY = "{\"template\":\"shares\",\"verdict\":\"TRUE\",\"headline\":\"ok\"}";
    CHECK(retort_play(&RTA, 1) == ZXM_OK && RTA.run[1].score == 100 && RTA.run[1].ms == 100 &&
              RTA.run[1].prompt == 6,
          "retort: a template that passed: %s", RTA.notice);
    RTA_PLAY = "{\"template\":\"shares\",\"verdict\":\"BOTH\"}";
    CHECK(retort_play(&RTA, 1) == ZXM_OK && RTA.run[2].score == RETORT_UNGRADED,
          "retort: a template held for a person waits ungraded");
    RTA_PLAY = "{\"template\":\"shares\",\"verdict\":\"MAYBE\"}";
    before = (int) RTA.nruns;
    CHECK(retort_play(&RTA, 1) == ZXM_E_PROTOCOL && RTA.nruns == (uint32_t) before,
          "retort: a verdict that is none: not kept");

    /* a function: its result against the expected value */
    RTA_AIPI = "{\"function\":\"zQm\",\"call\":\"zQm\",\"route\":\"local\",\"result\":\"42\"}";
    CHECK(retort_execute(&RTA, 2, (const int64_t[]){6, 7}, 2, 42) == ZXM_OK &&
              RTA.run[3].score == 100 && RTA.run[3].fuel == 4000 && RTA.run[3].prompt == 5,
          "retort: 6 x 7 is 42: %s", RTA.notice);
    CHECK(retort_execute(&RTA, 2, (const int64_t[]){6, 7}, 2, 41) == ZXM_OK &&
              RTA.run[4].score == 0,
          "retort: a result that is not the expected one scores 0");
    RTA_AIPI = "{\"function\":\"zQm\",\"result\":null}";
    CHECK(retort_execute(&RTA, 2, NULL, 0, 0) == ZXM_OK && RTA.run[5].score == 0,
          "retort: no result scores 0");
    RTA_AIPI = "{\"function\":\"zQm\",\"result\":\"4x\"}";
    CHECK(retort_execute(&RTA, 2, NULL, 0, 0) == ZXM_E_PROTOCOL && RTA.nruns == 6,
          "retort: a result that is no integer: not kept");

    /* past its seconds: kept as over time, graded 0, out of the latency fold */
    RTA_STEP = 2001;
    RTA_PLAY = "{\"template\":\"shares\",\"verdict\":\"TRUE\"}";
    CHECK(retort_play(&RTA, 1) == ZXM_OK && RTA.run[6].over && RTA.run[6].score == 0 &&
              RTA.run[6].ms == 2001 && strstr(RTA.notice, "past 2 seconds"),
          "retort: over time: %s", RTA.notice);

    /* a clock that goes back counts 0 ms; an energy reading that is none keeps nothing */
    RTA_STEP = -50;
    CHECK(retort_play(&RTA, 1) == ZXM_OK && RTA.run[7].ms == 0 && !RTA.run[7].over,
          "retort: a clock that went back: 0 ms");
    RTA_STEP = 10;
    RTA_POWER = "{\"total_uj\":-1}";
    CHECK(retort_play(&RTA, 1) == ZXM_E_PROTOCOL && RTA.nruns == 8,
          "retort: negative energy is no reading: not kept");
    RTA_POWER = NULL;
    CHECK(retort_play(&RTA, 1) == ZXM_E_REMOTE && RTA.nruns == 8,
          "retort: no power ledger in this harness: not kept (%s)", RTA_LINK.err);
    RTA_POWER = "{\"total_uj\":3001}";

    /* RM6: the comparison */
    CHECK(retort_compare(&RTA, s, 2) == ZXM_E_ARG, "retort: room for every contestant");
    CHECK(retort_compare(&RTA, s, RETORT_CONTESTANTS) == ZXM_OK, "retort: compared");
    CHECK(s[0].total == 90 && s[0].runs == 1 && s[0].graded == 1 && s[0].fastest == 250 &&
              s[0].slowest == 250 && s[0].uj_num == 9000 && s[0].uj_den == 1,
          "retort: the slot's model: 90 points in 250 ms");
    /* the template: 100 (passed), held (ungraded), 0 (over time), 100 (0 ms) */
    CHECK(s[1].total == 200 && s[1].runs == 4 && s[1].graded == 3 && s[1].fastest == 0 &&
              s[1].slowest == 100 && s[1].uj_num == 9000 && s[1].uj_den == 1,
          "retort: the template: %lld points, %lld..%lld ms", (long long) s[1].total,
          (long long) s[1].fastest, (long long) s[1].slowest);
    CHECK(s[2].total == 100 && s[2].runs == 3 && s[2].graded == 3 && s[2].fastest == 100 &&
              s[2].slowest == 100 && s[2].uj_num == 9000 && s[2].uj_den == 1,
          "retort: the function: one right of three");
    CHECK(s[3].total == 0 && s[3].runs == 0 && s[3].fastest == -1 && s[3].slowest == -1 &&
              s[3].uj_num == 0 && s[3].uj_den == 0,
          "retort: a contestant that never ran");
    CHECK(s[0].share_x100 == 2307 && s[1].share_x100 == 5128 && s[2].share_x100 == 2564 &&
              s[3].share_x100 == 0 && strstr(RTA.notice, "shares leads with 200 of 390 points"),
          "retort: shares of 390 points: %s", RTA.notice);
    /* energy per answer as a fraction: one more template run at 3001 uJ */
    CHECK(retort_play(&RTA, 1) == ZXM_OK && retort_compare(&RTA, s, RETORT_CONTESTANTS) == ZXM_OK &&
              s[1].uj_num == 39001 && s[1].uj_den == 5,
          "retort: 39001 uJ over 5 answers is 39001/5: %lld/%lld", (long long) s[1].uj_num,
          (long long) s[1].uj_den);

    /* RT5: every kept run fits ^ZXRETORT */
    bool fit = true;
    for (uint32_t i = 0; i < RTA.nruns; i++) fit = fit && retort_record_ok(&RTA, i);
    CHECK(fit && !retort_record_ok(&RTA, RTA.nruns) && !retort_record_ok(NULL, 0),
          "retort: every run fits ^ZXRETORT");

    /* the run list is bounded: past RETORT_RUNS nothing is asked */
    while (RTA.nruns < RETORT_RUNS && retort_play(&RTA, 1) == ZXM_OK) {
    }
    before = sent;
    CHECK(RTA.nruns == RETORT_RUNS && retort_play(&RTA, 1) == ZXM_E_STATE && sent == before,
          "retort: %u runs kept, then nothing asked", RTA.nruns);

    /* a full contestant list */
    retort_init(&RTA, &RTA_LINK, NULL, NULL);
    for (uint32_t i = 0; i < RETORT_CONTESTANTS; i++)
        (void) retort_add(&RTA, RETORT_KIND_PLAY, "t", 0);
    CHECK(RTA.ncontestants == RETORT_CONTESTANTS &&
              retort_add(&RTA, RETORT_KIND_PLAY, "t", 0) == ZXM_E_STATE &&
              retort_play(&RTA, 0) == ZXM_E_STATE,
          "retort: eight contestants at most; no clock, no run");

    RTA_POWER = RTA_PLAY = RTA_AIPI = NULL;
    MODEL = "{}";
    ANSWER = "ok";
}
