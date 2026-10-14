/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_rpc.c — zxvd_rpc.c on its own, against the real libzxv and a
 * stub host (zxvd_rpc.h): every method's exact reply, every JSON-RPC error,
 * parameter validation, then the socket (T1-T4) driven from this process.
 * Fast enough to be the mutation harness's test command
 * (tests/harness/mutator/targets.json "zxvd_rpc"); test_zxvd.py covers the
 * real daemon end to end. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700 /* nftw */
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
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>

#include <sys/resource.h>
#include <sys/file.h>

#include "zxvd_rpc.h"
#include "zxvd_ext.h"
#include "juris.h"
#include "zxv_api.h"
#include "sha256.h"
#include "web4_web2.h"

#include "zxv_files.h"
#include "zxv_spaces.h"

/* remove a scratch tree without a shell (no links followed) */
static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
    (void) st, (void) flag, (void) f;
    return remove(p);
}
static int rm_tree(const char *p)
{
    return nftw(p, rm_one, 16, FTW_DEPTH | FTW_PHYS);
}

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

/* phone.* stubs (zxvd_phone.h) */
static char last_phone_name[40];
static bool last_phone_post;
static const char *ph_pair(const char *name, bool post, zxvd_buf_t *o)
{
    snprintf(last_phone_name, sizeof last_phone_name, "%s", name);
    last_phone_post = post;
    if (!strcmp(name, "full")) return "too many phones paired; revoke one first";
    zxvd_buf_raw(o, "{\"paired\":true}", 15);
    return NULL;
}
static void ph_list(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, "{\"phones\":[]}", 13);
}
static const char *ph_revoke(const char *id, zxvd_buf_t *o)
{
    if (strcmp(id, "00112233445566778899aabbccddeeff")) return "no such phone";
    zxvd_buf_raw(o, "{\"revoked\":true}", 16);
    return NULL;
}
static int last_link_port;
static char last_link_bind[48];
static const char *ph_link(bool on, int32_t port, const char *bind, zxvd_buf_t *o)
{
    last_link_port = port;
    snprintf(last_link_bind, sizeof last_link_bind, "%s", bind ? bind : "(all)");
    if (port == 1) return "cannot listen on TCP 1: Permission denied";
    zxvd_buf_printf(o, "{\"on\":%s}", on ? "true" : "false");
    return NULL;
}
static const zxvd_phone_ops_t PHONE_STUB = {ph_link, ph_pair, ph_list, ph_revoke};
/* aipi.* stubs (zxvd_aipi_ops_t) */
static uint8_t put_data[ZXVD_AIPI_HEX_MAX + 1];
static uint32_t put_len;
static char put_path[1100], get_cid[200], exec_fn[200];
static bool put_priv, put_had_data, exec_priv;
static zxvd_aipi_fn_t pub_fn;
static int64_t exec_args[ZXVD_AIPI_ARGS_MAX + 1];
static uint32_t exec_n;
static int aipi_calls;
static const char *a_put(const uint8_t *data, uint32_t len, const char *path, bool priv,
                         zxvd_buf_t *o)
{
    aipi_calls++;
    put_had_data = data != NULL;
    put_len = len;
    if (data) memcpy(put_data, data, len);
    snprintf(put_path, sizeof put_path, "%s", path ? path : "<null>");
    put_priv = priv;
    if (path && !strcmp(path, "/fail")) {
        zxvd_buf_raw(o, "{\"partial\":", 11); /* must be discarded */
        return "the file could not be opened";
    }
    zxvd_buf_printf(o, "{\"bytes\":%u}", (unsigned) len);
    return NULL;
}
static const char *a_get(const char *cid, zxvd_buf_t *o)
{
    aipi_calls++;
    snprintf(get_cid, sizeof get_cid, "%s", cid);
    zxvd_buf_raw(o, "{\"hex\":\"\"}", 10);
    return NULL;
}
static const char *a_publish(const zxvd_aipi_fn_t *fn, zxvd_buf_t *o)
{
    aipi_calls++;
    pub_fn = *fn;
    zxvd_buf_raw(o, "{\"function\":\"zF\"}", 17);
    return NULL;
}
static const char *a_execute(const char *fn, const int64_t *args, uint32_t n, bool priv,
                             zxvd_buf_t *o)
{
    aipi_calls++;
    snprintf(exec_fn, sizeof exec_fn, "%s", fn);
    memcpy(exec_args, args, n * sizeof *args);
    exec_n = n;
    exec_priv = priv;
    if (n == 1 && args[0] == 13) return "AIPI_ERR_EXEC: the sandbox refused or stopped the run";
    zxvd_buf_raw(o, "{\"result\":\"1\"}", 14);
    return NULL;
}
static void a_status(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, "{\"online\":false}", 16);
}
static const zxvd_aipi_ops_t AIPI_STUB = {a_put, a_get, a_publish, a_execute, a_status};
static const zxvd_aipi_ops_t AIPI_NONE = {0};

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
              "\"notes.mark_read\",\"net.command\",\"net.peers\",\"net.offers\",\"player.card\","
              "\"peer.ask\",\"peer.serve\",\"peer.status\","
              "\"update.check\",\"currency.enable\","
              "\"ledger.open_account\",\"ledger.post\",\"ledger.balance\",\"ledger.accounts\","
              "\"ledger.entries\",\"ledger.check\",\"swarm.earnings\",\"mesh.books\","
              "\"books.file\",\"jurisdiction.get\",\"jurisdiction.set\",\"aipi.put\",\"aipi.get\","
              "\"aipi.publish\",\"aipi.execute\",\"aipi.status\",\"features.get\",\"features.set\",\"files.root\",\"files.list\","
              "\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\","
              "\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\","
              "\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\","
              "\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\","
              "\"phone.list\",\"phone.revoke\","
              "\"play.catalogue\",\"play.run\",\"play.draft\"]"
              "}"));
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
    EXPECT(REQ("1", "play.catalogue", ""), ERR("1", "-32601", "not available"));
    zxvd_rpc_set_ops(&SILENT);
    EXPECT(REQ("1", "swarm.ask", ",\"params\":{\"prompt\":\"x\"}"), OK("1", "{\"answer\":\"\"}"));
    zxvd_rpc_set_ops(&EMPTY);
    EXPECT(REQ("1", "swarm.state", ""), ERR("1", "-32603", "no result"));
    zxvd_rpc_set_ops(&OPS);
}

/* ---------------- the books (libzxv) ---------------- */

/* ---------------- spaces.* and files.* in a space (zxv_spaces.h) ---------------- */

#define FERR(id, msg) ERR(id, "-32014", msg)
#define SERR(id, msg) ERR(id, "-32013", msg)
#define SPS(id, kind, name, owner, role, view, write, pub, members, self)                          \
    "{\"id\":\"" id "\",\"kind\":\"" kind "\",\"name\":\"" name "\",\"owner\":\"" owner            \
    "\",\"role\":\"" role "\",\"view\":" view ",\"write\":" write ",\"public\":" pub               \
    ",\"members\":[" members "]" GOV_V(self) "}"
/* a space's governance with no proposals: vertical, 51%, this node as `self` */
#define GOV_V(self)                                                                                \
    ",\"governance\":\"vertical\",\"quorum_pct\":51,\"self\":\"" self                              \
    "\",\"proposals\":[],\"next_proposal\":0,\"cid\":\"\""
/* owned spaces vote as "me"; joined ones here were given no member id */
#define SP(id, kind, name, owner, role, view, write, pub, members)                                 \
    SPS(id, kind, name, owner, role, view, write, pub, members, "me")
#define SPJ(id, kind, name, owner, role, view, write, pub, members)                                \
    SPS(id, kind, name, owner, role, view, write, pub, members, "")
#define HOME_SP      SP("home", "home", "Home", "me", "owner", "true", "true", "false", "")
#define STUDIO_SP(m) SP("studio-a", "office", "Studio A", "me", "owner", "true", "true", "false", m)

static void test_spaces_methods(void)
{
    char root[200];
    snprintf(root, sizeof root, "%s/zxvd-spaces-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) != NULL, "scratch root");
    CHECK(zxf_set_root(root) == 0, "root set");

    /* S1 table */
    CHECK(zxs_rights(ZXS_HOME, ZXS_OWNER) == (ZXS_VIEW | ZXS_WRITE) &&
              zxs_rights(ZXS_HOME, ZXS_MEMBER) == 0 && zxs_rights(ZXS_HOME, ZXS_PUBLIC) == 0,
          "home is the owner's");
    CHECK(zxs_rights(ZXS_OFFICE, ZXS_MEMBER) == (ZXS_VIEW | ZXS_WRITE) &&
              zxs_rights(ZXS_TEAM, ZXS_MEMBER) == (ZXS_VIEW | ZXS_WRITE) &&
              zxs_rights(ZXS_OFFICE, ZXS_GUEST) == ZXS_VIEW &&
              zxs_rights(ZXS_TEAM, ZXS_GUEST) == ZXS_VIEW &&
              zxs_rights(ZXS_OFFICE, ZXS_PUBLIC) == 0 && zxs_rights(ZXS_TEAM, ZXS_PUBLIC) == 0,
          "offices and teams: members write, guests view");
    CHECK(zxs_rights(ZXS_CLOUDTOP, ZXS_OWNER) == (ZXS_VIEW | ZXS_WRITE) &&
              zxs_rights(ZXS_CLOUDTOP, ZXS_MEMBER) == ZXS_VIEW &&
              zxs_rights(ZXS_CLOUDTOP, ZXS_PUBLIC) == ZXS_VIEW,
          "cloud top: anyone views, the owner writes");
    CHECK(zxs_rights(ZXS_PARK, ZXS_PUBLIC) == (ZXS_VIEW | ZXS_WRITE) &&
              zxs_rights(ZXS_PARK, ZXS_GUEST) == (ZXS_VIEW | ZXS_WRITE),
          "a park is public");
    CHECK(zxs_rights(ZXS_KINDS, ZXS_OWNER) == 0 && zxs_rights(ZXS_HOME, ZXS_ROLES) == 0,
          "out of range");
    CHECK(zxs_kind_parse("park") == ZXS_PARK && zxs_kind_parse("Park") == -1 &&
              zxs_role_parse("guest") == ZXS_GUEST && zxs_role_parse("") == -1,
          "parse");
    CHECK(zxs_who_ok("peer-1") && !zxs_who_ok("") && !zxs_who_ok("a b") && !zxs_who_ok("a\"") &&
              !zxs_who_ok("a\\b"),
          "member ids");

    EXPECT(REQ("1", "spaces.list", ""), OK("1", "{\"spaces\":[" HOME_SP "]}"));
    EXPECT(REQ("1", "spaces.get", ",\"params\":{\"space\":\"home\"}"), OK("1", HOME_SP));
    EXPECT(REQ("1", "spaces.get", ""),
           ERR("1", "-32602", "missing string parameter \\\"space\\\""));
    EXPECT(REQ("1", "spaces.get", ",\"params\":{\"space\":\"nope\"}"), SERR("1", "no such space"));
    EXPECT(REQ("1", "spaces.get", ",\"params\":{\"space\":\"..\"}"),
           ERR("1", "-32602", "\\\"space\\\" is not a space id"));
    EXPECT(REQ("1", "files.list", ",\"params\":{\"space\":\"Nope\"}"),
           ERR("1", "-32602", "\\\"space\\\" is not a space id"));
    EXPECT(REQ("1", "files.list", ",\"params\":{\"space\":\"nope\"}"), SERR("1", "no such space"));

    /* create */
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"office\",\"name\":\"Studio A\"}"),
           OK("2", STUDIO_SP("")));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"office\",\"name\":\"Studio A\"}"),
           OK("2",
              SP("studio-a-2", "office", "Studio A", "me", "owner", "true", "true", "false", "")));
    EXPECT(REQ("2", "spaces.create",
               ",\"params\":{\"kind\":\"cloudtop\",\"name\":\"\\u00c9t\\u00e9 \\\"front\\\"\"}"),
           OK("2", SP("t-front", "cloudtop", "\xc3\x89t\xc3\xa9 \\\"front\\\"", "me", "owner",
                      "true", "true", "true", "")));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"park\",\"name\":\"!!\"}"),
           OK("2", SP("park", "park", "!!", "me", "owner", "true", "true", "true", "")));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"team\",\"name\":\"Home\"}"),
           OK("2", SP("team", "team", "Home", "me", "owner", "true", "true", "false", "")));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"home\",\"name\":\"x\"}"),
           ERR("2", "-32602", "\\\"kind\\\" must be office, team, cloudtop or park"));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"attic\",\"name\":\"x\"}"),
           ERR("2", "-32602", "\\\"kind\\\" must be office, team, cloudtop or park"));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"team\",\"name\":\"a\\nb\"}"),
           FERR("2", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"team\",\"name\":\"\"}"),
           FERR("2", "not a valid name or path in the ZXV folder"));

    /* files in a space stay in it */
    EXPECT(REQ("3", "files.write",
               ",\"params\":{\"space\":\"studio-a\",\"path\":\"plan.txt\",\"text\":\"hi\"}"),
           OK("3", "{\"path\":\"plan.txt\",\"size\":2,\"done\":true}"));
    EXPECT(REQ("3", "files.list", ",\"params\":{\"space\":\"home\"}"),
           OK("3", "{\"path\":\"\",\"entries\":[]}"));
    CHECK(has(REQ("3", "files.list", ",\"params\":{\"space\":\"studio-a\"}"),
              "{\"name\":\"plan.txt\",\"dir\":false,\"size\":2"),
          "listed in its space: %s", OUT.p);
    EXPECT(REQ("3", "files.read", ",\"params\":{\"space\":\"studio-a\",\"path\":\"plan.txt\"}"),
           OK("3", "{\"path\":\"plan.txt\",\"size\":2,\"offset\":0,\"next\":2,\"eof\":true,"
                   "\"text\":\"hi\"}"));
    EXPECT(REQ("3", "files.read", ",\"params\":{\"path\":\"plan.txt\"}"),
           FERR("3", "no such file or folder"));
    EXPECT(REQ("3", "files.write", ",\"params\":{\"path\":\".spaces/studio-a/x\",\"text\":\"\"}"),
           FERR("3", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("3", "files.trash", ",\"params\":{\"space\":\"studio-a\",\"path\":\"plan.txt\"}"),
           OK("3", "{\"trashed\":\".Trash/plan.txt\",\"removed\":false}"));
    EXPECT(REQ("3", "files.list", ",\"params\":{\"path\":\".Trash\"}"),
           OK("3", "{\"path\":\".Trash\",\"entries\":[]}"));
    EXPECT(REQ("3", "files.rename",
               ",\"params\":{\"space\":\"studio-a\",\"from\":\".Trash/"
               "plan.txt\",\"to\":\"plan.txt\"}"),
           OK("3", "{\"path\":\"plan.txt\"}"));

    /* joined spaces: rights by role (S1, S2) */
    EXPECT(
        REQ("4", "spaces.join",
            ",\"params\":{\"id\":\"crew\",\"kind\":\"team\",\"name\":\"Crew\",\"owner\":\"alice\","
            "\"role\":\"guest\"}"),
        OK("4", SPJ("crew", "team", "Crew", "alice", "guest", "true", "false", "false", "")));
    EXPECT(
        REQ("4", "spaces.join",
            ",\"params\":{\"id\":\"crew\",\"kind\":\"team\",\"name\":\"Crew\",\"owner\":\"alice\","
            "\"role\":\"guest\"}"),
        FERR("4", "something with that name already exists"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"x\",\"kind\":\"team\",\"name\":\"X\",\"owner\":\"me\","
               "\"role\":\"member\"}"),
           FERR("4", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"x\",\"kind\":\"team\",\"name\":\"X\",\"owner\":\"bob\","
               "\"role\":\"owner\"}"),
           ERR("4", "-32602", "\\\"role\\\" must be member or guest"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"home\",\"kind\":\"team\",\"name\":\"X\",\"owner\":\"bob\","
               "\"role\":\"member\"}"),
           FERR("4", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("4", "files.list", ",\"params\":{\"space\":\"crew\"}"),
           OK("4", "{\"path\":\"\",\"entries\":[]}"));
    EXPECT(REQ("4", "files.write", ",\"params\":{\"space\":\"crew\",\"path\":\"a\",\"text\":\"\"}"),
           SERR("4", "your role in this space (guest) may not change it"));
    EXPECT(REQ("4", "files.mkdir", ",\"params\":{\"space\":\"crew\",\"path\":\"a\"}"),
           SERR("4", "your role in this space (guest) may not change it"));
    EXPECT(REQ("4", "files.rename", ",\"params\":{\"space\":\"crew\",\"from\":\"a\",\"to\":\"b\"}"),
           SERR("4", "your role in this space (guest) may not change it"));
    EXPECT(REQ("4", "files.trash", ",\"params\":{\"space\":\"crew\",\"path\":\"a\"}"),
           SERR("4", "your role in this space (guest) may not change it"));
    EXPECT(REQ("4", "files.empty_trash", ",\"params\":{\"space\":\"crew\"}"),
           SERR("4", "your role in this space (guest) may not change it"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"lab\",\"kind\":\"office\",\"name\":\"Lab\",\"owner\":\"bob\","
               "\"role\":\"member\"}"),
           OK("4", SPJ("lab", "office", "Lab", "bob", "member", "true", "true", "false", "")));
    EXPECT(REQ("4", "files.mkdir", ",\"params\":{\"space\":\"lab\",\"path\":\"bench\"}"),
           OK("4", "{\"path\":\"bench\"}"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"bobs-front\",\"kind\":\"cloudtop\",\"name\":\"Bob\","
               "\"owner\":\"bob\",\"role\":\"member\"}"),
           OK("4",
              SPJ("bobs-front", "cloudtop", "Bob", "bob", "member", "true", "false", "true", "")));
    EXPECT(REQ("4", "files.stat", ",\"params\":{\"space\":\"bobs-front\",\"path\":\"x\"}"),
           FERR("4", "no such file or folder"));
    EXPECT(REQ("4", "files.write",
               ",\"params\":{\"space\":\"bobs-front\",\"path\":\"x\",\"text\":\"\"}"),
           SERR("4", "your role in this space (member) may not change it"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"commons\",\"kind\":\"park\",\"name\":\"Commons\","
               "\"owner\":\"town\",\"role\":\"guest\"}"),
           OK("4", SPJ("commons", "park", "Commons", "town", "guest", "true", "true", "true", "")));
    EXPECT(REQ("4", "files.write",
               ",\"params\":{\"space\":\"commons\",\"path\":\"hello.txt\",\"text\":\"hi\"}"),
           OK("4", "{\"path\":\"hello.txt\",\"size\":2,\"done\":true}"));

    /* members (S4) */
    EXPECT(REQ("5", "spaces.set_members",
               ",\"params\":{\"space\":\"studio-a\",\"members\":[{\"who\":\"bob\",\"role\":"
               "\"member\"},{\"who\":\"carol\",\"role\":\"guest\"}]}"),
           OK("5", STUDIO_SP("{\"who\":\"bob\",\"role\":\"member\"},{\"who\":\"carol\",\"role\":"
                             "\"guest\"}")));
    EXPECT(REQ("5", "spaces.get", ",\"params\":{\"space\":\"studio-a\"}"),
           OK("5", STUDIO_SP("{\"who\":\"bob\",\"role\":\"member\"},{\"who\":\"carol\",\"role\":"
                             "\"guest\"}")));
    EXPECT(REQ("5", "spaces.set_members", ",\"params\":{\"space\":\"crew\",\"members\":[]}"),
           SERR("5", "only the owner of an office, team, cloud top or park changes its members"));
    EXPECT(REQ("5", "spaces.set_members", ",\"params\":{\"space\":\"home\",\"members\":[]}"),
           SERR("5", "only the owner of an office, team, cloud top or park changes its members"));
    EXPECT(REQ("5", "spaces.set_members",
               ",\"params\":{\"space\":\"studio-a\",\"members\":[{\"who\":\"bob\",\"role\":"
               "\"member\"},{\"who\":\"bob\",\"role\":\"guest\"}]}"),
           ERR("5", "-32602", "member 1 is listed twice"));
    EXPECT(REQ("5", "spaces.set_members",
               ",\"params\":{\"space\":\"studio-a\",\"members\":[{\"who\":\"me\",\"role\":"
               "\"member\"}]}"),
           ERR("5", "-32602", "member 0 needs a printable id and the role member or guest"));
    EXPECT(REQ("5", "spaces.set_members",
               ",\"params\":{\"space\":\"studio-a\",\"members\":[{\"who\":\"x\",\"role\":"
               "\"owner\"}]}"),
           ERR("5", "-32602", "member 0 needs a printable id and the role member or guest"));
    EXPECT(REQ("5", "spaces.set_members", ",\"params\":{\"space\":\"studio-a\",\"members\":[7]}"),
           ERR("5", "-32602", "member 0 must be {\\\"who\\\", \\\"role\\\"}"));
    EXPECT(REQ("5", "spaces.set_members", ",\"params\":{\"space\":\"studio-a\"}"),
           ERR("5", "-32602", "\\\"members\\\" must be an array of at most 64 members"));
    EXPECT(REQ("5", "spaces.get", ",\"params\":{\"space\":\"studio-a\"}"),
           OK("5", STUDIO_SP("{\"who\":\"bob\",\"role\":\"member\"},{\"who\":\"carol\",\"role\":"
                             "\"guest\"}")));
    EXPECT(REQ("5", "spaces.set_members",
               ",\"params\":{\"space\":\"studio-a-2\",\"members\":[{\"who\":\"bob\",\"role\":"
               "\"guest\"}]}"),
           OK("5", SP("studio-a-2", "office", "Studio A", "me", "owner", "true", "true", "false",
                      "{\"who\":\"bob\",\"role\":\"guest\"}")));

    /* governance (S4): zxvd counts the votes */
    CHECK(zxs_decide(ZXS_GOV_VERTICAL, 51, ZXS_OWNER, 3, 1, 0) == ZXS_D_TRUE &&
              zxs_decide(ZXS_GOV_VERTICAL, 51, ZXS_MEMBER, 3, 3, 0) == ZXS_D_NEITHER &&
              zxs_decide(ZXS_GOV_VERTICAL, 51, ZXS_GUEST, 3, 3, 0) == ZXS_D_FALSE,
          "vertical: the owner decides");
    CHECK(zxs_decide(ZXS_GOV_HORIZONTAL, 60, ZXS_MEMBER, 3, 2, 0) == ZXS_D_TRUE &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 60, ZXS_MEMBER, 3, 2, 1) == ZXS_D_BOTH &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 60, ZXS_MEMBER, 3, 1, 1) == ZXS_D_NEITHER &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 60, ZXS_MEMBER, 3, 1, 2) == ZXS_D_FALSE &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 60, ZXS_MEMBER, 0, 0, 0) == ZXS_D_FALSE &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 100, ZXS_OWNER, 4, 4, 0) == ZXS_D_TRUE &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 100, ZXS_OWNER, 4, 3, 0) == ZXS_D_NEITHER &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 0, ZXS_OWNER, 2, 1, 0) == ZXS_D_NEITHER &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 0, ZXS_OWNER, 100, 51, 0) == ZXS_D_TRUE &&
              zxs_decide(ZXS_GOV_HORIZONTAL, 50, ZXS_OWNER, 2, 1, 0) == ZXS_D_TRUE,
          "horizontal: quorum, objections, can no longer pass");
    CHECK(!strcmp(zxs_decision_name(ZXS_D_BOTH), "both") &&
              !strcmp(zxs_decision_name(ZXS_D_NEITHER), "neither") &&
              !strcmp(zxs_decision_name((zxs_decision_t) 9), "?"),
          "decision names");
    EXPECT(REQ("10", "spaces.create",
               ",\"params\":{\"kind\":\"office\",\"name\":\"Votes\",\"governance\":\"diagonal\"}"),
           ERR("10", "-32602", "\\\"governance\\\" must be vertical or horizontal"));
    EXPECT(REQ("10", "spaces.create",
               ",\"params\":{\"kind\":\"office\",\"name\":\"Votes\",\"quorum_pct\":101}"),
           ERR("10", "-32602", "\\\"quorum_pct\\\" must be 1 to 100"));
    EXPECT(REQ("10", "spaces.create",
               ",\"params\":{\"kind\":\"office\",\"name\":\"Votes\",\"quorum_pct\":0}"),
           ERR("10", "-32602", "\\\"quorum_pct\\\" must be 1 to 100"));
    CHECK(has(REQ("10", "spaces.create",
                  ",\"params\":{\"kind\":\"office\",\"name\":\"Votes\",\"governance\":"
                  "\"horizontal\",\"quorum_pct\":60}"),
              "\"members\":[],\"governance\":\"horizontal\",\"quorum_pct\":60,\"self\":\"me\","
              "\"proposals\":[],\"next_proposal\":0,\"cid\":\"\"}"),
          "horizontal space: %s", OUT.p);
    EXPECT(REQ("10", "spaces.set_members", ",\"params\":{\"space\":\"votes\",\"members\":[]}"),
           SERR("10", "this space decides its members by vote"));
    /* alone, the owner's own yes is the whole quorum */
    CHECK(has(REQ("10", "spaces.propose",
                  ",\"params\":{\"space\":\"votes\",\"members\":[{\"who\":\"bob\",\"role\":"
                  "\"member\"},{\"who\":\"carol\",\"role\":\"member\"},{\"who\":\"dan\","
                  "\"role\":\"guest\"}]}"),
              "{\"proposal\":0,\"state\":\"true\",\"space\":{\"id\":\"votes\""),
          "applied at once: %s", OUT.p);
    CHECK(has(REQ("10", "spaces.get", ",\"params\":{\"space\":\"votes\"}"),
              "\"members\":[{\"who\":\"bob\",\"role\":\"member\"},{\"who\":\"carol\",\"role\":"
              "\"member\"},{\"who\":\"dan\",\"role\":\"guest\"}],\"governance\":\"horizontal\","
              "\"quorum_pct\":60,\"self\":\"me\",\"proposals\":[],\"next_proposal\":1,"),
          "members applied: %s", OUT.p);
    /* now three vote: 1 of 3 is short of 60% */
    CHECK(has(REQ("10", "spaces.propose",
                  ",\"params\":{\"space\":\"votes\",\"members\":[{\"who\":\"bob\",\"role\":"
                  "\"member\"},{\"who\":\"carol\",\"role\":\"member\"},{\"who\":\"eve\","
                  "\"role\":\"member\"}]}"),
              "{\"proposal\":2,\"state\":\"neither\""),
          "gathering: %s", OUT.p);
    CHECK(has(REQ("10", "spaces.get", ",\"params\":{\"space\":\"votes\"}"),
              "\"proposals\":[{\"id\":2,\"by\":\"me\",\"members\":[{\"who\":\"bob\",\"role\":"
              "\"member\"},{\"who\":\"carol\",\"role\":\"member\"},{\"who\":\"eve\",\"role\":"
              "\"member\"}],\"votes\":[{\"who\":\"me\",\"yes\":true}],\"yes\":1,\"no\":0,"
              "\"voters\":3,\"state\":\"neither\"}],\"next_proposal\":2,"),
          "open proposal shown: %s", OUT.p);
    {
        static zxs_space_t v;
        zxs_decision_t d;
        CHECK(zxs_load("votes", &v) == 0, "load votes");
        CHECK(zxs_vote_from(&v, 2, "dan", true, &d) == ZXS_E_ROLE, "a guest does not vote");
        CHECK(zxs_vote_from(&v, 2, "zed", true, &d) == ZXS_E_ROLE, "nor a stranger");
        CHECK(zxs_vote_from(&v, 9, "bob", true, &d) == ZXS_E_NOPROP, "no such proposal");
        CHECK(zxs_vote_from(&v, 2, "bob", false, &d) == 0 && d == ZXS_D_NEITHER, "1 yes 1 no");
        CHECK(zxs_vote_from(&v, 2, "carol", true, &d) == 0 && d == ZXS_D_BOTH,
              "quorum with an objection is a dispute");
        CHECK(v.np == 1 && v.n == 3 && !strcmp(v.m[2].who, "dan"), "a dispute is never applied");
        CHECK(zxs_save(&v, false) == 0, "save the dispute");
    }
    CHECK(has(REQ("10", "spaces.get", ",\"params\":{\"space\":\"votes\"}"),
              "\"yes\":2,\"no\":1,\"voters\":3,\"state\":\"both\"}]"),
          "the UI sees BOTH: %s", OUT.p);
    {
        static zxs_space_t v;
        zxs_decision_t d;
        CHECK(zxs_load("votes", &v) == 0 && zxs_vote_from(&v, 2, "bob", true, &d) == 0 &&
                  d == ZXS_D_TRUE && v.np == 0 && v.n == 3 && !strcmp(v.m[2].who, "eve"),
              "bob changes his vote: passed and applied");
        CHECK(zxs_save(&v, false) == 0, "save");
    }
    /* one that can no longer pass is dropped */
    CHECK(has(REQ("10", "spaces.propose", ",\"params\":{\"space\":\"votes\",\"members\":[]}"),
              "{\"proposal\":3,\"state\":\"neither\""),
          "proposal 3: %s", OUT.p);
    {
        static zxs_space_t v;
        zxs_decision_t d;
        CHECK(zxs_load("votes", &v) == 0 && zxs_vote_from(&v, 3, "bob", false, &d) == 0 &&
                  d == ZXS_D_NEITHER && zxs_vote_from(&v, 3, "eve", false, &d) == 0 &&
                  d == ZXS_D_FALSE && v.np == 0 && v.n == 3,
              "two noes of four: failed and dropped");
        CHECK(zxs_save(&v, false) == 0, "save");
    }
    EXPECT(REQ("10", "spaces.vote",
               ",\"params\":{\"space\":\"votes\",\"proposal\":3,"
               "\"vote\":\"yes\"}"),
           SERR("10", "no open proposal with that id"));
    EXPECT(REQ("10", "spaces.vote",
               ",\"params\":{\"space\":\"votes\",\"proposal\":3,"
               "\"vote\":\"maybe\"}"),
           ERR("10", "-32602", "\\\"vote\\\" must be yes or no"));
    EXPECT(REQ("10", "spaces.vote", ",\"params\":{\"space\":\"votes\",\"vote\":\"yes\"}"),
           ERR("10", "-32602", "\\\"proposal\\\" must be a proposal id"));
    EXPECT(REQ("10", "spaces.vote",
               ",\"params\":{\"space\":\"votes\",\"proposal\":4294967296,"
               "\"vote\":\"yes\"}"),
           ERR("10", "-32602", "\\\"proposal\\\" must be a proposal id"));
    EXPECT(REQ("10", "spaces.vote", ",\"params\":{\"proposal\":1,\"vote\":\"yes\"}"),
           ERR("10", "-32602", "missing string parameter \\\"space\\\""));
    EXPECT(REQ("10", "spaces.propose",
               ",\"params\":{\"space\":\"votes\",\"members\":"
               "[{\"who\":\"me\",\"role\":\"member\"}]}"),
           ERR("10", "-32602", "member 0 needs a printable id and the role member or guest"));
    EXPECT(REQ("10", "spaces.propose", ",\"params\":{\"members\":[]}"),
           ERR("10", "-32602", "missing string parameter \\\"space\\\""));
    /* at most four open; this node changes its own vote */
    for (int i = 4; i <= 7; i++) {
        char want[64];
        snprintf(want, sizeof want, "{\"proposal\":%d,\"state\":\"neither\"", i);
        CHECK(has(REQ("10", "spaces.propose", ",\"params\":{\"space\":\"votes\",\"members\":[]}"),
                  want),
              "open %d: %s", i, OUT.p);
    }
    EXPECT(REQ("10", "spaces.propose", ",\"params\":{\"space\":\"votes\",\"members\":[]}"),
           SERR("10", "too many open proposals or votes"));
    CHECK(has(REQ("10", "spaces.vote",
                  ",\"params\":{\"space\":\"votes\",\"proposal\":4,\"vote\":\"no\"}"),
              "{\"proposal\":4,\"state\":\"neither\""),
          "own vote changed: %s", OUT.p);
    CHECK(has(REQ("10", "spaces.get", ",\"params\":{\"space\":\"votes\"}"),
              "\"votes\":[{\"who\":\"me\",\"yes\":false}],\"yes\":0,\"no\":1"),
          "replaced, not added: %s", OUT.p);

    /* a member of a vertical space proposes; the owner decides */
    EXPECT(REQ("11", "spaces.join",
               ",\"params\":{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\","
               "\"owner\":\"bob\",\"role\":\"member\",\"self\":\"bob\"}"),
           ERR("11", "-32602", "\\\"self\\\" must be a printable id other than the owner's"));
    EXPECT(REQ("11", "spaces.join",
               ",\"params\":{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\","
               "\"owner\":\"bob\",\"role\":\"member\",\"self\":\"me\"}"),
           ERR("11", "-32602", "\\\"self\\\" must be a printable id other than the owner's"));
    EXPECT(REQ("11", "spaces.join",
               ",\"params\":{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\","
               "\"owner\":\"bob\",\"role\":\"member\",\"self\":\"dan\"}"),
           OK("11",
              SPS("shop", "office", "Shop", "bob", "member", "true", "true", "false", "", "dan")));
    CHECK(has(REQ("11", "spaces.propose",
                  ",\"params\":{\"space\":\"shop\",\"members\":[{\"who\":\"dan\",\"role\":"
                  "\"member\"},{\"who\":\"eve\",\"role\":\"guest\"}]}"),
              "{\"proposal\":1,\"state\":\"neither\""),
          "waits for the owner: %s", OUT.p);
    CHECK(has(REQ("11", "spaces.vote",
                  ",\"params\":{\"space\":\"shop\",\"proposal\":1,\"vote\":\"yes\"}"),
              "{\"proposal\":1,\"state\":\"neither\""),
          "a member's yes is not enough: %s", OUT.p);
    {
        static zxs_space_t v;
        zxs_decision_t d;
        CHECK(zxs_load("shop", &v) == 0 && zxs_vote_from(&v, 1, "bob", true, &d) == 0 &&
                  d == ZXS_D_TRUE && v.n == 2 && !strcmp(v.m[1].who, "eve"),
              "the owner's yes applies it");
    }
    EXPECT(REQ("11", "spaces.propose", ",\"params\":{\"space\":\"lab\",\"members\":[]}"),
           SERR("11", "this node has no member id in this space"));
    EXPECT(REQ("11", "spaces.vote",
               ",\"params\":{\"space\":\"lab\",\"proposal\":1,"
               "\"vote\":\"yes\"}"),
           SERR("11", "this node has no member id in this space"));
    EXPECT(REQ("11", "spaces.propose", ",\"params\":{\"space\":\"crew\",\"members\":[]}"),
           SERR("11", "your role in this space may not do that"));
    EXPECT(REQ("11", "spaces.propose", ",\"params\":{\"space\":\"home\",\"members\":[]}"),
           SERR("11", "your role in this space may not do that"));
    EXPECT(REQ("11", "spaces.propose",
               ",\"params\":{\"space\":\"shop\",\"members\":"
               "[{\"who\":\"bob\",\"role\":\"member\"}]}"),
           ERR("11", "-32602", "member 0 is the owner"));

    /* a damaged tally on disk is refused, not counted */
    {
        char mf2[600];
        snprintf(mf2, sizeof mf2, "%s/.spaces/shop.space", root);
        FILE *g = fopen(mf2, "wb");
        CHECK(g &&
                  fputs("{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\",\"owner\":\"bob\","
                        "\"role\":\"member\",\"members\":[],\"self\":\"dan\",\"next_proposal\":1,"
                        "\"proposals\":[{\"id\":1,\"by\":\"dan\",\"members\":[],\"votes\":["
                        "{\"who\":\"bob\",\"yes\":true},{\"who\":\"bob\",\"yes\":true}]}]}",
                        g) >= 0 &&
                  fclose(g) == 0,
              "a doubled vote");
        EXPECT(REQ("11", "spaces.get", ",\"params\":{\"space\":\"shop\"}"),
               FERR("11", "the file system refused"));
        g = fopen(mf2, "wb");
        CHECK(g &&
                  fputs("{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\",\"owner\":\"bob\","
                        "\"role\":\"member\",\"members\":[],\"self\":\"dan\",\"next_proposal\":1,"
                        "\"proposals\":[{\"id\":2,\"by\":\"dan\",\"members\":[],\"votes\":[]}]}",
                        g) >= 0 &&
                  fclose(g) == 0,
              "a proposal id from the future");
        EXPECT(REQ("11", "spaces.get", ",\"params\":{\"space\":\"shop\"}"),
               FERR("11", "the file system refused"));
        g = fopen(mf2, "wb");
        CHECK(g &&
                  fputs("{\"id\":\"shop\",\"kind\":\"office\",\"name\":\"Shop\",\"owner\":\"bob\","
                        "\"role\":\"member\",\"members\":[],\"self\":\"dan\"}",
                        g) >= 0 &&
                  fclose(g) == 0,
              "a manifest from before governance");
        CHECK(has(REQ("11", "spaces.get", ",\"params\":{\"space\":\"shop\"}"),
                  "\"governance\":\"vertical\",\"quorum_pct\":51,\"self\":\"dan\",\"proposals\":[],"
                  "\"next_proposal\":0,\"cid\":\"\"}"),
              "defaults: %s", OUT.p);
    }

    /* a published cloud top or park keeps its pspace CID */
#define CID "baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    CHECK(strlen(CID) == ZXS_CID_LEN && zxs_cid_ok(CID) && zxs_cid_ok("") && !zxs_cid_ok("Qm") &&
              !zxs_cid_ok(CID "a") && !zxs_cid_ok("b"),
          "CID shape");
    CHECK(
        has(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"t-front\",\"cid\":\"" CID "\"}"),
            "\"next_proposal\":0,\"cid\":\"" CID "\"}}"),
        "published: %s", OUT.p);
    CHECK(has(REQ("12", "spaces.get", ",\"params\":{\"space\":\"t-front\"}"),
              ",\"cid\":\"" CID "\"}"),
          "kept: %s", OUT.p);
    EXPECT(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"studio-a\",\"cid\":\"" CID "\"}"),
           SERR("12", "only the owner of a cloud top or park publishes it"));
    EXPECT(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"bobs-front\",\"cid\":\"\"}"),
           SERR("12", "only the owner of a cloud top or park publishes it"));
    EXPECT(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"t-front\",\"cid\":\"Qm1\"}"),
           ERR("12", "-32602", "\\\"cid\\\" must be a pspace CID or \\\"\\\""));
    EXPECT(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"t-front\",\"cid\":\"" CID "a\"}"),
           ERR("12", "-32602", "\\\"cid\\\" must be a pspace CID or \\\"\\\""));
    EXPECT(REQ("12", "spaces.set_cid",
               ",\"params\":{\"space\":\"t-front\",\"cid\":"
               "\"baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1\"}"),
           ERR("12", "-32602", "\\\"cid\\\" must be a pspace CID or \\\"\\\""));
    EXPECT(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"t-front\"}"),
           ERR("12", "-32602", "missing string parameter \\\"cid\\\""));
    CHECK(has(REQ("12", "spaces.join",
                  ",\"params\":{\"id\":\"anns-front\",\"kind\":\"cloudtop\",\"name\":\"Ann\","
                  "\"owner\":\"ann\",\"role\":\"guest\",\"cid\":\"" CID "\"}"),
              "\"id\":\"anns-front\"") &&
              has(REQ("12", "spaces.get", ",\"params\":{\"space\":\"anns-front\"}"),
                  ",\"cid\":\"" CID "\"}"),
          "joined with its CID: %s", OUT.p);
    EXPECT(REQ("12", "spaces.join",
               ",\"params\":{\"id\":\"x-office\",\"kind\":\"office\",\"name\":\"X\","
               "\"owner\":\"ann\",\"role\":\"guest\",\"cid\":\"" CID "\"}"),
           ERR("12", "-32602", "\\\"cid\\\" must be a pspace CID, for a cloud top or park"));
    CHECK(has(REQ("12", "spaces.set_cid", ",\"params\":{\"space\":\"t-front\",\"cid\":\"\"}"),
              "\"cid\":\"\"}"),
          "unpublished: %s", OUT.p);
#undef CID

    /* crossings (S3) */
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"diary.txt\",\"text\":\"secret\"}"),
           OK("6", "{\"path\":\"diary.txt\",\"size\":6,\"done\":true}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"diary.txt\",\"to_space\":"
               "\"studio-a\",\"to\":\"diary.txt\"}"),
           OK("6", "{\"held\":true,\"ticket\":1,\"reason\":\"the destination is a more open kind "
                   "of space\"}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"space\":\"studio-a\",\"path\":\"diary.txt\"}"),
           FERR("6", "no such file or folder"));
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":1}"),
           OK("6", "{\"held\":false,\"path\":\"diary.txt\",\"size\":6}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"space\":\"studio-a\",\"path\":\"diary.txt\"}"),
           OK("6", "{\"path\":\"diary.txt\",\"size\":6,\"offset\":0,\"next\":6,\"eof\":true,"
                   "\"text\":\"secret\"}"));
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":1}"),
           ERR("6", "-32602", "no held crossing with that ticket"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a\",\"path\":\"plan.txt\",\"to_space\":"
               "\"home\",\"to\":\"plan.txt\"}"),
           OK("6", "{\"held\":false,\"path\":\"plan.txt\",\"size\":2}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a\",\"path\":\"plan.txt\",\"to_space\":"
               "\"studio-a-2\",\"to\":\"plan.txt\"}"),
           OK("6", "{\"held\":false,\"path\":\"plan.txt\",\"size\":2}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a-2\",\"path\":\"plan.txt\",\"to_space\":"
               "\"studio-a\",\"to\":\"plan2.txt\"}"),
           OK("6", "{\"held\":true,\"ticket\":2,\"reason\":\"the destination shows it to people "
                   "who cannot see it now\"}")); /* carol was not in studio-a-2 */
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":2}"),
           OK("6", "{\"held\":false,\"path\":\"plan2.txt\",\"size\":2}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a\",\"path\":\"plan.txt\",\"to_space\":"
               "\"lab\",\"to\":\"plan.txt\"}"),
           OK("6", "{\"held\":false,\"path\":\"plan.txt\",\"size\":2}")); /* bob saw it */
    CHECK(has(REQ("6", "files.cross",
                  ",\"params\":{\"from_space\":\"studio-a\",\"path\":\"plan.txt\",\"to_space\":"
                  "\"team\",\"to\":\"plan.txt\"}"),
              "{\"held\":true,\"ticket\":3,\"reason\":\"the destination is a more open kind "
              "of space\"}"),
          "office to team is more open, though no one new sees it: %s", OUT.p);
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"plan.txt\",\"to_space\":"
               "\"lab\",\"to\":\"plan3.txt\"}"),
           OK("6", "{\"held\":true,\"ticket\":4,\"reason\":\"the destination is a more open kind "
                   "of space\"}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"plan.txt\",\"to_space\":"
               "\"t-front\",\"to\":\"plan.txt\"}"),
           OK("6", "{\"held\":true,\"ticket\":5,\"reason\":\"the destination is public\"}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"commons\",\"path\":\"hello.txt\",\"to_space\":"
               "\"t-front\",\"to\":\"hello.txt\"}"),
           OK("6", "{\"held\":false,\"path\":\"hello.txt\",\"size\":2}"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"plan.txt\",\"to_space\":"
               "\"crew\",\"to\":\"plan.txt\"}"),
           SERR("6", "your role in this space (guest) may not change it"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"plan.txt\",\"to_space\":"
               "\"home\",\"to\":\"p.txt\"}"),
           ERR("6", "-32602", "within one space, use files.rename"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a-2\",\"path\":\"plan.txt\",\"to_space\":"
               "\"home\",\"to\":\"plan.txt\"}"),
           FERR("6", "something with that name already exists"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"lab\",\"path\":\"bench\",\"to_space\":"
               "\"home\",\"to\":\"bench\"}"),
           FERR("6", "is a folder"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"lab\",\"path\":\"../x\",\"to_space\":"
               "\"home\",\"to\":\"x\"}"),
           FERR("6", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("6", "files.cross",
               ",\"params\":{\"from_space\":\"lab\",\"path\":\"nope\",\"to_space\":"
               "\"home\",\"to\":\"x\"}"),
           FERR("6", "no such file or folder"));
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"from_space\":\"lab\",\"to_space\":\"home\"}"),
           ERR("6", "-32602", "missing string parameter \\\"path\\\""));
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":-1}"),
           ERR("6", "-32602", "\\\"confirm\\\" must not be negative"));
    /* a held crossing whose destination left in the meantime */
    char mf[300];
    snprintf(mf, sizeof mf, "%s/.spaces/lab.space", root);
    CHECK(remove(mf) == 0, "lab leaves");
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":4}"),
           SERR("6", "the spaces changed since the crossing was held"));
    EXPECT(REQ("6", "files.cross", ",\"params\":{\"confirm\":5}"),
           OK("6", "{\"held\":false,\"path\":\"plan.txt\",\"size\":2}"));
    /* only the latest 8 held crossings are kept */
    for (int i = 0; i < 9; i++)
        rpc(REQ("7", "files.cross",
                ",\"params\":{\"from_space\":\"home\",\"path\":\"plan.txt\",\"to_space\":"
                "\"park\",\"to\":\"p.txt\"}"));
    EXPECT(REQ("7", "files.cross", ",\"params\":{\"confirm\":6}"),
           ERR("7", "-32602", "no held crossing with that ticket"));
    EXPECT(REQ("7", "files.cross", ",\"params\":{\"confirm\":7}"),
           OK("7", "{\"held\":false,\"path\":\"p.txt\",\"size\":2}"));
    EXPECT(REQ("7", "files.cross", ",\"params\":{\"confirm\":14}"),
           FERR("7", "something with that name already exists"));

    /* a long file crosses whole */
    static char big[ZXF_READ_MAX * 2 + 100];
    memset(big, 'z', sizeof big);
    uint64_t sz;
    CHECK(zxf_use_space("home") == 0 && zxf_write("big.txt", (const uint8_t *) big, sizeof big,
                                                  ZXF_W_REPLACE, false, &sz) == 0,
          "big file");
    char want[200];
    snprintf(want, sizeof want, OK("8", "{\"held\":false,\"path\":\"big.txt\",\"size\":%u}"),
             (unsigned) sizeof big);
    EXPECT(REQ("8", "files.cross",
               ",\"params\":{\"from_space\":\"studio-a\",\"path\":\"diary.txt\",\"to_space\":"
               "\"home\",\"to\":\"d2.txt\"}"),
           OK("8", "{\"held\":false,\"path\":\"d2.txt\",\"size\":6}"));
    EXPECT(REQ("8", "files.cross",
               ",\"params\":{\"from_space\":\"home\",\"path\":\"big.txt\","
               "\"to_space\":\"park\",\"to\":\"big.txt\"}"),
           OK("8", "{\"held\":true,\"ticket\":15,\"reason\":\"the destination is public\"}"));
    EXPECT(REQ("8", "files.cross", ",\"params\":{\"confirm\":15}"), want);

    /* damaged manifests are refused and left out of the list */
    snprintf(mf, sizeof mf, "%s/.spaces/park.space", root);
    FILE *f = fopen(mf, "wb");
    CHECK(f && fputs("{\"id\":\"park\",\"kind\":\"home\"}", f) >= 0 && fclose(f) == 0, "damage");
    EXPECT(REQ("9", "spaces.get", ",\"params\":{\"space\":\"park\"}"),
           FERR("9", "the file system refused"));
    snprintf(mf, sizeof mf, "%s/.spaces/team.space", root);
    f = fopen(mf, "wb");
    CHECK(f &&
              fputs("{\"id\":\"other\",\"kind\":\"team\",\"name\":\"T\",\"owner\":\"me\","
                    "\"role\":\"owner\",\"members\":[]}",
                    f) >= 0 &&
              fclose(f) == 0,
          "a manifest under another id");
    EXPECT(REQ("9", "files.list", ",\"params\":{\"space\":\"team\"}"),
           FERR("9", "the file system refused"));
    snprintf(mf, sizeof mf, "%s/.spaces/crew.space", root);
    f = fopen(mf, "wb");
    CHECK(f &&
              fputs("{\"id\":\"crew\",\"kind\":\"team\",\"name\":\"Crew\",\"owner\":\"alice\","
                    "\"role\":\"owner\",\"members\":[]}",
                    f) >= 0 &&
              fclose(f) == 0,
          "a joined space cannot claim to be owned");
    EXPECT(REQ("9", "spaces.get", ",\"params\":{\"space\":\"crew\"}"),
           FERR("9", "the file system refused"));
    const char *l = rpc(REQ("9", "spaces.list", ""));
    CHECK(strstr(l, "\"id\":\"studio-a\"") && strstr(l, "\"id\":\"commons\"") &&
              !strstr(l, "\"id\":\"park\"") && !strstr(l, "\"id\":\"crew\"") &&
              !strstr(l, "\"id\":\"lab\"") &&
              strstr(l, "{\"spaces\":[" HOME_SP ",{\"id\":\"anns-front\""),
          "list: %s", l);

    CHECK(zxf_use_space("home") == 0, "home again");
    CHECK(rm_tree(root) == 0, "cleanup");
}

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
    /* the smallest rise and fall: one minor unit each way */
    CHECK(zxvd_rpc_earnings_sync(8) == 0, "earnings: up by one");
    EARN("8", "6", "7");
    BAL(own, "1009", "1000", "9");
    CHECK(zxvd_rpc_earnings_sync(7) == 0, "earnings: down by one");
    EARN("7", "7", "8");
    BAL(own, "1009", "1001", "8");

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
        ("{\"op\":\"post\",\"from\":4,\"to\":5,\"amount\":\"7\",\"ccy\":\"EUR\",\"ref\":\"g\","
         "\"id\":\"6\"}"),
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

    /* F7: one zxvd per books file */
    {
        zxvd_rpc_books_close();
        int lk = open(BOOKS, O_RDONLY);
        CHECK(lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) == 0, "another process holds the file");
        CHECK(zxvd_rpc_books_open() == -1, "F7: a books file in use refuses the start");
        close(lk);
        CHECK(zxvd_rpc_books_open() == 0, "F7: free again, it opens");
        lk = open(BOOKS, O_RDONLY);
        CHECK(lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) != 0, "F7: and zxvd holds the lock");
        close(lk);
    }

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

/* ---------------- views (zxvd_rpc.h V1-V3) ---------------- */

static void v_peers(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"peers\":[],\"count\":0}");
}
static void v_offers(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"wants\":[]}");
}
static void v_card(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"tier\":0}");
}

static void test_views(void)
{
    static const zxvd_view_ops_t STUB = {v_peers, v_offers, v_card}, HALF = {v_peers, 0, 0};
    zxvd_rpc_set_view_ops(NULL);
    EXPECT(REQ("1", "net.peers", ""), ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "net.offers", ""), ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "player.card", ""), ERR("1", "-32601", "not available"));
    zxvd_rpc_set_view_ops(&HALF);
    EXPECT(REQ("2", "net.peers", ""), OK("2", "{\"peers\":[],\"count\":0}"));
    EXPECT(REQ("2", "net.offers", ""), ERR("2", "-32601", "not available"));
    EXPECT(REQ("2", "player.card", ""), ERR("2", "-32601", "not available"));
    zxvd_rpc_set_view_ops(&STUB);
    EXPECT(REQ("3", "net.offers", ""), OK("3", "{\"wants\":[]}"));
    EXPECT(REQ("3", "player.card", ""), OK("3", "{\"tier\":0}"));
    zxvd_rpc_set_view_ops(NULL);
}

/* ---------------- the jurisdiction bridge (zxvd_rpc.h J1-J5) ---------------- */

#define JU_LIST                                                                                    \
    "\"standards\":[\"IFRS\",\"US_GAAP\",\"IPSAS\",\"AAOIFI\",\"OHADA\",\"CAS\",\"SAP_INSURER\"]," \
    "\"in_effect\":true,\"plugins\":0,\"report_standard\":\"\"}"

/* J5: the result as kernel/src/juris reads it */
static bool ju_parse(const char *req, juris_settings_t *s)
{
    const char *r = strstr(rpc(req), "\"result\":");
    if (!r) return false;
    r += 9;
    size_t n = strlen(r);
    return n > 1 && juris_settings_parse(r, n - 1, s) == JURIS_OK; /* drop the closing } */
}

static void test_jurisdiction(void)
{
    char tmpl[] = "/tmp/zxvdjuris.XXXXXX", path[200];
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    EXPECT(REQ("1", "jurisdiction.get", ""), ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "jurisdiction.set", ",\"params\":{\"market\":\"open\"}"),
           ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "books open on a file");
    EXPECT(REQ("2", "jurisdiction.get", ""),
           OK("2", "{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\"," JU_LIST));
    CHECK(has(REQ("2", "books.file", ""), "\"records\":\"2\""), "nothing saved for the defaults");
    EXPECT(REQ("3", "jurisdiction.set", ",\"params\":{}"),
           OK("3", "{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\"," JU_LIST));
    CHECK(has(REQ("3", "books.file", ""), "\"records\":\"2\""),
          "J4: an unchanged set writes nothing");
    EXPECT(REQ("4", "jurisdiction.set", ",\"params\":{\"market\":\"open\",\"country\":\"NG\"}"),
           OK("4", "{\"market\":\"open\",\"standard\":\"\",\"country\":\"NG\"," JU_LIST));
    EXPECT(REQ("5", "jurisdiction.set", ",\"params\":{\"standard\":\"OHADA\"}"),
           OK("5", "{\"market\":\"open\",\"standard\":\"OHADA\",\"country\":\"NG\"," JU_LIST));
    CHECK(has(REQ("5", "books.file", ""), "\"records\":\"4\""), "J4: two changes, two records");
    long n;
    char *b = slurp(path, &n);
    CHECK(b && strstr(b, " {\"op\":\"juris\",\"market\":\"open\",\"standard\":\"OHADA\","
                         "\"country\":\"NG\"}\n"),
          "J4: the record is in the file");
    free(b);
    /* bad values change nothing */
    EXPECT(REQ("6", "jurisdiction.set", ",\"params\":{\"market\":\"Open\"}"),
           ERR("6", "-32602", "\\\"market\\\" must be \\\"closed\\\" or \\\"open\\\""));
    CHECK(has(REQ("6", "jurisdiction.set", ",\"params\":{\"standard\":\"GAAP\"}"), "-32602"),
          "J3: an unknown standard is refused");
    CHECK(has(REQ("6", "jurisdiction.set", ",\"params\":{\"standard\":\"ifrs\"}"), "-32602"),
          "J3: names are exact");
    const char *bad_cc[] = {"ng", "N", "NGA", "N1", "1G", "@G", "N["};
    for (unsigned i = 0; i < sizeof bad_cc / sizeof *bad_cc; i++) {
        char req[160];
        snprintf(req, sizeof req, REQ("7", "jurisdiction.set", ",\"params\":{\"country\":\"%s\"}"),
                 bad_cc[i]);
        CHECK(has(req, "two capital letters"), "J1: country \"%s\" refused", bad_cc[i]);
    }
    CHECK(has(REQ("7", "jurisdiction.set", ",\"params\":{\"country\":7}"), "-32602"),
          "J1: a number is not a country");
    CHECK(
        has(REQ("7", "jurisdiction.set", ",\"params\":{\"market\":\"closed\",\"standard\":\"x\"}"),
            "-32602"),
        "a set with one bad field changes nothing");
    EXPECT(REQ("8", "jurisdiction.get", ""),
           OK("8", "{\"market\":\"open\",\"standard\":\"OHADA\",\"country\":\"NG\"," JU_LIST));
    CHECK(has(REQ("8", "books.file", ""), "\"records\":\"4\""), "and wrote nothing");
    /* J4: kept across a restart */
    reopen();
    EXPECT(REQ("9", "jurisdiction.get", ""),
           OK("9", "{\"market\":\"open\",\"standard\":\"OHADA\",\"country\":\"NG\"," JU_LIST));
    EXPECT(REQ("9", "jurisdiction.set",
               ",\"params\":{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\"}"),
           OK("9", "{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\"," JU_LIST));
    reopen();
    CHECK(has(REQ("9", "jurisdiction.get", ""),
              "{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\""),
          "J4: the last record wins, an empty country included");
    EXPECT(
        REQ("9", "jurisdiction.set",
            ",\"params\":{\"standard\":\"SAP_INSURER\",\"country\":\"ZA\"}"),
        OK("9", "{\"market\":\"closed\",\"standard\":\"SAP_INSURER\",\"country\":\"ZA\"," JU_LIST));
    /* J5: the jurisdiction host follows the settings, across a restart */
    EXPECT(REQ("9", "jurisdiction.set", ",\"params\":{\"standard\":\"IFRS\",\"country\":\"KE\"}"),
           OK("9",
              "{\"market\":\"closed\",\"standard\":\"IFRS\",\"country\":\"KE\","
              "\"standards\":[\"IFRS\",\"US_GAAP\",\"IPSAS\",\"AAOIFI\",\"OHADA\",\"CAS\","
              "\"SAP_INSURER\"],\"in_effect\":true,\"plugins\":0,\"report_standard\":\"IFRS\"}"));
    reopen();
    CHECK(has(REQ("9", "jurisdiction.get", ""), "\"report_standard\":\"IFRS\"}"),
          "J5: the host has the replayed settings");
    juris_settings_t js;
    CHECK(ju_parse(REQ("9", "jurisdiction.get", ""), &js) && js.market == JURIS_MARKET_CLOSED &&
              js.standard == ACCT_STD_IFRS && !strcmp(js.country, "KE") && js.in_effect,
          "J5: kernel/src/juris reads the result");
    CHECK(
        has(REQ("9", "jurisdiction.set", ",\"params\":{\"standard\":\"AAOIFI\",\"country\":\"\"}"),
            "\"report_standard\":\"\"}"),
        "J5: no country, no report standard");
    CHECK(has(REQ("9", "jurisdiction.set", ",\"params\":{\"country\":\"MY\",\"market\":\"open\"}"),
              "\"report_standard\":\"AAOIFI\"}") &&
              ju_parse(REQ("9", "jurisdiction.get", ""), &js) && js.market == JURIS_MARKET_OPEN &&
              js.standard == ACCT_STD_AAOIFI,
          "J5: AAOIFI with a country");
    CHECK(has(REQ("9", "jurisdiction.set", ",\"params\":{\"standard\":\"IPSAS\"}"),
              "\"report_standard\":\"\"}"),
          "J5: a standard with no adapter gives none");
    EXPECT(
        REQ("9", "jurisdiction.set",
            ",\"params\":{\"market\":\"closed\",\"standard\":\"SAP_INSURER\",\"country\":\"ZA\"}"),
        OK("9", "{\"market\":\"closed\",\"standard\":\"SAP_INSURER\",\"country\":\"ZA\"," JU_LIST));
    /* a damaged record stops the start */
    const char *bad[] = {
        "{\"op\":\"juris\",\"market\":\"shut\",\"standard\":\"\",\"country\":\"\"}",
        "{\"op\":\"juris\",\"market\":\"open\",\"standard\":\"GAAP\",\"country\":\"\"}",
        "{\"op\":\"juris\",\"market\":\"open\",\"standard\":\"\",\"country\":\"ng\"}",
        "{\"op\":\"juris\",\"market\":\"open\",\"standard\":\"\"}",
        "{\"op\":\"juris\",\"standard\":\"\",\"country\":\"\"}",
        "{\"op\":\"juris\",\"market\":\"open\",\"country\":\"\"}",
    };
    zxvd_rpc_books_close();
    long keep;
    char *orig = slurp(path, &keep);
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        spit(path, orig, keep);
        append_chained(path, bad[i]);
        CHECK(zxvd_rpc_books_open() == -1, "bad jurisdiction record %u stops the start", i);
        zxvd_rpc_books_close();
    }
    spit(path, orig, keep);
    free(orig);
    CHECK(zxvd_rpc_books_open() == 0, "the good file opens again");
    CHECK(has(REQ("10", "jurisdiction.get", ""), "\"standard\":\"SAP_INSURER\",\"country\":\"ZA\""),
          "with its settings");
    /* in memory: back to the defaults, and settable */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "no file");
    CHECK(has(REQ("11", "jurisdiction.get", ""),
              "{\"market\":\"closed\",\"standard\":\"\",\"country\":\"\""),
          "a fresh start has the defaults");
    CHECK(has(REQ("11", "jurisdiction.set", ",\"params\":{\"market\":\"open\"}"),
              "\"market\":\"open\""),
          "set works without a file");
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0 &&
              has(REQ("11", "jurisdiction.get", ""), "{\"market\":\"closed\""),
          "and is gone after a restart");
    unlink(path);
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

/* ---------------- edges of the books file, earnings and mesh posting ---------------- */

static void test_edges(void)
{
    char tmpl[] = "/tmp/zxvdedge.XXXXXX", path[200], want[300];
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    EXPECT(REQ("1", "mesh.books", ""), ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "books.file", ""), ERR("1", "-32603", "the books are not open"));
    static char lp[1025];
    memset(lp, 'a', 1024);
    lp[1024] = 0;
    CHECK(zxvd_rpc_books_file(lp) == -1, "F1: a 1024-byte path does not fit");
    lp[1023] = 0;
    CHECK(zxvd_rpc_books_file(lp) == 0, "F1: a 1023-byte path does");
    CHECK(zxvd_rpc_books_file(NULL) == 0 && zxvd_rpc_books_open() == 0 &&
              has(REQ("1", "books.file", ""), "\"saved\":false,\"path\":null"),
          "F1: NULL means the books live in memory");
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "books on a file");
    snprintf(want, sizeof want, "\"saved\":true,\"path\":\"%s\"", path);
    CHECK(has(REQ("2", "books.file", ""), want), "books.file names the file: %s", OUT.p);

    /* E: a sync that changes nothing posts nothing */
    CHECK(zxvd_rpc_earnings_sync(0) == 0, "E2: sync 0");
    CHECK(has(REQ("3", "swarm.earnings", ""), "\"balance_minor\":\"0\",\"postings\":\"0\""),
          "E2: nothing held, nothing posted: %s", OUT.p);
    CHECK(zxvd_rpc_earnings_sync(5) == 0 &&
              has(REQ("3", "swarm.earnings", ""), "\"balance_minor\":\"5\",\"postings\":\"1\""),
          "E2: 5 held, one posting: %s", OUT.p);

    /* a new currency is a record; an enabled one is not written again */
    EXPECT(REQ("4", "currency.enable", ",\"params\":{\"code\":\"GBP\"}"),
           OK("4", "{\"enabled\":\"GBP\"}"));
    long n;
    char *b = slurp(path, &n);
    CHECK(b && strstr(b, " {\"op\":\"ccy\",\"code\":\"GBP\"}\n"), "F3: the GBP record");
    free(b);

    /* S: boundaries of the amounts */
    uint8_t inv[32];
    for (int i = 0; i < 32; i++) inv[i] = (uint8_t) (0x10 + i);
    CHECK(zxvd_rpc_mesh_post(true, 30, 0, inv) == 0, "S2: a sale of 30");
    inv[0] = 0x20;
    CHECK(zxvd_rpc_mesh_post(false, 30, 0, inv) == 0, "S4: a purchase of exactly the wallet");
    CHECK(has(REQ("5", "mesh.books", ""), "\"wallet_minor\":\"0\",\"fees_minor\":\"0\""),
          "S4: wallet empty: %s", OUT.p);
    CHECK(!has(REQ("5", "ledger.entries", ",\"params\":{\"account\":3}"), "mesh float"),
          "S4: no float when the wallet holds exactly enough: %s", OUT.p);
    /* the largest amounts, on books of their own (a rail's total is capped too) */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "books in memory");
    inv[0] = 0x30;
    CHECK(zxvd_rpc_mesh_post(true, (uint64_t) ZXV_AMOUNT_MAX, 0, inv) == 0,
          "S: a sale of exactly ZXV_AMOUNT_MAX");
    inv[0] = 0x40;
    CHECK(zxvd_rpc_mesh_post(false, 10, (uint64_t) ZXV_AMOUNT_MAX, inv) == -1,
          "S: a fee that overflows with the price is refused");
    snprintf(want, sizeof want, "\"wallet_minor\":\"%lld\"", (long long) ZXV_AMOUNT_MAX);
    CHECK(has(REQ("5", "mesh.books", ""), want), "and posts nothing: %s", OUT.p);
    inv[0] = 0x50;
    CHECK(zxvd_rpc_mesh_post(false, 1, (uint64_t) ZXV_AMOUNT_MAX - 1u, inv) == 0,
          "S: price + fee of exactly ZXV_AMOUNT_MAX is accepted");
    CHECK(has(REQ("5", "mesh.books", ""), "\"wallet_minor\":\"0\""), "wallet empty again: %s",
          OUT.p);

    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "back to the file");

    /* F5 with the mesh accounts open */
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = (rlim_t) file_size(path);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit the file size");
    snprintf(want, sizeof want, "%s", "\"name\":\"ZXV_E_IO\"");
    CHECK(has(REQ("6", "jurisdiction.set", ",\"params\":{\"market\":\"open\"}"), want) &&
              strstr(OUT.p, "this change is not saved"),
          "F5: a jurisdiction change that cannot be saved: %s", OUT.p);
    CHECK(has(REQ("6", "jurisdiction.get", ""), "\"market\":\"closed\""),
          "J4: and it did not apply");
    CHECK(has(REQ("6", "currency.enable", ",\"params\":{\"code\":\"GBP\"}"), want),
          "F5: even an enabled currency is refused while read-only");
    CHECK(
        has(REQ("6", "ledger.open_account", ",\"params\":{\"name\":\"late\",\"currency\":\"GBP\"}"),
            want) &&
            !has(REQ("6", "ledger.accounts", ""), "\"late\""),
        "F5: an account is refused and not opened");
    inv[0] = 0x60;
    CHECK(zxvd_rpc_mesh_post(true, 5, 0, inv) == -1, "F5: a sale cannot post");
    CHECK(zxvd_rpc_mesh_post(false, 5, 0, inv) == -1, "F5: a purchase needing a float cannot");
    CHECK(has(REQ("6", "mesh.books", ""), "\"sold\":\"0\",\"bought\":\"0\""),
          "F5: nothing counted: %s", OUT.p);
    setrlimit(RLIMIT_FSIZE, &old);
    reopen();
    inv[0] = 0x70;
    CHECK(zxvd_rpc_mesh_post(true, 50, 0, inv) == 0, "a sale after the restart");
    lim.rlim_cur = (rlim_t) file_size(path);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit the file size again");
    CHECK(has(REQ("7", "books.file", ""), "\"writable\":true}"), "still writable");
    inv[0] = 0x71;
    CHECK(zxvd_rpc_mesh_post(false, 5, 0, inv) == -1, "F5: a covered purchase cannot post");
    setrlimit(RLIMIT_FSIZE, &old);
    /* F5 before the mesh accounts exist: they cannot open */
    zxvd_rpc_books_close();
    unlink(path);
    CHECK(zxvd_rpc_books_open() == 0, "a new books file");
    lim.rlim_cur = (rlim_t) file_size(path);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit it at once");
    inv[0] = 0x80;
    CHECK(zxvd_rpc_mesh_post(true, 5, 0, inv) == -1, "F5: the mesh accounts cannot open");
    CHECK(has(REQ("8", "mesh.books", ""), "\"wallet_account\":null"), "and none did: %s", OUT.p);
    setrlimit(RLIMIT_FSIZE, &old);
    /* S1: mesh accounts that are the file's first ones are found again at the restart */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "the new file reopens");
    inv[0] = 0x90;
    CHECK(zxvd_rpc_mesh_post(true, 5, 0, inv) == 0, "a sale opens the mesh accounts first");
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "and reopens");
    CHECK(has(REQ("8", "mesh.books", ""),
              "\"wallet_account\":1,\"counterparty_account\":0,\"fee_account\":2,"
              "\"wallet_minor\":\"5\""),
          "S1: account 0 is found again: %s", OUT.p);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "back to memory");
    unlink(path);
    rmdir(tmpl);
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

/* ---------------- extension methods (zxvd_rpc.h X1-X4) ---------------- */

static int ext_calls;
static void x_echo(zxvd_call_t *c)
{
    char s[8] = "unset";
    bool flag = false;
    ext_calls++;
    if (!zxvd_param_str(c, "s", s, sizeof s, false) || !zxvd_param_bool(c, "flag", &flag, false))
        return;
    zxvd_buf_printf(zxvd_result(c), "{\"s\":\"%s\",\"flag\":%s}", s, flag ? "true" : "false");
}
static void x_need(zxvd_call_t *c)
{
    bool flag = true;
    if (!zxvd_param_bool(c, "flag", &flag, true)) return;
    zxvd_buf_printf(zxvd_result(c), "%s", flag ? "1" : "0");
}
static void x_fail(zxvd_call_t *c)
{
    zxvd_fail(c, -32042, "first %d", 1);
    zxvd_fail(c, -32043, "second");
    zxvd_buf_raw(zxvd_result(c), "1", 1); /* dropped: the error stands */
}
static void x_quiet(zxvd_call_t *c)
{
    (void) c; /* X3: neither a result nor an error */
}
static void x_shadow(zxvd_call_t *c)
{
    ext_calls += 100;
    zxvd_buf_raw(zxvd_result(c), "\"ext\"", 5);
}

static void x_num(zxvd_call_t *c)
{
    uint32_t u = 7;
    int64_t a = -1;
    bool need = false;
    if (!zxvd_param_bool(c, "need", &need, false) || !zxvd_param_u32(c, "u", &u, need) ||
        !zxvd_param_amount(c, "a", &a, need))
        return;
    zxvd_buf_printf(zxvd_result(c), "[%u,%lld]", (unsigned) u, (long long) a);
}
/* X2: walk an array of integers with the web4 tokenizer */
static void x_json(zxvd_call_t *c)
{
    zxvd_json_t v;
    if (!zxvd_param_json(c, "list", &v)) {
        zxvd_buf_printf(zxvd_result(c), "%s",
                        zxvd_param_json(c, NULL, &v) ? "\"none\"" : "\"no params\"");
        return;
    }
    const w4_jtok_t *t = v.tok;
    if (t[v.at].type != W4_J_ARR) return zxvd_fail(c, ZXVD_E_PARAMS, "\"list\" must be an array");
    int64_t sum = 0;
    int32_t i = v.at + 1;
    for (uint32_t k = 0; k < t[v.at].size; k++, i++) {
        int64_t x;
        if (t[i].type != W4_J_NUM || w4_json_i64(v.js, &t[i], &x) != W4_OK)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"list\" must hold integers");
        sum += x;
    }
    zxvd_buf_printf(zxvd_result(c), "%lld", (long long) sum);
}
static void x_zok(zxvd_call_t *c)
{
    bool bad = false;
    if (!zxvd_param_bool(c, "bad", &bad, false)) return;
    if (zxvd_libzxv_ok(c, bad ? ZXV_E_FUNDS : ZXV_OK)) zxvd_buf_raw(zxvd_result(c), "true", 4);
}
/* X6-X7 */
static void x_books(zxvd_call_t *c)
{
    uint32_t a, b;
    uint64_t e;
    if (!zxvd_books_cap() || !zxvd_books_ctx()) return zxvd_fail(c, -32050, "closed");
    if (!zxvd_libzxv_ok(c, zxvd_books_account("x issuer", "NGN", ZXV_ACCT_ISSUER, &a)) ||
        !zxvd_libzxv_ok(c, zxvd_books_account("x holder", "NGN", ZXV_ACCT_HOLDER, &b)) ||
        !zxvd_libzxv_ok(c, zxvd_books_post(a, b, 250, "NGN", "x ref", &e)))
        return;
    zxvd_buf_printf(zxvd_result(c), "[%u,%u,%llu]", (unsigned) a, (unsigned) b,
                    (unsigned long long) e);
}
static void x_other(zxvd_call_t *c)
{
    zxvd_buf_raw(zxvd_result(c), "\"second\"", 8);
}

static void test_ext(void)
{
    static const zxvd_method_t TAB[] = {{"x.echo", x_echo},
                                        {"x.need", x_need},
                                        {"x.fail", x_fail},
                                        {"x.quiet", x_quiet},
                                        {"zxvd.version", x_shadow}};
    zxvd_rpc_set_ops(&OPS);
    EXPECT(REQ("1", "x.echo", ""), ERR("1", "-32601", "method not found"));
    zxvd_rpc_add_methods(TAB, 5);
    CHECK(has(REQ("1", "rpc.discover", ""), "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"x."
                                            "echo\",\"x.need\",\"x.fail\",\"x.quiet\","
                                            "\"zxvd.version\"]}"),
          "X1: listed after the built-ins: %s", OUT.p);
    EXPECT(REQ("2", "x.echo", ""), OK("2", "{\"s\":\"\",\"flag\":false}"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"s\":\"abc\",\"flag\":true}"),
           OK("2", "{\"s\":\"abc\",\"flag\":true}"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"flag\":false}"),
           OK("2", "{\"s\":\"\",\"flag\":false}"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"s\":\"1234567\"}"),
           OK("2", "{\"s\":\"1234567\",\"flag\":false}"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"s\":\"12345678\"}"),
           ERR("2", "-32602", "\\\"s\\\" must be a string of at most 7 bytes"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"flag\":1}"),
           ERR("2", "-32602", "\\\"flag\\\" must be true or false"));
    EXPECT(REQ("2", "x.echo", ",\"params\":{\"flag\":null}"),
           ERR("2", "-32602", "\\\"flag\\\" must be true or false"));
    EXPECT(REQ("3", "x.need", ""), ERR("3", "-32602", "missing boolean parameter \\\"flag\\\""));
    EXPECT(REQ("3", "x.need", ",\"params\":{\"flag\":false}"), OK("3", "0"));
    EXPECT(REQ("3", "x.need", ",\"params\":{\"flag\":true}"), OK("3", "1"));
    EXPECT(REQ("4", "x.fail", ""), ERR("4", "-32042", "first 1"));
    EXPECT(REQ("5", "x.quiet", ""), ERR("5", "-32603", "no result"));
    int before = ext_calls;
    CHECK(has(REQ("6", "zxvd.version", ""), "\"app\":\"9.8.7\"") && ext_calls == before,
          "X1: a built-in name wins: %s", OUT.p);
    CHECK(zxvd_rpc_add_methods(TAB, 1) == 0, "X1: re-adding a table held updates its count");
    CHECK(has(REQ("7", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"x.echo\"]}") &&
              has(REQ("7", "x.need", ""), "method not found"),
          "X1: the count is the new one: %s", OUT.p);
    CHECK(zxvd_rpc_add_methods(TAB, 0) == -1 && zxvd_rpc_add_methods(NULL, 3) == -1,
          "X1: 0 or NULL adds nothing");
    CHECK(has(REQ("8", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"x.echo\"]}"),
          "X1: a refused add leaves the tables as they were: %s", OUT.p);

    /* several tables: each add appends; the first added wins a shared name */
    static const zxvd_method_t T2[] = {{"x.num", x_num},
                                       {"x.json", x_json},
                                       {"x.echo", x_other},
                                       {"x.zok", x_zok},
                                       {"x.books", x_books}};
    static const zxvd_method_t T3[] = {{"y.one", x_other}};
    static const zxvd_method_t T4[] = {{"y.two", x_other}};
    static const zxvd_method_t T5[] = {{"y.three", x_other}};
    CHECK(zxvd_rpc_add_methods(T2, 5) == 0 && zxvd_rpc_add_methods(T3, 1) == 0 &&
              zxvd_rpc_add_methods(T4, 1) == 0,
          "X1: four tables");
    CHECK(zxvd_rpc_add_methods(T5, 1) == -1, "X1: a fifth is refused");
    CHECK(has(REQ("9", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"x.echo\",\"x.num\",\"x.json\","
              "\"x.echo\",\"x.zok\",\"x.books\","
              "\"y.one\",\"y.two\"]}"),
          "X1: every table's names, in the order added: %s", OUT.p);
    EXPECT(REQ("9", "x.echo", ""), OK("9", "{\"s\":\"\",\"flag\":false}"));
    EXPECT(REQ("9", "y.two", ""), OK("9", "\"second\""));
    EXPECT(REQ("9", "y.three", ""), ERR("9", "-32601", "method not found"));
    zxvd_rpc_remove_methods(TAB);
    EXPECT(REQ("9", "x.echo", ""), OK("9", "\"second\""));
    CHECK(has(REQ("9", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"x.num\",\"x.json\",\"x.echo\","
              "\"x.zok\",\"x.books\",\"y.one\","
              "\"y.two\"]}"),
          "X1: removing one keeps the others in order: %s", OUT.p);
    zxvd_rpc_remove_methods(TAB); /* not held: ignored */
    zxvd_rpc_remove_methods(T3);
    CHECK(zxvd_rpc_add_methods(T5, 1) == 0 && has(REQ("9", "y.three", ""), "\"second\"") &&
              has(REQ("9", "y.one", ""), "method not found"),
          "X1: a freed slot takes a new table");

    /* X2: integers, amounts and raw JSON */
    EXPECT(REQ("10", "x.num", ""), OK("10", "[7,-1]"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"u\":4294967295,\"a\":\"-9223372036854775807\"}"),
           OK("10", "[4294967295,-9223372036854775807]"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"u\":0,\"a\":12}"), OK("10", "[0,12]"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"need\":true}"),
           ERR("10", "-32602", "missing integer parameter \\\"u\\\""));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"need\":true,\"u\":1}"),
           ERR("10", "-32602", "missing integer parameter \\\"a\\\""));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"u\":4294967296}"),
           ERR("10", "-32602", "\\\"u\\\" is out of range"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"u\":-1}"),
           ERR("10", "-32602", "\\\"u\\\" is out of range"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"u\":\"1\"}"),
           ERR("10", "-32602", "\\\"u\\\" must be an integer (no fraction or exponent)"));
    EXPECT(REQ("10", "x.num", ",\"params\":{\"a\":1.5}"),
           ERR("10", "-32602",
               "\\\"a\\\" must be an integer or a decimal string (no fraction or exponent)"));
    EXPECT(REQ("10", "x.json", ",\"params\":{\"list\":[1,2,-4]}"), OK("10", "-1"));
    EXPECT(REQ("10", "x.json", ",\"params\":{\"list\":[]}"), OK("10", "0"));
    EXPECT(REQ("10", "x.json", ",\"params\":{\"list\":{}}"),
           ERR("10", "-32602", "\\\"list\\\" must be an array"));
    EXPECT(REQ("10", "x.json", ",\"params\":{\"list\":[1,\"2\"]}"),
           ERR("10", "-32602", "\\\"list\\\" must hold integers"));
    EXPECT(REQ("10", "x.json", ",\"params\":{}"), OK("10", "\"none\""));
    EXPECT(REQ("10", "x.json", ""), OK("10", "\"no params\""));

    /* X5 */
    EXPECT(REQ("11", "x.zok", ""), OK("11", "true"));
    CHECK(has(REQ("11", "x.zok", ",\"params\":{\"bad\":true}"),
              "\"error\":{\"code\":-32000,\"message\":\"ZXV_E_FUNDS") &&
              has(REQ("11", "x.zok", ",\"params\":{\"bad\":true}"),
                  "\"data\":{\"status\":-6,\"name\":\"ZXV_E_FUNDS\",\"class\":\"REFUSED\"}"),
          "X5: a libzxv status as the book methods report it: %s", OUT.p);

    /* X6-X7: the books through the extension, recorded like ledger.* */
    zxvd_rpc_books_close();
    uint32_t aa, bb;
    uint64_t ee;
    CHECK(!zxvd_books_cap() && !zxvd_books_ctx() &&
              zxvd_books_account("n", "NGN", ZXV_ACCT_HOLDER, &aa) == ZXV_E_STATE &&
              zxvd_books_post(0, 1, 1, "NGN", "r", &ee) == ZXV_E_STATE,
          "X6-X7: nothing while the books are closed");
    EXPECT(REQ("12", "x.books", ""), ERR("12", "-32050", "closed"));
    CHECK(zxvd_rpc_books_file(NULL) == 0 && zxvd_rpc_books_open() == 0, "books open in memory");
    CHECK(zxvd_books_cap() && zxvd_books_ctx(), "X6: the cap and context once open");
    CHECK(has(REQ("12", "currency.enable", ",\"params\":{\"code\":\"NGN\"}"), "\"enabled\""),
          "NGN on");
    CHECK(has(REQ("12", "x.books", ""), "\"result\":[") &&
              has(REQ("12", "x.books", ""), "\"code\":-32000"),
          "X7: an account and a posting; the same reference again is refused: %s", OUT.p);
    CHECK(has(REQ("12", "ledger.accounts", ""), "\"name\":\"x holder\"") &&
              zxvd_books_account("x holder", "NGN", ZXV_ACCT_HOLDER, &bb) == ZXV_OK,
          "X7: the accounts are the books' own");
    zxvd_rpc_books_close();
    zxvd_rpc_clear_methods();
    CHECK(has(REQ("13", "rpc.discover", ""), "\"play.draft\"]}") &&
              has(REQ("13", "x.num", ""), "method not found") &&
              has(REQ("13", "y.two", ""), "method not found"),
          "X1: clear takes every table out");
    CHECK(zxvd_rpc_add_methods(T3, 1) == 0 && zxvd_rpc_add_methods(T4, 1) == 0 &&
              zxvd_rpc_add_methods(T5, 1) == 0 && zxvd_rpc_add_methods(TAB, 5) == 0,
          "X1: four slots free again after clear");
    zxvd_rpc_clear_methods();
}


/* one request from a paired phone -> its reply */
static const char *phone_rpc(const char *line, bool post)
{
    bool r = zxvd_rpc_handle_phone_line(line, strlen(line), &OUT, post);
    checks++;
    if (!r) {
        fails++;
        printf("  FAIL a phone request got no reply: %s\n", line);
        return "<none>";
    }
    return OUT.p ? OUT.p : "";
}

static void phone_expect(const char *req, bool post, const char *want, int line)
{
    const char *got = phone_rpc(req, post);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d (phone)\n    req  %s\n    want %s\n    got  %s\n", line, req, want,
               got);
    }
}
#define PHONE_EXPECT(req, post, want) phone_expect(req, post, want, __LINE__)

static void test_phone_methods(void)
{
    zxvd_rpc_set_ops(&OPS);
    zxvd_rpc_set_phone_ops(NULL);
    EXPECT(REQ("1", "phone.pair", ",\"params\":{\"name\":\"Pixel\"}"),
           ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "phone.list", ""), ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "phone.link", ",\"params\":{\"on\":true}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_phone_ops(&PHONE_STUB);
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":true}"), OK("2", "{\"on\":true}"));
    CHECK(last_link_port == -1 && !strcmp(last_link_bind, "(all)"),
          "phone.link defaults: port -1, every interface (%d %s)", last_link_port, last_link_bind);
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":true,\"port\":0,\"bind\":\"10.0.0.5\"}"),
           OK("2", "{\"on\":true}"));
    CHECK(last_link_port == 0 && !strcmp(last_link_bind, "10.0.0.5"),
          "phone.link passes port and bind");
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":false}"), OK("2", "{\"on\":false}"));
    EXPECT(REQ("2", "phone.link", ""), ERR("2", "-32602", "\\\"on\\\" must be true or false"));
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":\"yes\"}"),
           ERR("2", "-32602", "\\\"on\\\" must be true or false"));
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":true,\"port\":65536}"),
           ERR("2", "-32602", "\\\"port\\\" must be 0..65535"));
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":true,\"port\":-1}"),
           ERR("2", "-32602", "\\\"port\\\" is out of range"));
    EXPECT(REQ("2", "phone.link", ",\"params\":{\"on\":true,\"port\":1}"),
           ERR("2", "-32602", "cannot listen on TCP 1: Permission denied"));
    EXPECT(REQ("2", "phone.pair", ",\"params\":{\"name\":\"Pixel\"}"),
           OK("2", "{\"paired\":true}"));
    CHECK(!strcmp(last_phone_name, "Pixel") && !last_phone_post, "phone.pair passes the name");
    EXPECT(REQ("2", "phone.pair", ",\"params\":{\"name\":\"iPad\",\"allow_post\":true}"),
           OK("2", "{\"paired\":true}"));
    CHECK(last_phone_post, "phone.pair passes allow_post");
    EXPECT(REQ("3", "phone.pair", ",\"params\":{\"name\":\"x\",\"allow_post\":1}"),
           ERR("3", "-32602", "\\\"allow_post\\\" must be true or false"));
    EXPECT(REQ("3", "phone.pair", ""), ERR("3", "-32602", "missing string parameter \\\"name\\\""));
    EXPECT(REQ("3", "phone.pair", ",\"params\":{\"name\":\"012345678901234567890123456789012\"}"),
           ERR("3", "-32602", "\\\"name\\\" must be a string of at most 32 bytes"));
    EXPECT(REQ("3", "phone.pair", ",\"params\":{\"name\":\"full\"}"),
           ERR("3", "-32602", "too many phones paired; revoke one first"));
    EXPECT(REQ("4", "phone.list", ""), OK("4", "{\"phones\":[]}"));
    EXPECT(
        REQ("5", "phone.revoke", ",\"params\":{\"phone_id\":\"00112233445566778899aabbccddeeff\"}"),
        OK("5", "{\"revoked\":true}"));
    EXPECT(REQ("5", "phone.revoke", ",\"params\":{\"phone_id\":\"ff\"}"),
           ERR("5", "-32602", "no such phone"));

    /* A1-A3: what a paired phone may call */
    const char *nopost = "this phone was paired without allow_post";
    PHONE_EXPECT(REQ("6", "zxvd.shutdown", ""), true,
                 ERR("6", "-32015", "not allowed from a phone"));
    PHONE_EXPECT(REQ("6", "phone.pair", ",\"params\":{\"name\":\"x\"}"), true,
                 ERR("6", "-32015", "not allowed from a phone"));
    PHONE_EXPECT(REQ("6", "phone.list", ""), false, ERR("6", "-32015", "not allowed from a phone"));
    PHONE_EXPECT(REQ("6", "phone.revoke", ""), true,
                 ERR("6", "-32015", "not allowed from a phone"));
    PHONE_EXPECT(REQ("6", "phone.link", ",\"params\":{\"on\":false}"), true,
                 ERR("6", "-32015", "not allowed from a phone"));
    PHONE_EXPECT(REQ("6", "peer.serve", ""), true, ERR("6", "-32015", "not allowed from a phone"));
    static const char *const NEVER[] = {"jurisdiction.set", "aipi.put", "aipi.publish",
                                        "aipi.execute"};
    for (size_t i = 0; i < sizeof NEVER / sizeof NEVER[0]; i++) {
        char req[160];
        snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"%s\"}", NEVER[i]);
        CHECK(strstr(phone_rpc(req, true), "not allowed from a phone") != NULL,
              "%s is never served to a phone", NEVER[i]);
    }
    static const char *const POST[] = {"net.command", "peer.ask", "currency.enable",
                                       "ledger.open_account", "ledger.post"};
    for (size_t i = 0; i < sizeof POST / sizeof POST[0]; i++) {
        char req[160];
        snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"%s\"}", POST[i]);
        CHECK(strstr(phone_rpc(req, false), nopost) != NULL, "%s needs allow_post", POST[i]);
        CHECK(strstr(phone_rpc(req, true), "-32015") == NULL, "%s is allowed with allow_post",
              POST[i]);
    }
    stopped = 0;
    PHONE_EXPECT(REQ("8", "swarm.state", ""), false, OK("8", "{\"tick\":7}"));
    PHONE_EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"zxvd.shutdown\"}", true,
                 ERR("null", "-32600", "a phone must send an id"));
    CHECK(stopped == 0, "a phone's notification is never run");
    PHONE_EXPECT(REQ("9", "no.such", ""), false, ERR("9", "-32601", "method not found"));
    /* the control socket is unaffected after phone requests */
    EXPECT(REQ("10", "phone.list", ""), OK("10", "{\"phones\":[]}"));
    zxvd_rpc_set_phone_ops(NULL);
}

/* ---------------- aipi.* ---------------- */

static void test_aipi_methods(void)
{
    static const char *const M[] = {"aipi.put", "aipi.get", "aipi.publish", "aipi.execute",
                                    "aipi.status"};
    char req[256], want[256];
    for (int pass = 0; pass < 2; pass++) {
        zxvd_rpc_set_aipi_ops(pass ? &AIPI_NONE : NULL);
        for (unsigned i = 0; i < 5; i++) {
            snprintf(req, sizeof req, REQ("1", "%s", ""), M[i]);
            EXPECT(req, ERR("1", "-32601", "not available"));
        }
    }
    zxvd_rpc_set_aipi_ops(&AIPI_STUB);
    EXPECT(REQ("2", "aipi.status", ""), OK("2", "{\"online\":false}"));

    /* aipi.put */
    EXPECT(REQ("3", "aipi.put", ",\"params\":{\"hex\":\"00Ff7a\"}"), OK("3", "{\"bytes\":3}"));
    CHECK(put_had_data && put_len == 3 && put_data[0] == 0 && put_data[1] == 0xFF &&
              put_data[2] == 0x7A && !put_priv && !strcmp(put_path, "<null>"),
          "aipi.put decodes hex and passes it with no path, public by default");
    EXPECT(REQ("3", "aipi.put", ",\"params\":{\"hex\":\"\",\"private\":true}"),
           OK("3", "{\"bytes\":0}"));
    CHECK(put_had_data && put_len == 0 && put_priv, "an empty block, private");
    EXPECT(REQ("3", "aipi.put", ",\"params\":{\"path\":\"/tmp/m.wasm\",\"private\":false}"),
           OK("3", "{\"bytes\":0}"));
    CHECK(!put_had_data && !strcmp(put_path, "/tmp/m.wasm") && !put_priv,
          "aipi.put passes a path and no data");
    EXPECT(REQ("3", "aipi.put", ",\"params\":{\"path\":\"/fail\"}"),
           ERR("3", "-32011", "the file could not be opened"));
    int n = aipi_calls;
    EXPECT(REQ("4", "aipi.put", ",\"params\":{}"),
           ERR("4", "-32602", "give exactly one of \\\"hex\\\" and \\\"path\\\""));
    EXPECT(REQ("4", "aipi.put", ""),
           ERR("4", "-32602", "give exactly one of \\\"hex\\\" and \\\"path\\\""));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"hex\":\"00\",\"path\":\"/x\"}"),
           ERR("4", "-32602", "give exactly one of \\\"hex\\\" and \\\"path\\\""));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"hex\":\"0\"}"),
           ERR("4", "-32602", "\\\"hex\\\" must be an even number of hex digits"));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"hex\":\"0g\"}"),
           ERR("4", "-32602", "\\\"hex\\\" must be an even number of hex digits"));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"path\":\"\"}"),
           ERR("4", "-32602", "\\\"path\\\" must not be empty"));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"path\":7}"),
           ERR("4", "-32602", "\\\"path\\\" must be a string of at most 1023 bytes"));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"hex\":\"00\",\"private\":1}"),
           ERR("4", "-32602", "\\\"private\\\" must be true or false"));
    EXPECT(REQ("4", "aipi.put", ",\"params\":{\"path\":\"/x\",\"private\":\"yes\"}"),
           ERR("4", "-32602", "\\\"private\\\" must be true or false"));
    CHECK(aipi_calls == n, "bad aipi.put params never reach the host");
    /* ZXVD_AIPI_HEX_MAX bytes fit in one request line, one more does not */
    static char big[2 * ZXVD_AIPI_HEX_MAX + 200];
    int k =
        snprintf(big, sizeof big,
                 "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"aipi.put\",\"params\":{\"hex\":\"");
    memset(big + k, 'a', 2 * ZXVD_AIPI_HEX_MAX);
    strcpy(big + k + 2 * ZXVD_AIPI_HEX_MAX, "\"}}");
    CHECK(has(big, "\"result\"") && put_len == ZXVD_AIPI_HEX_MAX && put_data[0] == 0xAA &&
              put_data[ZXVD_AIPI_HEX_MAX - 1] == 0xAA,
          "aipi.put takes %u bytes of hex", ZXVD_AIPI_HEX_MAX);
    memset(big + k, 'a', 2 * ZXVD_AIPI_HEX_MAX + 2);
    strcpy(big + k + 2 * ZXVD_AIPI_HEX_MAX + 2, "\"}}");
    CHECK(has(big, "\\\"hex\\\" must be a string of at most 16000 bytes"),
          "one byte more is refused");

    /* aipi.get */
    EXPECT(REQ("6", "aipi.get", ",\"params\":{\"cid\":\"zb2rh\"}"), OK("6", "{\"hex\":\"\"}"));
    CHECK(!strcmp(get_cid, "zb2rh"), "aipi.get passes the CID");
    EXPECT(REQ("6", "aipi.get", ",\"params\":{}"),
           ERR("6", "-32602", "missing string parameter \\\"cid\\\""));

    /* aipi.publish */
    EXPECT(REQ("7", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"contract\":\"zC\",\"entry\":\"half\","
               "\"checker\":\"half_ok\",\"certifier\":\"cf\",\"verify\":\"cert\",\"fuel_max\":"
               "\"18446744073709551615\",\"local_only\":true,\"deterministic\":false}"),
           ERR("7", "-32602",
               "\\\"fuel_max\\\" must be an integer or a decimal string (no fraction or "
               "exponent)"));
    EXPECT(REQ("7", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"contract\":\"zC\",\"entry\":\"half\","
               "\"checker\":\"half_ok\",\"certifier\":\"cf\",\"verify\":\"cert\",\"fuel_max\":"
               "\"9223372036854775807\",\"local_only\":true,\"deterministic\":false}"),
           OK("7", "{\"function\":\"zF\"}"));
    CHECK(!strcmp(pub_fn.module, "zM") && !strcmp(pub_fn.contract, "zC") &&
              !strcmp(pub_fn.entry, "half") && !strcmp(pub_fn.checker, "half_ok") &&
              !strcmp(pub_fn.certifier, "cf") && pub_fn.verify == 2 && pub_fn.local_only &&
              !pub_fn.deterministic && pub_fn.fuel_max == 9223372036854775807ull,
          "aipi.publish passes every field");
    static const char *const V[] = {"checker", "cert", "quorum", "local"};
    for (unsigned i = 0; i < 4; i++) {
        snprintf(req, sizeof req,
                 REQ("8", "aipi.publish",
                     ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"%s\","
                     "\"fuel_max\":5}"),
                 V[i]);
        EXPECT(req, OK("8", "{\"function\":\"zF\"}"));
        CHECK(pub_fn.verify == i + 1 && pub_fn.fuel_max == 5 && !pub_fn.local_only &&
                  pub_fn.deterministic && !pub_fn.contract[0] && !pub_fn.checker[0] &&
                  !pub_fn.certifier[0],
              "verify \"%s\" is mode %u, with the defaults", V[i], i + 1);
    }
    n = aipi_calls;
    snprintf(want, sizeof want, "%s",
             ERR("9", "-32602",
                 "\\\"verify\\\" must be \\\"checker\\\", \\\"cert\\\", \\\"quorum\\\" or "
                 "\\\"local\\\""));
    EXPECT(
        REQ("9", "aipi.publish",
            ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"none\",\"fuel_max\":5}"),
        want);
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"\",\"fuel_max\":5}"),
           want);
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"local\","
               "\"fuel_max\":-1}"),
           ERR("9", "-32602", "\\\"fuel_max\\\" must not be negative"));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"local\"}"),
           ERR("9", "-32602", "missing integer parameter \\\"fuel_max\\\""));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":1}"),
           ERR("9", "-32602", "missing string parameter \\\"module\\\""));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"verify\":\"local\",\"fuel_max\":1}"),
           ERR("9", "-32602", "missing string parameter \\\"entry\\\""));
    EXPECT(
        REQ("9", "aipi.publish", ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"fuel_max\":1}"),
        ERR("9", "-32602", "missing string parameter \\\"verify\\\""));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":1,"
               "\"deterministic\":0}"),
           ERR("9", "-32602", "\\\"deterministic\\\" must be true or false"));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":1,"
               "\"local_only\":null}"),
           ERR("9", "-32602", "\\\"local_only\\\" must be true or false"));
    EXPECT(REQ("9", "aipi.publish",
               ",\"params\":{\"module\":\"zM\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":1,"
               "\"checker\":5}"),
           ERR("9", "-32602", "\\\"checker\\\" must be a string of at most 63 bytes"));
    CHECK(aipi_calls == n, "bad aipi.publish params never reach the host");

    /* aipi.execute */
    EXPECT(REQ("10", "aipi.execute",
               ",\"params\":{\"function\":\"zF\",\"args\":[1,-2,9223372036854775807,"
               "-9223372036854775808,5,6,7,8]}"),
           OK("10", "{\"result\":\"1\"}"));
    CHECK(!strcmp(exec_fn, "zF") && exec_n == 8 && exec_args[0] == 1 && exec_args[1] == -2 &&
              exec_args[2] == INT64_MAX && exec_args[3] == INT64_MIN && exec_args[7] == 8 &&
              !exec_priv,
          "aipi.execute passes up to 8 integers, public by default");
    EXPECT(
        REQ("10", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[],\"private\":true}"),
        OK("10", "{\"result\":\"1\"}"));
    CHECK(exec_n == 0 && exec_priv, "no arguments, private input");
    EXPECT(REQ("10", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[13]}"),
           ERR("10", "-32011", "AIPI_ERR_EXEC: the sandbox refused or stopped the run"));
    n = aipi_calls;
    snprintf(want, sizeof want, "%s",
             ERR("11", "-32602", "\\\"args\\\" must be an array of at most 8 integers"));
    EXPECT(
        REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[1,2,3,4,5,6,7,8,9]}"),
        want);
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\"}"), want);
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":5}"), want);
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":{\"a\":1}}"), want);
    snprintf(
        want, sizeof want, "%s",
        ERR("11", "-32602", "\\\"args\\\" must hold 64-bit integers (no fraction or exponent)"));
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[1,1.5]}"), want);
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[\"1\"]}"), want);
    EXPECT(REQ("11", "aipi.execute",
               ",\"params\":{\"function\":\"zF\",\"args\":[9223372036854775808]}"),
           want);
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"args\":[]}"),
           ERR("11", "-32602", "missing string parameter \\\"function\\\""));
    EXPECT(REQ("11", "aipi.execute", ",\"params\":{\"function\":\"zF\",\"args\":[],\"private\":2}"),
           ERR("11", "-32602", "\\\"private\\\" must be true or false"));
    CHECK(aipi_calls == n, "bad aipi.execute params never reach the host");
    zxvd_rpc_set_aipi_ops(NULL);
}

/* ---------------- method groups and switches (zxvd_ext.h X1-X6, P1-P3) ---------------- */

static int g_calls, g_closed, g_closed_b;
static void g_echo(zxvd_call_t *c)
{
    char s[8];
    int64_t i = 0, a = 0;
    uint32_t u = 0;
    bool b = false;
    if (!zxvd_p_str(c, "s", s, sizeof s, false) || !zxvd_p_bool(c, "b", false, &b)) return;
    if (zxvd_p_has(c, "i") && !zxvd_p_i64(c, "i", &i)) return;
    if (zxvd_p_has(c, "u") && !zxvd_p_u32(c, "u", &u)) return;
    if (zxvd_p_has(c, "a") && !zxvd_p_amount(c, "a", &a)) return;
    g_calls++;
    zxvd_buf_printf(zxvd_res(c), "{\"s\":\"%s\",\"b\":%d,\"i\":%lld,\"u\":%u,\"a\":%lld}", s, b,
                    (long long) i, (unsigned) u, (long long) a);
}
static void g_hex(zxvd_call_t *c)
{
    uint8_t h[4];
    uint32_t n = 9;
    bool req = zxvd_p_has(c, "need");
    if (!zxvd_p_hex(c, "h", h, sizeof h, &n, req)) return;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_raw(o, "{\"n\":", 5);
    zxvd_buf_printf(o, "%u,\"h\":", (unsigned) n);
    zxvd_buf_hex(o, h, n);
    zxvd_buf_raw(o, "}", 1);
}
static void g_list(zxvd_call_t *c)
{
    int32_t n = zxvd_p_len(c, "items");
    if (zxvd_failed(c)) return;
    int64_t sum = 0;
    for (int32_t k = 0; k < n; k++) {
        int32_t saved;
        int64_t v;
        if (!zxvd_p_enter(c, "items", (uint32_t) k, &saved)) return;
        bool ok = zxvd_p_i64(c, "v", &v);
        zxvd_p_leave(c, saved);
        if (!ok) return;
        sum += v;
    }
    int32_t m = zxvd_p_len(c, "nums");
    if (zxvd_failed(c)) return;
    for (int32_t k = 0; k < m; k++) {
        int64_t v;
        if (!zxvd_p_i64_at(c, "nums", (uint32_t) k, &v)) return;
        sum += v;
    }
    int64_t after = 0;
    if (zxvd_p_has(c, "after") && !zxvd_p_i64(c, "after", &after)) return; /* scope restored */
    zxvd_buf_printf(zxvd_res(c), "{\"n\":%d,\"m\":%d,\"sum\":%lld}", (int) n, (int) m,
                    (long long) (sum + after));
}
static void g_status(zxvd_call_t *c)
{
    zxvd_buf_raw(zxvd_res(c), "{\"ok\":true}", 11);
}
static void g_fails(zxvd_call_t *c)
{
    zxvd_fail(c, -32050, "nope %d", 7);
    zxvd_fail(c, -32051, "second"); /* the first error stays */
    CHECK(zxvd_failed(c), "zxvd_failed after zxvd_fail");
}
static void g_close(void)
{
    g_closed++;
}
static void g_close_b(void)
{
    g_closed_b = g_closed + 1; /* closed before the first group: reverse order */
}
static const zxvd_group_method_t G_A[] = {
    {"ga.echo", g_echo, false},    {"ga.hex", g_hex, false},     {"ga.list", g_list, false},
    {"ga.status", g_status, true}, {"ga.fails", g_fails, false},
};
static const zxvd_group_method_t G_B[] = {{"gb.status", g_status, false}};
static const zxvd_group_method_t G_DUP_CORE[] = {{"ledger.post", g_status, false}};
static const zxvd_group_method_t G_DUP_GRP[] = {{"ga.echo", g_status, false}};
static const zxvd_group_method_t G_DUP_SELF[] = {{"gc.x", g_status, false},
                                                 {"gc.x", g_status, false}};
static const zxvd_group_method_t G_NULLFN[] = {{"gc.y", NULL, false}};
static const zxvd_group_method_t G_EMPTY[] = {{"", g_status, false}};
static const zxvd_group_method_t G_ONE[] = {{"gd.one", g_status, false}};

static void test_groups(void)
{
    zxvd_rpc_groups_reset();
    const zxvd_group_t A = {"alpha", true, "first \"group\"", G_A, 5, g_close};
    const zxvd_group_t B = {"beta_2", false, NULL, G_B, 1, g_close_b};
    CHECK(zxvd_rpc_add_group(&A) == 0, "add a group");
    CHECK(zxvd_rpc_add_group(&B) == 0, "add a second group");
    const zxvd_group_t bad[] = {
        {"alpha", false, "", G_DUP_CORE, 1, NULL},
        {"alpha", false, "", G_DUP_GRP, 1, NULL},
        {"alpha", false, "", G_DUP_SELF, 2, NULL},
        {"alpha", false, "", G_NULLFN, 1, NULL},
        {"alpha", false, "", G_EMPTY, 1, NULL},
        {"Alpha", false, "", G_ONE, 1, NULL},
        {"all", false, "", G_ONE, 1, NULL},
        {"", false, "", G_ONE, 1, NULL},
        {NULL, false, "", G_ONE, 1, NULL},
        {"a-b", false, "", G_ONE, 1, NULL},
        {"abcdefghijklmnopqrstuvwx", false, "", G_ONE, 1, NULL},
        {"alpha", false, "", NULL, 1, NULL},
        {"alpha", false, "", G_ONE, 0, NULL},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(zxvd_rpc_add_group(&bad[i]) == -1, "bad group %zu refused", i);
    CHECK(zxvd_rpc_add_group(NULL) == -1, "NULL group refused");
    const zxvd_group_t ok23 = {"abcdefghijklmnopqrstuvw", false, "", G_ONE, 1, NULL};
    CHECK(zxvd_rpc_add_group(&ok23) == 0, "a 23-byte feature name is fine");
    CHECK(has(REQ("1", "rpc.discover", ""),
              "\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\",\"play.draft\","
              "\"ga.echo\",\"ga.hex\",\"ga.list\","
              "\"ga.status\",\"ga.fails\",\"gb.status\",\"gd.one\"]}"),
          "X2: discover lists group methods after the core, on or off");

    /* X3/X4: off by default; always-methods answer; the reason names the switch */
    EXPECT(REQ("2", "ga.echo", ""),
           ERR("2", "-32012",
               "alpha is off: start zxvd with --enable alpha, or call features.set (regulated: "
               "test or licensed use only)"));
    EXPECT(
        REQ("3", "gb.status", ""),
        ERR("3", "-32012", "beta_2 is off: start zxvd with --enable beta_2, or call features.set"));
    EXPECT(REQ("4", "ga.status", ""), OK("4", "{\"ok\":true}"));
    CHECK(g_calls == 0, "an off method does nothing");
    EXPECT(REQ("5", "features.get", ""),
           OK("5", "{\"features\":[{\"name\":\"alpha\",\"enabled\":false,\"regulated\":true,"
                   "\"groups\":[{\"note\":\"first \\\"group\\\"\",\"methods\":5}]},"
                   "{\"name\":\"beta_2\",\"enabled\":false,\"regulated\":false,\"groups\":[{"
                   "\"note\":\"\",\"methods\":1}]},{\"name\":\"abcdefghijklmnopqrstuvw\","
                   "\"enabled\":false,\"regulated\":false,\"groups\":[{\"note\":\"\","
                   "\"methods\":1}]}],\"saved\":false}"));
    CHECK(!zxvd_rpc_enabled("alpha") && !zxvd_rpc_enabled(NULL) && !zxvd_rpc_enabled("nope"),
          "enabled() is false for off and unknown features");

    /* features.set */
    EXPECT(REQ("6", "features.set", ",\"params\":{\"feature\":\"alpha\"}"),
           OK("6", "{\"feature\":\"alpha\",\"enabled\":true,\"regulated\":true,\"saved\":false}"));
    CHECK(zxvd_rpc_enabled("alpha") && !zxvd_rpc_enabled("beta_2"), "set turns on one feature");
    EXPECT(REQ("7", "features.set", ",\"params\":{\"feature\":\"zeta\"}"),
           ERR("7", "-32602", "unknown feature \\\"zeta\\\""));
    EXPECT(REQ("8", "features.set", ",\"params\":{}"),
           ERR("8", "-32602", "missing string parameter \\\"feature\\\""));
    EXPECT(REQ("9", "features.set", ",\"params\":{\"feature\":\"alpha\",\"enabled\":1}"),
           ERR("9", "-32602", "\\\"enabled\\\" must be true or false"));
    CHECK(zxvd_rpc_enabled("alpha"), "a refused set changes nothing");

    /* P1 */
    EXPECT(
        REQ("10", "ga.echo", ",\"params\":{\"s\":\"hi\",\"b\":true,\"i\":-5,\"u\":7,\"a\":\"12\"}"),
        OK("10", "{\"s\":\"hi\",\"b\":1,\"i\":-5,\"u\":7,\"a\":12}"));
    EXPECT(REQ("11", "ga.echo", ""), OK("11", "{\"s\":\"\",\"b\":0,\"i\":0,\"u\":0,\"a\":0}"));
    EXPECT(REQ("12", "ga.echo", ",\"params\":{\"i\":1.5}"),
           ERR("12", "-32602", "\\\"i\\\" must be an integer (no fraction or exponent)"));
    EXPECT(REQ("13", "ga.echo", ",\"params\":{\"u\":-1}"),
           ERR("13", "-32602", "\\\"u\\\" is out of range"));
    EXPECT(REQ("14", "ga.echo", ",\"params\":{\"a\":\"1.0\"}"),
           ERR("14", "-32602",
               "\\\"a\\\" must be an integer or a decimal string (no fraction or exponent)"));
    EXPECT(REQ("15", "ga.echo", ",\"params\":{\"s\":\"12345678\"}"),
           ERR("15", "-32602", "\\\"s\\\" must be a string of at most 7 bytes"));
    EXPECT(REQ("16", "ga.fails", ""), ERR("16", "-32050", "nope 7"));

    /* P2 */
    EXPECT(REQ("20", "ga.hex", ",\"params\":{\"h\":\"00aF7e\"}"),
           OK("20", "{\"n\":3,\"h\":\"00af7e\"}"));
    EXPECT(REQ("21", "ga.hex", ",\"params\":{\"h\":\"\"}"), OK("21", "{\"n\":0,\"h\":\"\"}"));
    EXPECT(REQ("22", "ga.hex", ""), OK("22", "{\"n\":0,\"h\":\"\"}"));
    EXPECT(REQ("23", "ga.hex", ",\"params\":{\"need\":1}"),
           ERR("23", "-32602", "missing hex parameter \\\"h\\\""));
    const char *bad_hex[] = {"\"abc\"", "\"0011223344\"", "\"0g\"", "\"g0\"", "12", "\"zz\""};
    for (size_t i = 0; i < sizeof bad_hex / sizeof bad_hex[0]; i++) {
        char req[160];
        snprintf(req, sizeof req, REQ("24", "ga.hex", ",\"params\":{\"h\":%s}"), bad_hex[i]);
        CHECK(has(req, "must be a hex string of at most 4 bytes"), "bad hex %s", bad_hex[i]);
    }
    EXPECT(REQ("25", "ga.hex", ",\"params\":{\"h\":\"01020304\"}"),
           OK("25", "{\"n\":4,\"h\":\"01020304\"}"));

    /* P3 */
    EXPECT(REQ("30", "ga.list",
               ",\"params\":{\"items\":[{\"v\":1},{\"v\":20}],\"nums\":[300,4000],\"after\":5}"),
           OK("30", "{\"n\":2,\"m\":2,\"sum\":4326}"));
    EXPECT(REQ("31", "ga.list", ""), OK("31", "{\"n\":-1,\"m\":-1,\"sum\":0}"));
    EXPECT(REQ("32", "ga.list", ",\"params\":{\"items\":[]}"),
           OK("32", "{\"n\":0,\"m\":-1,\"sum\":0}"));
    EXPECT(REQ("33", "ga.list", ",\"params\":{\"items\":{}}"),
           ERR("33", "-32602", "\\\"items\\\" must be an array"));
    EXPECT(REQ("34", "ga.list", ",\"params\":{\"items\":[1]}"),
           ERR("34", "-32602", "\\\"items\\\"[0] must be an object"));
    EXPECT(REQ("35", "ga.list", ",\"params\":{\"items\":[{\"v\":1},{\"w\":2}]}"),
           ERR("35", "-32602", "missing integer parameter \\\"v\\\""));
    EXPECT(REQ("36", "ga.list", ",\"params\":{\"nums\":[1,\"2\"]}"),
           ERR("36", "-32602", "\\\"nums\\\"[1] must be an integer"));
    EXPECT(REQ("37", "ga.list", ",\"params\":{\"nums\":3}"),
           ERR("37", "-32602", "\\\"nums\\\" must be an array"));
    EXPECT(REQ("38", "ga.list", ",\"params\":{\"items\":[{\"v\":1}],\"after\":\"x\"}"),
           ERR("38", "-32602", "\\\"after\\\" must be an integer (no fraction or exponent)"));

    /* X3: --enable lists, X6: off again keeps nothing broken */
    EXPECT(
        REQ("40", "features.set", ",\"params\":{\"feature\":\"alpha\",\"enabled\":false}"),
        OK("40", "{\"feature\":\"alpha\",\"enabled\":false,\"regulated\":true,\"saved\":false}"));
    CHECK(has(REQ("41", "ga.echo", ""), "-32012"), "off again");
    CHECK(zxvd_rpc_enable("beta_2,,nope,alpha") == -1, "an unknown name is reported");
    CHECK(zxvd_rpc_enabled("alpha") && zxvd_rpc_enabled("beta_2"), "the known names still turn on");
    CHECK(!zxvd_rpc_enabled("abcdefghijklmnopqrstuvw"), "unlisted stays off");
    CHECK(zxvd_rpc_enable("all") == 0 && zxvd_rpc_enabled("abcdefghijklmnopqrstuvw"), "all");
    CHECK(zxvd_rpc_enable(NULL) == 0 && zxvd_rpc_enable("") == 0, "empty lists");
    CHECK(zxvd_rpc_enable("alph") == -1 && zxvd_rpc_enable("alphaa") == -1,
          "prefixes do not match");
    EXPECT(REQ("42", "gd.one", ""), OK("42", "{\"ok\":true}"));
    CHECK(has(REQ("43", "features.get", ""), "\"name\":\"beta_2\",\"enabled\":true"),
          "get shows on");

    /* two groups may share a feature; a later regulated one makes it regulated */
    zxvd_rpc_groups_reset();
    CHECK(g_closed == 1 && g_closed_b == 1, "reset closes every group, last first");
    CHECK(!zxvd_rpc_enabled("alpha"), "reset forgets the switches");
    EXPECT(REQ("44", "ga.echo", ""), ERR("44", "-32601", "method not found"));
    const zxvd_group_t S1 = {"shared", false, "one", G_B, 1, NULL};
    const zxvd_group_t S2 = {"shared", true, "two", G_ONE, 1, NULL};
    CHECK(zxvd_rpc_add_group(&S1) == 0 && zxvd_rpc_add_group(&S2) == 0, "shared feature");
    EXPECT(REQ("45", "features.get", ""),
           OK("45",
              "{\"features\":[{\"name\":\"shared\",\"enabled\":false,\"regulated\":true,"
              "\"groups\":[{\"note\":\"one\",\"methods\":1},{\"note\":\"two\",\"methods\":1}]}],"
              "\"saved\":false}"));
    zxvd_rpc_groups_reset();
    EXPECT(REQ("46", "features.get", ""), OK("46", "{\"features\":[],\"saved\":false}"));

    /* X1: re-adding a group held does nothing; removing one forgets its
     * methods and keeps its switch */
    CHECK(zxvd_rpc_add_group(&S1) == 0 && zxvd_rpc_add_group(&S1) == 0 &&
              zxvd_rpc_enable("shared") == 0,
          "a group added twice");
    EXPECT(REQ("47", "features.get", ""),
           OK("47", "{\"features\":[{\"name\":\"shared\",\"enabled\":true,\"regulated\":true,"
                    "\"groups\":[{\"note\":\"one\",\"methods\":1}]}],\"saved\":false}"));
    zxvd_rpc_remove_group(&S2); /* not held: nothing changes */
    CHECK(has(REQ("48", "gb.status", ""), "\"result\""), "S1 still served");
    zxvd_rpc_remove_group(&S1);
    EXPECT(REQ("49", "gb.status", ""), ERR("49", "-32601", "method not found"));
    CHECK(zxvd_rpc_enabled("shared") && zxvd_rpc_add_group(&S1) == 0 &&
              has(REQ("50", "gb.status", ""), "\"result\""),
          "the switch outlives its group; re-added it serves again");
    zxvd_rpc_groups_reset();

    /* the table limit */
    static zxvd_group_method_t many[ZXVD_EXT_MAX_GROUPS + 1][1];
    static char names[ZXVD_EXT_MAX_GROUPS + 1][16], feats[ZXVD_EXT_MAX_GROUPS + 1][8];
    static zxvd_group_t gs[ZXVD_EXT_MAX_GROUPS + 1];
    for (uint32_t i = 0; i <= ZXVD_EXT_MAX_GROUPS; i++) {
        snprintf(names[i], sizeof names[i], "m%u.x", (unsigned) i);
        snprintf(feats[i], sizeof feats[i], "f%u", (unsigned) i);
        many[i][0] = (zxvd_group_method_t){names[i], g_status, false};
        gs[i] = (zxvd_group_t){feats[i], false, "", many[i], 1, NULL};
    }
    for (uint32_t i = 0; i < ZXVD_EXT_MAX_GROUPS; i++)
        CHECK(zxvd_rpc_add_group(&gs[i]) == 0, "group %u fits", (unsigned) i);
    CHECK(zxvd_rpc_add_group(&gs[ZXVD_EXT_MAX_GROUPS]) == -1, "one more is refused");
    zxvd_rpc_groups_reset();
}

/* X7: a switch is a record in the books file, replayed at start */
static void restart_groups(const zxvd_group_t *a, const zxvd_group_t *b, const char *cli)
{
    zxvd_rpc_books_close();
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(a) == 0 && zxvd_rpc_add_group(b) == 0, "groups again");
    if (cli) CHECK(zxvd_rpc_enable(cli) == 0, "--enable %s", cli);
    CHECK(zxvd_rpc_books_open() == 0, "the books reopen with the switches");
}

static void test_feature_save(void)
{
    char tmpl[] = "/tmp/zxvdfeat.XXXXXX", path[200];
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    zxvd_rpc_groups_reset();
    const zxvd_group_t A = {"alpha", true, "a", G_A, 5, NULL};
    const zxvd_group_t B = {"abcdefghijklmnopqrstuvw", false, "b", G_ONE, 1, NULL};
    CHECK(zxvd_rpc_add_group(&A) == 0 && zxvd_rpc_add_group(&B) == 0, "two groups");
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "books on a file");
    CHECK(has(REQ("1", "features.get", ""), "],\"saved\":true}"), "get says switches are saved");
    CHECK(has(REQ("1", "books.file", ""), "\"records\":\"2\""), "nothing written yet");
    EXPECT(REQ("2", "features.set", ",\"params\":{\"feature\":\"alpha\",\"enabled\":false}"),
           OK("2", "{\"feature\":\"alpha\",\"enabled\":false,\"regulated\":true,\"saved\":true}"));
    CHECK(has(REQ("2", "books.file", ""), "\"records\":\"2\""),
          "an unchanged switch writes nothing");
    EXPECT(REQ("3", "features.set", ",\"params\":{\"feature\":\"alpha\"}"),
           OK("3", "{\"feature\":\"alpha\",\"enabled\":true,\"regulated\":true,\"saved\":true}"));
    EXPECT(REQ("4", "features.set", ",\"params\":{\"feature\":\"abcdefghijklmnopqrstuvw\"}"),
           OK("4", "{\"feature\":\"abcdefghijklmnopqrstuvw\",\"enabled\":true,\"regulated\":false,"
                   "\"saved\":true}"));
    CHECK(has(REQ("4", "books.file", ""), "\"records\":\"4\""), "two changes, two records");
    long n;
    char *f = slurp(path, &n);
    CHECK(f && strstr(f, " {\"op\":\"feat\",\"name\":\"alpha\",\"on\":true}\n") &&
              strstr(f, " {\"op\":\"feat\",\"name\":\"abcdefghijklmnopqrstuvw\",\"on\":true}\n"),
          "the records are in the file");
    free(f);
    restart_groups(&A, &B, NULL);
    CHECK(zxvd_rpc_enabled("alpha") && zxvd_rpc_enabled("abcdefghijklmnopqrstuvw"),
          "a restart keeps both on");
    EXPECT(REQ("5", "ga.echo", ""), OK("5", "{\"s\":\"\",\"b\":0,\"i\":0,\"u\":0,\"a\":0}"));
    EXPECT(REQ("6", "features.set", ",\"params\":{\"feature\":\"alpha\",\"enabled\":false}"),
           OK("6", "{\"feature\":\"alpha\",\"enabled\":false,\"regulated\":true,\"saved\":true}"));
    restart_groups(&A, &B, NULL);
    CHECK(!zxvd_rpc_enabled("alpha") && zxvd_rpc_enabled("abcdefghijklmnopqrstuvw"),
          "the last record wins");
    restart_groups(&A, &B, "alpha");
    CHECK(zxvd_rpc_enabled("alpha"), "--enable wins over a saved off");
    restart_groups(&A, &B, NULL);
    CHECK(!zxvd_rpc_enabled("alpha"), "and was not saved");
    /* a build without the feature skips its record */
    zxvd_rpc_books_close();
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(&A) == 0 && zxvd_rpc_books_open() == 0,
          "a record for a missing feature does not stop the start");
    CHECK(!zxvd_rpc_enabled("abcdefghijklmnopqrstuvw"), "and turns nothing on");
    /* a damaged record stops the start */
    const char *bad[] = {
        "{\"op\":\"feat\",\"name\":\"alpha\"}",
        "{\"op\":\"feat\",\"on\":true}",
        "{\"op\":\"feat\",\"name\":\"alpha\",\"on\":1}",
        "{\"op\":\"feat\",\"name\":7,\"on\":true}",
        "{\"op\":\"feat\",\"name\":"
        "\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstu\",\"on\":"
        "true}",
    };
    zxvd_rpc_books_close();
    long keep;
    char *orig = slurp(path, &keep);
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        spit(path, orig, keep);
        append_chained(path, bad[i]);
        CHECK(zxvd_rpc_books_open() == -1, "bad feature record %u stops the start", i);
        zxvd_rpc_books_close();
    }
    spit(path, orig, keep);
    free(orig);
    /* without a file: this run only */
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "no file");
    EXPECT(REQ("7", "features.set", ",\"params\":{\"feature\":\"alpha\"}"),
           OK("7", "{\"feature\":\"alpha\",\"enabled\":true,\"regulated\":true,\"saved\":false}"));
    CHECK(has(REQ("7", "features.get", ""), "],\"saved\":false}"), "get says not saved");
    zxvd_rpc_books_close();
    zxvd_rpc_groups_reset();
}

/* ---------------- files.* (zxv_files.h; the module itself: test_zxv_files.c) ---------------- */

#define FERR(id, msg) ERR(id, "-32014", msg)

static void test_files_methods(void)
{
    char root[200], want[600];
    snprintf(root, sizeof root, "%s/zxvd-files-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) != NULL, "scratch root");
    CHECK(zxf_set_root(root) == 0, "root set");
    snprintf(
        want, sizeof want,
        OK("1", "{\"root\":\"%s\",\"trash\":\".Trash\",\"read_max\":49152,\"file_max\":268435456}"),
        root);
    EXPECT(REQ("1", "files.root", ""), want);
    EXPECT(REQ("2", "files.list", ""), OK("2", "{\"path\":\"\",\"entries\":[]}"));
    EXPECT(REQ("2", "files.list", ",\"params\":{\"path\":\"..\"}"),
           FERR("2", "not a valid name or path in the ZXV folder"));
    EXPECT(REQ("2", "files.list", ",\"params\":{\"path\":7}"),
           ERR("2", "-32602", "\\\"path\\\" must be a string of at most 1024 bytes"));
    /* write, read back, stat */
    EXPECT(REQ("3", "files.write", ",\"params\":{\"path\":\"a.txt\",\"text\":\"h\\u00e9\\n\"}"),
           OK("3", "{\"path\":\"a.txt\",\"size\":4,\"done\":true}"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\"}"),
           OK("4", "{\"path\":\"a.txt\",\"size\":4,\"offset\":0,\"next\":4,\"eof\":true,"
                   "\"text\":\"h\xc3\xa9\\u000a\"}"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"offset\":1,\"max\":2}"),
           OK("4", "{\"path\":\"a.txt\",\"size\":4,\"offset\":1,\"next\":3,\"eof\":false,"
                   "\"text\":\"\xc3\xa9\"}"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"offset\":1,\"max\":1}"),
           FERR("4", "not text (not UTF-8)"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"encoding\":\"base64\"}"),
           OK("4", "{\"path\":\"a.txt\",\"size\":4,\"offset\":0,\"next\":4,\"eof\":true,"
                   "\"base64\":\"aMOpCg==\"}"));
    EXPECT(REQ("4", "files.read",
               ",\"params\":{\"path\":\"a.txt\",\"offset\":0,\"max\":0,"
               "\"encoding\":\"base64\"}"),
           OK("4", "{\"path\":\"a.txt\",\"size\":4,\"offset\":0,\"next\":4,\"eof\":true,"
                   "\"base64\":\"aMOpCg==\"}"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"offset\":9}"),
           OK("4", "{\"path\":\"a.txt\",\"size\":4,\"offset\":9,\"next\":9,\"eof\":true,"
                   "\"text\":\"\"}"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"encoding\":\"hex\"}"),
           ERR("4", "-32602", "\\\"encoding\\\" must be \\\"utf8\\\" or \\\"base64\\\""));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"offset\":-1}"),
           ERR("4", "-32602", "\\\"offset\\\" must not be negative"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"a.txt\",\"max\":1.5}"),
           ERR("4", "-32602", "\\\"max\\\" must be an integer (no fraction or exponent)"));
    EXPECT(REQ("4", "files.read", ",\"params\":{\"path\":\"no.txt\"}"),
           FERR("4", "no such file or folder"));
    EXPECT(REQ("4", "files.read", ""), ERR("4", "-32602", "missing string parameter \\\"path\\\""));
    CHECK(has(REQ("5", "files.stat", ",\"params\":{\"path\":\"a.txt\"}"),
              "{\"name\":\"a.txt\",\"dir\":false,\"size\":4,\"mtime\":"),
          "stat: %s", OUT.p);
    /* base64 and the chunked modes */
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"b.bin\",\"base64\":\"AAEC\"}"),
           OK("6", "{\"path\":\"b.bin\",\"size\":3,\"done\":true}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"path\":\"b.bin\"}"),
           FERR("6", "not text (not UTF-8)"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"path\":\"b.bin\",\"encoding\":\"base64\"}"),
           OK("6", "{\"path\":\"b.bin\",\"size\":3,\"offset\":0,\"next\":3,\"eof\":true,"
                   "\"base64\":\"AAEC\"}"));
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"c.bin\",\"base64\":\"/w==\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":1,\"done\":true}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"path\":\"c.bin\",\"encoding\":\"base64\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":1,\"offset\":0,\"next\":1,\"eof\":true,"
                   "\"base64\":\"/w==\"}"));
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"c.bin\",\"base64\":\"+/8=\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":2,\"done\":true}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"path\":\"c.bin\",\"encoding\":\"base64\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":2,\"offset\":0,\"next\":2,\"eof\":true,"
                   "\"base64\":\"+/8=\"}"));
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"c.bin\",\"base64\":\"ABA=\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":2,\"done\":true}"));
    EXPECT(REQ("6", "files.read", ",\"params\":{\"path\":\"c.bin\",\"encoding\":\"base64\"}"),
           OK("6", "{\"path\":\"c.bin\",\"size\":2,\"offset\":0,\"next\":2,\"eof\":true,"
                   "\"base64\":\"ABA=\"}"));
    EXPECT(REQ("6", "files.write", ",\"params\":{\"path\":\"e.bin\",\"base64\":\"\"}"),
           OK("6", "{\"path\":\"e.bin\",\"size\":0,\"done\":true}"));
    EXPECT(REQ("6", "files.trash", ",\"params\":{\"path\":\"e.bin\"}"),
           OK("6", "{\"trashed\":\".Trash/e.bin\",\"removed\":false}"));
    static const char *const BAD64[] = {
        "AAE", "A=AA", "AA=A", "====", "AAE*", "AA==AAAA", "A===", "AAAAA\\u0000AA"};
    for (size_t i = 0; i < sizeof BAD64 / sizeof *BAD64; i++) {
        char req[200];
        snprintf(req, sizeof req,
                 REQ("7", "files.write", ",\"params\":{\"path\":\"x\",\"base64\":\"%s\"}"),
                 BAD64[i]);
        const char *r = rpc(req);
        CHECK(strstr(r, "-32602") != NULL, "bad base64 %s refused: %s", BAD64[i], r);
    }
    EXPECT(REQ("7", "files.write", ",\"params\":{\"path\":\"x\"}"),
           ERR("7", "-32602", "give exactly one of \\\"text\\\" or \\\"base64\\\""));
    EXPECT(REQ("7", "files.write", ",\"params\":{\"path\":\"x\",\"text\":\"\",\"base64\":\"\"}"),
           ERR("7", "-32602", "give exactly one of \\\"text\\\" or \\\"base64\\\""));
    EXPECT(
        REQ("7", "files.write", ",\"params\":{\"path\":\"x\",\"text\":\"\",\"mode\":\"append\"}"),
        ERR("7", "-32602", "\\\"mode\\\" must be replace, begin, add or end"));
    EXPECT(REQ("7", "files.write", ",\"params\":{\"path\":\"x\",\"text\":\"\",\"exclusive\":1}"),
           ERR("7", "-32602", "\\\"exclusive\\\" must be true or false"));
    EXPECT(REQ("8", "files.write",
               ",\"params\":{\"path\":\"a.txt\",\"text\":\"\",\"exclusive\":true}"),
           FERR("8", "something with that name already exists"));
    EXPECT(
        REQ("8", "files.write", ",\"params\":{\"path\":\"big\",\"text\":\"ab\",\"mode\":\"add\"}"),
        FERR("8", "no \\\"begin\\\" for this file"));
    EXPECT(REQ("8", "files.write",
               ",\"params\":{\"path\":\"big\",\"text\":\"ab\",\"mode\":\"begin\"}"),
           OK("8", "{\"path\":\"big\",\"size\":2,\"done\":false}"));
    EXPECT(REQ("8", "files.write",
               ",\"params\":{\"path\":\"big\",\"base64\":\"Yw==\",\"mode\":\"add\"}"),
           OK("8", "{\"path\":\"big\",\"size\":3,\"done\":false}"));
    EXPECT(
        REQ("8", "files.write", ",\"params\":{\"path\":\"big\",\"text\":\"d\",\"mode\":\"end\"}"),
        OK("8", "{\"path\":\"big\",\"size\":4,\"done\":true}"));
    EXPECT(REQ("8", "files.read", ",\"params\":{\"path\":\"big\"}"),
           OK("8", "{\"path\":\"big\",\"size\":4,\"offset\":0,\"next\":4,\"eof\":true,"
                   "\"text\":\"abcd\"}"));
    EXPECT(REQ("8", "files.write",
               ",\"params\":{\"path\":\"big\",\"text\":\"e\",\"mode\":\"replace\"}"),
           OK("8", "{\"path\":\"big\",\"size\":1,\"done\":true}"));
    /* folders, rename, list order */
    EXPECT(REQ("9", "files.mkdir", ",\"params\":{\"path\":\"Docs\"}"),
           OK("9", "{\"path\":\"Docs\"}"));
    EXPECT(REQ("9", "files.mkdir", ",\"params\":{\"path\":\"Docs\"}"),
           FERR("9", "something with that name already exists"));
    EXPECT(REQ("9", "files.mkdir", ""),
           ERR("9", "-32602", "missing string parameter \\\"path\\\""));
    EXPECT(REQ("10", "files.rename", ",\"params\":{\"from\":\"big\",\"to\":\"Docs/big\"}"),
           OK("10", "{\"path\":\"Docs/big\"}"));
    EXPECT(REQ("10", "files.rename", ",\"params\":{\"from\":\"big\",\"to\":\"Docs/big\"}"),
           FERR("10", "no such file or folder"));
    EXPECT(REQ("10", "files.rename", ",\"params\":{\"from\":\"a.txt\"}"),
           ERR("10", "-32602", "missing string parameter \\\"to\\\""));
    const char *l = rpc(REQ("11", "files.list", ""));
    const char *d = strstr(l, "\"name\":\"Docs\",\"dir\":true,\"size\":0");
    const char *a = strstr(l, "\"name\":\"a.txt\",\"dir\":false,\"size\":4");
    const char *b = strstr(l, "\"name\":\"b.bin\"");
    static const char HEAD[] =
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"result\":{\"path\":\"\",\"entries\":[{";
    CHECK(!strncmp(l, HEAD, sizeof HEAD - 1) && d && a && b && d < a && a < b && strstr(l, "}]}}"),
          "list: %s", l);
    CHECK(has(REQ("11", "files.list", ",\"params\":{\"path\":\"Docs\"}"),
              "{\"path\":\"Docs\",\"entries\":[{\"name\":\"big\",\"dir\":false,\"size\":1,"
              "\"mtime\":"),
          "list Docs: %s", OUT.p);
    /* trash */
    EXPECT(REQ("12", "files.trash", ",\"params\":{\"path\":\"a.txt\"}"),
           OK("12", "{\"trashed\":\".Trash/a.txt\",\"removed\":false}"));
    EXPECT(REQ("12", "files.trash", ",\"params\":{\"path\":\".Trash/a.txt\"}"),
           OK("12", "{\"trashed\":null,\"removed\":true}"));
    EXPECT(REQ("12", "files.trash", ",\"params\":{\"path\":\"a.txt\"}"),
           FERR("12", "no such file or folder"));
    EXPECT(REQ("12", "files.trash", ",\"params\":{\"path\":\"Docs\"}"),
           OK("12", "{\"trashed\":\".Trash/Docs\",\"removed\":false}"));
    EXPECT(REQ("13", "files.empty_trash", ""), OK("13", "{\"removed\":2}"));
    EXPECT(REQ("13", "files.empty_trash", ""), OK("13", "{\"removed\":0}"));
    EXPECT(REQ("13", "files.list", ",\"params\":{\"path\":\".Trash\"}"),
           OK("13", "{\"path\":\".Trash\",\"entries\":[]}"));
    /* a root that cannot be made */
    char bad[260];
    snprintf(bad, sizeof bad, "%s/b.bin", root);
    CHECK(zxf_set_root(bad) == ZXF_E_ROOT, "a file is no root");
    EXPECT(REQ("14", "files.root", ""), FERR("14", "the ZXV folder cannot be found or made"));
    EXPECT(REQ("14", "files.empty_trash", ""),
           FERR("14", "the ZXV folder cannot be found or made"));
    CHECK(zxf_set_root(root) == 0, "root back");
    CHECK(rm_tree(root) == 0, "cleanup");
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN); /* as zxv_host.c does: a closed peer is an error return */
    test_buf();
    test_protocol();
    test_host_methods();
    test_peer_methods();
    test_phone_methods();
    test_aipi_methods();
    test_files_methods();
    test_spaces_methods();
    test_books_closed();
    test_books();
    test_earnings();
    test_books_file();
    test_mesh();
    test_views();
    test_jurisdiction();
    test_groups();
    test_feature_save();
    test_edges();
    test_ext();
    test_socket();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_rpc: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_rpc: %d checks\n", checks);
    return 0;
}
