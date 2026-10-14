/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_pspace.c — zxvd_pspace.c (pspace.*, hall.*) behind the real RPC
 * layer: the feature switch, every parameter check, the network being off,
 * and two real net hosts on 127.0.0.1 (zxv_net_host.h U14) sharing a watch
 * hall, with the group bound to one host and then the other. Errors are
 * compared exactly with literal JSON; results that carry ids and clocks are
 * checked piece by piece. */
#define _DEFAULT_SOURCE /* select() */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include "zxvd_groups.h"
#include "zxvd_pspace.h"
#include "vna_space.h"

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
static char REQB[4000], WANT[4000];

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
        printf("  FAIL line %d\n    req  %.300s\n    want %.900s\n    got  %.900s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *call(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *call(const char *method, const char *fmt, ...)
{
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_list ap;
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *err(int code, const char *msg)
{
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}

/* the reply to a call holds every one of the pieces (NULL-terminated) */
static bool holds(const char *got, ...)
{
    va_list ap;
    va_start(ap, got);
    bool all = strncmp(got, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":", 32) == 0;
    for (const char *p; (p = va_arg(ap, const char *)) != NULL;)
        if (!strstr(got, p)) all = false;
    va_end(ap);
    return all;
}
#define HOLDS(req, ...)                                                                            \
    do {                                                                                           \
        const char *g_ = rpc(req);                                                                 \
        CHECK(holds(g_, __VA_ARGS__, (const char *) NULL), "%.200s\n    got %.900s", req, g_);     \
    } while (0)

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_pspace_group()) == 0, "add the pspace group");
}

static const char *const ALL[] = {
    "pspace.create", "pspace.find",    "pspace.join", "pspace.leave", "pspace.post",
    "pspace.events", "pspace.members", "pspace.list", "hall.load",    "hall.play",
    "hall.pause",    "hall.seek",      "hall.status"};
#define NALL (sizeof ALL / sizeof ALL[0])

/* ----- W1: the switch ----- */

static void test_switch(void)
{
    fresh();
    zxvd_pspace_bind(NULL);
    EXPECT(call("features.get", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"features\":[{\"name\":\"pspace\","
           "\"enabled\":false,\"regulated\":false,\"groups\":[{\"note\":\"public spaces on the "
           "peer network and the synced watch hall\",\"methods\":13}]}],\"saved\":false}}");
    /* always on: reading and leaving; the rest wait for the switch */
    static const bool always[NALL] = {false, false, false, true,  false, true, true,
                                      true,  false, false, false, false, true};
    for (size_t i = 0; i < NALL; i++) {
        const char *got = rpc(call(ALL[i], "{\"cid\":\"bafy\"}"));
        bool off = strstr(got, "\"code\":-32012") != NULL;
        CHECK(off != always[i], "%s with the switch off: %.200s", ALL[i], got);
    }
    EXPECT(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"owner\",\"name\":\"x\"}"),
           err(-32012, "pspace is off: start zxvd with --enable pspace, or call features.set"));
    EXPECT(call("pspace.list", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"online\":false,\"spaces\":[]}}");
    EXPECT(call("hall.status", "{\"cid\":\"bafy\"}"), err(ZXVD_E_PSPACE, "the network is off"));
    CHECK(zxvd_rpc_enable("pspace") == 0, "pspace on");
}

/* ----- parameters, then W2: the network is off ----- */

#define BADP(req, msg) EXPECT(req, err(-32602, msg))
#define NET_OFF(req)   EXPECT(req, err(ZXVD_E_PSPACE, "the network is off"))

static void test_params(void)
{
    zxvd_pspace_bind(NULL);
    /* pspace.create */
    NET_OFF(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"owner\",\"name\":\"x\"}"));
    NET_OFF(call("pspace.create",
                 "{\"kind\":\"hall\",\"gov\":\"quorum\",\"name\":\"x\",\"quorum_pct\":1,"
                 "\"topic\":\"film\"}"));
    NET_OFF(call("pspace.create",
                 "{\"kind\":\"hall\",\"gov\":\"quorum\",\"name\":\"x\",\"quorum_pct\":100}"));
    BADP(call("pspace.create", "{\"kind\":\"tent\",\"gov\":\"owner\",\"name\":\"x\"}"),
         "\\\"kind\\\" must be \\\"room\\\" or \\\"hall\\\"");
    BADP(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"king\",\"name\":\"x\"}"),
         "\\\"gov\\\" must be \\\"owner\\\" or \\\"quorum\\\"");
    BADP(call("pspace.create",
              "{\"kind\":\"room\",\"gov\":\"quorum\",\"name\":\"x\",\"quorum_pct\":0}"),
         "\\\"quorum_pct\\\" must be 1..100");
    BADP(call("pspace.create",
              "{\"kind\":\"room\",\"gov\":\"quorum\",\"name\":\"x\",\"quorum_pct\":101}"),
         "\\\"quorum_pct\\\" must be 1..100");
    const char *got = rpc(call("pspace.create", "{\"gov\":\"owner\",\"name\":\"x\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "kind"), "create needs a kind: %s", got);
    got = rpc(call("pspace.create", "{\"kind\":\"room\",\"name\":\"x\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "gov"), "create needs a gov: %s", got);
    got = rpc(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"owner\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "name"), "create needs a name: %s", got);
    got = rpc(call("pspace.create",
                   "{\"kind\":\"room\",\"gov\":\"owner\",\"name\":\"x\",\"quorum_pct\":\"5\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "quorum_pct"), "quorum_pct is a number: %s", got);
    got = rpc(call("pspace.create", "{\"kind\":\"rooms\",\"gov\":\"owner\",\"name\":\"x\"}"));
    CHECK(strstr(got, "-32602") != NULL, "a kind too long for its buffer: %s", got);

    /* pspace.find and the CID-only methods */
    NET_OFF(call("pspace.find", "{\"topic\":\"film\"}"));
    got = rpc(call("pspace.find", "{}"));
    CHECK(strstr(got, "-32602") && strstr(got, "topic"), "find needs a topic: %s", got);
    static const char *const C[] = {"pspace.join", "pspace.leave", "pspace.members", "hall.status",
                                    "pspace.events"};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        NET_OFF(call(C[i], "{\"cid\":\"bafy\"}"));
        got = rpc(call(C[i], "{}"));
        CHECK(strstr(got, "-32602") && strstr(got, "cid"), "%s needs a cid: %s", C[i], got);
    }

    /* pspace.post */
    NET_OFF(call("pspace.post", "{\"cid\":\"bafy\",\"text\":\"hi\"}"));
    BADP(call("pspace.post", "{\"cid\":\"bafy\",\"text\":\"\"}"), "\\\"text\\\" must not be empty");
    got = rpc(call("pspace.post", "{\"cid\":\"bafy\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "text"), "post needs text: %s", got);
    got = rpc(call("pspace.post", "{\"text\":\"x\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "cid"), "post needs a cid: %s", got);
    {
        static char big[1100];
        memset(big, 'a', 1024);
        big[1024] = 0;
        NET_OFF(call("pspace.post", "{\"cid\":\"b\",\"text\":\"%s\"}", big));
        big[1024] = 'a';
        big[1025] = 0;
        got = rpc(call("pspace.post", "{\"cid\":\"b\",\"text\":\"%s\"}", big));
        CHECK(strstr(got, "-32602") != NULL, "1025 bytes of text are refused");
    }

    /* pspace.events */
    NET_OFF(call("pspace.events", "{\"cid\":\"bafy\",\"since\":42}"));
    NET_OFF(call("pspace.events", "{\"cid\":\"bafy\",\"since\":\"42\"}"));
    BADP(call("pspace.events", "{\"cid\":\"bafy\",\"since\":-1}"),
         "\\\"since\\\" must not be negative");
    got = rpc(call("pspace.events", "{\"cid\":\"bafy\",\"since\":\"x\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "since"), "since is a number: %s", got);

    /* hall.* */
    NET_OFF(call("hall.load", "{\"cid\":\"h\",\"media\":\"m\"}"));
    NET_OFF(call("hall.load", "{\"cid\":\"h\",\"media\":\"m\",\"pos_ms\":7}"));
    got = rpc(call("hall.load", "{\"cid\":\"h\"}"));
    CHECK(strstr(got, "-32602") && strstr(got, "media"), "hall.load needs media: %s", got);
    NET_OFF(call("hall.play", "{\"cid\":\"h\"}"));
    NET_OFF(call("hall.play", "{\"cid\":\"h\",\"pos_ms\":90000,\"rate_q16\":262144}"));
    NET_OFF(call("hall.play", "{\"cid\":\"h\",\"rate_q16\":1}"));
    BADP(call("hall.play", "{\"cid\":\"h\",\"rate_q16\":0}"),
         "\\\"rate_q16\\\" must be 1..262144 (65536 = normal speed)");
    BADP(call("hall.play", "{\"cid\":\"h\",\"rate_q16\":262145}"),
         "\\\"rate_q16\\\" must be 1..262144 (65536 = normal speed)");
    got = rpc(call("hall.play", "{\"cid\":\"h\",\"rate_q16\":-1}"));
    CHECK(strstr(got, "-32602") && strstr(got, "rate_q16"), "a negative rate: %s", got);
    BADP(call("hall.pause", "{\"cid\":\"h\",\"rate_q16\":65536}"),
         "\\\"rate_q16\\\" is only for hall.play");
    BADP(call("hall.load", "{\"cid\":\"h\",\"media\":\"m\",\"rate_q16\":65536}"),
         "\\\"rate_q16\\\" is only for hall.play");
    BADP(call("hall.play", "{\"cid\":\"h\",\"media\":\"m\"}"),
         "\\\"media\\\" is only for hall.load");
    NET_OFF(call("hall.pause", "{\"cid\":\"h\"}"));
    NET_OFF(call("hall.seek", "{\"cid\":\"h\",\"pos_ms\":0}"));
    BADP(call("hall.seek", "{\"cid\":\"h\"}"), "missing integer parameter \\\"pos_ms\\\"");
    BADP(call("hall.seek", "{\"cid\":\"h\",\"pos_ms\":-5}"), "\\\"pos_ms\\\" must not be negative");
    got = rpc(call("hall.status", "{\"cid\":7}"));
    CHECK(strstr(got, "-32602") && strstr(got, "cid"), "a cid is text: %s", got);
}

/* ----- two real hosts ----- */

static void pump(zxv_net_t *a, zxv_net_t *b, int ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    int fa = (int) zxv_net_fd(a), fb = (int) zxv_net_fd(b), mx = -1;
    if (fa >= 0) FD_SET(fa, &rd), mx = fa;
    if (fb >= 0) FD_SET(fb, &rd), mx = fb > mx ? fb : mx;
    struct timeval tv = {0, ms * 1000};
    int r = select(mx + 1, &rd, NULL, NULL, &tv);
    uint64_t now = zxv_net_wall_ms();
    if (r > 0 && fa >= 0 && FD_ISSET(fa, &rd)) zxv_net_on_readable(a, now);
    if (r > 0 && fb >= 0 && FD_ISSET(fb, &rd)) zxv_net_on_readable(b, now);
    zxv_net_tick(a, now);
    zxv_net_tick(b, now);
}

/* the "cid" a result names, copied out */
static bool cid_of(const char *got, char *cid, size_t n)
{
    const char *p = strstr(got, "\"cid\":\"");
    if (!p) return false;
    p += 7;
    const char *q = strchr(p, '"');
    if (!q || (size_t) (q - p) >= n) return false;
    memcpy(cid, p, (size_t) (q - p));
    cid[q - p] = 0;
    return true;
}

/* an integer field of a result (the first one with that name) */
static long long num(const char *got, const char *key)
{
    char k[64];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(got, k);
    return p ? strtoll(p + strlen(k), NULL, 10) : -1;
}

static void hex_id(zxv_net_t *n, char out[65])
{
    static const char hx[] = "0123456789abcdef";
    uint8_t id[32];
    zxv_net_node_id(n, id);
    for (int i = 0; i < 32; i++) out[2 * i] = hx[id[i] >> 4], out[2 * i + 1] = hx[id[i] & 15];
    out[64] = 0;
}

static void test_hosts(void)
{
    zxv_net_t *e = zxv_net_new(), *f = zxv_net_new();
    zxv_net_status_t se, sf;
    char cid[80], room[80], eid[65], fid[65], piece[200];
    CHECK(e && f, "two hosts");
    zxv_net_set_lan_discovery(e, false, 0);
    zxv_net_set_lan_discovery(f, false, 0);

    /* a host that is made but not started is still "off" */
    zxvd_pspace_bind(e);
    EXPECT(call("pspace.list", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"online\":false,\"spaces\":[]}}");
    EXPECT(call("pspace.join", "{\"cid\":\"bafy\"}"), err(ZXVD_E_PSPACE, "the network is off"));

    uint64_t now = zxv_net_wall_ms();
    CHECK(zxv_net_start(e, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0 &&
              zxv_net_start(f, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0,
          "E and F start in LAN mode");
    zxv_net_status(e, &se);
    zxv_net_status(f, &sf);
    hex_id(e, eid);
    hex_id(f, fid);
    (void) zxv_net_add_peer(f, "127.0.0.1", se.port, now);
    for (int i = 0; i < 100 && !zxv_net_knows(e, "127.0.0.1", sf.port); i++) pump(e, f, 20);
    CHECK(zxv_net_knows(e, "127.0.0.1", sf.port), "E knows F");

    /* E makes an owner hall */
    EXPECT(call("pspace.list", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"online\":true,\"spaces\":[]}}");
    const char *got = rpc(call("pspace.create", "{\"kind\":\"hall\",\"gov\":\"owner\","
                                                "\"name\":\"Movie \\\"night\\\"\","
                                                "\"topic\":\"film\"}"));
    CHECK(cid_of(got, cid, sizeof cid) && cid[0] == 'b' && strlen(cid) == 59,
          "pspace.create: a CIDv1 (%s)", got);
    snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"cid\":\"%s\"}}", cid);
    CHECK(!strcmp(got, WANT), "the result is only the cid: %s", got);
    snprintf(piece, sizeof piece, "\"creator\":\"%s\"", eid);
    HOLDS(call("pspace.list", "{}"), "\"online\":true", cid, "\"phase\":\"in\"",
          "\"name\":\"Movie \\\"night\\\"\"", "\"topic\":\"film\"", "\"kind\":\"hall\"",
          "\"gov\":\"owner\"", "\"quorum_pct\":", piece,
          "\"hall\":{\"set\":false,\"state\":\"stop\",\"media\":\"\",\"pos_ms\":0,",
          "\"rate_q16\":65536", "\"viewers\":1,\"may_control\":true}", "\"members\":1}]}");
    EXPECT(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"owner\",\"name\":\"\"}"),
           err(ZXVD_E_PSPACE, "refused: check the CID, the name (1..48 bytes) and the topic "
                              "([a-z0-9-], at most 24)"));
    EXPECT(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"owner\",\"name\":\"x\","
                                 "\"topic\":\"Film!\"}"),
           err(ZXVD_E_PSPACE, "refused: check the CID, the name (1..48 bytes) and the topic "
                              "([a-z0-9-], at most 24)"));

    /* W4: playback */
    EXPECT(call("hall.play", "{\"cid\":\"%s\"}", cid),
           err(ZXVD_E_PSPACE, "nothing loaded (hall.load first)"));
    EXPECT(call("hall.pause", "{\"cid\":\"%s\"}", cid),
           err(ZXVD_E_PSPACE, "nothing loaded (hall.load first)"));
    /* a seek while stopped needs nothing loaded */
    HOLDS(call("hall.seek", "{\"cid\":\"%s\",\"pos_ms\":9}", cid),
          "{\"set\":true,\"state\":\"stop\",\"media\":\"\",\"pos_ms\":9,");
    EXPECT(call("hall.play", "{\"cid\":\"%s\"}", cid),
           err(ZXVD_E_PSPACE, "nothing loaded (hall.load first)"));
    {
        char longm[80];
        memset(longm, 'm', 65);
        longm[65] = 0;
        EXPECT(call("hall.load", "{\"cid\":\"%s\",\"media\":\"%s\"}", cid, longm),
               err(ZXVD_E_PSPACE, "\\\"media\\\" must be at most 64 bytes"));
        longm[64] = 0;
        HOLDS(call("hall.load", "{\"cid\":\"%s\",\"media\":\"%s\"}", cid, longm),
              "\"state\":\"stop\"", longm);
    }
    snprintf(piece, sizeof piece, "\"author\":\"%s\"}", eid);
    HOLDS(call("hall.load", "{\"cid\":\"%s\",\"media\":\"bafkfilm\",\"pos_ms\":7}", cid),
          "{\"set\":true,\"state\":\"stop\",\"media\":\"bafkfilm\",\"pos_ms\":7,\"base_pos_ms\":7,",
          "\"rate_q16\":65536", "\"viewers\":1,\"may_control\":true", piece);
    HOLDS(call("hall.play", "{\"cid\":\"%s\",\"pos_ms\":60000,\"rate_q16\":131072}", cid),
          "\"state\":\"play\"", "\"base_pos_ms\":60000,", "\"rate_q16\":131072");
    got = rpc(call("hall.status", "{\"cid\":\"%s\"}", cid));
    long long p0 = num(got, "pos_ms"), t0 = num(got, "now_ms");
    for (int i = 0; i < 10; i++) pump(e, f, 20);
    got = rpc(call("hall.status", "{\"cid\":\"%s\"}", cid));
    long long p1 = num(got, "pos_ms"), t1 = num(got, "now_ms");
    CHECK(p0 >= 60000 && p1 - p0 == 2 * (t1 - t0) && t1 > t0,
          "the playhead runs at 2x (%lld ms over %lld ms)", p1 - p0, t1 - t0);
    HOLDS(call("hall.pause", "{\"cid\":\"%s\"}", cid), "\"state\":\"pause\"", "\"rate_q16\":131072",
          "\"media\":\"bafkfilm\"");
    got = rpc(call("hall.status", "{\"cid\":\"%s\"}", cid));
    p0 = num(got, "pos_ms");
    CHECK(p0 >= 60000 && p0 == num(got, "base_pos_ms"), "paused where it was: %s", got);
    for (int i = 0; i < 3; i++) pump(e, f, 20);
    got = rpc(call("hall.status", "{\"cid\":\"%s\"}", cid));
    CHECK(num(got, "pos_ms") == p0, "pause holds the playhead");
    HOLDS(call("hall.seek", "{\"cid\":\"%s\",\"pos_ms\":5000}", cid),
          "\"state\":\"pause\",\"media\":\"bafkfilm\",\"pos_ms\":5000,", "\"rate_q16\":131072");
    HOLDS(call("hall.play", "{\"cid\":\"%s\"}", cid), "\"state\":\"play\"", "\"rate_q16\":65536",
          "\"base_pos_ms\":5000,");
    HOLDS(call("hall.seek", "{\"cid\":\"%s\",\"pos_ms\":1000}", cid),
          "\"state\":\"play\",\"media\":\"bafkfilm\",", "\"base_pos_ms\":1000,");
    HOLDS(call("hall.pause", "{\"cid\":\"%s\",\"pos_ms\":3}", cid),
          "\"state\":\"pause\",\"media\":\"bafkfilm\",\"pos_ms\":3,");

    /* posts and the event log */
    HOLDS(call("pspace.post", "{\"cid\":\"%s\",\"text\":\"hi \\\"you\\\"\\n\\u00e9\"}", cid),
          "\"posted\":true}");
    got = rpc(call("pspace.events", "{\"cid\":\"%s\"}", cid));
    CHECK(holds(got, "\"kind\":\"hall\",", "\"state\":\"stop\",\"pos_ms\":7,\"rate_q16\":65536",
                "\"state\":\"play\",\"pos_ms\":60000,\"rate_q16\":131072", "\"kind\":\"post\",",
                "\"text\":\"hi \\\"you\\\"\\u000a\xc3\xa9\"", NULL),
          "pspace.events: %.900s", got);
    long long next = num(strstr(got, "],\"next\":") ? strstr(got, "],\"next\":") + 2 : got, "next");
    CHECK(next >= 8, "next is the last index (%lld)", next);
    snprintf(piece, sizeof piece,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"events\":[],\"next\":%lld}}", next);
    EXPECT(call("pspace.events", "{\"cid\":\"%s\",\"since\":%lld}", cid, next), piece);
    got = rpc(call("pspace.events", "{\"cid\":\"%s\",\"since\":%lld}", cid, next - 1));
    CHECK(holds(got, "\"kind\":\"post\"", NULL) && !strstr(got, "\"kind\":\"hall\"") &&
              num(strstr(got, "],\"next\":") + 2, "next") == next,
          "since: only what came after (%.300s)", got);
    EXPECT(call("pspace.members", "{\"cid\":\"%s\"}", cid),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"members\":[],\"count\":1}}");

    /* a room is not a hall */
    got = rpc(call("pspace.create", "{\"kind\":\"room\",\"gov\":\"quorum\",\"quorum_pct\":67,"
                                    "\"name\":\"Lobby\"}"));
    CHECK(cid_of(got, room, sizeof room) && strcmp(room, cid) != 0, "a room: %s", got);
    EXPECT(call("hall.status", "{\"cid\":\"%s\"}", room),
           err(ZXVD_E_PSPACE, "that space is a room, not a watch hall"));
    EXPECT(call("hall.load", "{\"cid\":\"%s\",\"media\":\"m\"}", room),
           err(ZXVD_E_PSPACE, "that space is a room, not a watch hall"));
    got = rpc(call("pspace.list", "{}"));
    CHECK(holds(got, "\"kind\":\"room\",\"gov\":\"quorum\",\"quorum_pct\":67", "\"name\":\"Lobby\"",
                "\"name\":\"Movie", NULL),
          "both spaces listed: %.600s", got);
    {
        /* only the hall carries a "hall" object */
        int halls = 0;
        for (const char *p = got; (p = strstr(p, "\"hall\":{")) != NULL; p++) halls++;
        CHECK(halls == 1, "one hall object in the list (%d)", halls);
    }

    /* bad CIDs and spaces this node is not in */
    EXPECT(call("pspace.join", "{\"cid\":\"not-a-cid\"}"),
           err(ZXVD_E_PSPACE, "\\\"cid\\\" is not a space CID (base32 CIDv1, raw sha2-256)"));
    EXPECT(call("hall.status", "{\"cid\":\"not-a-cid\"}"),
           err(ZXVD_E_PSPACE, "\\\"cid\\\" is not a space CID (base32 CIDv1, raw sha2-256)"));
    {
        uint8_t raw[VNA_CID_RAW_LEN];
        char other[VNA_CID_STR_MAX];
        vna_cid_raw((const uint8_t *) "elsewhere", 9, raw);
        CHECK(vna_cid_to_string(raw, other, sizeof other) > 0, "another CID");
        static const char *const M[] = {"pspace.leave", "pspace.members", "pspace.events",
                                        "hall.status"};
        for (size_t i = 0; i < sizeof M / sizeof M[0]; i++)
            EXPECT(call(M[i], "{\"cid\":\"%s\"}", other),
                   err(ZXVD_E_PSPACE, "not in that space (pspace.join first)"));
        EXPECT(call("pspace.post", "{\"cid\":\"%s\",\"text\":\"x\"}", other),
               err(ZXVD_E_PSPACE, "not in that space (pspace.join first)"));
        EXPECT(call("hall.play", "{\"cid\":\"%s\"}", other),
               err(ZXVD_E_PSPACE, "not in that space (pspace.join first)"));
    }

    /* F joins the hall by the CID alone */
    for (int i = 0; i < 50; i++) pump(e, f, 20);
    zxvd_pspace_bind(f);
    HOLDS(call("pspace.join", "{\"cid\":\"%s\"}", cid), "\"phase\":\"", cid);
    for (int i = 0; i < 300; i++) {
        got = rpc(call("pspace.list", "{}"));
        if (strstr(got, "\"phase\":\"in\"") && strstr(got, "\"set\":true")) break;
        pump(e, f, 20);
    }
    snprintf(piece, sizeof piece, "\"creator\":\"%s\"", eid);
    HOLDS(call("pspace.list", "{}"), "\"phase\":\"in\"", piece, "\"may_control\":false",
          "\"state\":\"pause\"", "\"media\":\"bafkfilm\"", "\"members\":2}");
    EXPECT(call("hall.play", "{\"cid\":\"%s\"}", cid),
           err(ZXVD_E_PSPACE, "only the hall's owner controls playback"));
    HOLDS(call("hall.status", "{\"cid\":\"%s\"}", cid), "\"pos_ms\":3,", "\"viewers\":2,",
          "\"may_control\":false");
    snprintf(piece, sizeof piece, "{\"id\":\"%s\",\"addr\":\"127.0.0.1:%u\",\"last_seen_ms\":", eid,
             (unsigned) se.port);
    HOLDS(call("pspace.members", "{\"cid\":\"%s\"}", cid), piece, "\"count\":2}");
    /* F posts bytes that are not UTF-8, straight on the host: E shows U+FFFD */
    {
        static const uint8_t junk[] = {'a',  0xff, 0xc3, 'b',  0xe0, 0x80, 0x80, 0xed, 0xa0, 0x80,
                                       0xf4, 0x90, 0x80, 0x80, 0xf0, 0x9f, 0x98, 0x80, 0x7f, 0xc3};
        uint8_t raw[VNA_CID_RAW_LEN];
        CHECK(vna_cid_from_string(cid, (uint32_t) strlen(cid), raw) == VNA_OK, "the raw CID");
        CHECK(zxv_net_space_post(f, raw, junk, sizeof junk, zxv_net_wall_ms()) == 0, "F posts");
    }
    zxvd_pspace_bind(e);
    for (int i = 0; i < 200; i++) {
        got = rpc(call("pspace.events", "{\"cid\":\"%s\",\"since\":%lld}", cid, next));
        if (strstr(got, "\"kind\":\"post\"")) break;
        pump(e, f, 20);
    }
    snprintf(piece, sizeof piece, "\"author\":\"%s\"", fid);
    CHECK(holds(got, piece,
                "\"text\":\"a\\ufffd\\ufffdb\\ufffd\\ufffd\\ufffd\\ufffd\\ufffd\\ufffd"
                "\\ufffd\\ufffd\\ufffd\\ufffd\xf0\x9f\x98\x80\\u007f\\ufffd\"",
                NULL),
          "F's post at E, invalid UTF-8 replaced: %.600s", got);
    HOLDS(call("pspace.list", "{}"), "\"may_control\":true", "\"viewers\":2,");

    /* W5: the directory */
    for (int i = 0; i < 300; i++) {
        got = rpc(call("pspace.find", "{\"topic\":\"film\"}"));
        if (strstr(got, "\"done\":true")) break;
        pump(e, f, 20);
    }
    CHECK(holds(got, "{\"topic\":\"film\",\"done\":true,\"found\":[", NULL),
          "pspace.find finishes: %.600s", got);
    snprintf(piece, sizeof piece, "\"host\":\"%s\"}", eid);
    CHECK(holds(got, "\"found\":[{\"cid\":", cid, "\"name\":\"Movie \\\"night\\\"\"",
                "\"kind\":\"hall\",\"gov\":\"owner\"", "\"members\":", piece, NULL),
          "the hall is found by topic: %.600s", got);
    got = rpc(call("pspace.find", "{\"topic\":\"film\"}"));
    CHECK(holds(got, "{\"topic\":\"film\",\"done\":", NULL), "a second find starts again: %s", got);
    EXPECT(call("pspace.find", "{\"topic\":\"Film!\"}"),
           err(ZXVD_E_PSPACE, "\\\"topic\\\" must be 1..24 characters of [a-z0-9-]"));

    /* leaving */
    zxvd_pspace_bind(f);
    snprintf(piece, sizeof piece, "{\"cid\":\"%s\",\"left\":true}", cid);
    HOLDS(call("pspace.leave", "{\"cid\":\"%s\"}", cid), piece);
    EXPECT(call("hall.status", "{\"cid\":\"%s\"}", cid),
           err(ZXVD_E_PSPACE, "not in that space (pspace.join first)"));
    EXPECT(call("pspace.list", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"online\":true,\"spaces\":[]}}");
    /* the network stopped: every space is gone */
    zxvd_pspace_bind(e);
    zxv_net_stop(e);
    EXPECT(call("pspace.list", "{}"),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"online\":false,\"spaces\":[]}}");
    zxvd_pspace_bind(NULL);
    zxv_net_free(e);
    zxv_net_free(f);
}

int main(void)
{
    test_switch();
    test_params();
    test_hosts();
    zxvd_rpc_groups_reset();
    free(OUT.p);
    printf("test_zxvd_pspace: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
