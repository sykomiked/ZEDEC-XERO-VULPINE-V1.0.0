/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_web4.c — zxvd_web4.c (web4.*) behind the real RPC layer on the
 * real web4_agent envelopes and ML-DSA-65. The test keeps a mirror of the
 * owner's companion (same seed, same random draws, same order) and builds
 * every envelope, consent token and CONSENT answer itself, so every reply,
 * signature and envelope byte is compared exactly. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_groups.h"
#include "web4_agent.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[20000], WANT[40000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.600s\n    got  %.600s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *req(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *req(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *errc(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *errc(int code, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"", code);
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "\"}}");
    return WANT;
}
#define E4 (-32090)
#define EP (-32602)

static const char *hx(const uint8_t *p, uint32_t n)
{
    static char b[4][2 * W4_ENV_MAX + 1];
    static int i;
    i = (i + 1) & 3;
    w4_hex_encode(p, n, b[i], sizeof b[i]);
    return b[i];
}

/* ----- fixtures: a counted random stream and the owner's mirror ----- */

#define NOW 1791547200ull /* 2026-10-09 12:00:00Z */
#define MS  (NOW * 1000u)
static uint32_t RCOUNT, DRAW;
static bool RFAIL;
static int64_t RFAIL_AT = -1; /* fail the draw with this RCOUNT */

/* the k-th draw: k + 3i, with k's higher bytes mixed into the first three
 * (so nonces never repeat in a long test) */
static void stream(uint32_t k, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t) ((uint8_t) (k + i * 3u) ^ (i < 3 ? (uint8_t) (k >> (8 * (i + 1))) : 0));
}

static int fake_random(uint8_t *p, size_t n)
{
    if (RFAIL || RFAIL_AT == RCOUNT) return -1;
    stream(RCOUNT++, p, n);
    return 0;
}

/* the mirror's copy of the next draw */
static void draw(uint8_t *p, size_t n)
{
    stream(DRAW++, p, n);
}

static w4_agent_t ME, PEER;
static uint8_t SEED[32], TO[32];
static char SEEDHEX[65], TOHEX[65];
static const char *MEID; /* the owner's agent_id as hex */
static char MEIDB[65];

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_web4_group()) == 0, "add the web4 group");
    zxvd_web4_set_random(fake_random);
    RCOUNT = DRAW = 0;
    RFAIL = false;
    RFAIL_AT = -1;
}

/* open the feed with SEED (one draw: the binding signature), mirrored */
static void open_feed(void)
{
    uint8_t r[32];
    draw(r, 32);
    CHECK(w4_agent_create(&ME, SEED, "chiglet", NULL, NULL, MS, r) == W4_OK, "mirror");
    strcpy(MEIDB, hx(ME.card.agent_id, 32));
    MEID = MEIDB;
    const char *got = rpc(req("web4.open", "{\"seed\":\"%s\",\"now\":%llu}", SEEDHEX, NOW));
    CHECK(strstr(got, "\"new\":true") && strstr(got, MEID), "open: %.200s", got);
}

/* ----- the expected entry ----- */
typedef struct {
    uint32_t id;
    const char *kind, *status, *to, *tool;
    uint64_t req;
    uint32_t chunk;
    bool final;
    const char *payload; /* as JSON text, already escaped */
    bool pii;
    int64_t amount;
    uint64_t seq; /* 0: null (not signed) */
    uint64_t ts;
    uint32_t consent, answers; /* 0: null */
    int grant;                 /* -1: null */
    uint32_t bytes;
} X;

static const char *ej(const X *x)
{
    static char s[4][6000];
    static int i;
    char seq[64], ca[96];
    i = (i + 1) & 3;
    if (x->seq)
        snprintf(seq, sizeof seq, "\"seq\":\"%llu\",\"ts\":%llu", (unsigned long long) x->seq,
                 (unsigned long long) x->ts);
    else
        snprintf(seq, sizeof seq, "\"seq\":null,\"ts\":null");
    if (x->answers)
        snprintf(ca, sizeof ca, "\"consent\":null,\"answers\":%u,\"grant\":%s", x->answers,
                 x->grant ? "true" : "false");
    else if (x->consent)
        snprintf(ca, sizeof ca, "\"consent\":%u,\"answers\":null,\"grant\":null", x->consent);
    else
        snprintf(ca, sizeof ca, "\"consent\":null,\"answers\":null,\"grant\":null");
    snprintf(s[i], sizeof s[i],
             "{\"id\":%u,\"kind\":\"%s\",\"status\":\"%s\",\"from\":\"%s\",\"to\":\"%s\","
             "\"tool\":\"%s\",\"req_id\":\"%llu\",\"chunk\":%u,\"final\":%s,\"payload\":\"%s\","
             "\"pii\":%s,\"amount_minor\":\"%lld\",%s,%s,\"bytes\":%u}",
             x->id, x->kind, x->status, MEID, x->to, x->tool, (unsigned long long) x->req, x->chunk,
             x->final ? "true" : "false", x->payload, x->pii ? "true" : "false",
             (long long) x->amount, seq, ca, x->bytes);
    return s[i];
}

/* the mirror seals e (one draw) into out; returns its length */
static uint32_t seal(w4_env_t *e, uint64_t ms, uint8_t *out)
{
    uint8_t r[32];
    draw(r, 32);
    int32_t n = w4_env_seal(&ME, e, ms, r, out, W4_ENV_MAX);
    CHECK(n > 0, "mirror seal");
    return (uint32_t) n;
}

/* the CONSENT answer's bytes (zxvd_web4.c answer_body + signature) */
#define ANSWER_BODY  (4u + 1u + 1u + 32u + 8u + 8u + 4u + 32u)
#define ANSWER_BYTES (ANSWER_BODY + W4_SIG_LEN)
static void answer(bool grant, uint64_t seq, uint64_t ms, uint32_t id, const uint8_t act[32],
                   const uint8_t rnd[32], const w4_agent_t *who, uint8_t out[ANSWER_BYTES])
{
    memcpy(out, "W4CA", 4);
    out[4] = 1;
    out[5] = grant;
    memcpy(out + 6, who->card.agent_id, 32);
    w4_le64_put(out + 38, seq);
    w4_le64_put(out + 46, ms);
    w4_le32_put(out + 54, id);
    memcpy(out + 58, act, 32);
    w4_sign(who->sk, "web4/v1/consent-answer", out, ANSWER_BODY, rnd, out + ANSWER_BODY);
}

static void expect_bytes(uint32_t id, const char *kind, const uint8_t *p, uint32_t n, int line)
{
    char w[2 * W4_ENV_MAX + 200];
    snprintf(w, sizeof w,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"id\":%u,\"kind\":\"%s\",\"bytes\":%u,"
             "\"envelope\":\"%s\"}}",
             id, kind, n, hx(p, n));
    expect(req("web4.envelope", "{\"id\":%u}", id), w, line);
}
#define EXPECT_BYTES(id, kind, p, n) expect_bytes(id, kind, p, n, __LINE__)

static const char *verify_env_json(const w4_env_t *e, uint32_t n)
{
    static char s[2000];
    char c[200] = "";
    if (e->flags & W4_MF_CONSENT)
        snprintf(c, sizeof c, ",\"consent_scope\":{\"pii\":%s,\"money\":%s},\"max_minor\":\"%llu\"",
                 e->consent.scope & W4_CONSENT_PII ? "true" : "false",
                 e->consent.scope & W4_CONSENT_MONEY ? "true" : "false",
                 (unsigned long long) e->consent.max_vfv);
    char from[65], to[65];
    strcpy(from, hx(e->from, 32));
    strcpy(to, hx(e->to, 32));
    snprintf(s, sizeof s,
             "{\"valid\":true,\"kind\":\"%s\",\"from\":\"%s\",\"to\":\"%s\",\"seq\":\"%llu\","
             "\"ts\":%llu,\"req_id\":\"%llu\",\"chunk\":%u,\"tool\":\"%s\",\"final\":%s,"
             "\"payload_bytes\":%u,\"consent\":%s%s,\"bytes\":%u}",
             e->kind == 1   ? "REQUEST"
             : e->kind == 2 ? "RESPONSE"
                            : "STREAM",
             from, to, (unsigned long long) e->seq, (unsigned long long) (e->ts_ms / 1000u),
             (unsigned long long) e->req_id, e->chunk, e->tool,
             e->flags & W4_MF_FINAL ? "true" : "false", e->payload_len,
             e->flags & W4_MF_CONSENT ? "true" : "false", c, n);
    return s;
}

static const char *verify_answer_json(const w4_agent_t *who, bool grant, uint64_t seq, uint64_t ts,
                                      uint32_t id, const uint8_t act[32])
{
    static char s[600];
    char from[65];
    strcpy(from, hx(who->card.agent_id, 32));
    snprintf(s, sizeof s,
             "{\"valid\":true,\"kind\":\"CONSENT\",\"from\":\"%s\",\"seq\":\"%llu\",\"ts\":%llu,"
             "\"answers\":%u,\"grant\":%s,\"action\":\"%s\",\"bytes\":%u}",
             from, (unsigned long long) seq, (unsigned long long) ts, id, grant ? "true" : "false",
             hx(act, 32), ANSWER_BYTES);
    return s;
}

#define INVALID(why) ok("{\"valid\":false,\"reason\":\"%s\"}", why)

/* ----- tests ----- */

static const char *const OFFMSG =
    "web4 is off: start zxvd with --enable web4, or call features.set";
static const char *const NOTOPEN = "the feed is not open: call web4.open first";
#define CLOSED                                                                                     \
    "{\"open\":false,\"agent_id\":null,\"envelopes\":[],\"next\":null,\"total\":0,\"held\":0}"

static void test_switch(void)
{
    fresh();
    const zxvd_group_t *g = zxvd_web4_group();
    CHECK(!g->regulated && !strcmp(g->feature, "web4") && g->n_methods == 6, "group shape");
    CHECK(g->methods[0].always && g->methods[1].always && g->methods[2].always &&
              !g->methods[3].always && !g->methods[4].always && !g->methods[5].always,
          "reads answer while off");
    const char *d = rpc(req("rpc.discover", "{}"));
    CHECK(strstr(d, "\"web4.card_verify\",\"web4.feed\",\"web4.envelope\",\"web4.verify\","
                    "\"web4.open\",\"web4.post\",\"web4.consent\"]") != NULL,
          "discover lists the web4 methods: %.300s", d);
    EXPECT(req("web4.open", "{}"), errc(-32012, "%s", OFFMSG));
    EXPECT(req("web4.post", "{}"), errc(-32012, "%s", OFFMSG));
    EXPECT(req("web4.consent", "{}"), errc(-32012, "%s", OFFMSG));
    EXPECT(req("web4.feed", "{}"), ok(CLOSED));
    EXPECT(req("web4.envelope", "{\"id\":1}"), errc(E4, "no envelope 1 in the feed"));
    EXPECT(req("web4.verify", "{\"envelope\":\"00\"}"),
           errc(E4, "give the sender's \\\"pk\\\", or open the feed"));
    EXPECT(req("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"web4\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"Web4 envelope feed (kernel/src/web4): the owner's "
              "companion signs envelopes with ML-DSA-65; personal data and money wait for "
              "consent; in memory, no network delivery\",\"methods\":6}]}],\"saved\":false}"));
    CHECK(zxvd_rpc_enable("web4") == 0 && zxvd_rpc_enabled("web4"), "enable web4");
    CHECK(RCOUNT == 0, "nothing drawn");
}

static void test_open(void)
{
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\"}", TOHEX),
           errc(E4, "%s", NOTOPEN));
    EXPECT(req("web4.consent", "{\"id\":1,\"grant\":true}"), errc(E4, "%s", NOTOPEN));
    EXPECT(req("web4.open", "{\"name\":\"Big\"}"),
           errc(EP, "\\\"name\\\" must be 1..31 bytes of a-z 0-9 - _ ."));
    EXPECT(req("web4.open", "{\"name\":\"%s\"}", "abcdefghijabcdefghijabcdefghijab"),
           errc(EP, "\\\"name\\\" must be a string of at most 31 bytes"));
    EXPECT(req("web4.open", "{\"seed\":\"0011\"}"),
           errc(EP, "\\\"seed\\\" must be 32 bytes of hex"));
    EXPECT(req("web4.open", "{\"now\":-1}"), errc(EP, "\\\"now\\\" must be unix seconds"));
    EXPECT(req("web4.open", "{\"now\":253402300800}"),
           errc(EP, "\\\"now\\\" must be unix seconds"));
    EXPECT(req("web4.open", "{\"now\":1.5}"),
           errc(EP, "\\\"now\\\" must be an integer (no fraction or exponent)"));
    RFAIL = true;
    EXPECT(req("web4.open", "{}"), errc(-32603, "no random bytes"));
    EXPECT(req("web4.open", "{\"seed\":\"%s\"}", SEEDHEX), errc(-32603, "no random bytes"));
    RFAIL = false;
    RCOUNT = 0;
    RFAIL_AT = 1; /* the seed draws, the binding signature's does not */
    EXPECT(req("web4.open", "{}"), errc(-32603, "no random bytes"));
    RFAIL_AT = -1;
    EXPECT(req("web4.feed", "{}"), ok(CLOSED));
    /* a key from the OS (two draws: seed, then the binding signature) */
    RCOUNT = 0;
    uint8_t s[32], r[32];
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (i * 3);
    for (int i = 0; i < 32; i++) r[i] = (uint8_t) (1 + i * 3);
    w4_agent_t a;
    CHECK(w4_agent_create(&a, s, "fox.2", NULL, NULL, MS, r) == W4_OK, "os mirror");
    char pk[2 * W4_PK_LEN + 1];
    strcpy(pk, hx(a.card.pk, W4_PK_LEN));
    char id[65];
    strcpy(id, hx(a.card.agent_id, 32));
    EXPECT(req("web4.open", "{\"name\":\"fox.2\",\"now\":%llu}", NOW),
           ok("{\"open\":true,\"new\":true,\"agent_id\":\"%s\",\"name\":\"fox.2\",\"pk\":\"%s\","
              "\"envelopes\":0,\"held\":0,\"max_envelopes\":64,\"max_held\":32,\"in_memory\":true}",
              id, pk));
    CHECK(RCOUNT == 2, "two draws");
    /* open again: unchanged, nothing drawn, params still checked */
    EXPECT(req("web4.open", "{\"name\":\"other\",\"seed\":\"%s\"}", SEEDHEX),
           ok("{\"open\":true,\"new\":false,\"agent_id\":\"%s\",\"name\":\"fox.2\",\"pk\":\"%s\","
              "\"envelopes\":0,\"held\":0,\"max_envelopes\":64,\"max_held\":32,\"in_memory\":true}",
              id, pk));
    EXPECT(req("web4.open", "{\"seed\":\"00\"}"), errc(EP, "\\\"seed\\\" must be 32 bytes of hex"));
    CHECK(RCOUNT == 2, "nothing drawn again");
    EXPECT(req("web4.feed", "{}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[],\"next\":null,\"total\":0,"
              "\"held\":0}",
              id));
    /* the default name, from a seed (one draw) */
    fresh();
    zxvd_rpc_enable("web4");
    open_feed();
    EXPECT(req("web4.open", "{}"),
           ok("{\"open\":true,\"new\":false,\"agent_id\":\"%s\",\"name\":\"chiglet\",\"pk\":\"%s\","
              "\"envelopes\":0,\"held\":0,\"max_envelopes\":64,\"max_held\":32,\"in_memory\":true}",
              MEID, hx(ME.card.pk, W4_PK_LEN)));
    CHECK(RCOUNT == DRAW, "draws in step");
}

static w4_env_t E;
static uint8_t B[W4_ENV_MAX];

static void test_post(void)
{
    /* parameter errors draw nothing and add nothing */
    EXPECT(req("web4.post", "{\"to\":\"%s\"}", TOHEX),
           errc(EP, "missing string parameter \\\"kind\\\""));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\"}"),
           errc(EP, "missing hex parameter \\\"to\\\""));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"00\"}"),
           errc(EP, "\\\"to\\\" must be 32 bytes of hex"));
    EXPECT(req("web4.post", "{\"kind\":\"CANCEL\",\"to\":\"%s\"}", TOHEX),
           errc(EP, "\\\"kind\\\" must be REQUEST, RESPONSE or STREAM"));
    EXPECT(req("web4.post", "{\"kind\":\"request\",\"to\":\"%s\"}", TOHEX),
           errc(EP, "\\\"kind\\\" must be REQUEST, RESPONSE or STREAM"));
    EXPECT(req("web4.post", "{\"kind\":\"CONSENT\",\"to\":\"%s\"}", TOHEX),
           errc(EP, "a CONSENT envelope answers a held one: use web4.consent"));
    EXPECT(req("web4.post", "{\"kind\":\"RESPONSE\",\"to\":\"%s\"}", TOHEX),
           errc(EP, "\\\"req_id\\\" names the request this RESPONSE answers"));
    EXPECT(req("web4.post", "{\"kind\":\"STREAM\",\"to\":\"%s\"}", TOHEX),
           errc(EP, "\\\"req_id\\\" names the request this STREAM answers"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"req_id\":0}", TOHEX),
           errc(EP, "\\\"req_id\\\" must be a positive integer"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"req_id\":-4}", TOHEX),
           errc(EP, "\\\"req_id\\\" must be a positive integer"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"req_id\":1.5}", TOHEX),
           errc(EP, "\\\"req_id\\\" must be an integer or a decimal string (no fraction or "
                    "exponent)"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"amount_minor\":-1}", TOHEX),
           errc(EP, "\\\"amount_minor\\\" must not be negative"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"amount_minor\":\"x\"}", TOHEX),
           errc(EP, "\\\"amount_minor\\\" must be an integer or a decimal string (no fraction or "
                    "exponent)"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"tool\":\"Bad\"}", TOHEX),
           errc(EP, "\\\"tool\\\" must be 1..31 bytes of a-z 0-9 - _ ."));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"chunk\":-1}", TOHEX),
           errc(EP, "\\\"chunk\\\" is out of range"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"final\":1}", TOHEX),
           errc(EP, "\\\"final\\\" must be true or false"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"pii\":\"yes\"}", TOHEX),
           errc(EP, "\\\"pii\\\" must be true or false"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"now\":-5}", TOHEX),
           errc(EP, "\\\"now\\\" must be unix seconds"));
    static char big[2100];
    memset(big, 'a', 2049);
    big[2049] = 0;
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"payload\":\"%s\"}", TOHEX, big),
           errc(EP, "\\\"payload\\\" must be a string of at most 2048 bytes"));
    RFAIL = true;
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\"}", TOHEX),
           errc(-32603, "no random bytes"));
    RFAIL = false;
    EXPECT(req("web4.feed", "{}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[],\"next\":null,\"total\":0,"
              "\"held\":0}",
              MEID));

    /* 1: a REQUEST with the defaults (tool "chat", req_id = its id) */
    CHECK(w4_env_make(&E, W4_MSG_REQUEST, ME.card.agent_id, TO, 1, 0, "chat",
                      (const uint8_t *) "hi there", 8) == W4_OK,
          "mirror 1");
    uint32_t n = seal(&E, MS, B);
    X x1 = {1,     "REQUEST", "signed", TOHEX, "chat", 1, 0,  false, "hi there",
            false, 0,         E.seq,    NOW,   0,      0, -1, n};
    EXPECT(req("web4.post",
               "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"payload\":\"hi there\","
               "\"now\":%llu}",
               TOHEX, NOW),
           ok("%s", ej(&x1)));
    CHECK(E.seq == 1, "the first envelope is seq 1");
    EXPECT_BYTES(1, "REQUEST", B, n);
    EXPECT(req("web4.verify", "{\"id\":1}"), ok("%s", verify_env_json(&E, n)));

    /* 2: a final RESPONSE, escaped text, decimal-string req_id, upper-case to */
    char up[65];
    for (int i = 0; i < 64; i++) up[i] = (char) (TOHEX[i] >= 'a' ? TOHEX[i] - 32 : TOHEX[i]);
    up[64] = 0;
    const char *txt = "say \"hi\"\\\n\xc3\xa9";
    CHECK(w4_env_make(&E, W4_MSG_RESPONSE, ME.card.agent_id, TO, 77, 0, "chat.v2",
                      (const uint8_t *) txt, (uint32_t) strlen(txt)) == W4_OK,
          "mirror 2");
    E.flags = W4_MF_FINAL;
    n = seal(&E, MS + 5000u, B);
    X x2 = {2,       "RESPONSE", "signed",
            TOHEX,   "chat.v2",  77,
            0,       true,       "say \\\"hi\\\"\\\\\\u000a\xc3\xa9",
            false,   0,          E.seq,
            NOW + 5, 0,          0,
            -1,      n};
    EXPECT(req("web4.post",
               "{\"kind\":\"RESPONSE\",\"to\":\"%s\",\"tool\":\"chat.v2\","
               "\"req_id\":\"77\",\"final\":true,\"payload\":\"say \\\"hi\\\"\\\\\\n"
               "\\u00e9\",\"now\":%llu}",
               up, NOW + 5),
           ok("%s", ej(&x2)));
    EXPECT_BYTES(2, "RESPONSE", B, n);
    EXPECT(req("web4.verify", "{\"id\":2}"), ok("%s", verify_env_json(&E, n)));

    /* 3: a STREAM chunk, empty payload, explicit zero amount, pii false */
    CHECK(w4_env_make(&E, W4_MSG_STREAM, ME.card.agent_id, TO, 77, 3, "chat", NULL, 0) == W4_OK,
          "mirror 3");
    n = seal(&E, MS, B);
    X x3 = {3,     "STREAM", "signed", TOHEX, "chat", 77, 3,  false, "",
            false, 0,        E.seq,    NOW,   0,      0,  -1, n};
    EXPECT(req("web4.post",
               "{\"kind\":\"STREAM\",\"to\":\"%s\",\"req_id\":77,\"chunk\":3,"
               "\"amount_minor\":0,\"pii\":false,\"now\":%llu}",
               TOHEX, NOW),
           ok("%s", ej(&x3)));
    EXPECT(req("web4.verify", "{\"id\":3}"), ok("%s", verify_env_json(&E, n)));
    EXPECT(req("web4.feed", "{}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[%s,%s,%s],\"next\":null,"
              "\"total\":3,\"held\":0}",
              MEID, ej(&x3), ej(&x2), ej(&x1)));
    CHECK(RCOUNT == DRAW, "draws in step");
}

static uint8_t ACT[32], AB[ANSWER_BYTES];

static void test_consent(void)
{
    /* 4: personal data: held, unsigned, nothing to read */
    const char *p4 = "my address is 1 Elm St";
    X x4 = {4, "REQUEST", "held", TOHEX, "share", 4, 0, false, p4, true, 0, 0, 0, 0, 0, -1, 0};
    EXPECT(req("web4.post",
               "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"tool\":\"share\",\"payload\":"
               "\"%s\",\"pii\":true,\"now\":%llu}",
               TOHEX, p4, NOW),
           ok("%s", ej(&x4)));
    CHECK(RCOUNT == DRAW, "a held envelope draws nothing");
    EXPECT(req("web4.envelope", "{\"id\":4}"),
           errc(E4, "envelope 4 is held for consent: nothing leaves until the owner answers "
                    "(web4.consent)"));
    EXPECT(req("web4.verify", "{\"id\":4}"),
           errc(E4, "envelope 4 is held for consent: nothing leaves until the owner answers "
                    "(web4.consent)"));
    EXPECT(req("web4.feed", "{\"status\":\"held\"}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[%s],\"next\":null,\"total\":4,"
              "\"held\":1}",
              MEID, ej(&x4)));

    /* parameter errors */
    EXPECT(req("web4.consent", "{\"grant\":true}"),
           errc(EP, "missing integer parameter \\\"id\\\""));
    EXPECT(req("web4.consent", "{\"id\":4}"), errc(EP, "missing boolean parameter \\\"grant\\\""));
    EXPECT(req("web4.consent", "{\"id\":4,\"grant\":1}"),
           errc(EP, "\\\"grant\\\" must be true or false"));
    EXPECT(req("web4.consent", "{\"id\":4,\"grant\":true,\"expires_in\":0}"),
           errc(EP, "\\\"expires_in\\\" must be 1..86400"));
    EXPECT(req("web4.consent", "{\"id\":4,\"grant\":true,\"expires_in\":86401}"),
           errc(EP, "\\\"expires_in\\\" must be 1..86400"));
    EXPECT(req("web4.consent", "{\"id\":4,\"grant\":true,\"now\":-2}"),
           errc(EP, "\\\"now\\\" must be unix seconds"));
    EXPECT(req("web4.consent", "{\"id\":99,\"grant\":true}"),
           errc(E4, "no envelope 99 in the feed"));
    EXPECT(req("web4.consent", "{\"id\":1,\"grant\":true}"),
           errc(E4, "envelope 1 is not waiting for consent"));
    /* each draw of a grant can fail; nothing changes */
    for (int k = 0; k < 4; k++) {
        RFAIL_AT = RCOUNT + k;
        EXPECT(req("web4.consent", "{\"id\":4,\"grant\":true}"), errc(-32603, "no random bytes"));
        RCOUNT = DRAW;
    }
    RFAIL_AT = -1;
    EXPECT(req("web4.feed", "{\"status\":\"held\"}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[%s],\"next\":null,\"total\":4,"
              "\"held\":1}",
              MEID, ej(&x4)));

    /* grant 4: the token, the CONSENT answer (5), then the sealed envelope */
    uint8_t ra[32], nonce[16], rc[32];
    draw(ra, 32);
    draw(nonce, 16);
    draw(rc, 32);
    CHECK(w4_env_make(&E, W4_MSG_REQUEST, ME.card.agent_id, TO, 4, 0, "share", (const uint8_t *) p4,
                      (uint32_t) strlen(p4)) == W4_OK,
          "mirror 4");
    w4_action_digest("share", ME.card.agent_id, TO, 4, E.payload, E.payload_len, ACT);
    CHECK(w4_consent_issue(ME.card.pk, ME.sk, ME.card.agent_id, ACT, W4_CONSENT_PII, 0, MS + 60000u,
                           MS + 60000u + 120000u, nonce, rc, &E.consent) == W4_OK,
          "mirror token");
    uint64_t aseq = ME.next_seq++;
    answer(true, aseq, MS + 60000u, 4, ACT, ra, &ME, AB);
    E.flags = W4_MF_CONSENT;
    uint32_t n = seal(&E, MS + 60000u, B);
    X g4 = {4,    "REQUEST", "signed", TOHEX,    "share", 4, 0,  false, p4,
            true, 0,         E.seq,    NOW + 60, 5,       0, -1, n};
    X a5 = {5,    "CONSENT", "signed", TOHEX,    "share", 4, 0, false,       "",
            true, 0,         aseq,     NOW + 60, 0,       4, 1, ANSWER_BYTES};
    char want[12000];
    snprintf(want, sizeof want, "{\"envelope\":%s,\"consent\":%s}", ej(&g4), ej(&a5));
    EXPECT(
        req("web4.consent", "{\"id\":4,\"grant\":true,\"expires_in\":120,\"now\":%llu}", NOW + 60),
        ok("%s", want));
    CHECK(aseq + 1 == E.seq, "the answer is signed before the envelope");
    EXPECT_BYTES(4, "REQUEST", B, n);
    EXPECT_BYTES(5, "CONSENT", AB, ANSWER_BYTES);
    EXPECT(req("web4.verify", "{\"id\":4}"), ok("%s", verify_env_json(&E, n)));
    EXPECT(req("web4.verify", "{\"id\":5}"),
           ok("%s", verify_answer_json(&ME, true, aseq, NOW + 60, 4, ACT)));
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(AB, ANSWER_BYTES)),
           ok("%s", verify_answer_json(&ME, true, aseq, NOW + 60, 4, ACT)));
    EXPECT(req("web4.consent", "{\"id\":4,\"grant\":false}"),
           errc(E4, "envelope 4 is not waiting for consent"));
    EXPECT(req("web4.consent", "{\"id\":5,\"grant\":true}"),
           errc(E4, "envelope 5 is not waiting for consent"));

    /* 6: money (an amount as a decimal string): held; deny drops it (answer 7) */
    X x6 = {6,     "REQUEST", "held", TOHEX, "pay", 6, 0,  false, "invoice 12",
            false, 2500,      0,      0,     0,     0, -1, 0};
    EXPECT(req("web4.post",
               "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"tool\":\"pay\",\"payload\":"
               "\"invoice 12\",\"amount_minor\":\"2500\",\"now\":%llu}",
               TOHEX, NOW),
           ok("%s", ej(&x6)));
    RFAIL = true;
    EXPECT(req("web4.consent", "{\"id\":6,\"grant\":false}"), errc(-32603, "no random bytes"));
    RFAIL = false;
    draw(ra, 32);
    w4_action_digest("pay", ME.card.agent_id, TO, 6, (const uint8_t *) "invoice 12", 10, ACT);
    aseq = ME.next_seq++;
    answer(false, aseq, MS + 7000u, 6, ACT, ra, &ME, AB);
    X d6 = {6, "REQUEST", "denied", TOHEX, "pay", 6, 0, false, "", false, 2500, 0, 0, 7, 0, -1, 0};
    X a7 = {7,     "CONSENT", "signed", TOHEX,   "pay", 6, 0, false,       "",
            false, 2500,      aseq,     NOW + 7, 0,     6, 0, ANSWER_BYTES};
    snprintf(want, sizeof want, "{\"envelope\":%s,\"consent\":%s}", ej(&d6), ej(&a7));
    EXPECT(req("web4.consent", "{\"id\":6,\"grant\":false,\"now\":%llu}", NOW + 7), ok("%s", want));
    CHECK(RCOUNT == DRAW, "a denial draws once");
    EXPECT(req("web4.envelope", "{\"id\":6}"),
           errc(E4, "envelope 6 was denied: it was dropped and never signed"));
    EXPECT(req("web4.verify", "{\"id\":6}"),
           errc(E4, "envelope 6 was denied: it was dropped and never signed"));
    EXPECT(req("web4.consent", "{\"id\":6,\"grant\":true}"),
           errc(E4, "envelope 6 is not waiting for consent"));
    EXPECT_BYTES(7, "CONSENT", AB, ANSWER_BYTES);
    EXPECT(req("web4.verify", "{\"id\":7}"),
           ok("%s", verify_answer_json(&ME, false, aseq, NOW + 7, 6, ACT)));

    /* 8: money and personal data, granted with the default ten minutes (answer 9) */
    X x8 = {8,    "STREAM", "held", TOHEX, "pay", 6, 1,  true, "card 4111",
            true, 99,       0,      0,     0,     0, -1, 0};
    EXPECT(req("web4.post",
               "{\"kind\":\"STREAM\",\"to\":\"%s\",\"tool\":\"pay\",\"req_id\":6,"
               "\"chunk\":1,\"final\":true,\"payload\":\"card 4111\",\"pii\":true,"
               "\"amount_minor\":99,\"now\":%llu}",
               TOHEX, NOW),
           ok("%s", ej(&x8)));
    draw(ra, 32);
    draw(nonce, 16);
    draw(rc, 32);
    CHECK(w4_env_make(&E, W4_MSG_STREAM, ME.card.agent_id, TO, 6, 1, "pay",
                      (const uint8_t *) "card 4111", 9) == W4_OK,
          "mirror 8");
    w4_action_digest("pay", ME.card.agent_id, TO, 6, E.payload, 9, ACT);
    CHECK(w4_consent_issue(ME.card.pk, ME.sk, ME.card.agent_id, ACT,
                           W4_CONSENT_PII | W4_CONSENT_MONEY, 99, MS, MS + 600000u, nonce, rc,
                           &E.consent) == W4_OK,
          "mirror token 8");
    aseq = ME.next_seq++;
    answer(true, aseq, MS, 8, ACT, ra, &ME, AB);
    E.flags = W4_MF_FINAL | W4_MF_CONSENT;
    n = seal(&E, MS, B);
    X g8 = {8,    "STREAM", "signed", TOHEX, "pay", 6, 1,  true, "card 4111",
            true, 99,       E.seq,    NOW,   9,     0, -1, n};
    X a9 = {9,    "CONSENT", "signed", TOHEX, "pay", 6, 0, false,       "",
            true, 99,        aseq,     NOW,   0,     8, 1, ANSWER_BYTES};
    snprintf(want, sizeof want, "{\"envelope\":%s,\"consent\":%s}", ej(&g8), ej(&a9));
    EXPECT(req("web4.consent", "{\"id\":8,\"grant\":true,\"now\":%llu}", NOW), ok("%s", want));
    EXPECT_BYTES(8, "STREAM", B, n);
    EXPECT(req("web4.verify", "{\"id\":8}"), ok("%s", verify_env_json(&E, n)));
    CHECK(strstr(verify_env_json(&E, n), "\"consent_scope\":{\"pii\":true,\"money\":true},"
                                         "\"max_minor\":\"99\"") != NULL,
          "scope and ceiling");
    EXPECT(req("web4.feed", "{\"limit\":3}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[%s,%s,%s],\"next\":7,"
              "\"total\":9,\"held\":0}",
              MEID, ej(&a9), ej(&g8), ej(&a7)));
    EXPECT(req("web4.feed", "{\"status\":\"denied\"}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[%s],\"next\":null,\"total\":9,"
              "\"held\":0}",
              MEID, ej(&d6)));
    CHECK(RCOUNT == DRAW, "draws in step");
}

static void test_feed_pages(void)
{
    EXPECT(req("web4.feed", "{\"limit\":0}"), errc(EP, "\\\"limit\\\" must be 1..50"));
    EXPECT(req("web4.feed", "{\"limit\":51}"), errc(EP, "\\\"limit\\\" must be 1..50"));
    EXPECT(req("web4.feed", "{\"before\":0}"), errc(EP, "\\\"before\\\" must be 1..4294967295"));
    EXPECT(req("web4.feed", "{\"before\":-1}"), errc(EP, "\\\"before\\\" is out of range"));
    EXPECT(req("web4.feed", "{\"status\":\"sent\"}"),
           errc(EP, "\\\"status\\\" must be signed, held or denied"));
    EXPECT(req("web4.feed", "{\"status\":\"signedxx\"}"),
           errc(EP, "\\\"status\\\" must be a string of at most 7 bytes"));
    /* ids newest first: 9 8 7 6 5 4 3 2 1; pages of 4 */
    const char *f = rpc(req("web4.feed", "{\"limit\":4}"));
    CHECK(strstr(f, "\"envelopes\":[{\"id\":9,") && strstr(f, "\"next\":6,\"total\":9"), "p1 %.80s",
          f);
    f = rpc(req("web4.feed", "{\"limit\":4,\"before\":6}"));
    CHECK(strstr(f, "\"envelopes\":[{\"id\":5,") && strstr(f, "{\"id\":2,") &&
              !strstr(f, "{\"id\":6,") && strstr(f, "\"next\":2,\"total\":9"),
          "p2 %.80s", f);
    f = rpc(req("web4.feed", "{\"limit\":4,\"before\":2}"));
    CHECK(strstr(f, "\"envelopes\":[{\"id\":1,") && strstr(f, "\"next\":null"), "p3 %.80s", f);
    f = rpc(req("web4.feed", "{\"limit\":1,\"before\":2}"));
    CHECK(strstr(f, "\"envelopes\":[{\"id\":1,") && strstr(f, "\"next\":null"), "exact %.80s", f);
    f = rpc(req("web4.feed", "{\"before\":1}"));
    CHECK(strstr(f, "\"envelopes\":[],\"next\":null,\"total\":9"), "before the first %.80s", f);
    f = rpc(req("web4.feed", "{\"status\":\"signed\",\"limit\":50}"));
    CHECK(strstr(f, "{\"id\":9,") && !strstr(f, "{\"id\":6,") && strstr(f, "{\"id\":1,") &&
              strstr(f, "\"next\":null"),
          "signed only %.80s", f);
}

static void test_verify(void)
{
    EXPECT(req("web4.verify", "{}"), errc(EP, "give \\\"id\\\" or \\\"envelope\\\", not both"));
    EXPECT(req("web4.verify", "{\"id\":1,\"envelope\":\"00\"}"),
           errc(EP, "give \\\"id\\\" or \\\"envelope\\\", not both"));
    EXPECT(req("web4.verify", "{\"id\":\"1\"}"),
           errc(EP, "\\\"id\\\" must be an integer (no fraction or exponent)"));
    EXPECT(req("web4.verify", "{\"id\":100}"), errc(E4, "no envelope 100 in the feed"));
    EXPECT(req("web4.verify", "{\"envelope\":\"0g\"}"),
           errc(EP, "\\\"envelope\\\" must be a hex string of at most %u bytes", W4_ENV_MAX));
    EXPECT(req("web4.verify", "{\"envelope\":\"00\",\"pk\":\"00\"}"),
           errc(EP, "\\\"pk\\\" must be %u bytes of hex", W4_PK_LEN));
    EXPECT(req("web4.envelope", "{}"), errc(EP, "missing integer parameter \\\"id\\\""));

    /* another companion's envelope, checked with its key */
    CHECK(w4_env_make(&E, W4_MSG_REQUEST, PEER.card.agent_id, ME.card.agent_id, 31, 2, "ask",
                      (const uint8_t *) "ping", 4) == W4_OK,
          "peer env");
    uint8_t r[32] = {9};
    int32_t n = w4_env_seal(&PEER, &E, MS + 1000u, r, B, sizeof B);
    char pk[2 * W4_PK_LEN + 1];
    strcpy(pk, hx(PEER.card.pk, W4_PK_LEN));
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(B, (uint32_t) n), pk),
           ok("%s", verify_env_json(&E, (uint32_t) n)));
    /* without its key, the owner's is used: not the sender */
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(B, (uint32_t) n)),
           INVALID("the key is not the sender's"));
    /* a flipped byte in the payload: the signature fails */
    B[n - W4_SIG_LEN - 1] ^= 1;
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(B, (uint32_t) n), pk),
           INVALID("signature"));
    B[n - W4_SIG_LEN - 1] ^= 1;
    /* cut short: malformed */
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(B, (uint32_t) n - 1), pk),
           INVALID("malformed"));
    EXPECT(req("web4.verify", "{\"envelope\":\"\"}"), INVALID("malformed"));
    EXPECT(req("web4.verify", "{\"envelope\":\"573443\"}"), INVALID("malformed"));

    /* an owner-signed envelope whose token was issued for another action */
    w4_agent_t tmp = ME;
    uint8_t other[32] = {1}, nonce[16] = {2};
    CHECK(w4_env_make(&E, W4_MSG_REQUEST, ME.card.agent_id, TO, 50, 0, "pay", (const uint8_t *) "x",
                      1) == W4_OK,
          "bad-token env");
    CHECK(w4_consent_issue(ME.card.pk, ME.sk, ME.card.agent_id, other, W4_CONSENT_MONEY, 5, MS,
                           MS + 1000u, nonce, r, &E.consent) == W4_OK,
          "token for another action");
    E.flags = W4_MF_CONSENT;
    n = w4_env_seal(&tmp, &E, MS, r, B, sizeof B);
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(B, (uint32_t) n)), INVALID("consent"));
    /* the same with the right action verifies, and only checks, never spends it */
    w4_action_digest("pay", ME.card.agent_id, TO, 50, (const uint8_t *) "x", 1, other);
    CHECK(w4_consent_issue(ME.card.pk, ME.sk, ME.card.agent_id, other, W4_CONSENT_MONEY, 5, MS,
                           MS + 1000u, nonce, r, &E.consent) == W4_OK,
          "token for this action");
    n = w4_env_seal(&tmp, &E, MS, r, B, sizeof B);
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(B, (uint32_t) n)),
           ok("%s", verify_env_json(&E, (uint32_t) n)));
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(B, (uint32_t) n)),
           ok("%s", verify_env_json(&E, (uint32_t) n)));

    /* CONSENT answers: another key's, tampered, malformed */
    uint8_t act[32] = {7};
    answer(true, 3, MS, 12, act, r, &PEER, AB);
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES), pk),
           ok("%s", verify_answer_json(&PEER, true, 3, NOW, 12, act)));
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\"}", hx(AB, ANSWER_BYTES)),
           INVALID("the key is not the sender's"));
    AB[60] ^= 1;
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES), pk),
           INVALID("signature"));
    AB[60] ^= 1;
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES - 1), pk),
           INVALID("malformed"));
    AB[4] = 2;
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES), pk),
           INVALID("malformed"));
    AB[4] = 1;
    AB[5] = 2;
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES), pk),
           INVALID("malformed"));
    /* a refused answer (grant 0) with the other key */
    answer(false, 4, MS + 2000u, 13, act, r, &PEER, AB);
    EXPECT(req("web4.verify", "{\"envelope\":\"%s\",\"pk\":\"%s\"}", hx(AB, ANSWER_BYTES), pk),
           ok("%s", verify_answer_json(&PEER, false, 4, NOW + 2, 13, act)));
    CHECK(RCOUNT == DRAW, "verify draws nothing");
}

/* posts a plain request with this id, mirrored */
static void post_plain(uint32_t id)
{
    CHECK(w4_env_make(&E, W4_MSG_REQUEST, ME.card.agent_id, TO, id, 0, "chat", NULL, 0) == W4_OK,
          "plain");
    uint32_t n = seal(&E, MS, B);
    X x = {id,    "REQUEST", "signed", TOHEX, "chat", id, 0,  false, "",
           false, 0,         E.seq,    NOW,   0,      0,  -1, n};
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"now\":%llu}", TOHEX, NOW),
           ok("%s", ej(&x)));
}

static void post_held(uint32_t id)
{
    X x = {id, "REQUEST", "held", TOHEX, "chat", id, 0, false, "p", true, 0, 0, 0, 0, 0, -1, 0};
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"payload\":\"p\",\"pii\":true}",
               TOHEX),
           ok("%s", ej(&x)));
}

/* grants id, mirrored: the answer and the sealed envelope each take a seq */
static void grant(uint32_t id, uint32_t ttl, uint64_t now)
{
    uint8_t ra[32], nonce[16], rc[32];
    draw(ra, 32);
    draw(nonce, 16);
    draw(rc, 32);
    ME.next_seq++;
    uint8_t rr[32];
    draw(rr, 32);
    ME.next_seq++;
    const char *got = rpc(req("web4.consent",
                              "{\"id\":%u,\"grant\":true,\"expires_in\":%u,"
                              "\"now\":%llu}",
                              id, ttl, (unsigned long long) now));
    char w[64];
    snprintf(w, sizeof w, "{\"envelope\":{\"id\":%u,", id);
    CHECK(strstr(got, w) && strstr(got, "\"status\":\"signed\""), "grant %u: %.200s", id, got);
}

static void test_limits(void)
{
    fresh();
    zxvd_rpc_enable("web4");
    open_feed();
    /* 32 held at most */
    for (uint32_t i = 1; i <= 32; i++) post_held(i);
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"pii\":true}", TOHEX),
           errc(E4, "32 envelopes already wait for consent: answer one with web4.consent"));
    EXPECT(req("web4.post", "{\"kind\":\"REQUEST\",\"to\":\"%s\",\"amount_minor\":1}", TOHEX),
           errc(E4, "32 envelopes already wait for consent: answer one with web4.consent"));
    /* a plain one still goes */
    for (uint32_t i = 33; i <= 64; i++) post_plain(i);
    const char *f = rpc(req("web4.feed", "{\"limit\":1}"));
    CHECK(strstr(f, "\"total\":64,\"held\":32"), "full: %.120s", f);
    /* full: the oldest entry that is not held (33) makes room; held 1..32 stay */
    post_plain(65);
    f = rpc(req("web4.feed", "{\"limit\":50,\"before\":40}"));
    CHECK(!strstr(f, "{\"id\":33,") && strstr(f, "{\"id\":34,") && strstr(f, "{\"id\":1,") &&
              strstr(f, "\"total\":64,\"held\":32"),
          "evicted 33: %.120s", f);
    EXPECT(req("web4.envelope", "{\"id\":33}"), errc(E4, "no envelope 33 in the feed"));
    /* granting 1 adds answer 66 and evicts 34; 1 stays */
    grant(1, 86400, NOW);
    f = rpc(req("web4.feed", "{\"limit\":50,\"before\":40}"));
    CHECK(!strstr(f, "{\"id\":34,") &&
              strstr(f, "{\"id\":1,\"kind\":\"REQUEST\",\"status\":"
                        "\"signed\"") &&
              strstr(f, "\"consent\":66") && strstr(f, "\"total\":64,\"held\":31"),
          "answered 1: %.300s", f);
    /* 64 unexpired consents at most: grant 2..32 and 32 more */
    for (uint32_t i = 2; i <= 32; i++) grant(i, 86400, NOW);
    uint32_t next = 98;
    for (uint32_t i = 0; i < 32; i++) {
        post_held(next);
        grant(next, 86400, NOW);
        next += 2;
    }
    f = rpc(req("web4.feed", "{\"limit\":1}"));
    CHECK(strstr(f, "\"held\":0"), "all answered: %.200s", f);
    post_held(next);
    /* the 65th consent: the log is full of unexpired tokens (fail closed) */
    uint32_t s = RCOUNT;
    EXPECT(req("web4.consent", "{\"id\":%u,\"grant\":true,\"now\":%llu}", next, NOW),
           errc(E4, "64 consents are still unexpired: try again when one expires"));
    CHECK(RCOUNT == s + 4, "four draws");
    RCOUNT = DRAW;
    EXPECT(req("web4.feed", "{\"status\":\"held\",\"limit\":1}"),
           ok("{\"open\":true,\"agent_id\":\"%s\",\"envelopes\":[{\"id\":%u,\"kind\":\"REQUEST\","
              "\"status\":\"held\",\"from\":\"%s\",\"to\":\"%s\",\"tool\":\"chat\",\"req_id\":"
              "\"%u\",\"chunk\":0,\"final\":false,\"payload\":\"p\",\"pii\":true,\"amount_minor\":"
              "\"0\",\"seq\":null,\"ts\":null,\"consent\":null,\"answers\":null,\"grant\":null,"
              "\"bytes\":0}],\"next\":null,\"total\":64,\"held\":1}",
              MEID, next, MEID, TOHEX, next));
    /* a day later the old tokens have expired: it goes through */
    grant(next, 600, NOW + 86400);
    /* a denial needs no token */
    post_held(next + 2);
    uint8_t ra[32];
    draw(ra, 32);
    ME.next_seq++;
    f = rpc(req("web4.consent", "{\"id\":%u,\"grant\":false}", next + 2));
    CHECK(strstr(f, "\"status\":\"denied\""), "deny: %.120s", f);
    CHECK(RCOUNT == DRAW, "draws in step");
}

static void test_off_keeps_state(void)
{
    EXPECT(req("features.set", "{\"feature\":\"web4\",\"enabled\":false}"),
           ok("{\"feature\":\"web4\",\"enabled\":false,\"regulated\":false,\"saved\":false}"));
    EXPECT(req("web4.post", "{}"), errc(-32012, "%s", OFFMSG));
    const char *f = rpc(req("web4.feed", "{\"limit\":1}"));
    CHECK(strstr(f, "\"open\":true") && strstr(f, "\"total\":64"), "the feed reads while off");
    CHECK(zxvd_rpc_enable("web4") == 0, "on again");
    f = rpc(req("web4.open", "{}"));
    CHECK(strstr(f, "\"new\":false") && strstr(f, MEID), "same companion: %.100s", f);
    /* close forgets the feed and the key */
    fresh();
    zxvd_rpc_enable("web4");
    EXPECT(req("web4.feed", "{}"), ok(CLOSED));
    EXPECT(req("web4.envelope", "{\"id\":1}"), errc(E4, "no envelope 1 in the feed"));
    zxvd_rpc_groups_reset();
}

int main(void)
{
    for (int i = 0; i < 32; i++) SEED[i] = (uint8_t) (i * 7 + 1);
    for (int i = 0; i < 32; i++) TO[i] = (uint8_t) (0xa0 + i);
    strcpy(SEEDHEX, hx(SEED, 32));
    strcpy(TOHEX, hx(TO, 32));
    uint8_t ps[32] = {5, 4, 3};
    CHECK(w4_agent_create(&PEER, ps, "peer", NULL, NULL, MS, NULL) == W4_OK, "peer");

    test_switch();
    test_open();
    test_post();
    test_consent();
    test_feed_pages();
    test_verify();
    test_limits();
    test_off_keeps_state();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_web4: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_web4: %d checks\n", checks);
    return 0;
}
