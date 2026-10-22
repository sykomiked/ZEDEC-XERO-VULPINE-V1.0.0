/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Whetstone's model against zxvd's real dispatcher (LINK): files.read,
 * files.write and sys.info as zxvd answers them, and play.run refused
 * there, since the Playground is not linked in this in-process zxvd.
 * play.run is answered on a link of its own (WHT_LINK), which passes every
 * other request to the real dispatcher and counts what it is sent; with
 * WHT_FAKE_CLOCK set it also answers sys.info from the test's clock, which
 * moves WHT_STEP ms at each read. */
#include "whetstone_model.h"

static whetstone_t WHT;
static zxm_link_t WHT_LINK;
static const char *WHT_RUN[4]; /* play.run results, one per try; the last repeats */
static uint32_t WHT_TRY;       /* play.run requests answered */
static uint32_t WHT_SENT;      /* play.run requests sent */
static char WHT_REQ[ZXM_REQ_MAX + 1];
static bool WHT_FAKE_CLOCK;
static int64_t WHT_NOW, WHT_STEP;

static int32_t wht_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"method\":\"play.run\"")) {
        WHT_SENT++;
        memcpy(WHT_REQ, req, len);
        WHT_REQ[len] = 0;
        const char *r = WHT_RUN[WHT_TRY < 3u ? WHT_TRY : 3u];
        WHT_TRY++;
        if (!r) r = WHT_RUN[0];
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    }
    if (WHT_FAKE_CLOCK && strstr(req, "\"method\":\"sys.info\"")) {
        int64_t now = WHT_NOW;
        WHT_NOW += WHT_STEP;
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":%lld}}",
                        l->seq, (long long) now);
    }
    return inproc(NULL, req, len, resp, cap);
}

static bool wht_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static void wht_answers(const char *a, const char *b, const char *c, const char *d)
{
    WHT_RUN[0] = a, WHT_RUN[1] = b, WHT_RUN[2] = c, WHT_RUN[3] = d;
    WHT_TRY = 0;
}

#define WHT_RESULT(v, s1, s2, s3)                                                                  \
    "{\"template\":\"t\",\"verdict\":\"" v "\",\"headline\":\"h\",\"approval\":\"not required\","  \
    "\"safety\":\"advisory\",\"steps\":["                                                          \
    "{\"name\":\"a\",\"lang\":\"sutra\",\"file\":\"a.sutra\",\"verdict\":\"" s1                    \
    "\",\"ran\":true},"                                                                            \
    "{\"name\":\"b\",\"lang\":\"kron\",\"file\":\"b.kron\",\"verdict\":\"" s2 "\",\"ran\":true},"  \
    "{\"name\":\"c\",\"lang\":\"eve\",\"file\":\"c.eve\",\"verdict\":\"" s3 "\",\"ran\":true}]}"

APPS_TEST(whetstone)
{
    whetstone_summary_t s;
    uint32_t before;

    /* WR1-WR8 through the model's own C */
    {
        static const int64_t v[3] = {LPRES_STATE_TRUE, LPRES_STATE_BOTH, LPRES_STATE_TRUE};
        static const int64_t ms[3] = {10, 20, 30};
        CHECK(whetstone_suite_verdict(v, 3) == LPRES_STATE_BOTH &&
                  whetstone_suite_passed(3, 0, 0, 0) && !whetstone_suite_passed(3, 0, 1, 0) &&
                  whetstone_results_total(1, 2, 3) == 6 &&
                  whetstone_results_total(999999999, 1, 0) == WHETSTONE_REFUSED &&
                  whetstone_ms_total(ms, 3) == 60 && whetstone_retry_ok(3, 3600) &&
                  !whetstone_retry_ok(4, 1) && whetstone_record_fits(1, 2, 1, 3, 0, 0, 99) &&
                  !whetstone_record_fits(1, 2, 1, 3, 0, 0, 100000000) &&
                  whetstone_pass_x100(2, 3) == 6666 && whetstone_kind_count(v, 3, 1) == 2,
              "whetstone: the rules (WR1-WR8)");
    }

    /* WM1: a plan read from the Hoard with the real files.read */
    CHECK(wht_write("whetstone-plan.txt", "# the nightly targets\n"
                                          "account-statement\n"
                                          "\n"
                                          "pump-controller 2 5\n"
                                          "  ledger-check\t1 3600\n") &&
              wht_write("whetstone-bad.txt", "account-statement 4 60\n") &&
              wht_write("whetstone-half.txt", "account-statement 2\n") &&
              wht_write("whetstone-path.txt", "a/b\n"),
          "whetstone: plans written: %s", LINK.err);
    zxm_link_init(&WHT_LINK, wht_send, &WHT_LINK);
    whetstone_init(&WHT, &WHT_LINK);
    CHECK(whetstone_run(&WHT, 0) == ZXM_E_ARG && WHT_SENT == 0 && WHT.nrecords == 0,
          "whetstone: nothing runs before a plan");
    CHECK(whetstone_load_plan(&WHT, "", "whetstone-plan.txt") == ZXM_OK && WHT.planned &&
              WHT.ntargets == 3 && !strcmp(WHT.target[0].id, "account-statement") &&
              WHT.target[0].retries == WHETSTONE_DEFAULT_RETRIES &&
              WHT.target[0].timeout_s == WHETSTONE_DEFAULT_TIMEOUT &&
              !strcmp(WHT.target[1].id, "pump-controller") && WHT.target[1].retries == 2 &&
              WHT.target[1].timeout_s == 5 && !strcmp(WHT.target[2].id, "ledger-check") &&
              WHT.target[2].retries == 1 && WHT.target[2].timeout_s == 3600,
          "whetstone: the plan: %s (%s)", WHT.notice, WHT_LINK.err);
    CHECK(whetstone_load_plan(&WHT, "", "whetstone-bad.txt") == ZXM_E_ARG && WHT.ntargets == 3 &&
              strstr(WHT.notice, "line 1") &&
              whetstone_load_plan(&WHT, "", "whetstone-half.txt") == ZXM_E_ARG &&
              whetstone_load_plan(&WHT, "", "whetstone-path.txt") == ZXM_E_ARG && WHT.ntargets == 3,
          "whetstone: 4 retries, a missing timeout or an id with '/' refuse the plan; it stays");
    CHECK(whetstone_load_plan(&WHT, "", "no-such-plan.txt") == ZXM_E_REMOTE && WHT.ntargets == 3 &&
              whetstone_load_plan(&WHT, "", "") == ZXM_E_ARG,
          "whetstone: a missing plan is zxvd's error: %s", WHT_LINK.err);
    {
        static char many[WHETSTONE_TARGETS * 4u + 8u];
        uint32_t k = 0;
        for (uint32_t i = 0; i <= WHETSTONE_TARGETS; i++) {
            many[k++] = 't';
            many[k++] = (char) ('a' + i % 26u);
            many[k++] = (char) ('a' + i / 26u);
            many[k++] = '\n';
        }
        many[k] = 0;
        CHECK(wht_write("whetstone-many.txt", many) &&
                  whetstone_load_plan(&WHT, "", "whetstone-many.txt") == ZXM_E_TOOBIG &&
                  WHT.ntargets == 3,
              "whetstone: %u targets are refused, the plan kept", WHETSTONE_TARGETS + 1u);
    }

    /* WM3: zxvd's real dispatcher has no Playground: refused, nothing kept */
    {
        whetstone_t real;
        whetstone_init(&real, &LINK);
        CHECK(whetstone_load_plan(&real, "", "whetstone-plan.txt") == ZXM_OK &&
                  whetstone_run(&real, 0) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
                  real.nrecords == 0,
              "whetstone: play.run where zxvd has no Playground: %s", LINK.err);
    }

    /* WM3: a run timed by zxvd's real clock (sys.info) */
    wht_answers(WHT_RESULT("TRUE", "TRUE", "TRUE", "TRUE"), NULL, NULL, NULL);
    before = WHT_SENT;
    CHECK(whetstone_run(&WHT, 0) == ZXM_OK && WHT_SENT == before + 1u && WHT.nrecords == 1 &&
              WHT.record[0].verdict == LPRES_STATE_TRUE && WHT.record[0].passed == 3 &&
              WHT.record[0].failed == 0 && WHT.record[0].tries == 1 && WHT.record[0].ms >= 0 &&
              WHT.record[0].ms <= WHETSTONE_DEFAULT_TIMEOUT * 1000 && !WHT.record[0].timed_out &&
              !strcmp(WHT.record[0].target, "account-statement") &&
              strstr(WHT_REQ, "\"template\":\"account-statement\""),
          "whetstone: a run on zxvd's clock: %s (%s)", WHT.notice, WHT_REQ);

    /* WM3 on the test's clock: a FALSE is a failure, never retried */
    WHT_FAKE_CLOCK = true, WHT_NOW = 1000, WHT_STEP = 400;
    wht_answers(WHT_RESULT("FALSE", "TRUE", "FALSE", "TRUE"),
                WHT_RESULT("TRUE", "TRUE", "TRUE", "TRUE"), NULL, NULL);
    before = WHT_SENT;
    CHECK(whetstone_run(&WHT, 1) == ZXM_OK && WHT_SENT == before + 1u && WHT.nrecords == 2 &&
              WHT.record[1].verdict == LPRES_STATE_FALSE && WHT.record[1].tries == 1 &&
              WHT.record[1].passed == 2 && WHT.record[1].failed == 1 && WHT.record[1].ms == 400 &&
              WHT.record[1].place == 1,
          "whetstone: a FALSE is kept as it is: %s", WHT.notice);

    /* a NEITHER is tried again, up to its retries */
    wht_answers(WHT_RESULT("NEITHER", "NEITHER", "TRUE", "TRUE"),
                WHT_RESULT("BOTH", "TRUE", "BOTH", "NEITHER"), NULL, NULL);
    before = WHT_SENT;
    CHECK(whetstone_run(&WHT, 1) == ZXM_OK && WHT_SENT == before + 2u && WHT.nrecords == 3 &&
              WHT.record[2].verdict == LPRES_STATE_BOTH && WHT.record[2].tries == 2 &&
              WHT.record[2].passed == 1 && WHT.record[2].held == 1 && WHT.record[2].unknown == 1 &&
              WHT.record[2].ms == 800,
          "whetstone: a NEITHER retried, then BOTH held, never retried: %s", WHT.notice);

    /* every try past its timeout (5 s): NEITHER after 1 + 2 tries, each counted at 5000 ms */
    WHT_STEP = 6000;
    wht_answers(WHT_RESULT("TRUE", "TRUE", "TRUE", "TRUE"), NULL, NULL, NULL);
    before = WHT_SENT;
    CHECK(whetstone_run(&WHT, 1) == ZXM_OK && WHT_SENT == before + 3u && WHT.nrecords == 4 &&
              WHT.record[3].verdict == LPRES_STATE_NEITHER && WHT.record[3].timed_out &&
              WHT.record[3].tries == 3 && WHT.record[3].ms == 15000 && WHT.record[3].unknown == 1 &&
              WHT.record[3].passed == 0 && strstr(WHT.notice, "timeout"),
          "whetstone: past its timeout three times: %s", WHT.notice);
    WHT_STEP = 400;

    /* an answer that is not zxvd's keeps nothing */
    wht_answers("{\"verdict\":\"MAYBE\",\"steps\":[]}", NULL, NULL, NULL);
    CHECK(whetstone_run(&WHT, 0) == ZXM_E_PROTOCOL && WHT.nrecords == 4,
          "whetstone: a verdict that is none");
    wht_answers("{\"verdict\":\"TRUE\"}", NULL, NULL, NULL);
    CHECK(whetstone_run(&WHT, 0) == ZXM_E_PROTOCOL && WHT.nrecords == 4,
          "whetstone: a result without its steps");
    wht_answers("{\"verdict\":\"TRUE\",\"steps\":[{\"name\":\"a\",\"verdict\":\"YES\"}]}", NULL,
                NULL, NULL);
    CHECK(whetstone_run(&WHT, 0) == ZXM_E_PROTOCOL && WHT.nrecords == 4,
          "whetstone: a step whose verdict is none");
    CHECK(whetstone_run(&WHT, 3) == ZXM_E_ARG, "whetstone: a target that is none");

    /* WM4: TRUE, FALSE, BOTH, NEITHER conjoined: FALSE */
    CHECK(whetstone_summary(&WHT, &s) == ZXM_OK && s.verdict == LPRES_STATE_FALSE && !s.passed &&
              s.runs == 4 && s.runs_true == 1 && s.runs_false == 1 && s.runs_both == 1 &&
              s.runs_neither == 1 && s.passed_n == 6 && s.failed_n == 1 && s.held_n == 1 &&
              s.unknown_n == 2 && s.ms == WHT.record[0].ms + 400 + 800 + 15000 &&
              s.pass_x100 == 6000 && strstr(WHT.notice, "FALSE"),
          "whetstone: the verdict: %s", WHT.notice);
    CHECK(whetstone_summary(&WHT, NULL) == ZXM_E_ARG, "whetstone: no summary to fill");

    /* WM5: clearing only on the owner's word */
    CHECK(whetstone_clear(&WHT, false) == ZXM_E_STATE && WHT.nrecords == 4,
          "whetstone: the runs are not cleared without the owner's word");
    CHECK(whetstone_clear(&WHT, true) == ZXM_OK && WHT.nrecords == 0 &&
              whetstone_summary(&WHT, &s) == ZXM_OK && s.verdict == LPRES_STATE_NEITHER &&
              !s.passed && s.runs == 0 && s.pass_x100 == WHETSTONE_REFUSED,
          "whetstone: cleared; nothing ran is NEITHER: %s", WHT.notice);

    /* two TRUE runs: the suite passed; then one BOTH: held, never TRUE */
    wht_answers(WHT_RESULT("TRUE", "TRUE", "TRUE", "TRUE"), NULL, NULL, NULL);
    CHECK(whetstone_run(&WHT, 0) == ZXM_OK && whetstone_run(&WHT, 2) == ZXM_OK &&
              whetstone_summary(&WHT, &s) == ZXM_OK && s.verdict == LPRES_STATE_TRUE && s.passed &&
              s.pass_x100 == 10000 && s.ms == 800,
          "whetstone: every run TRUE: %s", WHT.notice);
    wht_answers(WHT_RESULT("BOTH", "TRUE", "BOTH", "TRUE"), NULL, NULL, NULL);
    CHECK(whetstone_run(&WHT, 0) == ZXM_OK && whetstone_summary(&WHT, &s) == ZXM_OK &&
              s.verdict == LPRES_STATE_BOTH && !s.passed,
          "whetstone: one BOTH holds the suite: %s", WHT.notice);

    /* WM2: limits only inside WR5 and on the owner's word */
    before = WHT_SENT;
    CHECK(whetstone_set_limits(&WHT, 0, 2, 10, false) == ZXM_E_STATE &&
              WHT.target[0].retries == 0 && WHT.target[0].timeout_s == WHETSTONE_DEFAULT_TIMEOUT &&
              WHT_SENT == before,
          "whetstone: no limits changed without the owner's word");
    CHECK(whetstone_set_limits(&WHT, 0, 4, 10, true) == ZXM_E_ARG &&
              whetstone_set_limits(&WHT, 0, 1, 0, true) == ZXM_E_ARG &&
              whetstone_set_limits(&WHT, 0, 1, 3601, true) == ZXM_E_ARG &&
              whetstone_set_limits(&WHT, 9, 1, 10, true) == ZXM_E_ARG && WHT.target[0].retries == 0,
          "whetstone: limits outside WR5 are refused");
    CHECK(whetstone_set_limits(&WHT, 0, 3, 1, true) == ZXM_OK && WHT.target[0].retries == 3 &&
              WHT.target[0].timeout_s == 1,
          "whetstone: limits set on the owner's word: %s", WHT.notice);
    WHT_STEP = 1001;
    wht_answers(WHT_RESULT("TRUE", "TRUE", "TRUE", "TRUE"), NULL, NULL, NULL);
    before = WHT_SENT;
    CHECK(whetstone_run(&WHT, 0) == ZXM_OK && WHT_SENT == before + 4u &&
              WHT.record[WHT.nrecords - 1u].tries == 4 &&
              WHT.record[WHT.nrecords - 1u].ms == 4000 && WHT.record[WHT.nrecords - 1u].timed_out,
          "whetstone: 1 s a try, 3 retries: four tries, 4000 ms: %s", WHT.notice);
    WHT_STEP = 400;

    /* WM3: the runs a session keeps */
    while (WHT.nrecords < WHETSTONE_RECORDS && whetstone_run(&WHT, 2) == ZXM_OK) {
    }
    before = WHT_SENT;
    CHECK(WHT.nrecords == WHETSTONE_RECORDS && whetstone_run(&WHT, 2) == ZXM_E_STATE &&
              WHT_SENT == before,
          "whetstone: %u runs kept, then none sent", WHETSTONE_RECORDS);
    WHT_FAKE_CLOCK = false;
}
