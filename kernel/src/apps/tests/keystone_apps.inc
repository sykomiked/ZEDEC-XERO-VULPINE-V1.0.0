/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Keystone's model against zxvd's real dispatcher (LINK): sys.info,
 * files.root and model.info as they are (model.info through the host ops'
 * MODEL), ui.get and ui.set through people ops that keep simple mode
 * below, and power.budget, which this in-process zxvd does not link
 * (zxvd_power.c), refused there; then against a power ledger of its own
 * (KS_LINK) that answers power.budget and records what it was sent. */
#include "keystone_model.h"

static bool ks_simple;
static bool ks_echo_wrong; /* ui.set answers the opposite of what it was asked */

static int ks_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    (void) len;
    if (!strcmp(method, "ui.set")) ks_simple = strstr(params, "true") != NULL;
    if (strcmp(method, "ui.get") && strcmp(method, "ui.set")) {
        snprintf(msg, msg_cap, "not here");
        return -32601;
    }
    bool say = ks_echo_wrong ? !ks_simple : ks_simple;
    zxvd_buf_raw(out, say ? "{\"simple_mode\":true}" : "{\"simple_mode\":false}", say ? 20 : 21);
    return 0;
}

static zxm_link_t KS_LINK;
static char ks_sent[8][160]; /* the power.budget params, in order */
static uint32_t ks_nsent;
static uint32_t ks_refuse_at = 99; /* the call (0..) the ledger refuses */

static int32_t ks_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *p = strstr(req, "\"params\":");
    uint32_t i = ks_nsent++;
    (void) len;
    if (p && i < 8) snprintf(ks_sent[i], sizeof ks_sent[i], "%s", p + 9);
    if (!strstr(req, "\"power.budget\"") || i == ks_refuse_at)
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32095,"
                        "\"message\":\"the budget was refused\"}}",
                        l->seq);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true}}", l->seq);
}

APPS_TEST(keystone)
{
    static keystone_t K;
    static const zxvd_people_ops_t people = {ks_people};

    /* the rules the model runs */
    CHECK(keystone_setting_ok(0, 100, 50) && keystone_setting_ok(100, 0, 400) &&
              !keystone_setting_ok(101, 0, 100) && !keystone_setting_ok(0, -1, 100) &&
              !keystone_setting_ok(0, 0, 49) && !keystone_setting_ok(0, 0, 401),
          "keystone: setting limits at their edges (KS1)");
    CHECK(keystone_energy_budget_ok(1, 1, 1) &&
              keystone_energy_budget_ok(86400000, 999999999, 999999999) &&
              !keystone_energy_budget_ok(0, 1, 1) && !keystone_energy_budget_ok(86400001, 1, 1) &&
              !keystone_energy_budget_ok(1, 0, 1) && !keystone_energy_budget_ok(1, 2, 1) &&
              !keystone_energy_budget_ok(1, 1, 1000000000),
          "keystone: energy budget limits at their edges (KS2)");

    /* KM1 on the real dispatcher: no ui.* served, then simple mode served */
    keystone_init(&K, &LINK);
    zxvd_rpc_set_people_ops(NULL);
    MODEL = "{\"loaded\":false,\"can_generate\":false,\"name\":\"\",\"status\":\"none\"}";
    CHECK(keystone_refresh(&K) == ZXM_OK && K.have && K.cpus > 0 && K.mem_total > 0 &&
              K.root[0] == '/' && K.file_max > 0 && !K.model_loaded && !K.model_name[0] &&
              !K.simple_known && !K.simple_mode,
          "keystone: refresh, ui.get not served: %s", LINK.err);
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tiny-7\",\"status\":\"ready\"}";
    ks_simple = true;
    zxvd_rpc_set_people_ops(&people);
    CHECK(keystone_refresh(&K) == ZXM_OK && K.model_loaded && !strcmp(K.model_name, "tiny-7") &&
              K.simple_known && K.simple_mode,
          "keystone: refresh, a model loaded and simple mode on: %s", LINK.err);
    MODEL = "{\"loaded\":true}";
    CHECK(keystone_refresh(&K) == ZXM_E_PROTOCOL && !strcmp(K.model_name, "tiny-7") &&
              K.simple_mode,
          "keystone: a model answer without its name changes nothing");
    MODEL = "{}";

    /* KM2: without the owner's word nothing is sent; with it simple mode is set and
     * echoed; an answer that does not echo it is refused */
    int ks_before = sent;
    CHECK(keystone_set_simple(&K, false, false) == ZXM_E_STATE && sent == ks_before && ks_simple &&
              K.simple_mode,
          "keystone: simple mode not changed without the owner's word, nothing sent");
    CHECK(keystone_set_simple(&K, false, true) == ZXM_OK && !ks_simple && K.simple_known &&
              !K.simple_mode && strstr(K.notice, "off"),
          "keystone: simple mode off: %s", LINK.err);
    ks_echo_wrong = true;
    CHECK(keystone_set_simple(&K, true, true) == ZXM_E_PROTOCOL && !K.simple_mode,
          "keystone: an answer that is not what was asked is refused");
    ks_echo_wrong = false;
    zxvd_rpc_set_people_ops(NULL);
    CHECK(keystone_set_simple(&K, true, true) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
              !K.simple_mode,
          "keystone: ui.set not served: nothing changes");

    /* KM3: a proposal is held, nothing is sent */
    static const char *const apps[3] = {"quillon", "glint", "banker"};
    static const int64_t w[3] = {5, 3, 2};
    int before = sent;
    CHECK(keystone_propose(&K, 60000, 1000, apps, w, 3) == ZXM_OK && K.held.pending &&
              K.held.n == 3 && K.held.app[0].share == 500 && K.held.app[1].share == 300 &&
              K.held.app[2].share == 200 && sent == before && strstr(K.notice, "confirm"),
          "keystone: 1000 mJ by 5:3:2 is 500, 300, 200, held");
    CHECK(keystone_propose(&K, 60000, 1000, apps, w, 3) == ZXM_E_STATE,
          "keystone: one proposal at a time");
    keystone_cancel(&K);
    CHECK(!K.held.pending, "keystone: cancelled");
    static const int64_t w7[3] = {1, 1, 1}, w0[3] = {1, 0, 1}, wn[3] = {1, -1, 1};
    CHECK(keystone_propose(&K, 60000, 7, apps, w7, 3) == ZXM_OK && K.held.app[0].share == 3 &&
              K.held.app[1].share == 2 && K.held.app[2].share == 2,
          "keystone: 7 mJ in three: the leftover to the earliest");
    keystone_cancel(&K);
    static const char *const dup[2] = {"glint", "glint"}, *const bad[2] = {"glint", "has space"};
    static const char *const empty[1] = {""};
    CHECK(keystone_propose(&K, 60000, 1000, apps, w0, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000, apps, wn, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 0, 1000, apps, w, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 86400001, 1000, apps, w, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000000000, apps, w, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 2, apps, w, 3) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000, dup, w, 2) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000, bad, w, 2) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000, empty, w, 1) == ZXM_E_ARG &&
              keystone_propose(&K, 60000, 1000, apps, w, 0) == ZXM_E_ARG && !K.held.pending,
          "keystone: a zero share, a negative weight, a window or whole past its limits, and a "
          "bad or repeated name refuse the whole proposal");

    /* KM4 on the real dispatcher: no word, no send; the power group is not linked here */
    CHECK(keystone_propose(&K, 60000, 1000, apps, w, 3) == ZXM_OK, "keystone: proposed again");
    before = sent;
    CHECK(keystone_apply(&K, false) == ZXM_E_STATE && sent == before && K.held.pending,
          "keystone: without the owner's word nothing is sent");
    CHECK(keystone_apply(&K, true) == ZXM_E_REMOTE &&
              (LINK.err_code == -32601 || LINK.err_code == -32095) && K.applied == 0 &&
              K.held.pending,
          "keystone: power.budget refused by this zxvd (%d), still held", LINK.err_code);

    /* KM4 on a ledger that takes budgets */
    zxm_link_init(&KS_LINK, ks_send, &KS_LINK);
    K.link = &KS_LINK;
    ks_nsent = 0;
    CHECK(keystone_apply(&K, true) == ZXM_OK && K.applied == 4 && !K.held.pending &&
              ks_nsent == 4 &&
              !strcmp(ks_sent[0], "{\"target\":\"app\",\"app\":\"quillon\",\"window_ms\":60000,"
                                  "\"max_uj\":500000}}") &&
              strstr(ks_sent[1], "\"glint\"") && strstr(ks_sent[1], "\"max_uj\":300000") &&
              strstr(ks_sent[2], "\"banker\"") && strstr(ks_sent[2], "\"max_uj\":200000") &&
              !strcmp(ks_sent[3], "{\"target\":\"device\",\"window_ms\":60000,"
                                  "\"max_uj\":1000000}}"),
          "keystone: every app's budget then the device's, in microjoules: %s", ks_sent[0]);
    CHECK(keystone_apply(&K, true) == ZXM_E_STATE, "keystone: the word is spent by one apply");
    CHECK(keystone_propose(&K, 60000, 1000, apps, w, 3) == ZXM_OK, "keystone: a new proposal");
    ks_nsent = 0;
    ks_refuse_at = 1;
    CHECK(keystone_apply(&K, true) == ZXM_E_REMOTE && K.applied == 1 && ks_nsent == 2 &&
              K.held.pending && strstr(K.notice, "refused"),
          "keystone: a refusal stops the rest, the proposal stays held");
    ks_refuse_at = 99;
    keystone_cancel(&K);
    ks_nsent = 0;
    CHECK(keystone_clear(&K, "glint", false) == ZXM_E_STATE && ks_nsent == 0 &&
              keystone_clear(&K, "", true) == ZXM_E_ARG &&
              keystone_clear(&K, "glint", true) == ZXM_OK && ks_nsent == 1 &&
              strstr(ks_sent[0], "\"window_ms\":0"),
          "keystone: a budget cleared only with the owner's word");
    K.link = &LINK;
}
