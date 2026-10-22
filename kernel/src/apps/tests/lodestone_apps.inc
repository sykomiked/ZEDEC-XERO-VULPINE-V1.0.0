/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lodestone's model against zxvd's real dispatcher (LINK): rpc.discover,
 * zxvd.version and features.get as zxvd answers them, model.info as the
 * stub host gives it (MODEL), and net.peers through view ops that list the
 * contacts below (and none set: networking off). A discovery zxvd would
 * never send is answered on a link of its own (LDA_LINK), which passes
 * every other request to the real dispatcher. The clock is the test's:
 * each read moves it LDA_STEP ms. */
#include "lodestone_model.h"

#define LDA_ID_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define LDA_ID_B "bb00000000000000000000000000000000000000000000000000000000000002"
#define LDA_PEER(id, bucket, seen, member, pending)                                                \
    "{\"id\":\"" id "\",\"addr\":\"192.168.1.7:8723\",\"bucket\":" #bucket ",\"ubh\":true,"        \
    "\"last_seen_ms\":\"" seen "\",\"in_book\":false,\"member\":" #member ",\"held\":\"0\","       \
    "\"receivable\":\"0\",\"social\":\"0\",\"pending\":" #pending "}"

static lodestone_t LDA;
static zxm_link_t LDA_LINK;
static const char *LDA_DISCOVER; /* rpc.discover's result, when set */
static const char *LDA_PEERS;
static int64_t LDA_NOW, LDA_STEP;

static int32_t lda_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (LDA_DISCOVER && strstr(req, "\"rpc.discover\""))
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        LDA_DISCOVER);
    return inproc(NULL, req, len, resp, cap);
}

static int64_t lda_clock(void *ctx)
{
    (void) ctx;
    int64_t now = LDA_NOW;
    LDA_NOW += LDA_STEP;
    return now;
}

static void lda_peers(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, LDA_PEERS, strlen(LDA_PEERS));
}

/* how many of the kept method names start with prefix and a '.' */
static int64_t lda_prefixed(const lodestone_t *t, const char *prefix)
{
    int64_t n = 0;
    size_t k = strlen(prefix);
    for (uint32_t i = 0; i < t->methods_kept; i++)
        n += !strncmp(t->method[i], prefix, k) && t->method[i][k] == '.';
    return n;
}

APPS_TEST(lodestone)
{
    static const zxvd_view_ops_t VIEW = {lda_peers, NULL, NULL};
    lodestone_latency_t lat;

    /* LR1-LR4 through the model's own C */
    {
        int64_t ms[4] = {10, 20, 20, 30}, grp[5] = {1, 1, 2, 3, 1}, n = 0, d = 0, lo = 0, hi = 0;
        CHECK(lodestone_latency_mean(ms, 4, &n, &d) && n == 20 && d == 1 &&
                  lodestone_latency_mean(ms, 3, &n, &d) && n == 50 && d == 3 &&
                  lodestone_latency_span(ms, 4, &lo, &hi) && lo == 10 && hi == 30 &&
                  lodestone_group_count(grp, 5, 1) == 3 && lodestone_group_count(grp, 5, 4) == 0 &&
                  lodestone_contact_record_fits(0, 255, 1, 1700000000123, 0, 1, 2) &&
                  !lodestone_contact_record_fits(1000, 0, 0, 0, 0, 0, 0),
              "lodestone: the rules (LR1-LR4)");
    }

    /* LM1: the methods zxvd serves, by group */
    lodestone_init(&LDA, &LINK, lda_clock, NULL);
    CHECK(lodestone_group_methods(&LDA, "fab") == -1 && !lodestone_has_method(&LDA, "net.peers"),
          "lodestone: nothing known before the first read");
    CHECK(lodestone_read_methods(&LDA) == ZXM_OK && LDA.methods_known && LDA.methods > 100 &&
              LDA.methods_kept == LDA.methods && LDA.methods_left_out == 0 &&
              lodestone_has_method(&LDA, "rpc.discover") &&
              lodestone_has_method(&LDA, "model.info") && lodestone_has_method(&LDA, "net.peers") &&
              !lodestone_has_method(&LDA, "net.nope"),
          "lodestone: rpc.discover: %s (%s)", LDA.notice, LINK.err);
    {
        int64_t sum = 0;
        for (uint32_t g = 0; g < LDA.ngroups; g++) sum += LDA.group[g].methods;
        CHECK(sum == (int64_t) LDA.methods && LDA.ngroups > 10,
              "lodestone: every method in one group, %u groups", LDA.ngroups);
    }
    CHECK(lodestone_group_methods(&LDA, "fab") == lda_prefixed(&LDA, "fab") &&
              lodestone_group_methods(&LDA, "fab") >= 13 &&
              lodestone_group_methods(&LDA, "net") == lda_prefixed(&LDA, "net") &&
              lodestone_group_methods(&LDA, "rpc") == 1 &&
              lodestone_group_methods(&LDA, "nogroup") == -1 &&
              lodestone_group_methods(&LDA, NULL) == -1,
          "lodestone: fab has %lld methods, net %lld (LR1)",
          (long long) lodestone_group_methods(&LDA, "fab"),
          (long long) lodestone_group_methods(&LDA, "net"));

    /* LM1: a discovery that is not zxvd's leaves the last one */
    lodestone_init(&LDA, &LDA_LINK, lda_clock, NULL);
    zxm_link_init(&LDA_LINK, lda_send, &LDA_LINK);
    LDA_DISCOVER = "{\"protocol\":\"zxvd/1\",\"methods\":[\"a.one\",\"a.two\",\"b.one\",\"solo\"]}";
    CHECK(lodestone_read_methods(&LDA) == ZXM_OK && LDA.methods == 4 && LDA.ngroups == 3 &&
              lodestone_group_methods(&LDA, "a") == 2 && lodestone_group_methods(&LDA, "b") == 1 &&
              lodestone_group_methods(&LDA, "solo") == 1 && !strcmp(LDA.protocol, "zxvd/1"),
          "lodestone: four methods in three groups: %s", LDA.notice);
    LDA_DISCOVER = "{\"protocol\":\"zxvd/1\",\"methods\":[\"a.one\",\"Upper.Case\"]}";
    CHECK(lodestone_read_methods(&LDA) == ZXM_E_PROTOCOL && LDA.methods == 4 &&
              lodestone_group_methods(&LDA, "a") == 2,
          "lodestone: an upper-case name is not zxvd's; the last list stays");
    LDA_DISCOVER = "{\"protocol\":\"zxvd/1\",\"methods\":[\"a.one\",7]}";
    CHECK(lodestone_read_methods(&LDA) == ZXM_E_PROTOCOL && LDA.methods == 4,
          "lodestone: a number is no method name");
    LDA_DISCOVER = "{\"protocol\":\"zxvd/1\",\"methods\":[\"\"]}";
    CHECK(lodestone_read_methods(&LDA) == ZXM_E_PROTOCOL, "lodestone: an empty name is none");
    LDA_DISCOVER = "{\"protocol\":\"zxvd/1\",\"methods\":[\"a.one\",\"a0123456789012345678901234."
                   "x\"]}";
    CHECK(lodestone_read_methods(&LDA) == ZXM_OK && LDA.methods == 2 && LDA.methods_left_out == 1 &&
              LDA.ngroups == 1 && strstr(LDA.notice, "not listed"),
          "lodestone: a group name past %u bytes is counted, not filed: %s", LODESTONE_GROUP,
          LDA.notice);
    LDA_DISCOVER = NULL;

    /* LM2: the model as the stub host gives it */
    lodestone_init(&LDA, &LINK, lda_clock, NULL);
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\",\"arch\":\"llama\","
            "\"n_layers\":22,\"n_vocab\":32000,\"status\":\"ready\"}";
    CHECK(lodestone_read_model(&LDA) == ZXM_OK && LDA.model_known && LDA.can_generate &&
              !strcmp(LDA.model, "tinyllama") && !strcmp(LDA.arch, "llama") && LDA.layers == 22 &&
              LDA.vocab == 32000 && !strcmp(LDA.status, "ready") && strstr(LDA.notice, "tinyllama"),
          "lodestone: model.info: %s (%s)", LDA.notice, LINK.err);
    MODEL = "{\"loaded\":false,\"name\":\"\",\"status\":\"no model\"}";
    CHECK(lodestone_read_model(&LDA) == ZXM_OK && !LDA.loaded && !LDA.can_generate &&
              strstr(LDA.notice, "No model"),
          "lodestone: an empty slot: %s", LDA.notice);
    MODEL = "{\"name\":\"x\"}";
    lodestone_init(&LDA, &LINK, lda_clock, NULL);
    CHECK(lodestone_read_model(&LDA) == ZXM_E_PROTOCOL && !LDA.model_known,
          "lodestone: a slot without loaded is not zxvd's answer");
    MODEL = "{\"loaded\":true,\"name\":\"x\",\"n_vocab\":-1}";
    CHECK(lodestone_read_model(&LDA) == ZXM_E_PROTOCOL && !LDA.model_known,
          "lodestone: a negative vocabulary is not zxvd's answer");

    /* LM3: the routing table's contacts */
    zxvd_rpc_set_view_ops(NULL);
    CHECK(lodestone_read_peers(&LDA) == ZXM_E_REMOTE && LINK.err_code == -32601 && !LDA.peers_known,
          "lodestone: no networking: zxvd says so: %s", LINK.err);
    LDA_PEERS = "{\"peers\":[" LDA_PEER(LDA_ID_A, 3, "1700000000123", true, 2) "," LDA_PEER(
        LDA_ID_B, 250, "0", false, 0) "],\"count\":2}";
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(lodestone_read_peers(&LDA) == ZXM_OK && LDA.peers_known && LDA.contacts == 2 &&
              LDA.contacts_kept == 2 && LDA.members == 1 && LDA.contact[0].bucket == 3 &&
              LDA.contact[0].seen == 1700000000123 && LDA.contact[0].pending == 2 &&
              LDA.contact[0].ubh && LDA.contact[0].member && !LDA.contact[1].member &&
              lodestone_contact_ok(&LDA, 0) && lodestone_contact_ok(&LDA, 1) &&
              !lodestone_contact_ok(&LDA, 2),
          "lodestone: two contacts: %s (%s)", LDA.notice, LINK.err);
    LDA_PEERS =
        "{\"peers\":[" LDA_PEER(LDA_ID_A, 3, "1700000000123", true, 1000000000) "],\"count\":1}";
    CHECK(lodestone_read_peers(&LDA) == ZXM_E_PROTOCOL && LDA.contacts == 2,
          "lodestone: pending past its record's 9 digits: the last list stays (LR4)");
    LDA_PEERS = "{\"peers\":[" LDA_PEER(LDA_ID_A, 3, "10000000000000000", true, 0) "],\"count\":1}";
    CHECK(lodestone_read_peers(&LDA) == ZXM_E_PROTOCOL && LDA.contacts == 2,
          "lodestone: a last-seen past 15 digits does not fit the record");
    LDA_PEERS = "{\"peers\":[" LDA_PEER("AA00", 3, "1", true, 0) "],\"count\":1}";
    CHECK(lodestone_read_peers(&LDA) == ZXM_E_PROTOCOL && LDA.contacts == 2,
          "lodestone: a NodeID is 64 lower-case hex digits");
    LDA_PEERS = "{\"peers\":[],\"count\":0}";
    CHECK(lodestone_read_peers(&LDA) == ZXM_OK && LDA.contacts == 0 &&
              strstr(LDA.notice, "network is off"),
          "lodestone: an empty table: %s", LDA.notice);

    /* LM4: capabilities, read only */
    CHECK(lodestone_read_version(&LDA) == ZXM_OK && LDA.version_known && LDA.app[0] &&
              LDA.libzxv[0] && LDA.lib_features >= 0,
          "lodestone: zxvd.version: %s (%s)", LDA.notice, LINK.err);
    CHECK(lodestone_read_features(&LDA) == ZXM_OK && LDA.features_known && LDA.features > 0 &&
              LDA.features_kept <= LDA.features && LDA.features_on <= LDA.features &&
              LDA.feature[0].name[0] && LDA.feature[0].methods >= 1,
          "lodestone: features.get: %s (%s)", LDA.notice, LINK.err);

    /* LM5, LM6: probes timed by the clock */
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\",\"arch\":\"llama\"}";
    lodestone_init(&LDA, &LINK, lda_clock, NULL);
    CHECK(lodestone_latency(&LDA, -1, &lat) == ZXM_E_STATE &&
              lodestone_probe(&LDA, -1) == ZXM_E_ARG &&
              lodestone_probe(&LDA, LODESTONE_PROBES) == ZXM_E_ARG &&
              lodestone_latency(&LDA, 5, &lat) == ZXM_E_ARG && LDA.head == 0,
          "lodestone: no sample yet, no such probe");
    LDA_NOW = 1000, LDA_STEP = 7;
    for (int64_t p = 0; p < LODESTONE_PROBES; p++)
        CHECK(lodestone_probe(&LDA, p) == ZXM_OK, "lodestone: probe %lld: %s (%s)", (long long) p,
              LDA.notice, LINK.err);
    CHECK(lodestone_latency(&LDA, -1, &lat) == ZXM_OK && lat.samples == 5 && lat.mean_num == 7 &&
              lat.mean_den == 1 && lat.fastest == 7 && lat.slowest == 7 && LDA.methods_known &&
              LDA.model_known && LDA.peers_known && LDA.version_known && LDA.features_known,
          "lodestone: five calls of 7 ms each");
    LDA_STEP = 2;
    CHECK(lodestone_probe(&LDA, LODESTONE_PROBE_MODEL) == ZXM_OK &&
              lodestone_probe(&LDA, LODESTONE_PROBE_MODEL) == ZXM_OK &&
              lodestone_latency(&LDA, LODESTONE_PROBE_MODEL, &lat) == ZXM_OK && lat.samples == 3 &&
              lat.mean_num == 11 && lat.mean_den == 3 && lat.fastest == 2 && lat.slowest == 7,
          "lodestone: model.info at 7, 2 and 2 ms: a mean of 11/3 (LR2, LR3)");
    zxvd_rpc_set_view_ops(NULL);
    CHECK(lodestone_probe(&LDA, LODESTONE_PROBE_PEERS) == ZXM_E_REMOTE && LDA.head == 8 &&
              lodestone_latency(&LDA, LODESTONE_PROBE_PEERS, &lat) == ZXM_OK && lat.samples == 2,
          "lodestone: a refusal is an answer too, timed: %s", LDA.notice);
    LDA_STEP = LODESTONE_LATENCY_MAX + 1;
    CHECK(lodestone_probe(&LDA, LODESTONE_PROBE_VERSION) == ZXM_OK && LDA.too_slow == 1 &&
              LDA.head == 8 && strstr(LDA.notice, "not kept"),
          "lodestone: a call past an hour is no latency: %s", LDA.notice);
    LDA_STEP = -5;
    CHECK(lodestone_probe(&LDA, LODESTONE_PROBE_VERSION) == ZXM_OK && LDA.head == 9 &&
              lodestone_latency(&LDA, LODESTONE_PROBE_VERSION, &lat) == ZXM_OK &&
              lat.samples == 2 && lat.fastest == 0,
          "lodestone: a clock that went back counts 0 ms");
    LDA_STEP = 1;
    for (uint32_t i = 0; i < LODESTONE_SAMPLES; i++)
        (void) lodestone_probe(&LDA, LODESTONE_PROBE_VERSION);
    CHECK(LDA.head - LDA.tail == LODESTONE_SAMPLES && LDA.dropped == 9 &&
              lodestone_latency(&LDA, -1, &lat) == ZXM_OK && lat.samples == LODESTONE_SAMPLES &&
              lat.mean_num == 1 && lat.mean_den == 1 &&
              lodestone_latency(&LDA, LODESTONE_PROBE_MODEL, &lat) == ZXM_E_STATE,
          "lodestone: the ring holds %u, the oldest dropped", LODESTONE_SAMPLES);
    lodestone_init(&LDA, &LINK, NULL, NULL);
    CHECK(lodestone_probe(&LDA, LODESTONE_PROBE_MODEL) == ZXM_E_STATE,
          "lodestone: no clock, no probe");
}
