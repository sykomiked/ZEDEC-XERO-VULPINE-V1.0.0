/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_rpc.c — zxvd_rpc.c on its own, against the real libzxv and a
 * stub host (zxvd_rpc.h): every method's exact reply, every JSON-RPC error,
 * parameter validation, then the socket (T1-T4) driven from this process.
 * Fast enough to be the mutation harness's test command
 * (tests/harness/mutator/targets.json "zxvd_rpc"); test_zxvd.py covers the
 * real daemon end to end. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>

#include <sys/resource.h>

#include "zxvd_rpc.h"
#include "zxv_api.h"
#include "sha256.h"

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

/* ---------------- the stub host ---------------- */

static int asked, read_marks, updated, stopped;
static char last_prompt[4096], last_net[256];

static void h_state(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, "{\"tick\":7}", 10);
}
static void h_ask(const char *q, zxvd_buf_t *o)
{
    asked++;
    snprintf(last_prompt, sizeof last_prompt, "%s", q);
    zxvd_buf_json_str(o, "re: \"x\"\\\n\x01");
}
static void h_model(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"loaded\":%s,\"n\":%d}", "false", 12);
}
static void h_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, "{\"unread\":0,\"notes\":[]}", 23);
}
static uint32_t h_notes_read(void)
{
    return (uint32_t) (3 + read_marks++);
}
static const char *h_net(const char *cmd)
{
    snprintf(last_net, sizeof last_net, "%s", cmd);
    return strcmp(cmd, "lan") ? NULL : "networking is on";
}
static void h_update(zxvd_buf_t *o)
{
    updated++;
    zxvd_buf_raw(o, "{\"status\":\"up to date\"}", 23);
}
static void h_shutdown(void)
{
    stopped++;
}
static void h_ask_silent(const char *q, zxvd_buf_t *o)
{
    (void) q;
    (void) o; /* writes nothing: the answer must still be "" */
}
static void h_empty(zxvd_buf_t *o)
{
    (void) o; /* writes nothing: must become -32603 "no result" */
}

static const zxvd_host_ops_t OPS = {h_state, h_ask,    h_model,    h_notes, h_notes_read,
                                    h_net,   h_update, h_shutdown, "9.8.7"};
static const zxvd_host_ops_t NONE = {0};
static const zxvd_host_ops_t EMPTY = {h_empty, 0, 0, 0, 0, 0, 0, 0, 0};
static const zxvd_host_ops_t SILENT = {0, h_ask_silent, 0, 0, 0, 0, 0, 0, 0};

/* peer.* stubs (zxvd_peer_ops_t) */
static char last_peer[64], last_peer_prompt[4200];
static uint64_t last_max, last_serve_price;
static int peer_asks, serve_calls;
static bool last_serve_on;
static const char *p_ask(const char *peer, const char *prompt, uint64_t max_price, zxvd_buf_t *o)
{
    peer_asks++;
    snprintf(last_peer, sizeof last_peer, "%s", peer);
    snprintf(last_peer_prompt, sizeof last_peer_prompt, "%s", prompt);
    last_max = max_price;
    if (!strcmp(prompt, "fail")) {
        zxvd_buf_raw(o, "{\"partial\":", 11); /* must be discarded */
        return "the peer's price is above max_price";
    }
    zxvd_buf_raw(o, "{\"answer\":\"ok\"}", 15);
    return NULL;
}
static const char *p_serve(bool on, uint64_t price, zxvd_buf_t *o)
{
    serve_calls++;
    last_serve_on = on;
    last_serve_price = price;
    if (on && price == 13) return "networking is off";
    zxvd_buf_printf(o, "{\"serving\":%s}", on ? "true" : "false");
    return NULL;
}
static void p_status(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, "{\"sold\":2}", 10);
}
static const zxvd_peer_ops_t PEER_STUB = {p_ask, p_serve, p_status};
static const zxvd_peer_ops_t PEER_NONE = {0};

/* ---------------- helpers ---------------- */

static zxvd_buf_t OUT;

/* one request -> its reply (or "<none>" for a notification) */
static const char *rpc(const char *line)
{
    bool r = zxvd_rpc_handle_line(line, strlen(line), &OUT);
    if (!r) return "<none>";
    checks++;
    if (OUT.p && OUT.len != strlen(OUT.p)) { /* no stray NUL inside a reply */
        fails++;
        printf("  FAIL reply length %zu != text length %zu: %s\n", OUT.len, strlen(OUT.p), OUT.p);
    }
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %s\n    want %s\n    got  %s\n", line, req, want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static bool has(const char *req, const char *needle)
{
    return strstr(rpc(req), needle) != NULL;
}

#define REQ(id, m, p) "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"method\":\"" m "\"" p "}"
#define ERR(id, code, msg)                                                                         \
    "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"error\":{\"code\":" code ",\"message\":\"" msg "\"}}"
#define OK(id, res) "{\"jsonrpc\":\"2.0\",\"id\":" id ",\"result\":" res "}"

/* ---------------- buffer ---------------- */

static void test_buf(void)
{
    zxvd_buf_t b = {0};
    zxvd_buf_json_str(&b, "a\"b\\c\n\x1f"
                          "d\x7f");
    CHECK(b.p && !strcmp(b.p, "\"a\\\"b\\\\c\\u000a\\u001fd\x7f\""), "json_str escapes: %s", b.p);
    size_t before = b.len;
    zxvd_buf_printf(&b, "%d-%s", -42, "x");
    CHECK(b.len == before + 5 && !strcmp(b.p + before, "-42-x"), "printf appends");
    for (int i = 0; i < 2000; i++) zxvd_buf_raw(&b, "0123456789", 10);
    CHECK(b.len == before + 5 + 20000 && b.p[b.len] == 0 && !b.oom, "grows and stays terminated");
    CHECK(!memcmp(b.p + b.len - 10, "0123456789", 10), "content kept across growth");
    free(b.p);
    zxvd_buf_t e = {0};
    zxvd_buf_raw(&e, "", 0);
    CHECK(e.len == 0 && e.p && e.p[0] == 0, "empty append terminates");
    free(e.p);
    zxvd_buf_t ep = {0};
    zxvd_buf_printf(&ep, "%s", "");
    CHECK(ep.len == 0 && ep.p && ep.p[0] == 0 && !ep.oom, "empty printf terminates");
    free(ep.p);
    /* one append far larger than the first allocation */
    static char big[100000];
    memset(big, 'b', sizeof big);
    zxvd_buf_t g = {0};
    zxvd_buf_raw(&g, big, sizeof big);
    CHECK(!g.oom && g.len == sizeof big && g.p[g.len] == 0 && !memcmp(g.p, big, sizeof big),
          "one large append");
    zxvd_buf_printf(&g, "%s", "tail");
    CHECK(!g.oom && g.len == sizeof big + 4 && !strcmp(g.p + sizeof big, "tail"), "then printf");
    free(g.p);
    /* an allocation that cannot succeed sets oom and leaves the buffer alone */
    zxvd_buf_t h = {0};
    zxvd_buf_raw(&h, "x", SIZE_MAX / 4);
    CHECK(h.oom && h.len == 0 && !h.p, "an impossible allocation sets oom");
    /* once oom, every write is dropped (and never touches the NULL pointer) */
    zxvd_buf_raw(&h, "abc", 3);
    zxvd_buf_printf(&h, "%d", 12345);
    zxvd_buf_json_str(&h, "q");
    CHECK(h.oom && h.len == 0 && !h.p, "writes after oom are dropped");
    /* a format the C library cannot encode (vsnprintf < 0) writes nothing */
    if (snprintf(NULL, 0, "%lc", (wint_t) 0x4e2d) < 0) {
        zxvd_buf_t w = {0};
        zxvd_buf_raw(&w, "k", 1);
        zxvd_buf_printf(&w, "%lc", (wint_t) 0x4e2d);
        CHECK(!w.oom && w.len == 1 && !strcmp(w.p, "k"), "an encoding error writes nothing");
        free(w.p);
    }
}

/* ---------------- protocol ---------------- */

static void test_protocol(void)
{
    zxvd_rpc_set_ops(&OPS);
    EXPECT("{not json", ERR("null", "-32700", "parse error"));
    EXPECT("", ERR("null", "-32700", "parse error"));
    EXPECT("  \t ", ERR("null", "-32700", "parse error"));
    EXPECT("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"zxvd.version\"}]",
           ERR("null", "-32600", "batches are not supported"));
    EXPECT("\"hello\"", ERR("null", "-32600", "not a request object"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,\"method\":\"swarm.state\"}",
           ERR("null", "-32600", "duplicate id"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"swarm.state\"}",
           ERR("null", "-32600", "id must be a string, number or null"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":[1],\"method\":\"swarm.state\"}",
           ERR("null", "-32600", "id must be a string, number or null"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"swarm.state\"}",
           ERR("null", "-32600", "id must be a string, number or null"));
    EXPECT("{\"id\":5,\"method\":\"swarm.state\"}", ERR("5", "-32600", "invalid request"));
    EXPECT("{\"jsonrpc\":\"1.0\",\"id\":5,\"method\":\"swarm.state\"}",
           ERR("5", "-32600", "invalid request"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":5}", ERR("5", "-32600", "invalid request"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":7}", ERR("5", "-32600", "invalid request"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"swarm.state\",\"params\":{},\"params\":{}}",
           ERR("5", "-32600", "invalid request"));
    EXPECT(REQ("6", "swarm.state", ",\"params\":[]"),
           ERR("6", "-32602", "params must be an object"));
    EXPECT(REQ("6", "swarm.state", ",\"params\":3"),
           ERR("6", "-32602", "params must be an object"));
    EXPECT(REQ("8", "swarm.nope", ""), ERR("8", "-32601", "method not found"));
    EXPECT(REQ("8", "swarm.stat", ""), ERR("8", "-32601", "method not found"));
    EXPECT(REQ("8", "swarm.state2", ""), ERR("8", "-32601", "method not found"));
    /* ids are echoed verbatim */
    EXPECT(REQ("\"a-1\"", "swarm.state", ""), OK("\"a-1\"", "{\"tick\":7}"));
    EXPECT(REQ("-12", "swarm.state", ""), OK("-12", "{\"tick\":7}"));
    EXPECT(REQ("0", "swarm.state", ""), OK("0", "{\"tick\":7}"));
    EXPECT(REQ("null", "swarm.state", ""), OK("null", "{\"tick\":7}"));
    EXPECT(REQ("1", "swarm.state", ",\"params\":{}"), OK("1", "{\"tick\":7}"));
    /* notifications run and get no reply, even when they fail */
    int before = read_marks;
    EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"notes.mark_read\"}", "<none>");
    CHECK(read_marks == before + 1, "a notification is executed");
    EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"no.such\"}", "<none>");
    EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"swarm.ask\"}", "<none>");
    EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"swarm.ask\",\"params\":5}", "<none>");
    EXPECT("{\"jsonrpc\":\"1.0\",\"method\":\"swarm.state\"}", "<none>");
    /* a request with more than 512 tokens */
    static const char head[] =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"swarm.state\",\"params\":{\"a\":[";
    char big[8192];
    size_t n = sizeof head - 1;
    memcpy(big, head, n);
    for (int i = 0; i < 600; i++) { /* "0,0,...,0": 1199 bytes */
        if (i) big[n++] = ',';
        big[n++] = '0';
    }
    memcpy(big + n, "]}}", 4);
    EXPECT(big, ERR("null", "-32600", "request too complex"));
    /* nesting deeper than 16 */
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"swarm.state\",\"params\":{\"a\":"
           "[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]}}",
           ERR("null", "-32700", "parse error"));
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"swarm.state\",\"params\":{\"a\":"
           "[[[[[[[[[[[[[[1]]]]]]]]]]]]]]}}",
           OK("1", "{\"tick\":7}"));
    /* 17 levels (object, params, 15 arrays): one too deep */
    EXPECT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"swarm.state\",\"params\":{\"a\":"
           "[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]}}",
           ERR("null", "-32700", "parse error"));
    /* a line over the limit */
    char *huge = malloc(ZXVD_RPC_MAX_LINE + 2);
    memset(huge, ' ', ZXVD_RPC_MAX_LINE + 1);
    memcpy(huge, REQ("1", "swarm.state", ""), sizeof(REQ("1", "swarm.state", "")) - 1);
    huge[ZXVD_RPC_MAX_LINE + 1] = 0;
    EXPECT(huge, ERR("null", "-32600", "request too long"));
    huge[ZXVD_RPC_MAX_LINE] = 0; /* exactly at the limit: served */
    EXPECT(huge, OK("1", "{\"tick\":7}"));
    free(huge);
}

/* ---------------- host methods ---------------- */

static void test_host_methods(void)
{
    zxvd_rpc_set_ops(&OPS);
    EXPECT(REQ("1", "swarm.state", ""), OK("1", "{\"tick\":7}"));
    EXPECT(REQ("2", "swarm.ask", ",\"params\":{\"prompt\":\"hi \\u00e9\"}"),
           OK("2", "{\"answer\":\"re: \\\"x\\\"\\\\\\u000a\\u0001\"}"));
    CHECK(!strcmp(last_prompt, "hi \xc3\xa9"), "prompt decoded: %s", last_prompt);
    EXPECT(REQ("3", "swarm.ask", ""),
           ERR("3", "-32602", "missing string parameter \\\"prompt\\\""));
    EXPECT(REQ("3", "swarm.ask", ",\"params\":{\"prompt\":1}"),
           ERR("3", "-32602", "\\\"prompt\\\" must be a string of at most 4095 bytes"));
    EXPECT(REQ("3", "swarm.ask", ",\"params\":{\"prompt\":null}"),
           ERR("3", "-32602", "\\\"prompt\\\" must be a string of at most 4095 bytes"));
    EXPECT(REQ("3", "swarm.ask", ",\"params\":{\"prompt\":\"a\\u0000b\"}"),
           ERR("3", "-32602", "\\\"prompt\\\" must not contain NUL"));
    /* 4095 bytes fit, 4096 do not */
    static char req[5000];
    int k = snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"swarm.ask\",\"params\":{\"prompt\":\"");
    memset(req + k, 'p', 4095);
    strcpy(req + k + 4095, "\"}}");
    CHECK(has(req, "\"result\"") && strlen(last_prompt) == 4095, "a 4095-byte prompt is accepted");
    memset(req + k, 'p', 4096);
    strcpy(req + k + 4096, "\"}}");
    CHECK(has(req, "at most 4095 bytes"), "a 4096-byte prompt is refused");
    int a = asked;
    rpc(REQ("3", "swarm.ask", ",\"params\":{\"prompt\":2}"));
    CHECK(asked == a, "a bad prompt never reaches the host");

    EXPECT(REQ("5", "model.info", ""), OK("5", "{\"loaded\":false,\"n\":12}"));
    EXPECT(REQ("6", "notes.list", ""), OK("6", "{\"unread\":0,\"notes\":[]}"));
    read_marks = 0;
    EXPECT(REQ("7", "notes.mark_read", ""), OK("7", "{\"marked\":3}"));
    EXPECT(REQ("7", "notes.mark_read", ""), OK("7", "{\"marked\":4}"));
    EXPECT(REQ("8", "net.command", ",\"params\":{\"command\":\"lan\"}"),
           OK("8", "{\"result\":\"networking is on\"}"));
    EXPECT(REQ("8", "net.command", ",\"params\":{\"command\":\"everything\"}"),
           ERR("8", "-32602", "command must be off, lan, online, peer, share, want or sell"));
    CHECK(!strcmp(last_net, "everything"), "the command reaches the host");
    EXPECT(REQ("8", "net.command", ""),
           ERR("8", "-32602", "missing string parameter \\\"command\\\""));
    /* 255-byte commands reach the host, 256 do not */
    k = snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"net.command\",\"params\":{\"command\":\"");
    memset(req + k, 'c', 255);
    strcpy(req + k + 255, "\"}}");
    CHECK(has(req, "command must be off") && strlen(last_net) == 255,
          "a 255-byte command reaches the host");
    memset(req + k, 'c', 256);
    strcpy(req + k + 256, "\"}}");
    CHECK(has(req, "\\\"command\\\" must be a string of at most 255 bytes"),
          "a 256-byte command is refused");
    updated = 0;
    EXPECT(REQ("9", "update.check", ""), OK("9", "{\"status\":\"up to date\"}"));
    CHECK(updated == 1, "update.check runs one check");
    uint32_t ma, mi, pa, ft;
    zxv_version(&ma, &mi, &pa);
    zxv_features(&ft);
    char want[256];
    snprintf(want, sizeof want,
             OK("10", "{\"app\":\"9.8.7\",\"protocol\":\"zxvd-rpc/1\",\"libzxv\":\"%u.%u.%u\","
                      "\"libzxv_features\":%u}"),
             ma, mi, pa, ft);
    EXPECT(REQ("10", "zxvd.version", ""), want);
    CHECK(ma == 1 && mi >= 2, "libzxv ABI 1.2 or later (%u.%u)", ma, mi);
    EXPECT(REQ("11", "rpc.discover", ""),
           OK("11",
              "{\"protocol\":\"zxvd-rpc/1\",\"methods\":[\"rpc.discover\",\"zxvd.version\","
              "\"zxvd.shutdown\",\"swarm.state\",\"swarm.ask\",\"model.info\",\"notes.list\","
              "\"notes.mark_read\",\"net.command\",\"peer.ask\",\"peer.serve\",\"peer.status\","
              "\"update.check\",\"currency.enable\","
              "\"ledger.open_account\",\"ledger.post\",\"ledger.balance\",\"ledger.accounts\","
              "\"ledger.entries\",\"ledger.check\",\"swarm.earnings\",\"mesh.books\","
              "\"books.file\"]}"));
    stopped = 0;
    EXPECT(REQ("12", "zxvd.shutdown", ""), OK("12", "{\"stopping\":true}"));
    CHECK(stopped == 1, "shutdown reaches the host");

    /* a host without these ops */
    zxvd_rpc_set_ops(&NONE);
    const char *ms[] = {"swarm.state",     "swarm.ask",   "model.info",   "notes.list",
                        "notes.mark_read", "net.command", "update.check", "zxvd.shutdown"};
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        char r[200], w[200];
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":{\"prompt\":\"x\","
                 "\"command\":\"lan\"}}",
                 ms[i]);
        snprintf(w, sizeof w, ERR("1", "-32601", "not available"));
        EXPECT(r, w);
    }
    snprintf(want, sizeof want,
             OK("1", "{\"app\":\"unknown\",\"protocol\":\"zxvd-rpc/1\",\"libzxv\":\"%u.%u.%u\","
                     "\"libzxv_features\":%u}"),
             ma, mi, pa, ft);
    EXPECT(REQ("1", "zxvd.version", ""), want);
    zxvd_rpc_set_ops(NULL);
    EXPECT(REQ("1", "swarm.state", ""), ERR("1", "-32601", "not available"));
    zxvd_rpc_set_ops(&SILENT);
    EXPECT(REQ("1", "swarm.ask", ",\"params\":{\"prompt\":\"x\"}"), OK("1", "{\"answer\":\"\"}"));
    zxvd_rpc_set_ops(&EMPTY);
    EXPECT(REQ("1", "swarm.state", ""), ERR("1", "-32603", "no result"));
    zxvd_rpc_set_ops(&OPS);
}

/* ---------------- the books (libzxv) ---------------- */

static void test_books_closed(void)
{
    EXPECT(REQ("1", "ledger.check", ""), ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "currency.enable", ",\"params\":{\"code\":\"USD\"}"),
           ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "ledger.accounts", ""), ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "swarm.earnings", ""), ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_earnings_sync(5) == 0, "earnings: nothing to mirror before the books open");
    /* E1: the code is 3..8 bytes, the rate 1..ZXV_AMOUNT_MAX */
    CHECK(zxvd_rpc_earnings_set("VF", 1) == -1, "earnings: a 2-byte code is refused");
    CHECK(zxvd_rpc_earnings_set("ABCDEFGHI", 1) == -1, "earnings: a 9-byte code is refused");
    CHECK(zxvd_rpc_earnings_set("ABCDEFGH", 1) == 0, "earnings: an 8-byte code is accepted");
    CHECK(zxvd_rpc_earnings_set("VFV", 0) == -1, "earnings: rate 0 is refused");
    CHECK(zxvd_rpc_earnings_set("VFV", (uint64_t) ZXV_AMOUNT_MAX + 1u) == -1,
          "earnings: a rate above ZXV_AMOUNT_MAX is refused");
    CHECK(zxvd_rpc_earnings_set("VFV", (uint64_t) ZXV_AMOUNT_MAX) == 0,
          "earnings: rate ZXV_AMOUNT_MAX is accepted");
    CHECK(zxvd_rpc_earnings_set("VFV", 1) == 0, "earnings: back to the default");
}

static void test_books(void)
{
    CHECK(zxvd_rpc_books_open() == 0, "books open");
    CHECK(zxvd_rpc_books_open() == 0, "opening twice is harmless");
    EXPECT(REQ("1", "ledger.accounts", ""), OK("1", "{\"accounts\":[]}"));
    EXPECT(REQ("1", "ledger.check", ""), OK("1", "{\"ok\":true}"));
    EXPECT(REQ("2", "currency.enable", ",\"params\":{\"code\":\"USD\"}"),
           OK("2", "{\"enabled\":\"USD\"}"));
    EXPECT(REQ("2", "currency.enable", ",\"params\":{\"code\":\"NGN\"}"),
           OK("2", "{\"enabled\":\"NGN\"}"));
    EXPECT(
        REQ("2", "currency.enable", ",\"params\":{\"code\":\"XXQ\"}"),
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32000,\"message\":\"ZXV_E_CURRENCY: "
        "'XXQ' is not an ISO 4217 currency\",\"data\":{\"status\":-7,\"name\":\"ZXV_E_CURRENCY\","
        "\"class\":\"USAGE\"}}}");
    EXPECT(REQ("2", "currency.enable", ""),
           ERR("2", "-32602", "missing string parameter \\\"code\\\""));
    EXPECT(REQ("2", "currency.enable", ",\"params\":{\"code\":\"0123456789ABCDEF\"}"),
           ERR("2", "-32602", "\\\"code\\\" must be a string of at most 15 bytes"));

    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"bank\",\"currency\":\"USD\",\"kind\":\"issuer\"}"),
           OK("3", "{\"account\":0}"));
    EXPECT(REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"alice\",\"currency\":\"USD\"}"),
           OK("3", "{\"account\":1}"));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"bob\",\"currency\":\"USD\",\"kind\":\"holder\"}"),
           OK("3", "{\"account\":2}"));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"ng\",\"currency\":\"NGN\",\"kind\":\"holder\"}"),
           OK("3", "{\"account\":3}"));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"x\",\"currency\":\"USD\",\"kind\":\"owner\"}"),
           ERR("3", "-32602", "kind must be \\\"holder\\\" or \\\"issuer\\\""));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"x\",\"currency\":\"USD\",\"kind\":7}"),
           ERR("3", "-32602", "\\\"kind\\\" must be a string of at most 15 bytes"));
    EXPECT(REQ("3", "ledger.open_account", ",\"params\":{\"currency\":\"USD\"}"),
           ERR("3", "-32602", "missing string parameter \\\"name\\\""));
    EXPECT(REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"x\"}"),
           ERR("3", "-32602", "missing string parameter \\\"currency\\\""));
    CHECK(has(REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"x\",\"currency\":\"EUR\"}"),
              "\"name\":\"ZXV_E_CURRENCY\""),
          "a disabled currency is refused by libzxv");
    /* 63-byte names fit, 64 do not */
    char req[800], nm[80];
    memset(nm, 'n', 63);
    nm[63] = 0;
    snprintf(req, sizeof req,
             REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"%s\",\"currency\":\"USD\"}"),
             nm);
    EXPECT(req, OK("3", "{\"account\":4}"));
    nm[63] = 'n';
    nm[64] = 0;
    snprintf(req, sizeof req,
             REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"%s\",\"currency\":\"USD\"}"),
             nm);
    EXPECT(req, ERR("3", "-32602", "\\\"name\\\" must be a string of at most 63 bytes"));
    /* refused names, currencies and kinds open nothing (the account list is checked below) */
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"a\\u0000b\",\"currency\":\"USD\"}"),
           ERR("3", "-32602", "\\\"name\\\" must not contain NUL"));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"c\",\"currency\":\"0123456789ABCDEF\"}"),
           ERR("3", "-32602", "\\\"currency\\\" must be a string of at most 15 bytes"));
    EXPECT(REQ("3", "ledger.open_account",
               ",\"params\":{\"name\":\"c\",\"currency\":\"USD\",\"kind\":\"0123456789ABCDEF\"}"),
           ERR("3", "-32602", "\\\"kind\\\" must be a string of at most 15 bytes"));
    /* reopening the books keeps them */
    CHECK(zxvd_rpc_books_open() == 0, "opening again with accounts open");
    EXPECT(REQ("3", "ledger.balance", ",\"params\":{\"account\":4}"),
           OK("3", "{\"debit_minor\":\"0\",\"credit_minor\":\"0\",\"equity_minor\":\"0\"}"));

#define POST(f, t, a, ref)                                                                         \
    REQ("4", "ledger.post",                                                                        \
        ",\"params\":{\"from\":" f ",\"to\":" t ",\"amount_minor\":" a                             \
        ",\"currency\":\"USD\",\"reference\":\"" ref "\"}")
    EXPECT(POST("0", "1", "10000", "R1"), OK("4", "{\"entry_id\":\"1\"}"));
    EXPECT(POST("1", "2", "2550", "R2"), OK("4", "{\"entry_id\":\"2\"}"));
    CHECK(has(POST("2", "1", "2551", "R3"),
              "\"data\":{\"status\":-6,\"name\":\"ZXV_E_FUNDS\",\"class\":\"REFUSED\"}"),
          "overdraft: ZXV_E_FUNDS");
    EXPECT(POST("2", "1", "2550", "R4"), OK("4", "{\"entry_id\":\"3\"}"));
    CHECK(has(POST("1", "2", "1", "R1"), "\"name\":\"ZXV_E_DUPLICATE\""), "reused reference");
    CHECK(has(POST("1", "2", "0", "R5"), "\"name\":\"ZXV_E_RANGE\""), "zero amount");
    CHECK(has(POST("1", "2", "-5", "R6"), "\"name\":\"ZXV_E_RANGE\""), "negative amount");
    CHECK(has(POST("1", "9", "1", "R7"), "\"name\":\"ZXV_E_NOT_FOUND\""), "unknown account");
    EXPECT(POST("1", "2", "1.5", "R8"), ERR("4", "-32602",
                                            "\\\"amount_minor\\\" must be an integer or a decimal "
                                            "string (no fraction or exponent)"));
    EXPECT(POST("1", "2", "1e3", "R8"), ERR("4", "-32602",
                                            "\\\"amount_minor\\\" must be an integer or a decimal "
                                            "string (no fraction or exponent)"));
    EXPECT(POST("1", "2", "\"5.0\"", "R8"), ERR("4", "-32602",
                                                "\\\"amount_minor\\\" must be an integer or a "
                                                "decimal string (no fraction or exponent)"));
    EXPECT(POST("-1", "2", "1", "R8"), ERR("4", "-32602", "\\\"from\\\" is out of range"));
    EXPECT(POST("4294967296", "2", "1", "R8"), ERR("4", "-32602", "\\\"from\\\" is out of range"));
    CHECK(has(POST("4294967295", "2", "1", "R8"), "\"name\":\"ZXV_E_NOT_FOUND\""),
          "UINT32_MAX is in range (and unknown)");
    EXPECT(POST("1", "-1", "1", "R8"), ERR("4", "-32602", "\\\"to\\\" is out of range"));
    EXPECT(REQ("4", "ledger.post", ",\"params\":{\"to\":1}"),
           ERR("4", "-32602", "missing integer parameter \\\"from\\\""));
    EXPECT(REQ("4", "ledger.post", ",\"params\":{\"from\":1}"),
           ERR("4", "-32602", "missing integer parameter \\\"to\\\""));
    EXPECT(REQ("4", "ledger.post", ",\"params\":{\"from\":1,\"to\":2}"),
           ERR("4", "-32602", "missing integer parameter \\\"amount_minor\\\""));
    EXPECT(REQ("4", "ledger.post", ",\"params\":{\"from\":1,\"to\":2,\"amount_minor\":1}"),
           ERR("4", "-32602", "missing string parameter \\\"currency\\\""));
    EXPECT(REQ("4", "ledger.post",
               ",\"params\":{\"from\":1,\"to\":2,\"amount_minor\":1,\"currency\":\"USD\"}"),
           ERR("4", "-32602", "missing string parameter \\\"reference\\\""));
    EXPECT(POST("1", "2", "1", "0123456789012345678901234567890123456789"),
           ERR("4", "-32602", "\\\"reference\\\" must be a string of at most 39 bytes"));
    EXPECT(POST("1", "2", "1", "N\\u0000x"),
           ERR("4", "-32602", "\\\"reference\\\" must not contain NUL"));
    EXPECT(
        REQ("4", "ledger.post",
            ",\"params\":{\"from\":1,\"to\":2,\"amount_minor\":1,\"currency\":\"0123456789ABCDEF\","
            "\"reference\":\"R10\"}"),
        ERR("4", "-32602", "\\\"currency\\\" must be a string of at most 15 bytes"));
    EXPECT(POST("1", "2", "1", "R9"), OK("4", "{\"entry_id\":\"4\"}"));

    EXPECT(
        REQ("5", "ledger.balance", ",\"params\":{\"account\":1}"),
        OK("5", "{\"debit_minor\":\"12550\",\"credit_minor\":\"2551\",\"equity_minor\":\"9999\"}"));
    EXPECT(REQ("5", "ledger.balance", ",\"params\":{\"account\":2}"),
           OK("5", "{\"debit_minor\":\"2551\",\"credit_minor\":\"2550\",\"equity_minor\":\"1\"}"));
    EXPECT(
        REQ("5", "ledger.balance", ",\"params\":{\"account\":0}"),
        OK("5", "{\"debit_minor\":\"0\",\"credit_minor\":\"10000\",\"equity_minor\":\"-10000\"}"));
    CHECK(has(REQ("5", "ledger.balance", ",\"params\":{\"account\":44}"), "ZXV_E_NOT_FOUND"),
          "unknown account balance");
    EXPECT(REQ("5", "ledger.balance", ""),
           ERR("5", "-32602", "missing integer parameter \\\"account\\\""));

    EXPECT(REQ("6", "ledger.entries", ",\"params\":{\"account\":2}"),
           OK("6",
              "{\"entries\":[{\"entry_id\":\"2\",\"signed_minor\":\"2550\",\"counterparty\":1,"
              "\"reference\":\"R2\"},{\"entry_id\":\"3\",\"signed_minor\":\"-2550\","
              "\"counterparty\":1,"
              "\"reference\":\"R4\"},{\"entry_id\":\"4\",\"signed_minor\":\"1\",\"counterparty\":1,"
              "\"reference\":\"R9\"}]}"));
    EXPECT(REQ("6", "ledger.entries", ",\"params\":{\"account\":3}"), OK("6", "{\"entries\":[]}"));
    CHECK(has(REQ("6", "ledger.entries", ",\"params\":{\"account\":99}"), "ZXV_E_NOT_FOUND"),
          "unknown account journal");
    EXPECT(REQ("6", "ledger.entries", ""),
           ERR("6", "-32602", "missing integer parameter \\\"account\\\""));

    snprintf(req, sizeof req,
             OK("7", "{\"accounts\":[{\"account\":0,\"name\":\"bank\",\"currency\":\"USD\","
                     "\"kind\":\"issuer\"},"
                     "{\"account\":1,\"name\":\"alice\",\"currency\":\"USD\",\"kind\":\"holder\"},"
                     "{\"account\":2,\"name\":\"bob\",\"currency\":\"USD\",\"kind\":\"holder\"},"
                     "{\"account\":3,\"name\":\"ng\",\"currency\":\"NGN\",\"kind\":\"holder\"},"
                     "{\"account\":4,\"name\":\"%s\",\"currency\":\"USD\",\"kind\":\"holder\"}]}"),
             "nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn");
    EXPECT(REQ("7", "ledger.accounts", ""), req);
    EXPECT(POST("1", "2", "\"2\"", "S1"), OK("4", "{\"entry_id\":\"5\"}"));
    EXPECT(POST("2", "1", "\"0002\"", "S2"), OK("4", "{\"entry_id\":\"6\"}"));
    CHECK(has(POST("1", "2", "\"-3\"", "S3"), "\"name\":\"ZXV_E_RANGE\""),
          "a negative string amount");
    CHECK(has(POST("1", "2", "\"9223372036854775807\"", "S4"), "\"name\":\"ZXV_E_RANGE\""),
          "INT64_MAX parses, then libzxv refuses its range");
    const char *bad[] = {"\"\"",
                         "\"-\"",
                         "\"+5\"",
                         "\" 5\"",
                         "\"5 \"",
                         "\"1.0\"",
                         "\"12a\"",
                         "\"1e3\"",
                         "\"9223372036854775808\"",
                         "\"10000000000000000000\"",
                         "\"-9223372036854775809\"",
                         "\"99999999999999999999\"",
                         "true",
                         "null",
                         "{}",
                         "[1]"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char r[300];
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"ledger.post\",\"params\":{\"from\":1,"
                 "\"to\":2,\"amount_minor\":%s,\"currency\":\"USD\",\"reference\":\"SB\"}}",
                 bad[i]);
        EXPECT(r, ERR("4", "-32602",
                      "\\\"amount_minor\\\" must be an integer or a decimal string (no "
                      "fraction or exponent)"));
    }
    EXPECT(REQ("8", "ledger.check", ""), OK("8", "{\"ok\":true}"));
}

/* ---------------- the socket ---------------- */

static char DIR_[80], SOCK[100];

static int connect_to(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *) &a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    struct timeval to = {0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
    return fd;
}

static uint64_t NOW = 1000;

/* run the server loop a few times without blocking */
static void pump(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        fd_set rd;
        FD_ZERO(&rd);
        int mx = zxvd_rpc_fds(&rd);
        struct timeval tv = {0, 20000};
        int r = mx >= 0 ? select(mx + 1, &rd, NULL, NULL, &tv) : 0;
        if (r < 0) FD_ZERO(&rd);
        zxvd_rpc_service(&rd, NOW);
    }
}

/* read one full reply line (pumping the server meanwhile) */
static bool read_line(int fd, char *out, size_t cap)
{
    size_t n = 0;
    for (int tries = 0; tries < 50; tries++) {
        pump(2);
        ssize_t k = recv(fd, out + n, 1, MSG_DONTWAIT);
        while (k == 1) {
            if (out[n] == '\n') {
                out[n] = 0;
                return true;
            }
            if (++n >= cap - 1) return false;
            k = recv(fd, out + n, 1, MSG_DONTWAIT);
        }
        if (k == 0) return false; /* closed */
    }
    return false;
}

static bool closed_by_server(int fd)
{
    char c;
    for (int tries = 0; tries < 50; tries++) {
        pump(2);
        ssize_t k = recv(fd, &c, 1, MSG_DONTWAIT);
        if (k == 0) return true;
        if (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return true;
    }
    return false;
}

/* send a buffer larger than the socket's send buffer (8 KiB for AF_UNIX on
 * macOS) without blocking: the server runs on this thread, so it is pumped
 * whenever the buffer is full. macOS blocks such a send even with
 * MSG_DONTWAIT, so the descriptor itself is made non-blocking meanwhile.
 * Stops if the server closes the client. */
static void send_pumped(int fd, const char *buf, size_t len)
{
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    size_t off = 0;
    for (int tries = 0; off < len && tries < 1000; tries++) {
        ssize_t k = send(fd, buf + off, len - off, 0);
        if (k > 0) {
            off += (size_t) k;
            continue;
        }
        if (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        pump(1);
    }
    fcntl(fd, F_SETFL, fl);
}

/* zxvd_rpc.h E1-E4. Runs after test_books: the books are open, in VFV. */
static size_t count(const char *s, const char *needle)
{
    size_t n = 0;
    for (const char *p = strstr(s, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

static void test_earnings(void)
{
    char req[400], want[400];
    CHECK(zxvd_rpc_earnings_set("USD", 1) == -1, "earnings: no change once the books are open");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"source_account\":null,"
                   "\"owner_account\":null,\"balance_minor\":\"0\",\"postings\":\"0\","
                   "\"cycles\":\"0\",\"resumed\":false}"));
    uint32_t src = (uint32_t) count(rpc(REQ("1", "ledger.accounts", "")), "\"account\":");
    uint32_t own = src + 1u;
    CHECK(zxvd_rpc_earnings_sync(1000) == 0, "earnings: the first cycle posts");
#define EARN(bal, posts, cycles)                                                                   \
    snprintf(want, sizeof want,                                                                    \
             OK("1", "{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"source_account\":%u,"           \
                     "\"owner_account\":%u,\"balance_minor\":\"" bal "\",\"postings\":\"" posts    \
                     "\",\"cycles\":\"" cycles "\",\"resumed\":false}"),                           \
             src, own);                                                                            \
    EXPECT(REQ("1", "swarm.earnings", ""), want)
    EARN("1000", "1", "1");
    snprintf(req, sizeof req, REQ("2", "ledger.accounts", ""));
    snprintf(want, sizeof want,
             "{\"account\":%u,\"name\":\"swarm market\",\"currency\":\"VFV\",\"kind\":\"issuer\"},"
             "{\"account\":%u,\"name\":\"owner: swarm earnings\",\"currency\":\"VFV\","
             "\"kind\":\"holder\"}]",
             src, own);
    CHECK(has(req, want), "earnings: an issuer and a holder in VFV: %s", OUT.p);
#define BAL(acct, d, cr, eq)                                                                       \
    snprintf(req, sizeof req, REQ("3", "ledger.balance", ",\"params\":{\"account\":%u}"), acct);   \
    EXPECT(req, OK("3", "{\"debit_minor\":\"" d "\",\"credit_minor\":\"" cr                        \
                        "\",\"equity_minor\":\"" eq "\"}"))
    BAL(own, "1000", "0", "1000");
    snprintf(req, sizeof req, REQ("4", "ledger.entries", ",\"params\":{\"account\":%u}"), own);
    snprintf(want, sizeof want,
             "\"signed_minor\":\"1000\",\"counterparty\":%u,\"reference\":\"swarm earnings 1\"}]",
             src);
    CHECK(has(req, want), "earnings: posted from the market: %s", OUT.p);
    /* unchanged holdings: a cycle, no posting */
    CHECK(zxvd_rpc_earnings_sync(1000) == 0, "earnings: an unchanged cycle");
    EARN("1000", "1", "2");
    /* the fee leaves the models: owner -> market */
    CHECK(zxvd_rpc_earnings_sync(997) == 0, "earnings: a fall posts back");
    EARN("997", "2", "3");
    BAL(own, "1000", "3", "997");
    snprintf(req, sizeof req, REQ("4", "ledger.entries", ",\"params\":{\"account\":%u}"), own);
    snprintf(want, sizeof want,
             "\"signed_minor\":\"-3\",\"counterparty\":%u,\"reference\":\"swarm earnings 2\"}]",
             src);
    CHECK(has(req, want), "earnings: 3 back to the market: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(0) == 0, "earnings: down to nothing");
    EARN("0", "3", "4");
    CHECK(zxvd_rpc_earnings_sync((uint64_t) ZXV_AMOUNT_MAX + 1u) == -1,
          "earnings: above ZXV_AMOUNT_MAX is refused");
    EARN("0", "3", "4");
    CHECK(zxvd_rpc_earnings_sync(5) == 0, "earnings: back up");
    EARN("5", "4", "5");
    /* a refused posting (its reference already used) changes nothing */
    snprintf(req, sizeof req,
             REQ("5", "ledger.post",
                 ",\"params\":{\"from\":%u,\"to\":%u,\"amount_minor\":1,\"currency\":\"VFV\","
                 "\"reference\":\"swarm earnings 5\"}"),
             src, own);
    CHECK(has(req, "\"result\""), "a manual posting takes the next reference: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(7) == 0, "earnings: a used reference is skipped");
    EARN("7", "5", "6");
    BAL(own, "1008", "1000", "8");
    snprintf(req, sizeof req, REQ("4", "ledger.entries", ",\"params\":{\"account\":%u}"), own);
    snprintf(want, sizeof want,
             "\"signed_minor\":\"2\",\"counterparty\":%u,\"reference\":\"swarm earnings 6\"}]",
             src);
    CHECK(has(req, want), "earnings: posted as swarm earnings 6: %s", OUT.p);
    EXPECT(REQ("6", "ledger.check", ""), OK("6", "{\"ok\":true}"));

    /* another currency and rate: an ISO code is enabled from the table */
    zxvd_rpc_books_close();
    EXPECT(REQ("1", "swarm.earnings", ""), ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_earnings_set("USD", 100) == 0, "earnings: USD at 100 per credit");
    CHECK(zxvd_rpc_books_open() == 0, "books open in USD");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"USD\",\"rate_minor\":\"100\",\"source_account\":null,"
                   "\"owner_account\":null,\"balance_minor\":\"0\",\"postings\":\"0\","
                   "\"cycles\":\"0\",\"resumed\":false}"));
    CHECK(zxvd_rpc_earnings_sync((uint64_t) ZXV_AMOUNT_MAX / 100u + 1u) == -1,
          "earnings: credits times the rate above ZXV_AMOUNT_MAX are refused");
    EXPECT(REQ("1", "ledger.accounts", ""), OK("1", "{\"accounts\":[]}"));
    CHECK(zxvd_rpc_earnings_sync(3) == 0, "earnings: 3 credits");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"USD\",\"rate_minor\":\"100\",\"source_account\":0,"
                   "\"owner_account\":1,\"balance_minor\":\"300\",\"postings\":\"1\","
                   "\"cycles\":\"1\",\"resumed\":false}"));
    BAL(1u, "300", "0", "300");
    /* a code libzxv refuses keeps the books closed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_earnings_set("vfv", 1) == 0, "earnings: a lower-case code is set");
    CHECK(zxvd_rpc_books_open() == -1, "and refused when the books open");
    EXPECT(REQ("1", "ledger.check", ""), ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_earnings_sync(3) == 0, "earnings: nothing to mirror while closed");
    /* the accounts open at the first settled cycle, after any the owner opened */
    CHECK(zxvd_rpc_earnings_set("VFV", 1) == 0, "earnings: back to VFV");
    CHECK(zxvd_rpc_books_open() == 0, "books open in VFV");
    EXPECT(
        REQ("1", "ledger.open_account", ",\"params\":{\"name\":\"filler\",\"currency\":\"VFV\"}"),
        OK("1", "{\"account\":0}"));
    CHECK(zxvd_rpc_earnings_sync(2) == 0, "earnings: 2 credits");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"source_account\":1,"
                   "\"owner_account\":2,\"balance_minor\":\"2\",\"postings\":\"1\","
                   "\"cycles\":\"1\",\"resumed\":false}"));
#undef EARN
#undef BAL
}

/* ---------------- the books file (zxvd_rpc.h F1-F6, E5) ---------------- */

static char BOOKS[200];

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long) st.st_size : -1;
}

static char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t) *n + 1);
    if (b && fread(b, 1, (size_t) *n, f) != (size_t) *n) *n = -1;
    if (b) b[*n > 0 ? *n : 0] = 0;
    fclose(f);
    return b;
}

static void spit(const char *path, const char *b, long n)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(b, 1, (size_t) n, f);
        fclose(f);
    }
    chmod(path, 0600);
}

/* the chain head after the file's last record (F2), as hex */
static void last_hash(const char *b, long n, char out[65])
{
    out[0] = 0;
    long end = n - 1, st = end - 1;
    while (st > 0 && b[st - 1] != '\n') st--;
    if (end > st + 64) {
        memcpy(out, b + st, 64);
        out[64] = 0;
    }
}

/* append a record correctly chained after the file's last one */
static void append_chained(const char *path, const char *js)
{
    long n;
    char *b = slurp(path, &n), hx[65];
    last_hash(b, n, hx);
    uint8_t prev[32], h[32];
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(hx + 2 * i, "%2x", &v);
        prev[i] = (uint8_t) v;
    }
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, prev, 32);
    sha256_update(&c, (const uint8_t *) js, strlen(js));
    sha256_final(&c, h);
    FILE *f = fopen(path, "ab");
    for (int i = 0; i < 32; i++) fprintf(f, "%02x", h[i]);
    fprintf(f, " %s\n", js);
    fclose(f);
    free(b);
}

static void reopen(void)
{
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "the books reopen from the file");
}

static void test_books_file(void)
{
    char tmpl[] = "/tmp/zxvdbooks.XXXXXX", req[400], want[400];
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(BOOKS, sizeof BOOKS, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(BOOKS) == 0, "F1: a books file is set while the books are closed");
    char longp[1100];
    memset(longp, 'a', sizeof longp - 1);
    longp[sizeof longp - 1] = 0;
    CHECK(zxvd_rpc_books_file(longp) == -1, "F1: too long a path is refused");
    CHECK(zxvd_rpc_books_open() == 0, "F1: a new books file is created");
    CHECK(zxvd_rpc_books_file(NULL) == -1, "F1: no change while the books are open");
    struct stat st;
    CHECK(stat(BOOKS, &st) == 0 && (st.st_mode & 0777) == 0600, "F1: the file is 0600");
    long n;
    char *b = slurp(BOOKS, &n);
    CHECK(b && n > 66 && !memcmp(b + 65, "{\"op\":\"books\",\"v\":1}\n", 21),
          "F2: the first record is the books header: %s", b ? b : "(none)");
    CHECK(b && strstr(b, " {\"op\":\"unit\",\"code\":\"VFV\",\"minor\":2}\n"),
          "F3: the earnings unit is a record");
    free(b);
    CHECK(has(REQ("1", "books.file", ""), "\"saved\":true") && strstr(OUT.p, "\"records\":\"2\"") &&
              strstr(OUT.p, "\"writable\":true}"),
          "books.file: saved, 2 records: %s", OUT.p);

    /* owner changes and earnings, all saved */
    EXPECT(REQ("2", "currency.enable", ",\"params\":{\"code\":\"USD\"}"),
           OK("2", "{\"enabled\":\"USD\"}"));
    EXPECT(REQ("2", "currency.enable", ",\"params\":{\"code\":\"USD\"}"),
           OK("2", "{\"enabled\":\"USD\"}"));
    EXPECT(
        REQ("3", "ledger.open_account",
            ",\"params\":{\"name\":\"bank \\\"one\\\"\",\"currency\":\"USD\",\"kind\":\"issuer\"}"),
        OK("3", "{\"account\":0}"));
    EXPECT(REQ("3", "ledger.open_account", ",\"params\":{\"name\":\"cash\",\"currency\":\"USD\"}"),
           OK("3", "{\"account\":1}"));
    EXPECT(REQ("4", "ledger.post",
               ",\"params\":{\"from\":0,\"to\":1,\"amount_minor\":\"9007199254740991\","
               "\"currency\":\"USD\",\"reference\":\"e2e \\u00e9 1\"}"),
           OK("4", "{\"entry_id\":\"1\"}"));
    CHECK(has(REQ("4", "ledger.post",
                  ",\"params\":{\"from\":1,\"to\":0,\"amount_minor\":-5,"
                  "\"currency\":\"USD\",\"reference\":\"neg\"}"),
              "\"name\":\"ZXV_E_RANGE\""),
          "a refused posting is not saved: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(40) == 0, "earnings: 40 credits");
    CHECK(zxvd_rpc_earnings_sync(25) == 0, "earnings: 25 credits");
    b = slurp(BOOKS, &n);
    size_t lines = 0;
    for (long i = 0; b && i < n; i++) lines += b[i] == '\n';
    CHECK(lines == 10, "F3: header, VFV, USD once, 2 + 2 accounts, 1 + 2 postings = 10 (%zu)",
          lines);
    free(b);

    /* F4 + E5: everything comes back with the same numbers */
    reopen();
    EXPECT(REQ("5", "ledger.accounts", ""),
           OK("5",
              "{\"accounts\":[{\"account\":0,\"name\":\"bank \\\"one\\\"\",\"currency\":\"USD\","
              "\"kind\":\"issuer\"},{\"account\":1,\"name\":\"cash\",\"currency\":\"USD\","
              "\"kind\":\"holder\"},{\"account\":2,\"name\":\"swarm market\",\"currency\":"
              "\"VFV\",\"kind\":\"issuer\"},{\"account\":3,\"name\":\"owner: swarm "
              "earnings\",\"currency\":\"VFV\",\"kind\":\"holder\"}]}"));
    EXPECT(REQ("6", "ledger.balance", ",\"params\":{\"account\":1}"),
           OK("6", "{\"debit_minor\":\"9007199254740991\",\"credit_minor\":\"0\","
                   "\"equity_minor\":\"9007199254740991\"}"));
    EXPECT(REQ("6", "ledger.entries", ",\"params\":{\"account\":1}"),
           OK("6", "{\"entries\":[{\"entry_id\":\"1\",\"signed_minor\":\"9007199254740991\","
                   "\"counterparty\":0,\"reference\":\"e2e \xc3\xa9 1\"}]}"));
    EXPECT(REQ("7", "swarm.earnings", ""),
           OK("7", "{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"source_account\":2,"
                   "\"owner_account\":3,\"balance_minor\":\"25\",\"postings\":\"2\","
                   "\"cycles\":\"0\",\"resumed\":true}"));
    EXPECT(REQ("8", "ledger.check", ""), OK("8", "{\"ok\":true}"));
    /* E5: the fresh swarm's holdings are the base; only the change posts */
    CHECK(zxvd_rpc_earnings_sync(1000) == 0, "E5: first sync of the run sets the base");
    EXPECT(REQ("7", "swarm.earnings", ""),
           OK("7", "{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"source_account\":2,"
                   "\"owner_account\":3,\"balance_minor\":\"25\",\"postings\":\"2\","
                   "\"cycles\":\"1\",\"resumed\":true}"));
    CHECK(zxvd_rpc_earnings_sync(1010) == 0, "E5: +10 credits");
    CHECK(has(REQ("7", "swarm.earnings", ""), "\"balance_minor\":\"35\",\"postings\":\"3\""),
          "E5: 25 carried + 10: %s", OUT.p);
    CHECK(has(REQ("6", "ledger.entries", ",\"params\":{\"account\":3}"),
              "\"signed_minor\":\"10\",\"counterparty\":2,\"reference\":\"swarm earnings 3\"}]"),
          "E5: the next reference continues the numbering: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(900) == 0, "E5: a fall below the carried balance");
    CHECK(has(REQ("7", "swarm.earnings", ""), "\"balance_minor\":\"0\",\"postings\":\"4\""),
          "E5: never below zero: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(899) == 0 &&
              has(REQ("7", "swarm.earnings", ""), "\"postings\":\"4\""),
          "E5: still zero, no posting");
    reopen();
    CHECK(has(REQ("7", "swarm.earnings", ""), "\"balance_minor\":\"0\",\"postings\":\"4\""),
          "E5: the zero balance came back: %s", OUT.p);

    /* F6: a record cut short by a crash is dropped */
    long good = file_size(BOOKS);
    FILE *f = fopen(BOOKS, "ab");
    fputs("0123456789abcdef {\"op\":\"po", f);
    fclose(f);
    reopen();
    CHECK(file_size(BOOKS) == good, "F6: the file is truncated to its last whole record");
    EXPECT(REQ("8", "ledger.check", ""), OK("8", "{\"ok\":true}"));

    /* F4: damage stops the books from opening; nothing is repaired */
    b = slurp(BOOKS, &n);
    char *copy = malloc((size_t) n + 1);
    memcpy(copy, b, (size_t) n + 1);
    char *amt = strstr(copy, "9007199254740991");
    *amt = '8';
    spit(BOOKS, copy, n);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == -1, "F4: an edited amount breaks the chain");
    CHECK(file_size(BOOKS) == n, "F4: the damaged file is left as it was");
    memcpy(copy, b, (size_t) n + 1);
    copy[3] = copy[3] == 'a' ? 'b' : 'a';
    spit(BOOKS, copy, n);
    CHECK(zxvd_rpc_books_open() == -1, "F4: an edited hash breaks the chain");
    memcpy(copy, b, (size_t) n + 1);
    copy[64] = 'x';
    spit(BOOKS, copy, n);
    CHECK(zxvd_rpc_books_open() == -1, "F4: a malformed record is refused");
    memcpy(copy, b, (size_t) n + 1);
    copy[10] = 'G';
    spit(BOOKS, copy, n);
    CHECK(zxvd_rpc_books_open() == -1, "F4: a non-hex hash is refused");
    char *nl = strchr(copy, '\n');
    spit(BOOKS, nl + 1, n - (long) (nl + 1 - copy));
    CHECK(zxvd_rpc_books_open() == -1, "F4: a file without its header is refused");
    spit(BOOKS, b, n);
    static const char *const BAD[] = {
        "{\"op\":\"nope\"}",
        "[1]",
        "{\"code\":\"USD\"}",
        "{\"op\":\"ccy\",\"code\":\"ZZZ\"}",
        "{\"op\":\"ccy\",\"code\":5}",
        "{\"op\":\"unit\",\"code\":\"GBP\",\"minor\":2}",
        "{\"op\":\"unit\",\"code\":\"QQQ\",\"minor\":256}",
        "{\"op\":\"acct\",\"name\":\"x\",\"ccy\":\"USD\",\"kind\":0,\"id\":9}",
        "{\"op\":\"acct\",\"name\":\"x\",\"ccy\":\"USD\",\"kind\":2,\"id\":4}",
        "{\"op\":\"acct\",\"name\":\"x\",\"ccy\":\"XXX\",\"kind\":0,\"id\":4}",
        "{\"op\":\"post\",\"from\":0,\"to\":1,\"amount\":\"1\",\"ccy\":\"USD\",\"ref\":\"r\","
        "\"id\":\"99\"}",
        "{\"op\":\"post\",\"from\":0,\"to\":1,\"amount\":1,\"ccy\":\"USD\",\"ref\":\"r\","
        "\"id\":\"5\"}",
        "{\"op\":\"post\",\"from\":0,\"to\":1,\"amount\":\"x\",\"ccy\":\"USD\",\"ref\":\"r\","
        "\"id\":\"5\"}",
        "{\"op\":\"post\",\"from\":0,\"to\":1,\"amount\":\"1\",\"ccy\":\"USD\",\"ref\":\"neg\","
        "\"id\":\"x\"}",
        "{\"op\":\"post\",\"from\":4294967296,\"to\":1,\"amount\":\"1\",\"ccy\":\"USD\","
        "\"ref\":\"r\",\"id\":\"5\"}",
        "{\"op\":\"post\",\"from\":1,\"to\":0,\"amount\":\"1\",\"ccy\":\"USD\","
        "\"ref\":\"e2e \\u00e9 1\",\"id\":\"5\"}",
        "{\"op\":\"books\",\"v\":1}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        spit(BOOKS, b, n);
        append_chained(BOOKS, BAD[i]);
        CHECK(zxvd_rpc_books_open() == -1, "F4: refused: %s", BAD[i]);
    }
    /* the same kinds of record, well formed, replay */
    static const char *const GOOD[] = {
        "{\"op\":\"ccy\",\"code\":\"EUR\"}",
        "{\"op\":\"unit\",\"code\":\"QQQ\",\"minor\":3}",
        "{\"op\":\"acct\",\"name\":\"y\",\"ccy\":\"EUR\",\"kind\":1,\"id\":4}",
        "{\"op\":\"acct\",\"name\":\"z\",\"ccy\":\"EUR\",\"kind\":0,\"id\":5}",
        "{\"op\":\"post\",\"from\":4,\"to\":5,\"amount\":\"7\",\"ccy\":\"EUR\",\"ref\":\"g\","
        "\"id\":\"6\"}",
    };
    spit(BOOKS, b, n);
    for (size_t i = 0; i < sizeof GOOD / sizeof GOOD[0]; i++) append_chained(BOOKS, GOOD[i]);
    CHECK(zxvd_rpc_books_open() == 0, "F4: well-formed records of every kind replay");
    EXPECT(REQ("9", "ledger.balance", ",\"params\":{\"account\":5}"),
           OK("9", "{\"debit_minor\":\"7\",\"credit_minor\":\"0\",\"equity_minor\":\"7\"}"));
    CHECK(has(REQ("1", "books.file", ""), "\"records\":\"17\""), "books.file: 17 records: %s",
          OUT.p);
    free(copy);
    free(b);

    /* the file must be ours alone */
    zxvd_rpc_books_close();
    chmod(BOOKS, 0640);
    CHECK(zxvd_rpc_books_open() == -1, "a group-readable books file is refused");
    chmod(BOOKS, 0600);
    char lnk[220];
    snprintf(lnk, sizeof lnk, "%s/link.zxvb", tmpl);
    CHECK(symlink(BOOKS, lnk) == 0, "symlink");
    CHECK(zxvd_rpc_books_file(lnk) == 0 && zxvd_rpc_books_open() == -1,
          "a books file behind a symlink is refused");
    char dir[220];
    snprintf(dir, sizeof dir, "%s/adir", tmpl);
    mkdir(dir, 0700);
    CHECK(zxvd_rpc_books_file(dir) == 0 && zxvd_rpc_books_open() == -1,
          "a folder is not a books file");
    CHECK(zxvd_rpc_books_file(BOOKS) == 0 && zxvd_rpc_books_open() == 0, "back to the file");

    /* F5: a write that fails leaves the books read-only for the run */
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = (rlim_t) file_size(BOOKS);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit the file size");
    snprintf(want, sizeof want, "%s", "\"name\":\"ZXV_E_IO\",\"class\":\"SYSTEM\"}}}");
    CHECK(has(REQ("10", "ledger.open_account",
                  ",\"params\":{\"name\":\"lost\",\"currency\":\"EUR\"}"),
              want) &&
              strstr(OUT.p, "this change is not saved"),
          "F5: the change that could not be saved answers ZXV_E_IO: %s", OUT.p);
    CHECK(has(REQ("10", "ledger.post",
                  ",\"params\":{\"from\":4,\"to\":5,\"amount_minor\":1,\"currency\":\"EUR\","
                  "\"reference\":\"after\"}"),
              want) &&
              strstr(OUT.p, "could not be written earlier"),
          "F5: later changes are refused: %s", OUT.p);
    CHECK(has(REQ("10", "currency.enable", ",\"params\":{\"code\":\"JPY\"}"), want),
          "F5: a currency too");
    CHECK(zxvd_rpc_earnings_sync(5000) == 0, "F5: the run's base needs no posting");
    CHECK(zxvd_rpc_earnings_sync(6000) == -1, "F5: earnings cannot post either");
    CHECK(has(REQ("10", "books.file", ""), "\"writable\":false}"), "books.file: read-only");
    EXPECT(REQ("10", "ledger.check", ""), OK("10", "{\"ok\":true}"));
    setrlimit(RLIMIT_FSIZE, &old);
    snprintf(req, sizeof req, "%s", REQ("10", "ledger.accounts", ""));
    CHECK(has(req, "\"name\":\"lost\""), "F5: the unsaved account is in memory this run");
    reopen();
    CHECK(!has(req, "\"name\":\"lost\""), "F5: and gone after a restart");
    CHECK(has(REQ("10", "books.file", ""), "\"writable\":true}"), "writable again");

    /* the in-memory default */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "F1: no file");
    EXPECT(REQ("11", "books.file", ""),
           OK("11", "{\"saved\":false,\"path\":null,\"records\":\"0\",\"head\":"
                    "\"0000000000000000000000000000000000000000000000000000000000000000\","
                    "\"writable\":true}"));
    EXPECT(REQ("11", "ledger.accounts", ""), OK("11", "{\"accounts\":[]}"));
    unlink(lnk);
    rmdir(dir);
    unlink(BOOKS);
    rmdir(tmpl);
}

/* ---------------- mesh sales in the books (zxvd_rpc.h S1-S4) ---------------- */

static void test_mesh(void)
{
    uint8_t inv[32];
    for (int i = 0; i < 32; i++) inv[i] = (uint8_t) (0xa0 + i);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_mesh_post(true, 5, 0, inv) == 0, "S: nothing to post while closed");
    CHECK(zxvd_rpc_books_open() == 0, "books open");
    EXPECT(REQ("1", "mesh.books", ""),
           OK("1", "{\"currency\":\"VFV\",\"wallet_account\":null,\"counterparty_account\":null,"
                   "\"fee_account\":null,\"wallet_minor\":\"0\",\"fees_minor\":\"0\","
                   "\"sold\":\"0\",\"bought\":\"0\"}"));
    CHECK(zxvd_rpc_mesh_post(true, 0, 0, inv) == 0 &&
              has(REQ("1", "mesh.books", ""), "\"wallet_account\":null"),
          "S: a free answer opens nothing");
    CHECK(zxvd_rpc_mesh_post(true, 100, 7, NULL) == -1, "S: no invoice");
    CHECK(zxvd_rpc_mesh_post(true, (uint64_t) ZXV_AMOUNT_MAX + 1u, 0, inv) == -1,
          "S: a price above ZXV_AMOUNT_MAX");
    CHECK(zxvd_rpc_mesh_post(false, (uint64_t) ZXV_AMOUNT_MAX, 1, inv) == -1,
          "S: price + fee above ZXV_AMOUNT_MAX");
    /* the earnings accounts are 0 and 1 (none yet: sync opens them) */
    CHECK(zxvd_rpc_mesh_post(true, 100, 9, inv) == 0, "S2: a sale");
    EXPECT(REQ("2", "mesh.books", ""),
           OK("2", "{\"currency\":\"VFV\",\"wallet_account\":1,\"counterparty_account\":0,"
                   "\"fee_account\":2,\"wallet_minor\":\"100\",\"fees_minor\":\"0\","
                   "\"sold\":\"1\",\"bought\":\"0\"}"));
    EXPECT(REQ("3", "ledger.entries", ",\"params\":{\"account\":1}"),
           OK("3", "{\"entries\":[{\"entry_id\":\"1\",\"signed_minor\":\"100\","
                   "\"counterparty\":0,\"reference\":\"mesh sale a0a1a2a3a4a5a6a7\"}]}"));
    CHECK(zxvd_rpc_mesh_post(true, 100, 9, inv) == 0 &&
              has(REQ("2", "mesh.books", ""), "\"wallet_minor\":\"100\""),
          "S3: the same request is not posted twice");
    inv[0] = 0x01;
    CHECK(zxvd_rpc_mesh_post(false, 50, 1, inv) == 0, "S2: a purchase");
    CHECK(has(REQ("2", "mesh.books", ""), "\"wallet_minor\":\"49\",\"fees_minor\":\"1\""),
          "S2: 50 + fee 1 out of 100: %s", OUT.p);
    inv[0] = 0x02;
    CHECK(zxvd_rpc_mesh_post(false, 200, 2, inv) == 0, "S4: a purchase beyond the wallet");
    CHECK(has(REQ("2", "mesh.books", ""), "\"wallet_minor\":\"0\",\"fees_minor\":\"3\""),
          "S4: the float covers the 153 short: %s", OUT.p);
    CHECK(has(REQ("3", "ledger.entries", ",\"params\":{\"account\":1}"),
              "{\"entry_id\":\"4\",\"signed_minor\":\"153\",\"counterparty\":0,"
              "\"reference\":\"mesh float 02a1a2a3a4a5a6a7\"},{\"entry_id\":\"5\","
              "\"signed_minor\":\"-200\",\"counterparty\":0,\"reference\":\"mesh buy "
              "02a1a2a3a4a5a6a7\"},{\"entry_id\":\"6\",\"signed_minor\":\"-2\",\"counterparty\":2,"
              "\"reference\":\"mesh fee 02a1a2a3a4a5a6a7\"}"),
          "S4: float, price and fee: %s", OUT.p);
    inv[0] = 0x03;
    CHECK(zxvd_rpc_mesh_post(false, 10, 0, inv) == 0, "S2: a purchase with no fee");
    CHECK(has(REQ("2", "mesh.books", ""),
              "\"wallet_minor\":\"0\",\"fees_minor\":\"3\",\"sold\":\"2\",\"bought\":\"3\""),
          "S2: no fee posting: %s", OUT.p);
    EXPECT(REQ("4", "ledger.check", ""), OK("4", "{\"ok\":true}"));
    /* S1: the accounts are found again after a restart (in memory: a new book) */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "books open");
    CHECK(has(REQ("2", "mesh.books", ""), "\"wallet_account\":null"),
          "in memory, a restart starts empty");
}

static void test_socket(void)
{
    char tmpl[] = "/tmp/zxvdrpc.XXXXXX";
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(DIR_, sizeof DIR_, "%s/run", tmpl);
    snprintf(SOCK, sizeof SOCK, "%s/zxvd.sock", DIR_);

    /* T1: too long a path, a regular file, a foreign folder */
    char longp[300];
    memset(longp, 'a', sizeof longp - 1);
    longp[0] = '/';
    longp[sizeof longp - 1] = 0;
    CHECK(zxvd_rpc_open(longp, &OPS) == -1, "a path longer than sun_path is refused");
    /* the limit is exact: sun_path holds the path and its NUL */
    size_t sun = sizeof(((struct sockaddr_un *) 0)->sun_path);
    char edge[300];
    int el = snprintf(edge, sizeof edge, "%s/", tmpl);
    memset(edge + el, 'e', sun - (size_t) el);
    edge[sun] = 0;
    CHECK(zxvd_rpc_open(edge, &OPS) == -1, "a path of exactly sun_path bytes is refused");
    edge[sun - 1] = 0;
    CHECK(zxvd_rpc_open(edge, &OPS) == 0, "one byte shorter is served");
    zxvd_rpc_close();
    CHECK(access(edge, F_OK) != 0, "and removed on close");
    /* a bare file name is a socket in the current folder */
    char cwd[1024];
    CHECK(getcwd(cwd, sizeof cwd) != NULL && chdir(tmpl) == 0, "chdir");
    CHECK(zxvd_rpc_open("rel.sock", &OPS) == 0, "a relative socket name is served");
    struct stat rs;
    CHECK(lstat("rel.sock", &rs) == 0 && S_ISSOCK(rs.st_mode), "in the current folder");
    zxvd_rpc_close();
    CHECK(chdir(cwd) == 0, "chdir back");
    /* a folder others can write in is refused; so is one that cannot be made */
    char gw[260], gws[300];
    snprintf(gw, sizeof gw, "%s/gw", tmpl);
    snprintf(gws, sizeof gws, "%s/zxvd.sock", gw);
    CHECK(mkdir(gw, 0700) == 0, "folder made");
    int gfd = open(gw, O_RDONLY | O_DIRECTORY | O_CLOEXEC); /* modes set through it */
    CHECK(gfd >= 0 && fchmod(gfd, 0770) == 0, "group-writable folder");
    CHECK(zxvd_rpc_open(gws, &OPS) == -1, "a group-writable folder is refused");
    CHECK(fchmod(gfd, 0702) == 0 && zxvd_rpc_open(gws, &OPS) == -1,
          "an other-writable folder is refused");
    CHECK(fchmod(gfd, 0700) == 0 && zxvd_rpc_open(gws, &OPS) == 0,
          "the same folder at 0700 is served");
    zxvd_rpc_close();
    close(gfd);
    rmdir(gw);
    snprintf(gws, sizeof gws, "%s/no/such/zxvd.sock", tmpl);
    CHECK(zxvd_rpc_open(gws, &OPS) == -1, "a folder whose parent is missing is refused");
    CHECK(mkdir(DIR_, 0700) == 0, "mkdir");
    FILE *f = fopen(SOCK, "w");
    fputs("keep", f);
    fclose(f);
    CHECK(zxvd_rpc_open(SOCK, &OPS) == -1, "a regular file is refused");
    struct stat st;
    CHECK(stat(SOCK, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 4, "and kept");
    unlink(SOCK);
    char fileparent[300];
    snprintf(fileparent, sizeof fileparent, "%s/plain", tmpl);
    f = fopen(fileparent, "w");
    fclose(f);
    char under[320];
    snprintf(under, sizeof under, "%s/zxvd.sock", fileparent);
    CHECK(zxvd_rpc_open(under, &OPS) == -1, "a parent that is not a folder is refused");
    rmdir(DIR_);
    CHECK(zxvd_rpc_fds(&(fd_set){0}) == -1, "nothing is open after refusals");

    /* a stale socket file (nobody listening) is replaced */
    CHECK(mkdir(DIR_, 0755) == 0, "mkdir 0755");
    int stale = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", SOCK);
    CHECK(bind(stale, (struct sockaddr *) &a, sizeof a) == 0, "stale bind");
    close(stale);
    rmdir(DIR_); /* fails (not empty): the folder stays, created by us */
    CHECK(zxvd_rpc_open(SOCK, &OPS) == 0, "a stale socket is replaced");
    CHECK(stat(SOCK, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600,
          "socket mode 0600 (%o)", (unsigned) (st.st_mode & 0777));
    CHECK(zxvd_rpc_open(SOCK, &OPS) == -1, "a live socket is never taken over");

    /* zxvd_rpc_fds returns the highest descriptor even when a later slot
     * holds a lower one */
    int dummy[6];
    for (int i = 0; i < 6; i++) dummy[i] = open("/dev/null", O_RDONLY);
    int ca = connect_to(SOCK);
    pump(3);
    for (int i = 0; i < 6; i++) close(dummy[i]);
    int cb = connect_to(SOCK);
    pump(3);
    fd_set fs;
    FD_ZERO(&fs);
    int mx = zxvd_rpc_fds(&fs), hi = -1;
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        if (FD_ISSET(fd, &fs)) hi = fd;
    CHECK(ca >= 0 && cb >= 0 && mx == hi, "fds: highest %d, returned %d", hi, mx);
    close(ca);
    close(cb);
    pump(3);

    /* serve: two requests in one write, a split line, CRLF, an empty line */
    int c1 = connect_to(SOCK);
    CHECK(c1 >= 0, "connect");
    const char *two = REQ("1", "swarm.state", "") "\n" REQ("2", "notes.list", "") "\n";
    send(c1, two, strlen(two), 0);
    char line[70000];
    CHECK(read_line(c1, line, sizeof line) && !strcmp(line, OK("1", "{\"tick\":7}")), "first: %s",
          line);
    CHECK(read_line(c1, line, sizeof line) && !strcmp(line, OK("2", "{\"unread\":0,\"notes\":[]}")),
          "second: %s", line);
    send(c1, "{\"jsonrpc\":\"2.0\",\"id\":3,", 24, 0);
    pump(3);
    send(c1, "\"method\":\"swarm.state\"}\r\n\n", 26, 0);
    CHECK(read_line(c1, line, sizeof line) && !strcmp(line, OK("3", "{\"tick\":7}")),
          "split + CRLF: %s", line);
    send(c1, "{\"jsonrpc\":\"2.0\",\"method\":\"notes.mark_read\"}\n", 45, 0);
    send(c1, REQ("4", "swarm.state", "") "\n", sizeof(REQ("4", "swarm.state", "")), 0);
    CHECK(read_line(c1, line, sizeof line) && !strncmp(line, "{\"jsonrpc\":\"2.0\",\"id\":4,", 24),
          "a notification on the socket gets no reply: %s", line);
    send(c1, REQ("5", "ledger.check", "") "\n", sizeof(REQ("5", "ledger.check", "")), 0);
    CHECK(read_line(c1, line, sizeof line) && !strcmp(line, OK("5", "{\"ok\":true}")),
          "the socket's own books: %s", line);

    /* T4: eight clients at most, the ninth is closed */
    int cs[ZXVD_RPC_MAX_CLIENTS];
    cs[0] = c1;
    for (int i = 1; i < ZXVD_RPC_MAX_CLIENTS; i++) {
        cs[i] = connect_to(SOCK);
        pump(2);
    }
    int extra = connect_to(SOCK);
    pump(3);
    CHECK(closed_by_server(extra), "a ninth client is closed");
    close(extra);
    for (int i = 1; i < ZXVD_RPC_MAX_CLIENTS; i++) {
        send(cs[i], REQ("9", "swarm.state", "") "\n", sizeof(REQ("9", "swarm.state", "")), 0);
        CHECK(read_line(cs[i], line, sizeof line) && !strcmp(line, OK("9", "{\"tick\":7}")),
              "client %d served", i);
    }
    /* T3: an over-long line closes that client only */
    static char junk[ZXVD_RPC_MAX_LINE + 64];
    memset(junk, 'x', sizeof junk);
    send_pumped(cs[1], junk, sizeof junk);
    CHECK(closed_by_server(cs[1]), "over-long line: client closed");
    close(cs[1]);
    /* the line limit is exact: ZXVD_RPC_MAX_LINE bytes and LF are served,
     * one byte more closes the client */
    static char edge_line[ZXVD_RPC_MAX_LINE + 3];
    memset(edge_line, ' ', sizeof edge_line);
    memcpy(edge_line, REQ("13", "swarm.state", ""), sizeof(REQ("13", "swarm.state", "")) - 1);
    edge_line[ZXVD_RPC_MAX_LINE] = '\n';
    send_pumped(cs[3], edge_line, ZXVD_RPC_MAX_LINE + 1);
    CHECK(read_line(cs[3], line, sizeof line) && !strcmp(line, OK("13", "{\"tick\":7}")),
          "a line of exactly the limit is served");
    edge_line[ZXVD_RPC_MAX_LINE] = ' ';
    send_pumped(cs[4], edge_line, ZXVD_RPC_MAX_LINE + 1); /* no LF yet: the buffer is full */
    CHECK(closed_by_server(cs[4]), "one byte over the limit: client closed at once");
    /* a client that closes is dropped, freeing its slot */
    close(cs[2]);
    pump(3);
    int again = connect_to(SOCK), again2 = connect_to(SOCK);
    pump(3);
    send(again, REQ("10", "swarm.state", "") "\n", sizeof(REQ("10", "swarm.state", "")), 0);
    send(again2, REQ("11", "swarm.state", "") "\n", sizeof(REQ("11", "swarm.state", "")), 0);
    CHECK(read_line(again, line, sizeof line) && !strcmp(line, OK("10", "{\"tick\":7}")),
          "freed slots are reused (1)");
    CHECK(read_line(again2, line, sizeof line) && !strcmp(line, OK("11", "{\"tick\":7}")),
          "freed slots are reused (2)");
    /* T4: idle clients are dropped after ZXVD_RPC_IDLE_MS */
    NOW += ZXVD_RPC_IDLE_MS;
    pump(2);
    send(again, REQ("12", "swarm.state", "") "\n", sizeof(REQ("12", "swarm.state", "")), 0);
    CHECK(read_line(again, line, sizeof line), "exactly the idle limit: still served");
    NOW += ZXVD_RPC_IDLE_MS + 1;
    CHECK(closed_by_server(c1), "an idle client is dropped");
    int cz = connect_to(SOCK); /* takes slot 0 again */
    pump(3);
    send(cz, REQ("14", "swarm.state", "") "\n", sizeof(REQ("14", "swarm.state", "")), 0);
    CHECK(read_line(cz, line, sizeof line) && !strcmp(line, OK("14", "{\"tick\":7}")),
          "slot 0 reused");

    zxvd_rpc_close();
    CHECK(closed_by_server(cz) && closed_by_server(again), "close drops every client");
    close(cz);
    CHECK(stat(SOCK, &st) != 0, "close removes the socket file");
    CHECK(zxvd_rpc_fds(&(fd_set){0}) == -1, "and nothing is listening");
    f = fopen(SOCK, "w");
    fclose(f);
    zxvd_rpc_close(); /* closing twice touches nothing */
    CHECK(stat(SOCK, &st) == 0 && S_ISREG(st.st_mode), "a second close leaves the path alone");
    unlink(SOCK);
    EXPECT(REQ("1", "ledger.check", ""), ERR("1", "-32603", "the books are not open"));
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) close(cs[i]);
    close(again);
    close(again2);

    /* a folder that does not exist is created 0700 */
    rmdir(DIR_);
    char nd[260], ns[300];
    snprintf(nd, sizeof nd, "%s/fresh", tmpl);
    snprintf(ns, sizeof ns, "%s/zxvd.sock", nd);
    CHECK(zxvd_rpc_open(ns, &OPS) == 0, "open with a new folder");
    CHECK(stat(nd, &st) == 0 && (st.st_mode & 0777) == 0700, "new folder is 0700 (%o)",
          (unsigned) (st.st_mode & 0777));
    zxvd_rpc_close();
    rmdir(nd);
    unlink(fileparent);
    rmdir(tmpl);
}

/* ---------------- peer.* ---------------- */

static void test_peer_methods(void)
{
    zxvd_rpc_set_peer_ops(NULL);
    EXPECT(REQ("1", "peer.status", ""), ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "peer.ask", ",\"params\":{\"peer\":\"1.2.3.4\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_peer_ops(&PEER_NONE);
    EXPECT(REQ("1", "peer.serve", ""), ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "peer.status", ""), ERR("1", "-32601", "not available"));
    zxvd_rpc_set_peer_ops(&PEER_STUB);
    EXPECT(REQ("2", "peer.status", ""), OK("2", "{\"sold\":2}"));
    EXPECT(
        REQ("3", "peer.ask",
            ",\"params\":{\"peer\":\"10.0.0.2:9\",\"prompt\":\"hi\",\"max_price_minor\":125000}"),
        OK("3", "{\"answer\":\"ok\"}"));
    CHECK(!strcmp(last_peer, "10.0.0.2:9") && !strcmp(last_peer_prompt, "hi") && last_max == 125000,
          "peer.ask passes peer, prompt and price (%s %s %llu)", last_peer, last_peer_prompt,
          (unsigned long long) last_max);
    EXPECT(REQ("3", "peer.ask",
               ",\"params\":{\"peer\":\"10.0.0.2\",\"prompt\":\"hi\",\"max_price_minor\":"
               "\"9223372036854775807\"}"),
           OK("3", "{\"answer\":\"ok\"}"));
    CHECK(last_max == 9223372036854775807ull, "a price as a decimal string");
    EXPECT(REQ("4", "peer.ask",
               ",\"params\":{\"peer\":\"10.0.0.2\",\"prompt\":\"fail\",\"max_price_minor\":1}"),
           ERR("4", "-32010", "the peer's price is above max_price"));
    int n = peer_asks;
    EXPECT(REQ("5", "peer.ask", ",\"params\":{\"prompt\":\"hi\",\"max_price_minor\":1}"),
           ERR("5", "-32602", "missing string parameter \\\"peer\\\""));
    EXPECT(REQ("5", "peer.ask", ",\"params\":{\"peer\":\"10.0.0.2\",\"max_price_minor\":1}"),
           ERR("5", "-32602", "missing string parameter \\\"prompt\\\""));
    EXPECT(REQ("5", "peer.ask", ",\"params\":{\"peer\":\"10.0.0.2\",\"prompt\":\"hi\"}"),
           ERR("5", "-32602", "missing integer parameter \\\"max_price_minor\\\""));
    EXPECT(REQ("5", "peer.ask",
               ",\"params\":{\"peer\":\"10.0.0.2\",\"prompt\":\"hi\",\"max_price_minor\":-1}"),
           ERR("5", "-32602", "\\\"max_price_minor\\\" must not be negative"));
    CHECK(has(REQ("5", "peer.ask",
                  ",\"params\":{\"peer\":\"10.0.0.2\",\"prompt\":\"hi\",\"max_price_minor\":1.5}"),
              "-32602"),
          "a fractional price is refused");
    CHECK(peer_asks == n, "bad params never reach the host");
    /* 4096-byte prompts reach the host, 4097 do not; 63-byte peers do, 64 do not */
    static char req[5000];
    int k = snprintf(req, sizeof req,
                     "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"peer.ask\",\"params\":{"
                     "\"peer\":\"1.2.3.4\",\"max_price_minor\":0,\"prompt\":\"");
    memset(req + k, 'p', 4096);
    strcpy(req + k + 4096, "\"}}");
    CHECK(has(req, "\"result\"") && strlen(last_peer_prompt) == 4096,
          "a 4096-byte peer prompt is accepted");
    memset(req + k, 'p', 4097);
    strcpy(req + k + 4097, "\"}}");
    CHECK(has(req, "at most 4096 bytes"), "a 4097-byte peer prompt is refused");
    k = snprintf(req, sizeof req,
                 "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"peer.ask\",\"params\":{"
                 "\"prompt\":\"x\",\"max_price_minor\":0,\"peer\":\"");
    memset(req + k, '1', 63);
    strcpy(req + k + 63, "\"}}");
    CHECK(has(req, "\"result\"") && strlen(last_peer) == 63, "a 63-byte peer is accepted");
    memset(req + k, '1', 64);
    strcpy(req + k + 64, "\"}}");
    CHECK(has(req, "at most 63 bytes"), "a 64-byte peer is refused");

    EXPECT(REQ("7", "peer.serve", ",\"params\":{\"price_minor\":125000}"),
           OK("7", "{\"serving\":true}"));
    CHECK(last_serve_on && last_serve_price == 125000, "peer.serve with a price starts serving");
    EXPECT(REQ("7", "peer.serve", ",\"params\":{}"), OK("7", "{\"serving\":false}"));
    CHECK(!last_serve_on, "peer.serve without a price stops");
    EXPECT(REQ("7", "peer.serve", ""), OK("7", "{\"serving\":false}"));
    EXPECT(REQ("7", "peer.serve", ",\"params\":{\"price_minor\":13}"),
           ERR("7", "-32010", "networking is off"));
    n = serve_calls;
    EXPECT(REQ("7", "peer.serve", ",\"params\":{\"price_minor\":-5}"),
           ERR("7", "-32602", "\\\"price_minor\\\" must not be negative"));
    EXPECT(REQ("7", "peer.serve", ",\"params\":{\"price_minor\":\"x\"}"),
           ERR("7", "-32602",
               "\\\"price_minor\\\" must be an integer or a decimal string (no "
               "fraction or exponent)"));
    CHECK(serve_calls == n, "a bad price never reaches the host");
    zxvd_rpc_set_peer_ops(NULL);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN); /* as zxv_host.c does: a closed peer is an error return */
    test_buf();
    test_protocol();
    test_host_methods();
    test_peer_methods();
    test_books_closed();
    test_books();
    test_earnings();
    test_books_file();
    test_mesh();
    test_socket();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_rpc: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_rpc: %d checks\n", checks);
    return 0;
}
