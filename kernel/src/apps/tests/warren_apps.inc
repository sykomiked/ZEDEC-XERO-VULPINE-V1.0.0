/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warren's model against zxvd's real dispatcher: two engines of its own
 * behind feature switches (one plain, one regulated) added through
 * zxvd_rpc_add_group, so features.get and features.set run as zxvd runs
 * them; zxvd.version, sys.info and log.tail as they are. power.top (not
 * linked in this in-process zxvd) and an engine that will not come back
 * on are answered on a link of its own (WR_LINK), which passes every other
 * request to the real dispatcher. zxvd.shutdown is never sent. */
#include "warren_model.h"

static warren_t WR, WR2;
static zxm_link_t WR_LINK;
static const char *WR_TOP; /* the power.top result */
static bool WR_FAIL_ON;    /* features.set enabled true answers an error */
static bool WR_SHUTDOWN;   /* zxvd.shutdown was asked for */

static int32_t wr_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"zxvd.shutdown\"")) WR_SHUTDOWN = true;
    if (strstr(req, "\"power.top\""))
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        WR_TOP ? WR_TOP : "[]");
    if (WR_FAIL_ON && strstr(req, "\"features.set\"") && strstr(req, "\"enabled\":true"))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32000,"
                        "\"message\":\"the engine did not start\"}}",
                        l->seq);
    return inproc(NULL, req, len, resp, cap);
}

static void wr_ping(zxvd_call_t *c)
{
    zxvd_buf_raw(zxvd_res(c), "{\"ok\":true}", 11);
}

static const zxvd_group_method_t WR_PLAIN_M[] = {{"wrplain.ping", wr_ping, false},
                                                 {"wrplain.pong", wr_ping, false}};
static const zxvd_group_method_t WR_MONEY_M[] = {{"wrmoney.ping", wr_ping, false}};
static const zxvd_group_t WR_PLAIN = {"wrplain", false, "a plain engine", WR_PLAIN_M, 2, NULL};
static const zxvd_group_t WR_MONEY = {"wrmoney", true, "a regulated engine", WR_MONEY_M, 1, NULL};

#define WR_C(id, kind, name, uj, mw)                                                               \
    "{\"id\":" #id ",\"kind\":\"" kind "\",\"name\":\"" name "\",\"parent\":null,\"uj\":" #uj      \
    ",\"mw\":" #mw ",\"source\":\"attributed\",\"lifetime_uj\":1,\"cpu_ns\":1}"

APPS_TEST(warren)
{
    /* the rules (WR1-WR5) */
    CHECK(warren_change_ok(1, 0, 0, 1) && warren_change_ok(1, 0, 0, 0) &&
              warren_change_ok(1, 1, 0, 0) && warren_change_ok(1, 1, 1, 1) &&
              !warren_change_ok(1, 1, 0, 1) && !warren_change_ok(0, 0, 0, 0) &&
              !warren_change_ok(0, 1, 1, 1) && !warren_change_ok(1, 2, 0, 1) &&
              !warren_change_ok(1, 0, 0, 2),
          "warren: a switch changes in the owner's session; regulated on needs approval");
    CHECK(warren_approval_ok(1, 1) && !warren_approval_ok(1, 0) && !warren_approval_ok(0, 1) &&
              !warren_approval_ok(1, 2),
          "warren: the owner's approval, both flags exactly 1");
    CHECK(warren_backoff(1, 5, 2, 60) == 2 && warren_backoff(4, 5, 2, 60) == 16 &&
              warren_backoff(5, 5, 2, 60) == 32 && warren_backoff(6, 5, 2, 60) == WARREN_REFUSED &&
              warren_backoff(3, 5, 10, 25) == 25 && warren_backoff(1, 5, 0, 25) == WARREN_REFUSED,
          "warren: back-off doubles and caps, refused outside its domain");
    CHECK(warren_record_fits(31, 1, 1, 999, 999999999999999) &&
              !warren_record_fits(100, 1, 1, 0, 0) && !warren_record_fits(1, 1, 1, 1000, 0) &&
              !warren_record_fits(1, 1, 1, 0, 1000000000000000),
          "warren: a ^ZXWARREN record's digits");

    /* two engines of our own, both off (X3) */
    CHECK(zxvd_rpc_add_group(&WR_PLAIN) == 0 && zxvd_rpc_add_group(&WR_MONEY) == 0 &&
              !zxvd_rpc_enabled("wrplain") && !zxvd_rpc_enabled("wrmoney"),
          "warren: two engines added, off");
    warren_init(&WR, &LINK);
    int before = sent;
    CHECK(warren_switch(&WR, "wrplain", true, true) == ZXM_E_ARG && sent == before,
          "warren: nothing known before a refresh, nothing sent");
    CHECK(warren_refresh(&WR) == ZXM_OK && WR.have && !strcmp(WR.app, "test") && WR.protocol[0] &&
              WR.libzxv[0] && WR.time_ms > 0 && WR.pid > 0 && WR.nengines >= 2,
          "warren: the daemon and its engines: %s", LINK.err);
    const warren_engine_t *p = warren_find(&WR, "wrplain"), *m = warren_find(&WR, "wrmoney");
    CHECK(p && m && !p->enabled && !p->regulated && p->groups == 1 && p->methods == 2 &&
              !m->enabled && m->regulated && m->methods == 1 && !warren_find(&WR, "nothing"),
          "warren: features.get read: plain and regulated, off");
    CHECK(strstr(WR.notice, " engines, ") && !strstr(WR.notice, "wrmoney"), "warren: %s",
          WR.notice);

    /* WM1/WM2: nothing changes outside the owner's session */
    before = sent;
    CHECK(warren_switch(&WR, "wrplain", true, true) == ZXM_E_STATE &&
              warren_restart(&WR, "wrplain", true) == ZXM_E_STATE && sent == before &&
              !zxvd_rpc_enabled("wrplain"),
          "warren: not the owner's session: refused, nothing sent");
    warren_set_owner(&WR, true);
    CHECK(warren_switch(&WR, "wrplain", true, false) == ZXM_OK && zxvd_rpc_enabled("wrplain") &&
              warren_find(&WR, "wrplain")->enabled &&
              warren_find(&WR, "wrplain")->changed_ms >= WR.time_ms - 1000 &&
              warren_find(&WR, "wrplain")->changed_ms > 0,
          "warren: a plain engine on in the owner's session: %s", LINK.err);
    before = sent;
    CHECK(warren_switch(&WR, "wrmoney", true, false) == ZXM_E_STATE && sent == before &&
              !zxvd_rpc_enabled("wrmoney") && !warren_find(&WR, "wrmoney")->enabled,
          "warren: a regulated engine is never turned on without the owner's approval");
    CHECK(warren_switch(&WR, "wrmoney", true, true) == ZXM_OK && zxvd_rpc_enabled("wrmoney") &&
              strstr(WR.notice, "regulated and on, by the owner's approval: ") &&
              strstr(WR.notice, " wrmoney"),
          "warren: approved, on: %s", WR.notice);
    CHECK(warren_switch(&WR, "wrmoney", false, false) == ZXM_OK && !zxvd_rpc_enabled("wrmoney"),
          "warren: turning a regulated engine off needs no approval");
    CHECK(warren_switch(&WR, "nothing", true, true) == ZXM_E_ARG &&
              warren_switch(&WR, "Bad Name", true, true) == ZXM_E_ARG &&
              warren_switch(&WR, NULL, true, true) == ZXM_E_ARG &&
              warren_switch(&WR, "a234567890123456789012345", true, true) == ZXM_E_ARG,
          "warren: unknown or malformed engines refused");

    /* WM3: a restart is off then on */
    CHECK(warren_restart(&WR, "wrplain", false) == ZXM_OK && zxvd_rpc_enabled("wrplain") &&
              warren_find(&WR, "wrplain")->restarts == 1,
          "warren: restarted: %s", LINK.err);
    before = sent;
    CHECK(warren_restart(&WR, "wrmoney", true) == ZXM_E_STATE && sent == before,
          "warren: an engine that is off is not restarted");
    CHECK(warren_refresh(&WR) == ZXM_OK && warren_find(&WR, "wrplain")->restarts == 1 &&
              warren_find(&WR, "wrplain")->enabled && warren_record(&WR, 0) &&
              !warren_record(&WR, WR.nengines),
          "warren: a refresh keeps the owner's history; its records fit");
    CHECK(raw_call("wrplain.ping", "{}") == ZXM_OK &&
              raw_call("wrmoney.ping", "{}") == ZXM_E_REMOTE,
          "warren: an engine on answers, one off does not");

    /* WM3: an engine that does not come back on waits, attempt by attempt */
    zxm_link_init(&WR_LINK, wr_send, &WR_LINK);
    warren_init(&WR2, &WR_LINK);
    warren_set_owner(&WR2, true);
    CHECK(warren_set_backoff(&WR2, 3, 2, 60) == ZXM_OK &&
              warren_set_backoff(&WR2, 33, 2, 60) == ZXM_E_ARG &&
              warren_set_backoff(&WR2, 3, 0, 60) == ZXM_E_ARG && WR2.tries == 3 &&
              warren_wait(&WR2, 2) == 4 && warren_wait(&WR2, 4) == WARREN_REFUSED,
          "warren: back-off settings inside WR4 only");
    CHECK(warren_refresh(&WR2) == ZXM_OK && warren_find(&WR2, "wrplain")->enabled,
          "warren: a second view: %s", WR_LINK.err);
    WR_FAIL_ON = true;
    CHECK(warren_restart(&WR2, "wrplain", false) == ZXM_E_REMOTE && !zxvd_rpc_enabled("wrplain") &&
              WR2.retry.waiting && WR2.retry.attempt == 1 && WR2.retry.wait_s == 2 &&
              !warren_find(&WR2, "wrplain")->enabled &&
              strstr(WR2.notice, "wrplain did not come back on: attempt 2 of 3 in 2 s"),
          "warren: off, then the on failed: wait 2 s: %s", WR2.notice);
    CHECK(warren_restart(&WR2, "wrplain", false) == ZXM_E_STATE &&
              warren_switch(&WR2, "wrplain", true, false) == ZXM_E_STATE &&
              warren_set_backoff(&WR2, 5, 2, 60) == ZXM_E_STATE,
          "warren: while it waits, only the retry");
    CHECK(warren_retry(&WR2, false) == ZXM_E_REMOTE && WR2.retry.waiting &&
              WR2.retry.attempt == 2 && WR2.retry.wait_s == 4 &&
              strstr(WR2.notice, "attempt 3 of 3 in 4 s"),
          "warren: failed again: wait 4 s: %s", WR2.notice);
    CHECK(warren_retry(&WR2, false) == ZXM_E_REMOTE && !WR2.retry.waiting && WR2.retry.gave_up &&
              strstr(WR2.notice, "wrplain stayed off after 3 attempts") &&
              warren_retry(&WR2, false) == ZXM_E_STATE,
          "warren: the last attempt failed: given up: %s", WR2.notice);
    WR_FAIL_ON = false;
    CHECK(warren_switch(&WR2, "wrplain", true, false) == ZXM_OK && zxvd_rpc_enabled("wrplain") &&
              !WR2.retry.gave_up && !strstr(WR2.notice, "stayed off"),
          "warren: turned on by the owner: %s", WR2.notice);
    WR_FAIL_ON = true;
    CHECK(warren_restart(&WR2, "wrplain", false) == ZXM_E_REMOTE && WR2.retry.waiting,
          "warren: waiting again");
    WR_FAIL_ON = false;
    CHECK(warren_retry(&WR2, false) == ZXM_OK && !WR2.retry.waiting && !WR2.retry.gave_up &&
              zxvd_rpc_enabled("wrplain") && warren_find(&WR2, "wrplain")->restarts == 1,
          "warren: back on at the second attempt: %s", WR_LINK.err);

    /* WM5: energy per consumer, each a share of the whole */
    WR_TOP = "[" WR_C(0, "system", "system", 7045000, 7045) "," WR_C(
        3, "app", "warren", 5636000, 5636) "," WR_C(9, "agent", "scribe", 1409000, 1409) "]";
    CHECK(warren_energy(&WR2, 3) == ZXM_OK && WR2.nconsumers == 3 && WR2.whole_uj == 14090000 &&
              WR2.consumer[0].share_x100 == 5000 && WR2.consumer[1].share_x100 == 4000 &&
              WR2.consumer[2].share_x100 == 1000 && WR2.consumer[2].id == 9 &&
              !strcmp(WR2.consumer[1].kind, "app") &&
              strstr(WR2.notice, "most energy: system 50.00%"),
          "warren: shares of 14090000 uJ: %s", WR2.notice);
    static const char *const BAD_TOP[] = {
        "[" WR_C(0, "system", "system", 1, 1) "," WR_C(1, "app", "a", 2, 2) "]", /* not ranked */
        "[" WR_C(0, "daemon", "system", 1, 1) "]",                               /* no such kind */
        "[" WR_C(0, "system", "system", -1, 1) "]",                              /* negative */
        "{\"consumers\":[]}",                                                    /* no array */
        "[" WR_C(0, "system", "system", 9223372036854775807, 1) "," WR_C(1, "app", "a", 1, 1) "]",
    };
    for (uint32_t i = 0; i < sizeof BAD_TOP / sizeof BAD_TOP[0]; i++) {
        WR_TOP = BAD_TOP[i];
        CHECK(warren_energy(&WR2, 3) == ZXM_E_PROTOCOL && WR2.nconsumers == 3 &&
                  WR2.consumer[0].share_x100 == 5000,
              "warren: power.top reply %u refused, nothing changed", i);
    }
    WR_TOP = "[]";
    CHECK(warren_energy(&WR2, 0) == ZXM_E_ARG && warren_energy(&WR2, 65) == ZXM_E_ARG &&
              warren_energy(&WR2, 4) == ZXM_OK && WR2.nconsumers == 0 && WR2.whole_uj == 0 &&
              !strstr(WR2.notice, "most energy"),
          "warren: nothing measured");

    /* WM4: recent events, read on from where the last read stopped */
    zxvd_log_reset();
    zxvd_log(ZXVD_LOG_INFO, "rpc", "features.set wrplain on");
    zxvd_log(ZXVD_LOG_WARN, "rpc", "wrmoney.ping refused: off");
    zxvd_log(ZXVD_LOG_ERROR, "net", "link down");
    CHECK(warren_events(&WR, 64) == ZXM_OK && WR.nevents == 3 &&
              !strcmp(WR.event[1].level, "warn") && !strcmp(WR.event[2].source, "net") &&
              !strcmp(WR.event[2].text, "link down") && WR.event[0].seq < WR.event[2].seq &&
              WR.next > WR.event[2].seq - 1 && WR.dropped == 0,
          "warren: three events, oldest first: %u %s", WR.nevents, LINK.err);
    CHECK(warren_events(&WR, 64) == ZXM_OK && WR.nevents == 0, "warren: nothing new");
    zxvd_log(ZXVD_LOG_INFO, "rpc", "one more");
    CHECK(warren_events(&WR, 64) == ZXM_OK && WR.nevents == 1 &&
              !strcmp(WR.event[0].text, "one more") && warren_events(&WR, 0) == ZXM_E_ARG &&
              warren_events(&WR, 65) == ZXM_E_ARG,
          "warren: read on: %u", WR.nevents);

    /* made up replies: nothing out of bounds changes what was shown (WM4) */
    warren_t *F = &WR2;
    uint32_t n = F->nengines;
    zxm_link_init(&FAKE, canned, &FAKE);
    F->link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"app\":\"x\",\"protocol\":\"1\","
             "\"libzxv\":\"1\",\"libzxv_features\":-1}}";
    CHECK(warren_refresh(F) == ZXM_E_PROTOCOL && F->nengines == n && !strcmp(F->app, "test"),
          "warren: a version that is not one: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"seq\":5,\"time_ms\":1,"
             "\"level\":\"loud\",\"source\":\"x\",\"text\":\"y\"}],\"next\":6,\"dropped\":0}}";
    CHECK(warren_events(F, 8) == ZXM_E_PROTOCOL && F->nevents == 0, "warren: no such level");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"seq\":5,\"time_ms\":1,"
             "\"level\":\"info\",\"source\":\"x\",\"text\":\"y\"},{\"seq\":4,\"time_ms\":1,"
             "\"level\":\"info\",\"source\":\"x\",\"text\":\"y\"}],\"next\":6,\"dropped\":0}}";
    CHECK(warren_events(F, 8) == ZXM_E_PROTOCOL && F->nevents == 0, "warren: not oldest first");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"seq\":5,\"time_ms\":1,"
             "\"level\":\"info\",\"source\":\"x\",\"text\":\"y\"}],\"next\":6,\"dropped\":2}}";
    CHECK(warren_events(F, 8) == ZXM_OK && F->nevents == 1 && F->next == 6 && F->dropped == 2 &&
              strstr(F->notice, "some older events were not kept"),
          "warren: dropped events said: %s", F->notice);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"feature\":\"wrother\",\"enabled\":false,"
             "\"regulated\":false,\"saved\":false}}";
    CHECK(warren_switch(F, "wrplain", false, false) == ZXM_E_PROTOCOL &&
              warren_find(F, "wrplain")->enabled,
          "warren: zxvd says back another engine: the change is not taken");
    CANNED = NULL;
    F->link = &WR_LINK;

    /* clean up: both engines off, the groups gone */
    CHECK(warren_switch(&WR2, "wrplain", false, false) == ZXM_OK && !zxvd_rpc_enabled("wrplain") &&
              !zxvd_rpc_enabled("wrmoney") && !WR_SHUTDOWN,
          "warren: both engines off again, zxvd.shutdown never sent");
    zxvd_rpc_remove_group(&WR_MONEY);
    zxvd_rpc_remove_group(&WR_PLAIN);
}
