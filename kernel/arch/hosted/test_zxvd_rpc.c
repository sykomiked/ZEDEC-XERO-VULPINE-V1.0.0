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
#include <stdarg.h>
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
#include <time.h>
#include <wchar.h>
#include <stdint.h>

#include <sys/resource.h>
#include <sys/file.h>

#include "zxvd_rpc.h"
#include "zxvd_ext.h"
#include "juris_cp.h"
#include "zxv_api.h"
#include "sha256.h"
#include "web4_web2.h"

#include "zxv_files.h"
#include "zxv_spaces.h"
#include "zxv_sysinfo.h"
#include "zxvd_log.h"
#include "zxv_search.h"
#include "zxv_print_host.h"
#include "zxv_keepsake.h"
#include "zxv_journal.h"
#include "zxv_screen.h"
#include "zxv_place.h"
#include "zxv_caps.h"
#include "zxvd_shell.h"
#include "zxvd_dns.h"
#include "zxv_sched.h"
#include "zxvd_proc.h"
#include "test_fake_device.h"
#include "web4_web2.h"

/* Appends to b at *k, never past cap - 1: *k stops at the end when the text
 * does not fit. */
static void catf(char *b, size_t cap, int *k, const char *fmt, ...)
{
    if (*k < 0 || (size_t) *k + 1u >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(b + *k, cap - (size_t) *k, fmt, ap);
    va_end(ap);
    if (r > 0) *k = (size_t) r < cap - (size_t) *k ? *k + r : (int) cap - 1;
}

/* the module methods (zxvd_rpc_modules.h), listed after zxvd's own */
#define MODS                                                                                       \
    "\"freight.pack\",\"freight.unpack\",\"harmonic.encode\",\"harmonic.decode\",\"stream.plan\"," \
    "\"stream.segment\",\"ehop.bands\",\"ehop.seal\",\"ehop.open\",\"web4.cid\","                  \
    "\"web4.agent_card\",\"web4.card_verify\""

/* the people methods (zxvd_people.c), after the play methods */
#define PEOPLE_M                                                                                   \
    "\"social.post\",\"social.feed\",\"social.groups\",\"social.group_create\",\"social.join\","   \
    "\"social.leave\",\"social.react\",\"social.export\",\"social.import\",\"call.start\","        \
    "\"call.accept\",\"call.hangup\",\"call.status\",\"call.signal\",\"xlate.detect\","            \
    "\"xlate.translate\",\"xlate.languages\",\"devmesh.status\",\"devmesh.invite\","               \
    "\"devmesh.join\",\"devmesh.confirm_sas\",\"devmesh.revoke\",\"devmesh.devices\","             \
    "\"ui.get\",\"ui.set\""

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

static int organ_asks;
static uint8_t last_organ;
static char last_organ_prompt[64];
static const char *o_ask(uint8_t organ, const char *prompt, zxvd_buf_t *o)
{
    organ_asks++;
    last_organ = organ;
    snprintf(last_organ_prompt, sizeof last_organ_prompt, "%s", prompt);
    if (!strcmp(prompt, "fail")) {
        zxvd_buf_raw(o, "{\"partial\":", 11); /* must be discarded */
        return "the organ's budget is spent";
    }
    zxvd_buf_printf(o, "{\"organ\":%u}", (unsigned) organ);
    return NULL;
}
static const zxvd_organ_ops_t ORGAN_STUB = {o_ask};
static const zxvd_organ_ops_t ORGAN_NONE = {0};
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
    {
        /* JSON is UTF-8: well-formed sequences pass, every other byte is U+FFFD */
        static const struct {
            const char *in, *out;
        } U[] = {
            {"\xc3\xa9\xe2\x82\xac\xf0\x9f\xa6\x8a", "\"\xc3\xa9\xe2\x82\xac\xf0\x9f\xa6\x8a\""},
            {"a\x94z", "\"a\xef\xbf\xbdz\""},               /* a lone continuation byte */
            {"\xe2\x82", "\"\xef\xbf\xbd\xef\xbf\xbd\""},   /* cut at the end */
            {"\xe2\x82x", "\"\xef\xbf\xbd\xef\xbf\xbdx\""}, /* cut before ASCII */
            {"\xc0\xaf", "\"\xef\xbf\xbd\xef\xbf\xbd\""},   /* overlong '/' */
            {"\xe0\x80\xaf", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\""}, /* overlong */
            {"\xed\xa0\x80", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\""}, /* a surrogate */
            {"\xf4\x90\x80\x80", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\""},
            {"\xf4\x8f\xbf\xbf", "\"\xf4\x8f\xbf\xbf\""}, /* U+10FFFF */
            {"\xff\"", "\"\xef\xbf\xbd\\\"\""},
        };
        for (size_t i = 0; i < sizeof U / sizeof U[0]; i++) {
            zxvd_buf_t u = {0};
            zxvd_buf_json_str(&u, U[i].in);
            CHECK(u.p && !strcmp(u.p, U[i].out), "json_str UTF-8 case %zu", i);
            free(u.p);
        }
    }
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
    EXPECT(
        REQ("11", "rpc.discover", ""),
        OK("11",
           "{\"protocol\":\"zxvd-rpc/1\",\"methods\":[\"rpc.discover\",\"zxvd.version\","
           "\"zxvd.shutdown\",\"swarm.state\",\"swarm.ask\",\"swarm.organ\",\"model.info\","
           "\"notes.list\","
           "\"notes.mark_read\",\"net.command\",\"net.peers\",\"net.offers\",\"player.card\","
           "\"peer.ask\",\"peer.serve\",\"peer.status\","
           "\"update.check\",\"currency.enable\","
           "\"ledger.open_account\",\"ledger.post\",\"ledger.balance\",\"ledger.accounts\","
           "\"ledger.entries\",\"ledger.check\",\"swarm.earnings\",\"mesh.books\","
           "\"books.file\",\"jurisdiction.get\",\"jurisdiction.set\",\"jurisdiction.install\","
           "\"jurisdiction.check\",\"jurisdiction.tax_rate\",\"aipi.put\",\"aipi.get\","
           "\"aipi.publish\",\"aipi.execute\",\"aipi.status\",\"features.get\",\"features.set\","
           "\"files.root\",\"files.list\","
           "\"files.stat\",\"files.read\",\"files.write\",\"files.mkdir\",\"files.rename\","
           "\"files.trash\",\"files.empty_trash\",\"files.cross\",\"spaces.list\",\"spaces.get\","
           "\"spaces.create\",\"spaces.join\",\"spaces.set_members\",\"spaces.set_cid\","
           "\"spaces.propose\",\"spaces.vote\",\"phone.link\",\"phone.pair\","
           "\"phone.list\",\"phone.revoke\","
           "\"play.catalogue\",\"play.run\",\"play.draft\","
           "\"sys.info\",\"sys.disk\",\"sys.net\","
           "\"log.tail\",\"journal.query\",\"journal.verify\","
           "\"journal.append\",\"notify.send\",\"shell.run\",\"shell.read\",\"shell.write\","
           "\"shell.kill\",\"shell.jobs\",\"caps.list\",\"caps.check\",\"caps.grant\",\"caps."
           "revoke\","
           "\"dns.resolve\",\"dns.reverse\",\"dns.flush\",\"did.parse\",\"did.match\","
           "\"schedule.add\",\"schedule.list\",\"schedule.cancel\",\"schedule.fired\","
           "\"proc.list\",\"proc.signal\",\"proc.restart\","
           "\"screen.tick\",\"screen.check\","
           "\"screen.status\",\"screen.set_limit\",\"screen.clear_limit\",\"screen.grant\","
           "\"screen.lock\",\"place.here\",\"place.set_here\",\"place.forget_here\","
           "\"place.ask\",\"place.permit\",\"place.apps\",\"place.list\",\"place.add\","
           "\"place.remove\",\"search.refresh\",\"search.query\","
           "\"print.status\",\"print.submit\","
           "\"print.job\",\"print.cancel\",\"scan.status\",\"scan.start\",\"backup.run\","
           "\"backup.list\",\"backup.versions\",\"backup.verify\",\"backup.restore\","
           "\"backup.prune\"," PEOPLE_M ","
           /* the module methods (zxvd_rpc_modules.h), after zxvd's own */
           MODS "]"
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

/* rpc.discover's names after play.draft: the utilities, Wake, Shepherd,
 * Waymark, Scent, Platen and Keepsake, with the app services between */
#define ZXVD_MINE_TAIL                                                                             \
    "\"sys.info\",\"sys.disk\",\"sys.net\",\"log.tail\",\"journal.query\",\"journal.verify\","     \
    "\"journal.append\",\"notify.send\",\"shell.run\",\"shell.read\",\"shell.write\",\"shell."     \
    "kill\",\"shell.jobs\",\"caps.list\",\"caps.check\",\"caps.grant\",\"caps.revoke\","           \
    "\"dns.resolve\",\"dns.reverse\",\"dns.flush\",\"did.parse\",\"did.match\",\"schedule.add\","  \
    "\"schedule.list\",\"schedule.cancel\",\"schedule.fired\",\"proc.list\",\"proc.signal\","      \
    "\"proc.restart\","                                                                            \
    "\"screen.tick\",\"screen.check\",\"screen.status\",\"screen.set_limit\","                     \
    "\"screen.clear_limit\",\"screen.grant\",\"screen.lock\",\"place.here\",\"place.set_here\","   \
    "\"place.forget_here\",\"place.ask\",\"place.permit\",\"place.apps\",\"place.list\","          \
    "\"place.add\",\"place.remove\",\"search.refresh\",\"search.query\",\"print.status\","         \
    "\"print.submit\",\"print.job\",\"print.cancel\",\"scan.status\",\"scan.start\","              \
    "\"backup.run\",\"backup.list\",\"backup.versions\",\"backup.verify\",\"backup.restore\","     \
    "\"backup.prune\","
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
           FERR("2", "not a valid name or path in the Hoard"));
    EXPECT(REQ("2", "spaces.create", ",\"params\":{\"kind\":\"team\",\"name\":\"\"}"),
           FERR("2", "not a valid name or path in the Hoard"));

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
           FERR("3", "not a valid name or path in the Hoard"));
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
           FERR("4", "not a valid name or path in the Hoard"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"x\",\"kind\":\"team\",\"name\":\"X\",\"owner\":\"bob\","
               "\"role\":\"owner\"}"),
           ERR("4", "-32602", "\\\"role\\\" must be member or guest"));
    EXPECT(REQ("4", "spaces.join",
               ",\"params\":{\"id\":\"home\",\"kind\":\"team\",\"name\":\"X\",\"owner\":\"bob\","
               "\"role\":\"member\"}"),
           FERR("4", "not a valid name or path in the Hoard"));
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
    EXPECT(REQ("10", "spaces.create",
               ",\"params\":{\"kind\":\"office\",\"name\":\"\",\"quorum_pct\":1}"),
           FERR("10", "not a valid name or path in the Hoard"));
    EXPECT(REQ("10", "spaces.create",
               ",\"params\":{\"kind\":\"office\",\"name\":\"\",\"quorum_pct\":100}"),
           FERR("10", "not a valid name or path in the Hoard"));
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
    EXPECT(REQ("10", "spaces.vote",
               ",\"params\":{\"space\":\"votes\",\"proposal\":4294967295,"
               "\"vote\":\"yes\"}"),
           SERR("10", "no open proposal with that id"));
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
           FERR("6", "not a valid name or path in the Hoard"));
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
    /* E6: own-device work never earns vouchers, in any letter case */
    CHECK(zxvd_rpc_earnings_set("VFV", 1) == -1, "E6: VFV is refused");
    CHECK(zxvd_rpc_earnings_set("vfv", 1) == -1, "E6: vfv is refused");
    CHECK(zxvd_rpc_earnings_set("vFv", 1) == -1, "E6: vFv is refused");
    CHECK(zxvd_rpc_earnings_set("VFVX", 1) == 0, "E6: a longer code that starts VFV is a unit");
    CHECK(zxvd_rpc_earnings_set("ABCDEFGHI", 1) == -1, "earnings: a 9-byte code is refused");
    CHECK(zxvd_rpc_earnings_set("ABCDEFGH", 1) == 0, "earnings: an 8-byte code is accepted");
    CHECK(zxvd_rpc_earnings_set("SWC", 0) == -1, "earnings: rate 0 is refused");
    CHECK(zxvd_rpc_earnings_set("SWC", (uint64_t) ZXV_AMOUNT_MAX + 1u) == -1,
          "earnings: a rate above ZXV_AMOUNT_MAX is refused");
    CHECK(zxvd_rpc_earnings_set("SWC", (uint64_t) ZXV_AMOUNT_MAX) == 0,
          "earnings: rate ZXV_AMOUNT_MAX is accepted");
    CHECK(zxvd_rpc_earnings_set("SWC", 1) == 0, "earnings: back to the default");
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

/* zxvd_rpc.h E1-E4. Runs after test_books: the books are open, in SWC. */
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
           OK("1", "{\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":null,"
                   "\"owner_account\":null,\"balance_minor\":\"0\",\"postings\":\"0\","
                   "\"cycles\":\"0\",\"resumed\":false,\"vfv_reversed_minor\":\"0\"}"));
    uint32_t src = (uint32_t) count(rpc(REQ("1", "ledger.accounts", "")), "\"account\":");
    uint32_t own = src + 1u;
    CHECK(zxvd_rpc_earnings_sync(1000) == 0, "earnings: the first cycle posts");
#define EARN(bal, posts, cycles)                                                                   \
    snprintf(want, sizeof want,                                                                    \
             OK("1",                                                                               \
                "{\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":%u,"                \
                "\"owner_account\":%u,\"balance_minor\":\"" bal "\",\"postings\":\"" posts         \
                "\",\"cycles\":\"" cycles "\",\"resumed\":false,\"vfv_reversed_minor\":\"0\"}"),   \
             src, own);                                                                            \
    EXPECT(REQ("1", "swarm.earnings", ""), want)
    EARN("1000", "1", "1");
    snprintf(req, sizeof req, REQ("2", "ledger.accounts", ""));
    snprintf(want, sizeof want,
             "{\"account\":%u,\"name\":\"swarm market\",\"currency\":\"SWC\",\"kind\":\"issuer\"},"
             "{\"account\":%u,\"name\":\"owner: swarm earnings\",\"currency\":\"SWC\","
             "\"kind\":\"holder\"}]",
             src, own);
    CHECK(has(req, want), "earnings: an issuer and a holder in SWC: %s", OUT.p);
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
                 ",\"params\":{\"from\":%u,\"to\":%u,\"amount_minor\":1,\"currency\":\"SWC\","
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

    /* E6: an ISO 4217 currency is money, so the books refuse to open in it */
    zxvd_rpc_books_close();
    EXPECT(REQ("1", "swarm.earnings", ""), ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_earnings_set("USD", 100) == 0, "earnings: USD is set while closed");
    CHECK(zxvd_rpc_books_open() == -1, "E6: and the books refuse to open in it");
    EXPECT(REQ("1", "ledger.check", ""), ERR("1", "-32603", "the books are not open"));
    /* another private unit and rate */
    CHECK(zxvd_rpc_earnings_set("PTS", 100) == 0, "earnings: PTS at 100 per credit");
    CHECK(zxvd_rpc_books_open() == 0, "books open in PTS");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"PTS\",\"rate_minor\":\"100\",\"source_account\":null,"
                   "\"owner_account\":null,\"balance_minor\":\"0\",\"postings\":\"0\","
                   "\"cycles\":\"0\",\"resumed\":false,\"vfv_reversed_minor\":\"0\"}"));
    CHECK(zxvd_rpc_earnings_sync((uint64_t) ZXV_AMOUNT_MAX / 100u + 1u) == -1,
          "earnings: credits times the rate above ZXV_AMOUNT_MAX are refused");
    EXPECT(REQ("1", "ledger.accounts", ""), OK("1", "{\"accounts\":[]}"));
    CHECK(zxvd_rpc_earnings_sync(3) == 0, "earnings: 3 credits");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"PTS\",\"rate_minor\":\"100\",\"source_account\":0,"
                   "\"owner_account\":1,\"balance_minor\":\"300\",\"postings\":\"1\","
                   "\"cycles\":\"1\",\"resumed\":false,\"vfv_reversed_minor\":\"0\"}"));
    BAL(1u, "300", "0", "300");
    /* a code libzxv refuses keeps the books closed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_earnings_set("swc", 1) == 0, "earnings: a lower-case code is set");
    CHECK(zxvd_rpc_books_open() == -1, "and refused when the books open");
    EXPECT(REQ("1", "ledger.check", ""), ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_earnings_sync(3) == 0, "earnings: nothing to mirror while closed");
    /* the accounts open at the first settled cycle, after any the owner opened */
    CHECK(zxvd_rpc_earnings_set("SWC", 1) == 0, "earnings: back to SWC");
    CHECK(zxvd_rpc_books_open() == 0, "books open in SWC");
    EXPECT(
        REQ("1", "ledger.open_account", ",\"params\":{\"name\":\"filler\",\"currency\":\"SWC\"}"),
        OK("1", "{\"account\":0}"));
    CHECK(zxvd_rpc_earnings_sync(2) == 0, "earnings: 2 credits");
    EXPECT(REQ("1", "swarm.earnings", ""),
           OK("1", "{\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":1,"
                   "\"owner_account\":2,\"balance_minor\":\"2\",\"postings\":\"1\","
                   "\"cycles\":\"1\",\"resumed\":false,\"vfv_reversed_minor\":\"0\"}"));
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

/* open the books; on a refusal, why is the reason zxvd gave on stderr */
static char WHY[600];
static int books_open_why(void)
{
    fflush(stderr);
    int saved = dup(2);
    FILE *f = tmpfile();
    if (saved < 0 || !f) return zxvd_rpc_books_open();
    dup2(fileno(f), 2);
    int rc = zxvd_rpc_books_open();
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    rewind(f);
    size_t n = fread(WHY, 1, sizeof WHY - 1, f);
    WHY[n] = 0;
    fclose(f);
    return rc;
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
    CHECK(b && strstr(b, " {\"op\":\"unit\",\"code\":\"SWC\",\"minor\":2}\n"),
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
    CHECK(lines == 10, "F3: header, SWC, USD once, 2 + 2 accounts, 1 + 2 postings = 10 (%zu)",
          lines);
    free(b);

    /* F4 + E5: everything comes back with the same numbers */
    reopen();
    EXPECT(REQ("5", "ledger.accounts", ""),
           OK("5",
              "{\"accounts\":[{\"account\":0,\"name\":\"bank \\\"one\\\"\",\"currency\":\"USD\","
              "\"kind\":\"issuer\"},{\"account\":1,\"name\":\"cash\",\"currency\":\"USD\","
              "\"kind\":\"holder\"},{\"account\":2,\"name\":\"swarm market\",\"currency\":"
              "\"SWC\",\"kind\":\"issuer\"},{\"account\":3,\"name\":\"owner: swarm "
              "earnings\",\"currency\":\"SWC\",\"kind\":\"holder\"}]}"));
    EXPECT(REQ("6", "ledger.balance", ",\"params\":{\"account\":1}"),
           OK("6", "{\"debit_minor\":\"9007199254740991\",\"credit_minor\":\"0\","
                   "\"equity_minor\":\"9007199254740991\"}"));
    EXPECT(REQ("6", "ledger.entries", ",\"params\":{\"account\":1}"),
           OK("6", "{\"entries\":[{\"entry_id\":\"1\",\"signed_minor\":\"9007199254740991\","
                   "\"counterparty\":0,\"reference\":\"e2e \xc3\xa9 1\"}]}"));
    EXPECT(REQ("7", "swarm.earnings", ""),
           OK("7", "{\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":2,"
                   "\"owner_account\":3,\"balance_minor\":\"25\",\"postings\":\"2\","
                   "\"cycles\":\"0\",\"resumed\":true,\"vfv_reversed_minor\":\"0\"}"));
    EXPECT(REQ("8", "ledger.check", ""), OK("8", "{\"ok\":true}"));
    /* E5: the fresh swarm's holdings are the base; only the change posts */
    CHECK(zxvd_rpc_earnings_sync(1000) == 0, "E5: first sync of the run sets the base");
    EXPECT(REQ("7", "swarm.earnings", ""),
           OK("7", "{\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":2,"
                   "\"owner_account\":3,\"balance_minor\":\"25\",\"postings\":\"2\","
                   "\"cycles\":\"1\",\"resumed\":true,\"vfv_reversed_minor\":\"0\"}"));
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
    CHECK(books_open_why() == -1 && strstr(WHY, "record 6: the hash chain is broken"),
          "F4: an edited amount breaks the chain: %s", WHY);
    CHECK(file_size(BOOKS) == n, "F4: the damaged file is left as it was");
    memcpy(copy, b, (size_t) n + 1);
    copy[3] = copy[3] == 'a' ? 'b' : 'a';
    spit(BOOKS, copy, n);
    CHECK(books_open_why() == -1 && strstr(WHY, "record 1: the hash chain is broken"),
          "F4: an edited hash breaks the chain: %s", WHY);
    memcpy(copy, b, (size_t) n + 1);
    copy[64] = 'x';
    spit(BOOKS, copy, n);
    CHECK(books_open_why() == -1 && strstr(WHY, "record 1: malformed record"),
          "F4: a malformed record is refused: %s", WHY);
    memcpy(copy, b, (size_t) n + 1);
    copy[10] = 'G';
    spit(BOOKS, copy, n);
    CHECK(books_open_why() == -1 && strstr(WHY, "record 1: malformed record"),
          "F4: a non-hex hash is refused: %s", WHY);
    char *nl = strchr(copy, '\n');
    spit(BOOKS, nl + 1, n - (long) (nl + 1 - copy));
    CHECK(books_open_why() == -1 && strstr(WHY, "record 1: the hash chain is broken"),
          "F4: a file without its header is refused: %s", WHY);
    spit(BOOKS, b, n);
    static const char *const BAD[] = {
        "",
        "x",
        "{\"op\":\"nope\"}",
        "[1]",
        "{\"code\":\"USD\"}",
        "{\"op\":\"ccy\",\"code\":\"ZZZ\"}",
        "{\"op\":\"ccy\",\"code\":5}",
        "{\"op\":\"unit\",\"code\":\"GBP\",\"minor\":2}",
        "{\"op\":\"unit\",\"code\":\"QQQ\",\"minor\":256}",
        "{\"op\":\"unit\",\"code\":\"QQQ\",\"minor\":255}",
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
        "{\"op\":\"post\",\"from\":4294967295,\"to\":1,\"amount\":\"1\",\"ccy\":\"USD\","
        "\"ref\":\"r\",\"id\":\"5\"}",
        "{\"op\":\"post\",\"from\":1,\"to\":4294967295,\"amount\":\"1\",\"ccy\":\"USD\","
        "\"ref\":\"r\",\"id\":\"5\"}",
    };
    /* the reason each is refused with */
    static const char *const BADWHY[] = {
        "malformed record",
        "not a JSON object",
        "unknown op",
        "not a JSON object",
        "no op",
        "currency refused",
        "bad ccy record",
        "unit refused",
        "bad unit record",
        "unit refused",
        "account number differs",
        "bad account record",
        "account refused",
        "posting refused",
        "bad posting record",
        "bad posting record",
        "bad posting record",
        "bad posting record",
        "posting refused",
        "unknown op",
        "posting refused",
        "posting refused",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        spit(BOOKS, b, n);
        append_chained(BOOKS, BAD[i]);
        CHECK(books_open_why() == -1 && i < sizeof BADWHY / sizeof BADWHY[0] &&
                  strstr(WHY, BADWHY[i]),
              "F4: refused: %s: %s", BAD[i], WHY);
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
    /* a books file past the 64 KiB first read buffer (it grows twice) replays whole */
    zxvd_rpc_books_close();
    {
        long m;
        char *cur = slurp(BOOKS, &m), hx[65], rec[160];
        last_hash(cur, m, hx);
        uint8_t h[32];
        for (int i = 0; i < 32; i++) {
            unsigned v;
            sscanf(hx + 2 * i, "%2x", &v);
            h[i] = (uint8_t) v;
        }
        free(cur);
        FILE *f = fopen(BOOKS, "ab");
        for (int r = 0; r < 1500; r++) {
            snprintf(rec, sizeof rec,
                     "{\"op\":\"post\",\"from\":4,\"to\":5,\"amount\":\"1\",\"ccy\":\"EUR\","
                     "\"ref\":\"b%d\",\"id\":\"%d\"}",
                     r, 7 + r);
            sha256_ctx_t c;
            sha256_init(&c);
            sha256_update(&c, h, 32);
            sha256_update(&c, (const uint8_t *) rec, strlen(rec));
            sha256_final(&c, h);
            for (int i = 0; i < 32; i++) fprintf(f, "%02x", h[i]);
            fprintf(f, " %s\n", rec);
        }
        fclose(f);
    }
    CHECK(stat(BOOKS, &st) == 0 && st.st_size > 3 * 65536, "the books file is past 192 KiB");
    CHECK(zxvd_rpc_books_open() == 0, "a large books file replays");
    EXPECT(REQ("9", "ledger.balance", ",\"params\":{\"account\":5}"),
           OK("9", "{\"debit_minor\":\"1507\",\"credit_minor\":\"0\",\"equity_minor\":\"1507\"}"));
    CHECK(has(REQ("1", "books.file", ""), "\"records\":\"1517\""), "books.file: 1517 records: %s",
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

/* ---------------- own-device work never earns VFV (zxvd_rpc.h E6) ---------------- */

static void test_books_vfv_reversal(void)
{
    char tmpl[] = "/tmp/zxvdrev.XXXXXX", path[200];
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "E6: a new books file");
    zxvd_rpc_books_close();
    /* a mirror written by an older zxvd: 500 VFV credited for own-device work */
    static const char *const OLD[] = {
        "{\"op\":\"unit\",\"code\":\"VFV\",\"minor\":2}",
        "{\"op\":\"acct\",\"name\":\"swarm market\",\"ccy\":\"VFV\",\"kind\":1,\"id\":0}",
        "{\"op\":\"acct\",\"name\":\"owner: swarm earnings\",\"ccy\":\"VFV\",\"kind\":0,\"id\":1}",
        ("{\"op\":\"post\",\"from\":0,\"to\":1,\"amount\":\"500\",\"ccy\":\"VFV\","
         "\"ref\":\"swarm earnings 1\",\"id\":\"1\"}"),
    };
    for (size_t i = 0; i < sizeof OLD / sizeof OLD[0]; i++) append_chained(path, OLD[i]);
    CHECK(zxvd_rpc_books_open() == 0, "E6: the old books open");
    EXPECT(REQ("1", "ledger.balance", ",\"params\":{\"account\":1}"),
           OK("1", "{\"debit_minor\":\"500\",\"credit_minor\":\"500\",\"equity_minor\":\"0\"}"));
    CHECK(has(REQ("2", "ledger.entries", ",\"params\":{\"account\":1}"),
              "{\"entry_id\":\"1\",\"signed_minor\":\"500\",\"counterparty\":0,"
              "\"reference\":\"swarm earnings 1\"},{\"entry_id\":\"2\",\"signed_minor\":\"-500\","
              "\"counterparty\":0,\"reference\":\"vfv reversal 1\"}]"),
          "E6: the old entry stays and one reversal follows it: %s", OUT.p);
    CHECK(has(REQ("3", "swarm.earnings", ""),
              "\"currency\":\"SWC\",\"rate_minor\":\"1\",\"source_account\":null,"
              "\"owner_account\":null,\"balance_minor\":\"0\",\"postings\":\"0\","
              "\"cycles\":\"0\",\"resumed\":false,\"vfv_reversed_minor\":\"500\"}"),
          "E6: the mirror starts again in SWC and reports the reversal: %s", OUT.p);
    EXPECT(REQ("4", "ledger.check", ""), OK("4", "{\"ok\":true}"));
    /* the reversal is a record of the file, and is not made twice */
    reopen();
    CHECK(has(REQ("3", "swarm.earnings", ""), "\"vfv_reversed_minor\":\"0\"}"),
          "E6: a second open finds nothing to reverse: %s", OUT.p);
    CHECK(has(REQ("2", "ledger.entries", ",\"params\":{\"account\":1}"),
              "\"reference\":\"vfv reversal 1\"}]"),
          "E6: still one reversal: %s", OUT.p);
    /* own-device credit after the reversal goes to SWC, never VFV */
    CHECK(zxvd_rpc_earnings_sync(30) == 0, "E6: a cycle posts");
    EXPECT(REQ("5", "ledger.balance", ",\"params\":{\"account\":1}"),
           OK("5", "{\"debit_minor\":\"500\",\"credit_minor\":\"500\",\"equity_minor\":\"0\"}"));
    CHECK(has(REQ("3", "swarm.earnings", ""), "\"currency\":\"SWC\"") &&
              strstr(OUT.p, "\"balance_minor\":\"30\""),
          "E6: the credit is SWC: %s", OUT.p);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "back to memory");
    unlink(path);
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
    "\"in_effect\":true,\"plugins\":0,\"report_standard\":\"\",\"gateway_key\":null,"              \
    "\"installed\":[]}"

/* J5: the result as kernel/src/juris reads it */
static bool ju_parse(const char *req, juris_settings_t *s)
{
    const char *r = strstr(rpc(req), "\"result\":");
    if (!r) return false;
    r += 9;
    size_t n = strlen(r);
    return n > 1 && juris_settings_parse(r, n - 1, s) == JURIS_OK; /* drop the closing } */
}

/* ---------------- play.* (P1-P3) ---------------- */

/* what the stub saw: the template, the files (copied: zxvd frees them) and
 * the draft request; each call fails with `play_why` when it is set */
static char play_tid[64], play_req[1100];
static char play_name[20][24], play_text[20][24];
static size_t play_len[20];
static uint32_t play_n, play_calls;
static const char *play_why;

static void play_seen(const char *tid, const zxvd_play_file_t *f, uint32_t n)
{
    play_calls++;
    snprintf(play_tid, sizeof play_tid, "%s", tid);
    play_n = n;
    for (uint32_t i = 0; i < n && i < 20; i++) {
        snprintf(play_name[i], sizeof play_name[i], "%s", f[i].name);
        memset(play_text[i], 0, sizeof play_text[i]);
        memcpy(play_text[i], f[i].text, f[i].len < 23 ? f[i].len : 23);
        play_len[i] = f[i].len;
    }
}

static const char *play_cat(zxvd_buf_t *out)
{
    play_calls++;
    zxvd_buf_raw(out, "[{\"id\":\"t\"}]", 12);
    return play_why;
}

static const char *play_run(const char *tid, const zxvd_play_file_t *f, uint32_t n, zxvd_buf_t *out)
{
    play_seen(tid, f, n);
    zxvd_buf_raw(out, "{\"verdict\":\"TRUE\"}", 18);
    return play_why;
}

static const char *play_draft(const char *tid, const zxvd_play_file_t *f, uint32_t n,
                              const char *request, zxvd_buf_t *out)
{
    play_seen(tid, f, n);
    snprintf(play_req, sizeof play_req, "%s", request);
    zxvd_buf_raw(out, "{\"model\":false,\"draft\":\"x\"}", 27);
    return play_why;
}

static void test_play(void)
{
    static const zxvd_play_ops_t ALL = {play_cat, play_run, play_draft};
    static const zxvd_play_ops_t CAT_ONLY = {play_cat, NULL, NULL};
    char req[1400], big[1100];

    /* P1-P3: no ops, or an op left unset, is "not available" */
    zxvd_rpc_set_play_ops(NULL);
    EXPECT(REQ("1", "play.run", ",\"params\":{\"template\":\"t\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_play_ops(&CAT_ONLY);
    EXPECT(REQ("1", "play.run", ",\"params\":{\"template\":\"t\"}"),
           ERR("1", "-32601", "not available"));
    EXPECT(REQ("1", "play.draft", ",\"params\":{\"template\":\"t\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_play_ops(&ALL);
    play_why = NULL;
    play_calls = 0;

    /* P1 */
    EXPECT(REQ("1", "play.catalogue", ""), OK("1", "[{\"id\":\"t\"}]"));
    play_why = "no templates";
    EXPECT(REQ("2", "play.catalogue", ""), ERR("2", "-32016", "no templates"));
    CHECK(play_calls == 2, "the catalogue was asked twice");

    /* P2: a failure discards what the call wrote and answers -32016 */
    EXPECT(REQ("3", "play.run", ",\"params\":{\"template\":\"t\"}"),
           ERR("3", "-32016", "no templates"));
    play_why = NULL;
    play_calls = 0;
    EXPECT(REQ("4", "play.run", ",\"params\":{\"template\":\"cobol-hello\"}"),
           OK("4", "{\"verdict\":\"TRUE\"}"));
    CHECK(play_calls == 1 && !strcmp(play_tid, "cobol-hello") && play_n == 0,
          "play.run without files: template %s, %u files", play_tid, play_n);
    EXPECT(REQ("5", "play.run",
               ",\"params\":{\"template\":\"t\",\"files\":{\"a.cob\":\"X\\nY\",\"b\":\"\"}}"),
           OK("5", "{\"verdict\":\"TRUE\"}"));
    CHECK(play_n == 2 && !strcmp(play_name[0], "a.cob") && !strcmp(play_text[0], "X\nY") &&
              play_len[0] == 3 && !strcmp(play_name[1], "b") && play_len[1] == 0,
          "two files decoded in order with their lengths (%u)", play_n);
    play_calls = 0;
    CHECK(has(REQ("6", "play.run", ""), "\"code\":-32602") && play_calls == 0,
          "play.run needs a template");
    CHECK(has(REQ("6", "play.run", ",\"params\":{\"template\":\"t\",\"files\":[\"x\"]}"),
              "must be an object of name: text") &&
              play_calls == 0,
          "files must be an object");
    CHECK(has(REQ("6", "play.run", ",\"params\":{\"template\":\"t\",\"files\":{\"a\":1}}"),
              "at most 16 entries, each a string") &&
              play_calls == 0,
          "a file's text must be a string");
    CHECK(has(REQ("6", "play.run", ",\"params\":{\"template\":\"t\",\"files\":{\"\":\"x\"}}"),
              "a bad name or text") &&
              play_calls == 0,
          "a file needs a name");
    /* sixteen files are fine, seventeen are refused */
    for (int k = 16; k <= 17; k++) {
        zxvd_buf_t rq = {0};
        zxvd_buf_printf(&rq, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"play.run\","
                             "\"params\":{\"template\":\"t\",\"files\":{");
        for (int i = 0; i < k; i++) zxvd_buf_printf(&rq, "%s\"f%d\":\"%d\"", i ? "," : "", i, i);
        zxvd_buf_printf(&rq, "}}}");
        play_calls = 0;
        const char *r = rq.p && !rq.oom ? rpc(rq.p) : "";
        free(rq.p);
        if (k == 16)
            CHECK(strstr(r, "\"result\"") && play_n == 16 && !strcmp(play_name[15], "f15") &&
                      !strcmp(play_text[15], "15"),
                  "16 files reach the Playground: %s", r);
        else
            CHECK(strstr(r, "at most 16 entries") && play_calls == 0, "17 files are refused: %s",
                  r);
    }

    /* P3 */
    EXPECT(REQ("8", "play.draft",
               ",\"params\":{\"template\":\"relief\",\"request\":\"fair shares\","
               "\"files\":{\"x\":\"1\"}}"),
           OK("8", "{\"model\":false,\"draft\":\"x\"}"));
    CHECK(!strcmp(play_tid, "relief") && !strcmp(play_req, "fair shares") && play_n == 1 &&
              !strcmp(play_text[0], "1"),
          "play.draft passes the template, the request and the files");
    EXPECT(REQ("9", "play.draft", ",\"params\":{\"template\":\"relief\"}"),
           OK("9", "{\"model\":false,\"draft\":\"x\"}"));
    CHECK(play_req[0] == 0 && play_n == 0, "the request and the files are optional");
    play_calls = 0;
    CHECK(has(REQ("9", "play.draft", ",\"params\":{\"request\":\"r\"}"), "\"code\":-32602") &&
              play_calls == 0,
          "play.draft needs a template");
    memset(big, 'a', 1024);
    big[1024] = 0;
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"play.draft\",\"params\":{\"template\":"
             "\"t\",\"request\":\"%s\"}}",
             big);
    CHECK(strstr(rpc(req), "at most 1023 bytes") && play_calls == 0,
          "a request over 1023 bytes is refused");
    CHECK(has(REQ("9", "play.draft", ",\"params\":{\"template\":\"t\",\"files\":{\"a\":true}}"),
              "at most 16 entries") &&
              play_calls == 0,
          "play.draft checks its files too");
    play_why = "the model is busy";
    EXPECT(REQ("9", "play.draft", ",\"params\":{\"template\":\"t\"}"),
           ERR("9", "-32016", "the model is busy"));
    play_why = NULL;
    zxvd_rpc_set_play_ops(NULL);
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
           OK("9", "{\"market\":\"closed\",\"standard\":\"IFRS\",\"country\":\"KE\","
                   "\"standards\":[\"IFRS\",\"US_GAAP\",\"IPSAS\",\"AAOIFI\",\"OHADA\",\"CAS\","
                   "\"SAP_INSURER\"],\"in_effect\":true,\"plugins\":0,\"report_standard\":\"IFRS\","
                   "\"gateway_key\":null,\"installed\":[]}"));
    reopen();
    CHECK(has(REQ("9", "jurisdiction.get", ""), "\"report_standard\":\"IFRS\","),
          "J5: the host has the replayed settings");
    juris_settings_t js;
    CHECK(ju_parse(REQ("9", "jurisdiction.get", ""), &js) && js.market == JURIS_MARKET_CLOSED &&
              js.standard == ACCT_STD_IFRS && !strcmp(js.country, "KE") && js.in_effect,
          "J5: kernel/src/juris reads the result");
    CHECK(
        has(REQ("9", "jurisdiction.set", ",\"params\":{\"standard\":\"AAOIFI\",\"country\":\"\"}"),
            "\"report_standard\":\"\","),
        "J5: no country, no report standard");
    CHECK(has(REQ("9", "jurisdiction.set", ",\"params\":{\"country\":\"MY\",\"market\":\"open\"}"),
              "\"report_standard\":\"AAOIFI\",") &&
              ju_parse(REQ("9", "jurisdiction.get", ""), &js) && js.market == JURIS_MARKET_OPEN &&
              js.standard == ACCT_STD_AAOIFI,
          "J5: AAOIFI with a country");
    CHECK(has(REQ("9", "jurisdiction.set", ",\"params\":{\"standard\":\"IPSAS\"}"),
              "\"report_standard\":\"\","),
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
        CHECK(books_open_why() == -1 && strstr(WHY, ": bad jurisdiction record."),
              "bad jurisdiction record %u stops the start: %s", i, WHY);
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

/* ---------------- the gateway key and compliance plug-ins (J6-J9) ---------------- */

typedef struct {
    uint8_t pk[JURIS_GW_PK_LEN], sk[JURIS_GW_SK_LEN];
} ju_keys_t;
static ju_keys_t JK1, JK2;
static char JREQ[ZXVD_RPC_MAX_LINE + 64];
#define JB64(n) (((n) + 2) / 3 * 4 + 1)

static void ju_b64(const uint8_t *d, size_t n, char *out)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t) d[i] << 16 | (i + 1 < n ? (uint32_t) d[i + 1] << 8 : 0) |
                     (i + 2 < n ? d[i + 2] : 0);
        out[o++] = A[v >> 18 & 63];
        out[o++] = A[v >> 12 & 63];
        out[o++] = i + 1 < n ? A[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? A[v & 63] : '=';
    }
    out[o] = 0;
}

static const char *ju_set_key(const uint8_t *pk, size_t n)
{
    int k = snprintf(JREQ, sizeof JREQ,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"jurisdiction.set\","
                     "\"params\":{\"gateway_key\":\"");
    for (size_t i = 0; i < n; i++) k += snprintf(JREQ + k, sizeof JREQ - (size_t) k, "%02x", pk[i]);
    snprintf(JREQ + k, sizeof JREQ - (size_t) k, "\"}}");
    return rpc(JREQ);
}

/* a manifest the test signs: m (the compliance manifest, signed under cpk's
 * context by csk) and the J3 plug-in manifest over it signed by gsk with
 * the given hook mask and version (gsk NULL: under cpk's context instead) */
static uint8_t JM[CAPCTL_MANIFEST_MAX + TAXHOOK_MANIFEST_MAX];
static size_t JML;
static uint8_t JSIG[CPK_SIG_LEN], JGSIG[JURIS_GW_SIG_LEN];

static void ju_sign(const juris_plugin_t *p, uint32_t hooks, uint64_t version, const ju_keys_t *ck,
                    const ju_keys_t *gk, bool gw_ctx)
{
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(JM, JML, msg);
    cpk_sign(ck->sk, msg, sizeof msg, NULL, JSIG);
    juris_plugin_t q = *p;
    q.hooks = hooks;
    juris_manifest_t m;
    juris_cp_manifest(&q, version, JM, JML, JGSIG, JURIS_GW_SIG_LEN, &m);
    uint8_t jm[JURIS_MANIFEST_MSG];
    juris_manifest_msg(&m, jm);
    if (gw_ctx)
        juris_gw_sign(gk->sk, jm, sizeof jm, NULL, JGSIG);
    else
        cpk_sign(gk->sk, jm, sizeof jm, NULL, JGSIG);
}

static const char *ju_install_req(size_t mlen, size_t slen, size_t glen)
{
    static char m[JB64(sizeof JM)], sg[JB64(CPK_SIG_LEN)], gs[JB64(JURIS_GW_SIG_LEN)];
    ju_b64(JM, mlen, m);
    ju_b64(JSIG, slen, sg);
    ju_b64(JGSIG, glen, gs);
    snprintf(JREQ, sizeof JREQ,
             "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"jurisdiction.install\",\"params\":{"
             "\"manifest\":\"%s\",\"sig\":\"%s\",\"gateway_sig\":\"%s\"}}",
             m, sg, gs);
    return rpc(JREQ);
}

static const char *ju_install_now(void)
{
    return ju_install_req(JML, CPK_SIG_LEN, JURIS_GW_SIG_LEN);
}

static uint64_t ju_now(void)
{
    return (uint64_t) time(NULL);
}

static void ju_profile(capctl_profile_t *p, const char *cc, uint32_t version, const ju_keys_t *k)
{
    memset(p, 0, sizeof *p);
    p->hdr.kind = CP_KIND_CAPCTL;
    memcpy(p->hdr.country, cc, 2);
    p->hdr.version = version;
    /* +-10 days: an install time or a clock off by 0.1% (20 days) falls outside */
    p->hdr.not_before = ju_now() - 10 * 86400ull;
    p->hdr.not_after = ju_now() + 10 * 86400ull;
    cpk_key_id(k->pk, p->hdr.key_id);
    p->ccy = 404; /* KES */
    p->flags = CAPCTL_F_OUTBOUND | CAPCTL_F_INBOUND | CAPCTL_F_DEFAULT_ALLOW;
    p->per_tx_limit = 1000;
    p->report_single = 500;
    JML = capctl_encode(p, JM, sizeof JM);
}

static void ju_table(taxhook_table_t *t, const char *cc, uint8_t kind, uint32_t ppm,
                     uint32_t version, const ju_keys_t *k)
{
    memset(t, 0, sizeof *t);
    t->hdr.kind = CP_KIND_TAXTABLE;
    memcpy(t->hdr.country, cc, 2);
    t->hdr.version = version;
    t->hdr.not_before = ju_now() - 10 * 86400ull;
    t->hdr.not_after = ju_now() + 10 * 86400ull;
    cpk_key_id(k->pk, t->hdr.key_id);
    t->kind = kind;
    t->ccy = 404;
    t->rounding = TAXHOOK_HALF_UP;
    t->level = TAXHOOK_PER_LINE;
    memcpy(t->label, "TEST", 4);
    t->rate[0].category = TAXHOOK_STANDARD;
    t->rate[0].rate_ppm = ppm;
    t->rate[0].from = 20000101;
    t->n_rates = 1;
    JML = taxhook_encode(t, JM, sizeof JM);
}

static juris_plugin_t JP_CAP, JP_VAT, JP_GST;

/* a VAT table for one region of KE, signed for its own plug-in id */
static const char *ju_region_install(const char *region, uint32_t version, const ju_keys_t *k)
{
    static taxhook_t th;
    static juris_cp_tax_t ta;
    juris_plugin_t p;
    taxhook_table_t t;
    char reg[CP_REGION_LEN] = {0};
    memcpy(reg, region, strlen(region));
    juris_cp_tax_plugin(&p, &ta, &th, "KE", reg, TAXHOOK_VAT, NULL, NULL);
    ju_table(&t, "KE", TAXHOOK_VAT, 160000, version, k);
    memcpy(t.hdr.region, reg, CP_REGION_LEN);
    JML = taxhook_encode(&t, JM, sizeof JM);
    ju_sign(&p, p.hooks, version, k, k, true);
    return ju_install_now();
}

static void ju_plugins(void)
{
    static capctl_t cc;
    static juris_cp_capctl_t ca;
    static taxhook_t th;
    static juris_cp_tax_t ta, tg;
    static const char nat[CP_REGION_LEN] = {0};
    capctl_init(&cc, NULL, NULL, NULL, 0);
    juris_cp_capctl_plugin(&JP_CAP, &ca, &cc, "KE", NULL, NULL, NULL);
    juris_cp_tax_plugin(&JP_VAT, &ta, &th, "KE", nat, TAXHOOK_VAT, NULL, NULL);
    juris_cp_tax_plugin(&JP_GST, &tg, &th, "KE", nat, TAXHOOK_GST, NULL, NULL);
}

static void test_juris_plugins(void)
{
    char tmpl[] = "/tmp/zxvdjplug.XXXXXX", path[200], req[400];
    uint8_t seed[32];
    memset(seed, 1, sizeof seed);
    pqm_mldsa87_keygen(seed, JK1.pk, JK1.sk);
    memset(seed, 2, sizeof seed);
    pqm_mldsa87_keygen(seed, JK2.pk, JK2.sk);
    ju_plugins();
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/books.zxvb", tmpl);
    zxvd_rpc_books_close();
    EXPECT(REQ("1", "jurisdiction.install", ",\"params\":{}"),
           ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "jurisdiction.check", ",\"params\":{}"),
           ERR("1", "-32603", "the books are not open"));
    EXPECT(REQ("1", "jurisdiction.tax_rate", ",\"params\":{}"),
           ERR("1", "-32603", "the books are not open"));
    CHECK(zxvd_rpc_books_file(path) == 0 && zxvd_rpc_books_open() == 0, "books open on a file");
    char rc0[40], rc1[40], rc2[40], rc3[40];
    {
        const char *q = strstr(rpc(REQ("1", "books.file", "")), "\"records\":\"");
        unsigned long r0 = q ? strtoul(q + 11, NULL, 10) : 0;
        snprintf(rc0, sizeof rc0, "\"records\":\"%lu\"", r0);
        snprintf(rc1, sizeof rc1, "\"records\":\"%lu\"", r0 + 1);
        snprintf(rc2, sizeof rc2, "\"records\":\"%lu\"", r0 + 2);
        snprintf(rc3, sizeof rc3, "\"records\":\"%lu\"", r0 + 3);
    }
    capctl_profile_t pr;
    ju_profile(&pr, "KE", 2, &JK1);
    ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
    /* J7: refused while no key is set, and nothing is written */
    CHECK(has(REQ("2", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":10}"),
              "\"base\":\"neither\",\"action\":\"wait\",\"proceed\":false,\"reports\":0}"),
          "J9: no plug-in, no country: wait");
    CHECK(strstr(ju_install_now(), "{\"code\":-32097,\"message\":\"no gateway key is set"),
          "J7: no gateway key: refused: %s", OUT.p);
    /* J6: the key, by hex, checked */
    CHECK(has(REQ("3", "jurisdiction.set", ",\"params\":{\"gateway_key\":\"abc\"}"),
              "\"code\":-32602,\"message\":\"\\\"gateway_key\\\" must be \\\"\\\" or an ML-DSA-87 "
              "public key in hex (5184 digits)"),
          "J6: odd hex refused");
    CHECK(strstr(ju_set_key(JK1.pk, JURIS_GW_PK_LEN - 1), "-32602"), "J6: a short key refused");
    JREQ[strlen(JREQ) - 4] = 'g';
    CHECK(strstr(rpc(JREQ), "-32602"), "J6: a non-hex digit refused");
    CHECK(has(REQ("3", "jurisdiction.set", ",\"params\":{\"gateway_key\":7}"), "-32602"),
          "J6: a number refused");
    CHECK(has(REQ("3", "books.file", ""), rc0), "nothing written");
    CHECK(has(REQ("3", "jurisdiction.set", ",\"params\":{\"gateway_key\":\"\"}"),
              "\"gateway_key\":null") &&
              has(REQ("3", "books.file", ""), rc0),
          "J6: clearing an unset key writes nothing");
    uint8_t kid[32];
    char kidx[65], want[200];
    juris_gw_key_id(JK1.pk, kid);
    for (int i = 0; i < 32; i++) snprintf(kidx + 2 * i, 3, "%02x", kid[i]);
    snprintf(want, sizeof want, "\"gateway_key\":\"%s\",\"installed\":[]}", kidx);
    const char *r = ju_set_key(JK1.pk, JURIS_GW_PK_LEN);
    CHECK(strstr(r, want) && strlen(r) < 600, "J6: get shows the key id, never the key: %s", r);
    CHECK(has(REQ("3", "books.file", ""), rc1), "J6: one record");
    ju_set_key(JK1.pk, JURIS_GW_PK_LEN);
    CHECK(has(REQ("3", "books.file", ""), rc1), "J6: the same key writes nothing");
    CHECK(strstr(ju_install_now(), "-32097,\"message\":\"no country is set\""),
          "J7: no country: refused");
    rpc(REQ("4", "jurisdiction.set", ",\"params\":{\"country\":\"KE\"}"));
    CHECK(has(REQ("4", "books.file", ""), rc2), "country saved");
    /* every refusal changes nothing */
    struct {
        const char *what, *why;
        int how;
    } bad[] = {
        {"another gateway key", "the gateway signature does not verify", 0},
        {"the gateway key under ZXV-COMPLIANCE", "the gateway signature does not verify", 1},
        {"a hook mask other than the plug-in's", "the gateway signature does not verify", 2},
        {"a version other than the manifest's", "the gateway signature does not verify", 3},
        {"a truncated gateway signature", "the gateway signature does not verify", 4},
        {"a compliance signature by an untrusted key", "the compliance signature does not verify",
         5},
        {"a truncated compliance signature", "the compliance signature does not verify", 6},
        {"a manifest outside its window", "the profile is outside its validity window", 7},
        {"another country's manifest", "the manifest is for another country", 8},
        {"a manifest cut short", "not a compliance manifest", 9},
        {"a KYC revocation list",
         "the manifest is neither a capital-control profile nor a tax table", 10},
        {"a profile that does not decode", "the profile does not decode", 11},
        {"a tax table that does not decode", "the tax table does not decode", 12},
        {"a manifest for KF while the country is KE", "the manifest is for another country", 13},
        {"a tax table by an untrusted compliance key", "the compliance signature does not verify",
         14},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        size_t ml = JML, sl = CPK_SIG_LEN, gl = JURIS_GW_SIG_LEN;
        ju_profile(&pr, "KE", 2, &JK1);
        switch (bad[i].how) {
        case 0:
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK2, true);
            break;
        case 1:
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, false);
            break;
        case 2:
            ju_sign(&JP_CAP, JURIS_HOOK_TRANSFER, 2, &JK1, &JK1, true);
            break;
        case 3:
            ju_sign(&JP_CAP, JP_CAP.hooks, 3, &JK1, &JK1, true);
            break;
        case 4:
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            gl--;
            break;
        case 5:
            ju_profile(&pr, "KE", 2, &JK2);
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK2, &JK1, true);
            break;
        case 6:
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            sl--;
            break;
        case 7:
            pr.hdr.not_before = ju_now() + 86400;
            JML = capctl_encode(&pr, JM, sizeof JM);
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            break;
        case 8:
            ju_profile(&pr, "NG", 2, &JK1);
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            break;
        case 9:
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            ml = 40;
            break;
        case 10:
            JM[6] = CP_KIND_KYC_REVOKE;
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            break;
        case 11:
            JM[JML++] = 0;
            ml++;
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            break;
        case 13:
            ju_profile(&pr, "KF", 2, &JK1);
            ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
            break;
        case 14: {
            taxhook_table_t tt;
            ju_table(&tt, "KE", TAXHOOK_VAT, 160000, 1, &JK2);
            ju_sign(&JP_VAT, JP_VAT.hooks, 1, &JK2, &JK1, true);
            break;
        }
        default: {
            taxhook_table_t tt;
            ju_table(&tt, "KE", TAXHOOK_VAT, 160000, 1, &JK1);
            JM[JML++] = 0;
            ml = JML;
            ju_sign(&JP_VAT, JP_VAT.hooks, 1, &JK1, &JK1, true);
        }
        }
        if (bad[i].how != 9 && bad[i].how != 11 && bad[i].how != 12) ml = JML;
        r = ju_install_req(ml, sl, gl);
        snprintf(want, sizeof want, "{\"code\":-32097,\"message\":\"%s\"}", bad[i].why);
        CHECK(strstr(r, want) && has(REQ("5", "books.file", ""), rc2) &&
                  has(REQ("5", "jurisdiction.get", ""), "\"plugins\":0,") &&
                  has(REQ("5", "jurisdiction.get", ""), "\"installed\":[]}"),
              "J7: %s refused, nothing changed: %s", bad[i].what, r);
    }
    CHECK(
        strstr(
            rpc(REQ("5", "jurisdiction.install",
                    ",\"params\":{\"manifest\":\"!!\",\"sig\":\"AA==\",\"gateway_sig\":\"AA==\"}")),
            "-32602,\"message\":\"\\\"manifest\\\" must be base64 of 1 to"),
        "J7: bad base64 refused");
    CHECK(has(REQ("5", "jurisdiction.install",
                  ",\"params\":{\"manifest\":\"\",\"sig\":\"AA==\",\"gateway_sig\":\"AA==\"}"),
              "-32602,\"message\":\"\\\"manifest\\\" must be base64 of 1 to"),
          "J7: an empty manifest refused as a parameter");
    CHECK(has(REQ("5", "jurisdiction.install", ",\"params\":{\"manifest\":\"AA==\"}"),
              "missing string parameter \\\"sig\\\""),
          "J7: every part required");
    /* J7: installed */
    ju_profile(&pr, "KE", 2, &JK1);
    ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
    r = ju_install_now();
    CHECK(strstr(r, "\"plugins\":1,") &&
              strstr(r, "\"installed\":[{\"id\":\"capctl/KE\",\"version\":2,\"loaded\":true}]}"),
          "J7: a signed capctl profile is a loaded plug-in: %s", r);
    CHECK(has(REQ("6", "books.file", ""), rc3), "J7: one record");
    CHECK(strstr(ju_install_now(), "the manifest's version is not above the installed one"),
          "J8: the same version again refused");
    /* J9: decisions */
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":100,"
                  "\"sender\":1,\"receiver\":2}"),
              "\"base\":\"true\",\"action\":\"proceed\",\"proceed\":true,\"reports\":0}"),
          "J9: an allowed transfer proceeds");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":1}"),
              "\"base\":\"true\""),
          "J9: one minor unit is a transfer");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":\"600\"}"),
              "\"proceed\":true,\"reports\":1}"),
          "J9: a reported transfer proceeds with its report");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":1001}"),
              "\"base\":\"false\",\"action\":\"refuse\",\"proceed\":false,\"reports\":0}"),
          "J9: over the limit refused");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":10}"),
              "\"base\":\"neither\""),
          "J9: an unknown country waits");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"VFV\",\"amount\":10}"),
              "\"base\":\"false\""),
          "J9: a currency with no ISO 4217 number refused");
    CHECK(has(REQ("7", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KEN\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":10}"),
              "-32602") &&
              has(REQ("7", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NGA\",\"ccy\":\"KES\",\"amount\":10}"),
                  "-32602") &&
              has(REQ("7", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KESS\",\"amount\":10}"),
                  "-32602") &&
              has(REQ("7", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":0}"),
                  "\\\"amount\\\" must be above 0") &&
              has(REQ("7", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\"}"),
                  "-32602") &&
              has(REQ("7", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":1,"
                      "\"sender\":-1}"),
                  "-32602"),
          "J9: bad check parameters refused");
    /* J9: tax tables */
    taxhook_table_t tt;
    CHECK(has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
              "{\"code\":-32097,\"message\":\"no plug-in gives this rate\"}"),
          "J9: no tax plug-in, no rate");
    ju_table(&tt, "KE", TAXHOOK_VAT, 160000, 1, &JK1);
    ju_sign(&JP_VAT, JP_VAT.hooks, 1, &JK1, &JK1, true);
    r = ju_install_now();
    CHECK(strstr(r, "\"plugins\":2,") && strstr(r, "{\"id\":\"taxhook/KE//1\",\"version\":1"),
          "J7: a signed VAT table: %s", r);
    EXPECT(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
           OK("8", "{\"kind\":\"vat\",\"num\":\"160000\",\"den\":\"1000000\"}"));
    CHECK(has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"income\"}"),
              "no plug-in gives this rate") &&
              has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"withholding\"}"),
                  "no plug-in gives this rate"),
          "J9: a VAT table does not know income tax");
    CHECK(has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"VAT\"}"), "-32602") &&
              has(REQ("8", "jurisdiction.tax_rate", ""), "-32602"),
          "J9: kind is exact and required");
    ju_table(&tt, "KE", TAXHOOK_GST, 150000, 1, &JK1);
    ju_sign(&JP_GST, JP_GST.hooks, 1, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "\"plugins\":3,"), "a GST table with another rate");
    CHECK(has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
              "{\"code\":-32097,\"message\":\"the plug-ins disagree on the rate\"}"),
          "J9: two rates: refused, escalate");
    ju_table(&tt, "KE", TAXHOOK_GST, 160000, 2, &JK1);
    ju_sign(&JP_GST, JP_GST.hooks, 2, &JK1, &JK1, true);
    r = ju_install_now();
    CHECK(strstr(r, "\"plugins\":3,") && strstr(r, "{\"id\":\"taxhook/KE//2\",\"version\":2"),
          "J7: a newer version replaces in place: %s", r);
    CHECK(has(REQ("8", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
              "\"num\":\"160000\""),
          "and the rates agree");
    /* restart: replayed */
    char before[2000];
    snprintf(before, sizeof before, "%s", rpc(REQ("9", "jurisdiction.get", "")));
    /* F5: a key or an install the file cannot take */
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = (rlim_t) file_size(path);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit the file size");
    CHECK(strstr(ju_set_key(JK2.pk, JURIS_GW_PK_LEN), "\"name\":\"ZXV_E_IO\"") &&
              strstr(OUT.p, "this change is not saved"),
          "F5: a key change that cannot be saved: %s", OUT.p);
    CHECK(!strcmp(rpc(REQ("9", "jurisdiction.get", "")), before), "J6: and it did not apply");
    setrlimit(RLIMIT_FSIZE, &old);
    reopen();
    lim.rlim_cur = (rlim_t) file_size(path);
    CHECK(setrlimit(RLIMIT_FSIZE, &lim) == 0, "limit the file size again");
    ju_table(&tt, "KE", TAXHOOK_GST, 160000, 3, &JK1);
    ju_sign(&JP_GST, JP_GST.hooks, 3, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "\"name\":\"ZXV_E_IO\"") &&
              strstr(OUT.p, "this change is not saved"),
          "F5: an install that cannot be saved: %s", OUT.p);
    CHECK(has(REQ("9", "jurisdiction.get", ""), "{\"id\":\"taxhook/KE//2\",\"version\":3"),
          "F5: it stays for this run only");
    CHECK(strstr(ju_install_now(), "ZXV_E_IO") && strstr(OUT.p, "read-only"),
          "F5: and the books take nothing more: %s", OUT.p);
    setrlimit(RLIMIT_FSIZE, &old);
    reopen();
    snprintf(want, sizeof want, "\"gateway_key\":\"%s\"", kidx);
    r = rpc(REQ("9", "jurisdiction.get", ""));
    CHECK(!strcmp(r, before) && strstr(r, "\"plugins\":3,") && strstr(r, want),
          "J6/J7: a restart replays the key and the plug-ins: %s", r);
    CHECK(has(REQ("9", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":1001}"),
              "\"proceed\":false") &&
              has(REQ("9", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
                  "\"num\":\"160000\""),
          "and they answer as before");
    ju_table(&tt, "KE", TAXHOOK_GST, 160000, 2, &JK1);
    ju_sign(&JP_GST, JP_GST.hooks, 2, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "version is not above"), "J8: the floor survives a restart");
    /* J6: another country unloads them, coming back loads them again */
    r = rpc(REQ("10", "jurisdiction.set", ",\"params\":{\"country\":\"NG\"}"));
    CHECK(strstr(r, "\"plugins\":0,") && strstr(r, "\"loaded\":false}") &&
              !strstr(r, "\"loaded\":true"),
          "J6: another country: installed but not loaded: %s", r);
    ju_profile(&pr, "KE", 3, &JK1);
    ju_sign(&JP_CAP, JP_CAP.hooks, 3, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "the manifest is for another country"),
          "J7: a KE manifest while the country is NG");
    r = rpc(REQ("10", "jurisdiction.set", ",\"params\":{\"country\":\"KE\"}"));
    CHECK(strstr(r, "\"plugins\":3,") && !strstr(r, "\"loaded\":false"),
          "J6: back to KE, loaded again: %s", r);
    r = ju_install_now();
    CHECK(strstr(r, "{\"id\":\"capctl/KE\",\"version\":3,\"loaded\":true}") &&
              strstr(r, "\"plugins\":3,") && !strstr(strstr(r, "capctl/KE") + 1, "capctl/KE"),
          "capctl v3 replaces v2 in its slot: %s", r);
    /* J6: a new key drops every plug-in; the floors stay */
    long n0;
    char *b0 = slurp(path, &n0);
    r = ju_set_key(JK2.pk, JURIS_GW_PK_LEN);
    CHECK(strstr(r, "\"plugins\":0,") && strstr(r, "\"installed\":[]}") && !strstr(r, kidx),
          "J6: a new key drops the plug-ins: %s", r);
    CHECK(has(REQ("11", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":10}"),
              "\"base\":\"neither\""),
          "J6: and the host waits again");
    CHECK(has(REQ("11", "jurisdiction.tax_rate", ",\"params\":{\"kind\":\"vat\"}"),
              "no plug-in gives this rate"),
          "no rate either");
    ju_profile(&pr, "KE", 5, &JK1);
    ju_sign(&JP_CAP, JP_CAP.hooks, 5, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "the gateway signature does not verify"),
          "J6: the old key's manifests no longer verify");
    ju_profile(&pr, "KE", 3, &JK2);
    ju_sign(&JP_CAP, JP_CAP.hooks, 3, &JK2, &JK2, true);
    CHECK(strstr(ju_install_now(), "version is not above"), "J8: the floor survives a key change");
    ju_profile(&pr, "KE", 4, &JK2);
    ju_sign(&JP_CAP, JP_CAP.hooks, 4, &JK2, &JK2, true);
    CHECK(strstr(ju_install_now(), "{\"id\":\"capctl/KE\",\"version\":4,\"loaded\":true}"),
          "J7: v4 under the new key");
    reopen();
    CHECK(has(REQ("12", "jurisdiction.get", ""),
              "\"installed\":[{\"id\":\"capctl/KE\",\"version\":4,\"loaded\":true}]}"),
          "J6: the key change and the reinstall replay in order");
    r = rpc(REQ("12", "jurisdiction.set", ",\"params\":{\"gateway_key\":\"\"}"));
    CHECK(strstr(r, "\"plugins\":0,\"report_standard\":\"\",\"gateway_key\":null,"
                    "\"installed\":[]}"),
          "J6: clearing the key: %s", r);
    reopen();
    CHECK(has(REQ("12", "jurisdiction.get", ""), "\"gateway_key\":null"), "kept cleared");
    /* J7: an install record is judged at its own time: a profile valid then
     * but expired now replays, then fails closed */
    zxvd_rpc_books_close();
    spit(path, b0, n0);
    {
        static char rec[ZXVD_RPC_MAX_LINE];
        static char m64[JB64(sizeof JM)], s64[JB64(CPK_SIG_LEN)], g64[JB64(JURIS_GW_SIG_LEN)];
        ju_profile(&pr, "KE", 9, &JK1);
        pr.hdr.not_before = 1000;
        pr.hdr.not_after = 2000;
        JML = capctl_encode(&pr, JM, sizeof JM);
        ju_sign(&JP_CAP, JP_CAP.hooks, 9, &JK1, &JK1, true);
        ju_b64(JM, JML, m64);
        ju_b64(JSIG, CPK_SIG_LEN, s64);
        ju_b64(JGSIG, JURIS_GW_SIG_LEN, g64);
        snprintf(rec, sizeof rec,
                 "{\"op\":\"juris_install\",\"manifest\":\"%s\",\"sig\":\"%s\",\"gsig\":\"%s\","
                 "\"at\":\"1500\"}",
                 m64, s64, g64);
        append_chained(path, rec);
        CHECK(zxvd_rpc_books_open() == 0, "J7: the record replays at its own time");
        CHECK(has(REQ("13", "jurisdiction.get", ""), "{\"id\":\"capctl/KE\",\"version\":9,"),
              "installed");
        CHECK(has(REQ("13", "jurisdiction.check",
                      ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":10}"),
                  "\"base\":\"false\""),
              "C4: expired now: refused");
        zxvd_rpc_books_close();
        /* damaged records stop the start */
        long n1;
        char *b1 = slurp(path, &n1);
        snprintf(rec, sizeof rec,
                 "{\"op\":\"juris_install\",\"manifest\":\"%s\",\"sig\":\"%s\",\"gsig\":\"%s\","
                 "\"at\":\"1500\"}",
                 m64, s64, g64);
        const char *badrec[] = {
            "{\"op\":\"juris_gw\",\"key\":\"abc\"}",
            "{\"op\":\"juris_gw\",\"key\":\"00\"}",
            "{\"op\":\"juris_gw\"}",
            "{\"op\":\"juris_install\",\"manifest\":\"AA==\",\"sig\":\"AA==\",\"gsig\":\"AA==\"}",
            "{\"op\":\"juris_install\",\"manifest\":\"!\",\"sig\":\"AA==\",\"gsig\":\"AA==\","
            "\"at\":\"1\"}",
            "{\"op\":\"juris_install\",\"manifest\":\"AA==\",\"sig\":\"!\",\"gsig\":\"AA==\","
            "\"at\":\"1\"}",
            "{\"op\":\"juris_install\",\"manifest\":\"AA==\",\"sig\":\"AA==\",\"gsig\":\"!\","
            "\"at\":\"1\"}",
            "{\"op\":\"juris_install\",\"manifest\":\"AA==\",\"sig\":\"AA==\",\"gsig\":\"AA==\","
            "\"at\":\"x\"}",
            rec, /* the same version again: refused by J8 */
        };
        for (unsigned i = 0; i < sizeof badrec / sizeof badrec[0]; i++) {
            spit(path, b1, n1);
            append_chained(path, badrec[i]);
            CHECK(zxvd_rpc_books_open() == -1, "a damaged jurisdiction record %u stops the start",
                  i);
            zxvd_rpc_books_close();
        }
        spit(path, b1, n1);
        free(b1);
    }
    free(b0);
    CHECK(zxvd_rpc_books_open() == 0, "the good file opens again");
    /* without a books file: in memory for this run only */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && zxvd_rpc_books_open() == 0, "in memory");
    CHECK(strstr(ju_set_key(JK1.pk, JURIS_GW_PK_LEN), kidx), "a key without a file");
    rpc(REQ("14", "jurisdiction.set", ",\"params\":{\"country\":\"KE\"}"));
    ju_table(&tt, "KE", TAXHOOK_VAT, 160000, 1, &JK1);
    ju_sign(&JP_VAT, JP_VAT.hooks, 1, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "\"plugins\":1,"), "and an install");
    ju_profile(&pr, "KE", 2, &JK1);
    ju_sign(&JP_CAP, JP_CAP.hooks, 2, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "\"plugins\":2,"), "capctl in the second slot");
    CHECK(has(REQ("14", "jurisdiction.check",
                  ",\"params\":{\"from\":\"KE\",\"to\":\"NG\",\"ccy\":\"KES\",\"amount\":600}"),
              "\"proceed\":true,\"reports\":1}"),
          "J9: the report of a capctl plug-in in any slot");
    r = ju_region_install("NB", 1, &JK1);
    CHECK(strstr(r, "\"plugins\":3,") && strstr(r, "{\"id\":\"taxhook/KE/NB/1\",\"version\":1,"),
          "J7: a regional table of the country: %s", r);
    /* J7: eight slots */
    static const char *const RG[] = {"R1", "R2", "R3", "R4", "R5", "R6", "S1", "S2",
                                     "S3", "S4", "S5", "S6", "S7", "S8", "T1"};
    for (unsigned i = 0; i < 5; i++) {
        snprintf(want, sizeof want, "\"plugins\":%u,", 4 + i);
        CHECK(strstr(ju_region_install(RG[i], 1, &JK1), want), "slot %u", 4 + i);
    }
    CHECK(strstr(ju_region_install(RG[5], 1, &JK1), "no room for another plug-in"),
          "J7: a ninth plug-in refused: %s", OUT.p);
    CHECK(strstr(ju_region_install(RG[0], 2, &JK1), "\"plugins\":8,"),
          "a newer version of an installed one still fits");
    /* J8: sixteen ids have floors; a new key frees the slots, not the floors */
    ju_set_key(JK2.pk, JURIS_GW_PK_LEN);
    for (unsigned i = 6; i < 14; i++) {
        snprintf(want, sizeof want, "\"plugins\":%u,", i - 5);
        CHECK(strstr(ju_region_install(RG[i], 1, &JK2), want), "id %u", i + 3);
    }
    ju_set_key(JK1.pk, JURIS_GW_PK_LEN);
    CHECK(strstr(ju_region_install(RG[14], 1, &JK1), "no room for another plug-in"),
          "J8: a seventeenth id refused: %s", OUT.p);
    CHECK(strstr(ju_region_install(RG[0], 3, &JK1), "\"plugins\":1,"), "a known id still installs");
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0 &&
              has(REQ("14", "jurisdiction.get", ""), "\"gateway_key\":null"),
          "gone after a restart");
    ju_set_key(JK1.pk, JURIS_GW_PK_LEN);
    rpc(REQ("14", "jurisdiction.set", ",\"params\":{\"country\":\"KE\"}"));
    ju_table(&tt, "KE", TAXHOOK_VAT, 160000, 1, &JK1);
    ju_sign(&JP_VAT, JP_VAT.hooks, 1, &JK1, &JK1, true);
    CHECK(strstr(ju_install_now(), "\"plugins\":1,"), "the floors went with the run");
    snprintf(req, sizeof req, "%s/books.zxvb", tmpl);
    unlink(req);
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

/* ---------------- swarm.organ ---------------- */

static void test_organ_method(void)
{
    zxvd_rpc_set_organ_ops(NULL);
    EXPECT(REQ("1", "swarm.organ", ",\"params\":{\"organ\":\"smith\",\"prompt\":\"x\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_organ_ops(&ORGAN_NONE);
    EXPECT(REQ("1", "swarm.organ", ",\"params\":{\"organ\":\"smith\",\"prompt\":\"x\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_organ_ops(&ORGAN_STUB);
    EXPECT(REQ("2", "swarm.organ", ",\"params\":{\"organ\":\"smith\",\"prompt\":\"fix it\"}"),
           OK("2", "{\"organ\":2}"));
    CHECK(last_organ == 2 && !strcmp(last_organ_prompt, "fix it"),
          "swarm.organ passes the organ index and prompt (%u %s)", (unsigned) last_organ,
          last_organ_prompt);
    EXPECT(
        REQ("3", "swarm.organ", ",\"params\":{\"organ\":\"voice_of_tiamat\",\"prompt\":\"hello\"}"),
        OK("3", "{\"organ\":0}"));
    EXPECT(REQ("3", "swarm.organ", ",\"params\":{\"organ\":\"clerk\",\"prompt\":\"hello\"}"),
           OK("3", "{\"organ\":19}"));
    EXPECT(REQ("4", "swarm.organ", ",\"params\":{\"organ\":\"smith\",\"prompt\":\"fail\"}"),
           ERR("4", "-32071", "the organ's budget is spent"));
    int n = organ_asks;
    EXPECT(REQ("5", "swarm.organ", ",\"params\":{\"organ\":\"blacksmith\",\"prompt\":\"x\"}"),
           ERR("5", "-32602", "\\\"organ\\\" is not a Tiamat organ"));
    EXPECT(REQ("5", "swarm.organ", ",\"params\":{\"organ\":\"Smith\",\"prompt\":\"x\"}"),
           ERR("5", "-32602", "\\\"organ\\\" is not a Tiamat organ"));
    EXPECT(REQ("5", "swarm.organ", ",\"params\":{\"prompt\":\"x\"}"),
           ERR("5", "-32602", "missing string parameter \\\"organ\\\""));
    EXPECT(REQ("5", "swarm.organ", ",\"params\":{\"organ\":\"smith\"}"),
           ERR("5", "-32602", "missing string parameter \\\"prompt\\\""));
    CHECK(has(REQ("5", "swarm.organ", ",\"params\":{\"organ\":7,\"prompt\":\"x\"}"), "-32602"),
          "a non-string organ is refused");
    CHECK(organ_asks == n, "bad params never reach the host");
    zxvd_rpc_set_organ_ops(NULL);
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

/* ---------------- people (zxvd_people.c), through m_people ---------------- */

static char people_method[64], people_params[512];
static int people_calls;

static int people_stub(const char *method, const char *params, size_t len, zxvd_buf_t *out,
                       char *msg, size_t msg_cap)
{
    people_calls++;
    snprintf(people_method, sizeof people_method, "%s", method);
    snprintf(people_params, sizeof people_params, "%.*s", (int) len, params);
    if (!strcmp(method, "ui.set")) {
        snprintf(msg, msg_cap, "refused \"why\"");
        zxvd_buf_raw(out, "{\"partial\":", 11); /* a failing call's output is dropped */
        return ZXVD_E_PEOPLE;
    }
    if (!strcmp(method, "xlate.translate")) {
        snprintf(msg, msg_cap, "no model");
        return ZXVD_E_NO_MODEL;
    }
    if (!strcmp(method, "call.status")) return -32602; /* an empty reason */
    zxvd_buf_raw(out, "{\"ok\":1}", 8);
    return 0;
}

static const zxvd_people_ops_t PEOPLE_NONE = {NULL};
static const zxvd_people_ops_t PEOPLE_STUB = {people_stub};

static void test_people_methods(void)
{
    zxvd_rpc_set_people_ops(NULL);
    EXPECT(REQ("1", "social.feed", ""), ERR("1", "-32601", "not available"));
    zxvd_rpc_set_people_ops(&PEOPLE_NONE);
    EXPECT(REQ("1", "ui.get", ""), ERR("1", "-32601", "not available"));
    CHECK(people_calls == 0, "nothing reaches unset people ops");
    zxvd_rpc_set_people_ops(&PEOPLE_STUB);
    EXPECT(REQ("2", "social.feed", ""), OK("2", "{\"ok\":1}"));
    CHECK(!strcmp(people_method, "social.feed") && !strcmp(people_params, "{}"),
          "the method name, and {} for absent params (%s %s)", people_method, people_params);
    EXPECT(REQ("3", "devmesh.join", ",\"params\":{\"code\":\"AB-CD\", \"x\":[1,2]}"),
           OK("3", "{\"ok\":1}"));
    CHECK(!strcmp(people_method, "devmesh.join") &&
              !strcmp(people_params, "{\"code\":\"AB-CD\", \"x\":[1,2]}"),
          "the params object's text, verbatim (%s)", people_params);
    EXPECT(REQ("4", "ui.set", ",\"params\":{}"), ERR("4", "-32030", "refused \\\"why\\\""));
    EXPECT(REQ("5", "xlate.translate", ",\"params\":{}"), ERR("5", "-32031", "no model"));
    EXPECT(REQ("6", "call.status", ""), ERR("6", "-32602", "failed"));
    EXPECT(REQ("7", "ui.get", ",\"params\":[1]"), ERR("7", "-32602", "params must be an object"));
    int n = people_calls;
    EXPECT("{\"jsonrpc\":\"2.0\",\"method\":\"ui.get\"}", "<none>");
    CHECK(people_calls == n + 1, "a notification is executed and gets no reply");
    static const char *const ALL[] = {"social.post",
                                      "social.feed",
                                      "social.groups",
                                      "social.group_create",
                                      "social.join",
                                      "social.leave",
                                      "social.react",
                                      "social.export",
                                      "social.import",
                                      "call.start",
                                      "call.accept",
                                      "call.hangup",
                                      "call.status",
                                      "call.signal",
                                      "xlate.detect",
                                      "xlate.translate",
                                      "xlate.languages",
                                      "devmesh.status",
                                      "devmesh.invite",
                                      "devmesh.join",
                                      "devmesh.confirm_sas",
                                      "devmesh.revoke",
                                      "devmesh.devices",
                                      "ui.get",
                                      "ui.set"};
    unsigned routed = 0;
    for (unsigned i = 0; i < sizeof ALL / sizeof ALL[0]; i++) {
        char req[160];
        snprintf(req, sizeof req, REQ("8", "%s", ""), ALL[i]);
        (void) rpc(req);
        routed += !strcmp(people_method, ALL[i]);
    }
    CHECK(routed == 25, "all 25 people methods reach the ops under their own name (%u)", routed);
    zxvd_rpc_set_people_ops(NULL);
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
    CHECK(has(REQ("1", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
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
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
              "\"x.echo\"]}") &&
              has(REQ("7", "x.need", ""), "method not found"),
          "X1: the count is the new one: %s", OUT.p);
    CHECK(zxvd_rpc_add_methods(TAB, 0) == -1 && zxvd_rpc_add_methods(NULL, 3) == -1,
          "X1: 0 or NULL adds nothing");
    CHECK(has(REQ("8", "rpc.discover", ""),
              "\"aipi.status\",\"features.get\",\"features.set\","
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
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
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
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
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
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
    CHECK(has(REQ("13", "rpc.discover", ""), "\"backup.prune\"," PEOPLE_M "," MODS "]}") &&
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
    CHECK(!has(REQ("2", "phone.link",
                   ",\"params\":{\"on\":true,\"port\":65535,\"bind\":\"8.8.8.8\"}"),
               "must be 0..65535"),
          "port 65535 passes the range check: %s", OUT.p);
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
    static const char *const NEVER[] = {"jurisdiction.set", "jurisdiction.install", "aipi.put",
                                        "aipi.publish", "aipi.execute"};
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
static const zxvd_group_method_t G_DUP_MOD[] = {{"freight.pack", g_status, false}};
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
        {"alpha", false, "", G_DUP_MOD, 1, NULL}, /* a module method's name */
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
              "\"files.root\",\"files.list\",\"files.stat\",\"files.read\",\"files.write\",\"files."
              "mkdir\",\"files.rename\",\"files.trash\",\"files.empty_trash\",\"files.cross\","
              "\"spaces.list\",\"spaces.get\",\"spaces.create\",\"spaces.join\",\"spaces.set_"
              "members\",\"spaces.set_cid\",\"spaces.propose\",\"spaces.vote\",\"phone.link\","
              "\"phone.pair\",\"phone.list\",\"phone.revoke\",\"play.catalogue\",\"play.run\","
              "\"play.draft\"," ZXVD_MINE_TAIL PEOPLE_M "," MODS ","
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
    /* removing one from the middle keeps the rest; its feature stays known */
    zxvd_rpc_remove_group(&gs[5]);
    CHECK(has(REQ("1", "m5.x", ""), "method not found") &&
              !has(REQ("1", "m6.x", ""), "method not found") &&
              !has(REQ("1", "m31.x", ""), "method not found") &&
              !has(REQ("1", "m0.x", ""), "method not found"),
          "the others still answer: %s", OUT.p);
    CHECK(zxvd_rpc_add_group(&gs[ZXVD_EXT_MAX_GROUPS]) == -1, "a 33rd feature is refused");
    CHECK(zxvd_rpc_add_group(&gs[5]) == 0, "a known feature's group comes back");
    zxvd_rpc_groups_reset();
    /* a method a table added already has its name */
    static const zxvd_method_t XT[] = {{"gx.ext", x_echo}};
    static const zxvd_group_method_t G_DUP_EXT[] = {{"gx.ext", g_status, false}};
    const zxvd_group_t dup_ext = {"gx", false, "", G_DUP_EXT, 1, NULL};
    zxvd_rpc_add_methods(XT, 1);
    CHECK(zxvd_rpc_add_group(&dup_ext) == -1, "a table's method name is refused");
    zxvd_rpc_remove_methods(XT);
    CHECK(zxvd_rpc_add_group(&dup_ext) == 0, "and is free once the table goes");
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
        CHECK(books_open_why() == -1 && strstr(WHY, ": bad feature record."),
              "bad feature record %u stops the start: %s", i, WHY);
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
           FERR("2", "not a valid name or path in the Hoard"));
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
    EXPECT(REQ("14", "files.root", ""), FERR("14", "the Hoard cannot be found or made"));
    EXPECT(REQ("14", "files.empty_trash", ""), FERR("14", "the Hoard cannot be found or made"));
    CHECK(zxf_set_root(root) == 0, "root back");
    CHECK(rm_tree(root) == 0, "cleanup");
}

/* ---------------- sys.* and log.* (zxv_sysinfo.h, zxvd_log.h) ---------------- */

static void test_sys_and_log(void)
{
    char root[200], want[260];
    snprintf(root, sizeof root, "%s/zxvd-sys-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) != NULL, "scratch root");
    CHECK(zxf_set_root(root) == 0, "root set");

    zsi_host_t h;
    zsi_self_t me;
    zsi_disk_t d;
    CHECK(zsi_host(&h) == 0 && h.cpus > 0 && h.mem_total > 0 && h.arch[0] && h.os[0],
          "host facts: %u cpus, %llu bytes", h.cpus, (unsigned long long) h.mem_total);
#if defined(__linux__)
    CHECK(!strcmp(h.os, "linux") && h.uptime_s > 0 && h.mem_avail > 0 &&
              h.mem_avail <= h.mem_total && h.load_x100[0] >= 0,
          "linux: uptime, available memory, load");
#elif defined(__APPLE__)
    CHECK(!strcmp(h.os, "macos") && h.uptime_s > 0 && h.mem_avail > 0, "macos facts");
#endif
    CHECK(zsi_self(&me) == 0 && me.pid == (uint64_t) getpid() && me.max_rss_bytes > 0,
          "zxvd itself");
    CHECK(zsi_disk(root, &d) == 0 && d.total > 0 && d.free <= d.total && d.avail <= d.total,
          "the volume");
    CHECK(zsi_disk("/no/such/place/zxv", &d) == -1 && d.total == 0, "no volume");

    snprintf(want, sizeof want, "\"zxvd\":{\"pid\":%llu,", (unsigned long long) getpid());
    CHECK(has(REQ("1", "sys.info", ""), want) && has(REQ("1", "sys.info", ""), "\"cpus\":") &&
              !has(REQ("1", "sys.info", ""), "\"cpus\":0,"),
          "sys.info: %s", OUT.p);
    snprintf(want, sizeof want, "{\"path\":\"%s\",\"total\":", root);
    CHECK(has(REQ("2", "sys.disk", ""), want), "sys.disk: %s", OUT.p);
#if defined(__linux__) || defined(__APPLE__)
    CHECK(has(REQ("3", "sys.net", ""), ",\"supported\":true,\"interfaces\":[") &&
              has(REQ("3", "sys.net", ""), "\"loopback\":true,\"addresses\":[\"127.0.0.1\"") &&
              has(REQ("3", "sys.net", ""), "],\"rpc\":{\"requests\":"),
          "sys.net: %s", OUT.p);
    {
        static zsi_if_t ifs[2];
        uint32_t n = 9;
        CHECK(zsi_interfaces(ifs, 0, &n) == 0 && n == 0, "no room: none listed");
        CHECK(zsi_interfaces(ifs, 2, &n) == 0 && n >= 1 && n <= 2, "listed: %u", n);
    }
#endif

    /* the log: refusals and changes, not polls */
    zxvd_log_reset();
    EXPECT(REQ("4", "spaces.get", ",\"params\":{\"space\":\"nope\"}"),
           ERR("4", "-32013", "no such space"));
    CHECK(has(REQ("4", "files.list", ""), "\"entries\":[]"), "a poll");
    CHECK(has(REQ("4", "spaces.create", ",\"params\":{\"kind\":\"park\",\"name\":\"Green\"}"),
              "\"id\":\"green\""),
          "a change");
    rpc("{\"jsonrpc\":\"2.0\",\"method\":\"nope.nope\"}"); /* a notification: no reply */
    rpc("not json");
    zxvd_traffic_t tr;
    zxvd_traffic_get(&tr);
    CHECK(tr.requests == 5 && tr.errors == 3 && tr.bytes_in > 50 && tr.bytes_out > 50,
          "traffic: %llu requests, %llu errors", (unsigned long long) tr.requests,
          (unsigned long long) tr.errors);
    CHECK(zxvd_log_last() == 4, "four events: %llu", (unsigned long long) zxvd_log_last());
    const char *l = rpc(REQ("5", "log.tail", ""));
    CHECK(
        strstr(l, "{\"seq\":1,\"time_ms\":") &&
            strstr(l, "\"level\":\"warn\",\"source\":\"rpc\",\"text\":\"spaces.get refused "
                      "(-32013): no such space\"}") &&
            strstr(l, "\"level\":\"info\",\"source\":\"rpc\",\"text\":\"spaces.create done in ") &&
            strstr(l, "\"text\":\"(no method) refused (-32601): method not found\"") &&
            strstr(l, "\"text\":\"(no method) refused (-32700): parse error\"") &&
            !strstr(l, "files.list") && strstr(l, "],\"next\":4,\"dropped\":0}"),
        "log.tail: %s", l);
    l = rpc(REQ("5", "log.tail", ",\"params\":{\"level\":\"warn\",\"since\":1}"));
    CHECK(strstr(l, "{\"entries\":[{\"seq\":3,") && !strstr(l, "spaces.create") &&
              strstr(l, "\"next\":4,"),
          "warnings after 1: %s", l);
    l = rpc(REQ("5", "log.tail", ",\"params\":{\"max\":1}"));
    CHECK(strstr(l, "{\"entries\":[{\"seq\":1,") && strstr(l, "],\"next\":1,"), "one: %s", l);
    l = rpc(REQ("5", "log.tail", ",\"params\":{\"level\":\"error\"}"));
    CHECK(strstr(l, "{\"entries\":[],\"next\":"), "no errors: %s", l);
    EXPECT(REQ("5", "log.tail", ",\"params\":{\"max\":0}"),
           ERR("5", "-32602", "\\\"max\\\" must be 1 to 256"));
    EXPECT(REQ("5", "log.tail", ",\"params\":{\"max\":257}"),
           ERR("5", "-32602", "\\\"max\\\" must be 1 to 256"));
    EXPECT(REQ("5", "log.tail", ",\"params\":{\"level\":\"debug\"}"),
           ERR("5", "-32602", "\\\"level\\\" must be info, warn or error"));
    EXPECT(REQ("5", "log.tail", ",\"params\":{\"since\":-1}"),
           ERR("5", "-32602", "\\\"since\\\" must not be negative"));

    /* the ring: old events drop, the reader is told how many */
    zxvd_log_reset();
    for (int i = 0; i < 600; i++) zxvd_log(ZXVD_LOG_INFO, "test", "event %d", i);
    static zxvd_log_entry_t e[4];
    uint64_t dropped;
    CHECK(zxvd_log_tail(0, 4, ZXVD_LOG_INFO, e, &dropped) == 4 && dropped == 88 && e[0].seq == 89 &&
              !strcmp(e[0].text, "event 88") && e[3].seq == 92,
          "dropped %llu, first %llu", (unsigned long long) dropped, (unsigned long long) e[0].seq);
    CHECK(zxvd_log_tail(598, 4, ZXVD_LOG_INFO, e, &dropped) == 2 && dropped == 0 && e[1].seq == 600,
          "the newest");
    CHECK(zxvd_log_tail(600, 4, ZXVD_LOG_INFO, e, &dropped) == 0, "nothing newer");
    /* text: control characters become spaces, long text is cut whole characters */
    zxvd_log_reset();
    zxvd_log(ZXVD_LOG_ERROR, "a-source-longer-than-fifteen", "a\nb\tc\x7f");
    char big[400];
    memset(big, 'x', sizeof big);
    memcpy(big + 158, "\xc3\xa9\xc3\xa9", 4); /* é é across the 160-byte cut */
    big[sizeof big - 1] = 0;
    zxvd_log(ZXVD_LOG_WARN, NULL, "%s", big);
    CHECK(zxvd_log_tail(0, 4, ZXVD_LOG_INFO, e, &dropped) == 2 && !strcmp(e[0].text, "a b c ") &&
              !strcmp(e[0].source, "a-source-longer") && e[0].level == ZXVD_LOG_ERROR &&
              strlen(e[1].text) == 160 && !strcmp(e[1].text + 158, "\xc3\xa9") &&
              e[1].source[0] == 0,
          "clean text: [%s] %zu", e[0].text, strlen(e[1].text));
    CHECK(!strcmp(zxvd_log_level_name(ZXVD_LOG_WARN), "warn") &&
              !strcmp(zxvd_log_level_name((zxvd_log_level_t) 7), "error"),
          "level names");
    uint64_t a = zxvd_mono_us(), b = zxvd_mono_us();
    CHECK(b >= a && zxvd_now_ms() > 1700000000000ull, "clocks");

    CHECK(rm_tree(root) == 0, "cleanup");
}

/* well-formed JSON, so a stray or missing comma is caught */
static bool json_ok(const char *s)
{
    static w4_jtok_t t[4096];
    return w4_json_parse(s, (uint32_t) strlen(s), t, 4096, 16) > 0;
}

static void test_sys_shapes(void)
{
    char want[512];
    /* load: the same three numbers zsi_host reads, or null; retried while it moves */
    bool load_ok = false;
    for (int tries = 0; tries < 5 && !load_ok; tries++) {
        zsi_host_t a, b;
        zsi_host(&a);
        const char *r = rpc(REQ("1", "sys.info", ""));
        zsi_host(&b);
        if (memcmp(a.load_x100, b.load_x100, sizeof a.load_x100)) continue;
        if (a.load_x100[0] < 0)
            snprintf(want, sizeof want, ",\"load\":null,");
        else
            snprintf(want, sizeof want, ",\"load\":[%d,%d,%d],", (int) a.load_x100[0],
                     (int) a.load_x100[1], (int) a.load_x100[2]);
        load_ok = strstr(r, want) && json_ok(r);
    }
    CHECK(load_ok, "sys.info load: %s", OUT.p);
    CHECK(json_ok(rpc(REQ("1", "sys.disk", ""))) || strstr(OUT.p, "\"error\""), "sys.disk JSON");

#if defined(__linux__) || defined(__APPLE__)
    /* sys.net: every interface, in order, with every address */
    static zsi_if_t ifs[32];
    uint32_t n = 0;
    CHECK(zsi_interfaces(ifs, 32, &n) == 0 && n >= 1, "interfaces: %u", n);
    const char *r = rpc(REQ("2", "sys.net", ""));
    CHECK(json_ok(r), "sys.net is JSON: %s", r);
    const char *at = r;
    uint32_t names = 0;
    for (const char *q = r; (q = strstr(q, "{\"name\":")); q++) names++;
    CHECK(names == n, "%u interfaces listed, %u known", names, n);
    for (uint32_t i = 0; i < n; i++) {
        int k =
            snprintf(want, sizeof want,
                     "%s{\"name\":\"%s\",\"up\":%s,\"loopback\":%s,\"addresses\":[", i ? "," : "[",
                     ifs[i].name, ifs[i].up ? "true" : "false", ifs[i].loopback ? "true" : "false");
        for (uint32_t a = 0; a < ifs[i].naddr; a++)
            catf(want, sizeof want, &k, "%s\"%s\"", a ? "," : "", ifs[i].addr[a]);
        snprintf(want + k, sizeof want - (size_t) k, "],\"rx_bytes\":%s",
                 ifs[i].counters ? "" : "null");
        const char *f = strstr(at, want);
        CHECK(f != NULL, "interface %u in order: %s", i, want);
        if (f) at = f + 1;
    }
#endif

    /* traffic: a notification sends nothing back; busy time is a duration */
    zxvd_traffic_t t0, t1;
    zxvd_traffic_get(&t0);
    rpc("{\"jsonrpc\":\"2.0\",\"method\":\"files.list\"}");
    zxvd_traffic_get(&t1);
    CHECK(t1.requests == t0.requests + 1 && t1.bytes_out == t0.bytes_out &&
              t1.bytes_in > t0.bytes_in && t1.busy_us - t0.busy_us < 10000000u,
          "a notification: %llu bytes out, %llu us",
          (unsigned long long) (t1.bytes_out - t0.bytes_out),
          (unsigned long long) (t1.busy_us - t0.busy_us));

    /* "done in N ms" counts milliseconds */
    zxvd_log_reset();
    rpc(REQ("3", "spaces.create", ",\"params\":{\"kind\":\"park\",\"name\":\"Shapes\"}"));
    static zxvd_log_entry_t e[2];
    uint64_t dropped;
    unsigned long ms = 99999;
    CHECK(zxvd_log_tail(0, 2, ZXVD_LOG_INFO, e, &dropped) == 1 &&
              sscanf(e[0].text, "spaces.create done in %lu ms", &ms) == 1 && ms < 1000,
          "duration: %s", e[0].text);

    /* log.tail: 100 by default, 256 at most */
    zxvd_log_reset();
    for (int i = 0; i < 300; i++) zxvd_log(ZXVD_LOG_INFO, "test", "event %d", i);
    const char *l = rpc(REQ("4", "log.tail", ""));
    CHECK(strstr(l, "{\"seq\":100,") && !strstr(l, "{\"seq\":101,") && strstr(l, "\"next\":100,") &&
              json_ok(l),
          "default 100: %.80s", l);
    l = rpc(REQ("4", "log.tail", ",\"params\":{\"max\":256}"));
    CHECK(strstr(l, "{\"seq\":256,") && !strstr(l, "{\"seq\":257,") && strstr(l, "\"next\":256,"),
          "max 256: %.80s", l);
    /* a filter that skips the rest: next is the last event held */
    zxvd_log(ZXVD_LOG_WARN, "test", "the one warning");
    zxvd_log(ZXVD_LOG_INFO, "test", "after it");
    l = rpc(REQ("4", "log.tail", ",\"params\":{\"level\":\"warn\",\"max\":5}"));
    CHECK(strstr(l, "{\"entries\":[{\"seq\":301,") && strstr(l, "],\"next\":302,"), "skip: %s", l);
    l = rpc(REQ("4", "log.tail", ",\"params\":{\"since\":900}"));
    CHECK(strstr(l, "{\"entries\":[],\"next\":900,"), "since ahead: %s", l);
}

static void put(const char *path, const char *text, size_t n)
{
    uint64_t size;
    CHECK(zxf_write(path, (const uint8_t *) text, (uint32_t) n, ZXF_W_REPLACE, false, &size) == 0,
          "write %s", path);
}
#define PUT(p, lit) put(p, lit, sizeof lit - 1)

static void test_search(void)
{
    char root[200];
    snprintf(root, sizeof root, "%s/zxvd-search-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) != NULL, "scratch root");
    CHECK(zxf_set_root(root) == 0 && zxf_use_space("home") == 0, "root set");
    zsr_reset();
    CHECK(zxf_mkdir("notes") == 0 && zxf_mkdir("photos") == 0, "folders");
    PUT("notes/harvest.md", "The Harvest moon rises\tover the vineyard.\nharvest, HARVEST!");
    PUT("notes/recipe.txt", "Bread: flour, water, salt.");
    PUT("photos/pic.png", "\x89PNG\r\n\x1a\n\0\0\0\rIHDR harvest");
    PUT("Vineyard plan.txt", "rows of grapes, caf\xc3\xa9 by the gate");
    PUT("old.txt", "harvest of last year");
    char trashed[ZXF_PATH_MAX + 1];
    CHECK(zxf_trash("old.txt", trashed, sizeof trashed) == 0, "into the Trash");

    zsr_stats_t st;
    CHECK(zsr_refresh(&st) == 0 && st.files == 4 && st.text_files == 3 && st.added == 4 &&
              st.changed == 0 && st.removed == 0 && st.left_out == 0,
          "first refresh: %u files, %u text, %u added", st.files, st.text_files, st.added);

    static zsr_hit_t h[ZSR_HITS_MAX];
    uint32_t n, total;
    /* a prefix, in a name and the text; the Trash and binary text are not searched */
    CHECK(
        zsr_query("harv", h, 10, &n, &total) == 0 && n == 1 && total == 1 &&
            !strcmp(h[0].path, "notes/harvest.md") && h[0].in_name && h[0].score == 1003 &&
            !strcmp(h[0].snippet, "The Harvest moon rises over the vineyard. harvest, HARVEST!") &&
            h[0].size == 59,
        "harv: %u %u %s %u [%s]", n, total, n ? h[0].path : "", n ? h[0].score : 0,
        n ? h[0].snippet : "");
    /* a name ranks above the text */
    CHECK(zsr_query("Vineyard", h, 10, &n, &total) == 0 && n == 2 && total == 2 &&
              !strcmp(h[0].path, "Vineyard plan.txt") && h[0].in_name && h[0].score == 1000 &&
              h[0].snippet[0] == 0 && !strcmp(h[1].path, "notes/harvest.md") && !h[1].in_name &&
              h[1].score == 1 && strstr(h[1].snippet, "vineyard."),
          "vineyard: %u %s %u", n, n ? h[0].path : "", n ? h[0].score : 0);
    CHECK(zsr_query("vineyard", h, 1, &n, &total) == 0 && n == 1 && total == 2 &&
              !strcmp(h[0].path, "Vineyard plan.txt"),
          "the best one");
    /* a folder's name */
    CHECK(zsr_query("notes", h, 10, &n, &total) == 0 && n == 2 && h[0].score == 100 &&
              !strcmp(h[0].path, "notes/harvest.md") && !strcmp(h[1].path, "notes/recipe.txt") &&
              !h[0].in_name,
          "notes: %u", n);
    /* every term */
    CHECK(zsr_query("flour SALT", h, 10, &n, &total) == 0 && n == 1 && h[0].score == 2 &&
              !strcmp(h[0].snippet, "Bread: flour, water, salt."),
          "flour salt: %u [%s]", n, n ? h[0].snippet : "");
    CHECK(zsr_query("flour grapes", h, 10, &n, &total) == 0 && n == 0 && total == 0, "not both");
    CHECK(zsr_query("pic", h, 10, &n, &total) == 0 && n == 1 &&
              !strcmp(h[0].path, "photos/pic.png"),
          "a picture by name");
    CHECK(zsr_query("ihdr", h, 10, &n, &total) == 0 && n == 0, "binary not read");
    CHECK(zsr_query("caf\xc3\xa9", h, 10, &n, &total) == 0 && n == 1 && h[0].score == 1 &&
              !strcmp(h[0].path, "Vineyard plan.txt"),
          "UTF-8 words: %u", n);
    CHECK(zsr_query("m", h, 10, &n, &total) == 0 && n == 1 &&
              !strcmp(h[0].path, "notes/harvest.md"),
          "one letter: names only");
    CHECK(zsr_query("", h, 10, &n, &total) == ZXF_E_PATH &&
              zsr_query(" ,; ", h, 10, &n, &total) == ZXF_E_PATH,
          "no words");
    CHECK(zsr_query("a b c d e f g h", h, 10, &n, &total) == 0 &&
              zsr_query("a b c d e f g h i", h, 10, &n, &total) == ZXF_E_PATH,
          "eight words at most");
    {
        char big[ZSR_QUERY_MAX + 2];
        memset(big, 'a', sizeof big);
        big[ZSR_QUERY_MAX] = 0;
        CHECK(zsr_query(big, h, 10, &n, &total) == 0, "256 bytes");
        big[ZSR_QUERY_MAX] = 'a';
        big[ZSR_QUERY_MAX + 1] = 0;
        CHECK(zsr_query(big, h, 10, &n, &total) == ZXF_E_PATH, "257 bytes");
    }

    /* long words: prefixes to 24 bytes, and the whole word */
    PUT("long.txt", "x abcdefghijklmnopqrstuvwxyz0123 y");
    CHECK(zsr_refresh(&st) == 0 && st.added == 1 && st.files == 5 && st.changed == 0, "one added");
    CHECK(zsr_query("abcdefghijklmnopqrstuvwx", h, 10, &n, &total) == 0 && n == 1 &&
              zsr_query("abcdefghijklmnopqrstuvwxyz0123", h, 10, &n, &total) == 0 && n == 1 &&
              zsr_query("abcdefghijklmnopqrstuvwxy", h, 10, &n, &total) == 0 && n == 0,
          "long words");
    /* changes: only what changed is read again */
    PUT("notes/recipe.txt", "Rye bread: rye flour, water, salt, caraway.");
    CHECK(zxf_trash("long.txt", trashed, sizeof trashed) == 0, "trash one");
    CHECK(zsr_refresh(&st) == 0 && st.changed == 1 && st.removed == 1 && st.added == 0 &&
              st.files == 4,
          "changed %u removed %u added %u files %u", st.changed, st.removed, st.added, st.files);
    CHECK(zsr_query("caraway", h, 10, &n, &total) == 0 && n == 1 &&
              !strcmp(h[0].snippet, "Rye bread: rye flour, water, salt, caraway."),
          "the new text");
    CHECK(zsr_refresh(&st) == 0 && st.changed == 0 && st.removed == 0 && st.added == 0,
          "nothing new");

    /* the snippet: from up to 40 bytes before, on whole characters, 120 at most */
    {
        char t[400];
        int k = snprintf(t, sizeof t, "%s", "\xc3\xa9");
        for (int i = 0; i < 25; i++) catf(t, sizeof t, &k, "ab ");
        catf(t, sizeof t, &k, "needle");
        for (int i = 0; i < 60; i++) catf(t, sizeof t, &k, "\xc3\xa9");
        put("snip.txt", t, (size_t) k);
        CHECK(zsr_refresh(&st) == 0 && zsr_query("needle", h, 10, &n, &total) == 0 && n == 1,
              "needle");
        CHECK(!strncmp(h[0].snippet, " ab ab", 6) &&
                  strstr(h[0].snippet, "needle") == h[0].snippet + 40 &&
                  strlen(h[0].snippet) == 120,
              "snippet [%s] %zu", h[0].snippet, strlen(h[0].snippet));
        /* the first hit starts a word: "eedle" alone does not */
        PUT("snip2.txt", "needles needle");
        CHECK(zsr_refresh(&st) == 0 && zsr_query("needle", h, 10, &n, &total) == 0 && n == 2 &&
                  !strcmp(h[0].path, "snip2.txt") && h[0].score == 2 &&
                  !strcmp(h[0].snippet, "needles needle") && !strcmp(h[1].path, "snip.txt"),
              "two: [%s]", n ? h[0].snippet : "");
        CHECK(zsr_query("eedle", h, 10, &n, &total) == 0 && n == 0, "mid-word is not a word");
    }
    /* a hit far into a long file, across a read's cut */
    {
        static char big[ZXF_READ_MAX + 64];
        memset(big, 'z', sizeof big);
        for (size_t i = 7; i < sizeof big; i += 8) big[i] = ' ';
        memcpy(big + ZXF_READ_MAX - 3, " far", 4);
        put("big.txt", big, sizeof big);
        CHECK(zsr_refresh(&st) == 0 && zsr_query("far", h, 10, &n, &total) == 0 && n == 1 &&
                  strstr(h[0].snippet, "far"),
              "far: %u [%.20s]", n, n ? h[0].snippet : "");
    }

    /* too deep: left out */
    {
        char d[200] = "";
        for (int i = 0; i < 33; i++) {
            strcat(d, i ? "/d" : "d");
            CHECK(zxf_mkdir(d) == 0, "deep %d", i);
        }
        CHECK(zsr_refresh(&st) == 0 && st.left_out == 1, "left out: %u", st.left_out);
    }

    /* each space its own index; four kept, the least used goes */
    uint32_t files_home = st.files;
    const char *sp[] = {"s1", "s2", "s3", "s4"};
    for (int i = 0; i < 4; i++) {
        CHECK(zxf_use_space(sp[i]) == 0, "space");
        zsr_refresh(&st);
        CHECK(zsr_query("harvest", h, 10, &n, &total) == 0 && total == 0, "not in %s", sp[i]);
        if (i == 1) {
            zxf_use_space("home"); /* home used again: s1 is now the least used */
            CHECK(zsr_query("harvest", h, 10, &n, &total) == 0 && total == 1, "home kept");
        }
    }
    zxf_use_space("home");
    CHECK(zsr_query("harvest", h, 10, &n, &total) == 0 && total == 1, "home still kept");
    zxf_use_space("s1");
    CHECK(zsr_query("harvest", h, 10, &n, &total) == 0 && total == 0, "s1 gone");
    zxf_use_space("home");
    CHECK(zsr_refresh(&st) == 0 && st.files == files_home && st.added == 0, "home: %u", st.added);

    /* over zxvd */
    zxvd_log_reset();
    const char *r = rpc(REQ("1", "search.query", ",\"params\":{\"q\":\"harv\"}"));
    CHECK(
        strstr(r, "{\"space\":\"home\",\"hits\":[{\"path\":\"notes/"
                  "harvest.md\",\"size\":59,\"mtime\":") &&
            strstr(r,
                   ",\"in_name\":true,\"score\":1003,\"snippet\":\"The Harvest moon rises over the "
                   "vineyard. harvest, HARVEST!\"}],\"total\":1}") &&
            json_ok(r),
        "search.query: %s", r);
    r = rpc(REQ("1", "search.query", ",\"params\":{\"q\":\"needle\",\"max\":1,\"refresh\":false}"));
    CHECK(strstr(r, "\"hits\":[{\"path\":\"snip2.txt\",") && strstr(r, "}],\"total\":2}"),
          "max 1: %s", r);
    r = rpc(REQ("1", "search.query", ",\"params\":{\"q\":\"needle\",\"max\":100}"));
    CHECK(strstr(r, "\"path\":\"snip2.txt\",") && strstr(r, "},{\"path\":\"snip.txt\","), "two: %s",
          r);
    r = rpc(REQ("1", "search.refresh", ""));
    CHECK(strstr(r, "{\"space\":\"home\",\"files\":") &&
              strstr(r, ",\"added\":0,\"changed\":0,\"removed\":0,\"left_out\":1}"),
          "search.refresh: %s", r);
    EXPECT(REQ("1", "search.query", ",\"params\":{\"q\":\"x\",\"max\":0}"),
           ERR("1", "-32602", "\\\"max\\\" must be 1 to 100"));
    EXPECT(REQ("1", "search.query", ",\"params\":{\"q\":\"x\",\"max\":101}"),
           ERR("1", "-32602", "\\\"max\\\" must be 1 to 100"));
    EXPECT(REQ("1", "search.query", ",\"params\":{\"q\":\"--\"}"),
           ERR("1", "-32602", "\\\"q\\\" needs 1 to 8 words"));
    EXPECT(REQ("1", "search.query", ",\"params\":{\"q\":\"x\",\"refresh\":1}"),
           ERR("1", "-32602", "\\\"refresh\\\" must be true or false"));
    CHECK(has(REQ("1", "search.query", ""), "-32602"), "q required: %s", OUT.p);
    CHECK(has(REQ("1", "search.query", ",\"params\":{\"q\":\"x\",\"space\":\"nope\"}"),
              "no such space"),
          "space: %s", OUT.p);
    CHECK(has(REQ("1", "search.refresh", ",\"params\":{\"space\":\"nope\"}"), "no such space"),
          "space: %s", OUT.p);
    zxvd_log_reset();
    rpc(REQ("1", "search.query", ",\"params\":{\"q\":\"harv\"}"));
    rpc(REQ("1", "search.refresh", ""));
    rpc(REQ("1", "rpc.discover", ""));
    CHECK(zxvd_log_last() == 0, "searches and discovery are not logged: %llu",
          (unsigned long long) zxvd_log_last());

    zsr_reset();
    CHECK(zsr_query("harvest", h, 10, &n, &total) == 0 && total == 0, "reset");
    CHECK(rm_tree(root) == 0, "cleanup");
}

static void test_print(void)
{
    char root[200], err[ZPH_ERR], uri[128];
    snprintf(root, sizeof root, "%s/zxvd-print-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) != NULL, "scratch root");
    CHECK(zxf_set_root(root) == 0 && zxf_use_space("home") == 0, "root set");
    CHECK(fake_start(), "the fake printer and scanner");
    zph_set_timeout_ms(3000);

    /* formats by extension */
    CHECK(!strcmp(zph_format_of("a.PDF"), "application/pdf") &&
              !strcmp(zph_format_of("x/b.jpeg"), "image/jpeg") &&
              !strcmp(zph_format_of("c.Jpg"), "image/jpeg") &&
              !strcmp(zph_format_of("d.png"), "image/png") &&
              !strcmp(zph_format_of("e.pwg"), "image/pwg-raster") &&
              !strcmp(zph_format_of("f.urf"), "image/urf") &&
              !strcmp(zph_format_of("g.txt"), "text/plain") && !zph_format_of("h") &&
              !zph_format_of("i.pd") && !zph_format_of("j.pdfx") && !zph_format_of("k.d/l") &&
              !zph_format_of("m."),
          "formats");

    /* the printer */
    ipp_printer_t p;
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_OK && !strcmp(p.name, "Fake") &&
              p.state == IPP_PRINTER_IDLE && p.nformats == 2 && p.accepting && p.color,
          "printer: %s", err);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/ipp/chunked", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_OK && !strcmp(p.make, "Fake Printer 1"),
          "chunked answer: %s", err);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/continue", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_OK && !strcmp(p.name, "Fake"),
          "after 100 Continue: %s", err);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/refuse", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_E_HTTP && strstr(err, "IPP status 0x0400"),
          "refused: %s", err);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/garbage", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_E_PROTO && strstr(err, "not IPP"),
          "garbage: %s", err);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/500", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_E_HTTP && strstr(err, "HTTP 500"), "500: %s",
          err);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/close", FAKE_PORT);
    CHECK(zph_printer(uri, &p, err, sizeof err) == ZPH_E_PROTO && strstr(err, "not HTTP"),
          "closed: %s", err);

    /* addresses (N1, N2) */
    static const struct {
        const char *uri;
        int rc;
        const char *says;
    } A[] = {
        {"ipps://127.0.0.1/ipp", ZPH_E_URI, "secure"},
        {"https://127.0.0.1/ipp", ZPH_E_URI, "secure"},
        {"ftp://127.0.0.1/ipp", ZPH_E_URI, "ipp://"},
        {"ipp://", ZPH_E_URI, "no host"},
        {"ipp://:631/x", ZPH_E_URI, "no host"},
        {"ipp://user@127.0.0.1/x", ZPH_E_URI, "plain name"},
        {"ipp://127.0.0.1:0/x", ZPH_E_URI, "port"},
        {"ipp://127.0.0.1:65536/x", ZPH_E_URI, "port"},
        {"ipp://127.0.0.1:/x", ZPH_E_URI, "port"},
        {"ipp://127.0.0.1:12a/x", ZPH_E_URI, "port"},
        {"ipp://127.0.0.1:1234567/x", ZPH_E_URI, "port"},
        {"ipp://[::1/x", ZPH_E_URI, "closing ]"},
        {"ipp://[::1]x/x", ZPH_E_URI, "host[:port]"},
        {"ipp://127.0.0.1/a b", ZPH_E_URI, "space"},
        {"ipp://8.8.8.8/ipp/print", ZPH_E_LOCAL, "local network"},
        {"ipp://[2001:4860:4860::8888]/ipp", ZPH_E_LOCAL, "local network"},
        {"ipp://[::ffff:8.8.8.8]/ipp", ZPH_E_LOCAL, "local network"},
        {"ipp://no-such-host.invalid/ipp", ZPH_E_NET, "could not be found"},
    };
    for (size_t i = 0; i < sizeof A / sizeof *A; i++)
        CHECK(zph_printer(A[i].uri, &p, err, sizeof err) == A[i].rc && strstr(err, A[i].says),
              "%s: %s", A[i].uri, err);
    {
        char longu[ZPH_URI_MAX + 8];
        memset(longu, 'a', sizeof longu - 1);
        longu[sizeof longu - 1] = 0;
        memcpy(longu, "ipp://", 6);
        CHECK(zph_printer(longu, &p, err, sizeof err) == ZPH_E_URI && strstr(err, "too long"),
              "long");
    }
    /* private ranges pass N2 and fail to connect quickly (a closed port) */
    zph_set_timeout_ms(300);
    CHECK(zph_printer("ipp://127.0.0.1:1/ipp", &p, err, sizeof err) == ZPH_E_NET &&
              strstr(err, "did not answer"),
          "closed port: %s", err);
    zph_set_timeout_ms(3000);
    CHECK(zph_printer("http://127.0.0.1:1/ipp", &p, err, 0) == ZPH_E_NET, "no room for why");

    /* printing a file from the ZXV folder */
    uint64_t size;
    CHECK(zxf_write("Plan.pdf", (const uint8_t *) "%PDF-1.4 fake", 13, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              zxf_write("notes.txt", (const uint8_t *) "hi", 2, ZXF_W_REPLACE, false, &size) == 0 &&
              zxf_mkdir("Folder") == 0,
          "files");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    ipp_job_opts_t o = {NULL, NULL, 2, "one-sided", NULL, NULL};
    ipp_job_t j;
    CHECK(zph_print(uri, "Plan.pdf", &o, &j, err, sizeof err) == ZPH_OK && j.id == 77 &&
              j.state == IPP_JOB_PENDING,
          "printed: %s", err);
    CHECK(
        !strcmp(fake_get("/fake/last"),
                "print fmt=application/pdf name=Plan.pdf copies=2 sides=one-sided doc=13:%PDF-1.4"),
        "the printer got: %s", fake_get("/fake/last"));
    CHECK(
        zph_print(uri, "notes.txt", &o, &j, err, sizeof err) == ZPH_E_FORMAT &&
            !strcmp(
                err,
                "the printer does not take text/plain files; it takes application/pdf, image/jpeg"),
        "format: %s", err);
    CHECK(zxf_write("noext", (const uint8_t *) "x", 1, ZXF_W_REPLACE, false, &size) == 0 &&
              zph_print(uri, "noext", &o, &j, err, sizeof err) == ZPH_E_FORMAT &&
              strstr(err, "does not take these files"),
          "no extension: %s", err);
    CHECK(zph_print(uri, "missing.pdf", &o, &j, err, sizeof err) == ZPH_E_FILE &&
              strstr(err, "the file: "),
          "missing: %s", err);
    CHECK(zph_print(uri, "Folder", &o, &j, err, sizeof err) == ZPH_E_FILE && strstr(err, "folder"),
          "a folder: %s", err);
    CHECK(zph_print("ftp://x", "Plan.pdf", &o, &j, err, sizeof err) == ZPH_E_URI, "bad uri");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/refuse", FAKE_PORT);
    CHECK(zph_print(uri, "Plan.pdf", &o, &j, err, sizeof err) == ZPH_E_HTTP, "refused printer");
    /* jobs */
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    CHECK(zph_job(uri, 77, false, &j, err, sizeof err) == ZPH_OK && j.state == IPP_JOB_COMPLETED &&
              j.pages == 2,
          "job: %s", err);
    CHECK(zph_job(uri, 77, true, &j, err, sizeof err) == ZPH_OK && j.state == IPP_JOB_CANCELED,
          "cancel: %s", err);
    CHECK(zph_job(uri, 5, false, &j, err, sizeof err) == ZPH_E_HTTP &&
              !strcmp(err, "the printer refused (IPP status 0x0406): no such job"),
          "no such job: %s", err);
    CHECK(zph_job("ipps://h/x", 5, false, &j, err, sizeof err) == ZPH_E_URI, "bad uri");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/garbage", FAKE_PORT);
    CHECK(zph_job(uri, 77, false, &j, err, sizeof err) == ZPH_E_PROTO, "garbage job");

    /* scanning */
    escl_caps_t k;
    escl_status_t st;
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/eSCL", FAKE_PORT);
    CHECK(zph_scanner(uri, &k, &st, err, sizeof err) == ZPH_OK && !strcmp(k.make, "Fake Scanner") &&
              k.ndpi == 2 && !strcmp(st.state, "Idle"),
          "scanner: %s", err);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/eSCL/", FAKE_PORT);
    CHECK(zph_scanner(uri, &k, &st, err, sizeof err) == ZPH_OK, "a trailing slash");
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/nowhere", FAKE_PORT);
    CHECK(zph_scanner(uri, &k, &st, err, sizeof err) == ZPH_E_HTTP && strstr(err, "404"), "404: %s",
          err);
    CHECK(zph_scanner("ipp://127.0.0.1/eSCL", &k, &st, err, sizeof err) == ZPH_E_URI &&
              strstr(err, "starts http://"),
          "a scanner is http: %s", err);
    static char saved[ZPH_PAGES_MAX][ZXF_PATH_MAX + 1];
    uint32_t pages = 9;
    escl_settings_t s = {ESCL_SRC_FEEDER, ESCL_COLOR_RGB, 600, 0, 0, "image/jpeg"};
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/eSCL", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "Folder", saved, ZPH_PAGES_MAX, &pages, err, sizeof err) == ZPH_OK &&
              pages == 2 && !strcmp(saved[0], "Folder/Scan 1.jpg") &&
              !strcmp(saved[1], "Folder/Scan 2.jpg"),
          "scanned %u: %s", pages, err);
    zxf_entry_t e;
    CHECK(zxf_stat("Folder/Scan 1.jpg", &e) == 0 && e.size == 3000 &&
              zxf_stat("Folder/Scan 2.jpg", &e) == 0 && e.size == 10,
          "pages saved whole");
    CHECK(!strcmp(fake_get("/fake/last"), "scan dpi=600< src=Feeder"), "settings sent: %s",
          fake_get("/fake/last"));
    s.format = "application/pdf";
    s.source = ESCL_SRC_PLATEN;
    CHECK(zph_scan(uri, &s, "", saved, 1, &pages, err, sizeof err) == ZPH_OK && pages == 1 &&
              !strcmp(saved[0], "Scan 1.pdf"),
          "one page wanted, at the top: %u %s", pages, pages ? saved[0] : "");
    CHECK(zph_scan(uri, &s, "Folder", saved, 0, &pages, err, sizeof err) == ZPH_OK && pages == 0,
          "no room for pages");
    s.format = "image/png";
    CHECK(zph_scan(uri, &s, "Folder", saved, ZPH_PAGES_MAX, &pages, err, sizeof err) == ZPH_OK &&
              pages == 2 && !strcmp(saved[1], "Folder/Scan 2.png"),
          "png");
    s.dpi = 5;
    CHECK(zph_scan(uri, &s, "Folder", saved, 4, &pages, err, sizeof err) == ZPH_E_URI &&
              strstr(err, "out of range"),
          "bad settings");
    s.dpi = 300;
    CHECK(zph_scan(uri, &s, "Plan.pdf", saved, 4, &pages, err, sizeof err) == ZPH_E_FILE &&
              strstr(err, "it is a file"),
          "not a folder: %s", err);
    CHECK(zph_scan(uri, &s, "Nope", saved, 4, &pages, err, sizeof err) == ZPH_E_FILE, "no folder");
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/busy", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_HTTP &&
              strstr(err, "busy"),
          "busy: %s", err);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/nopaper", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_HTTP &&
              strstr(err, "paper"),
          "no paper: %s", err);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/noloc", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_PROTO &&
              strstr(err, "where"),
          "no location: %s", err);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/nowhere", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_HTTP, "404 job");
    CHECK(zph_scan("ftp://x", &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_URI, "bad url");

    /* over zxvd */
    char req[512];
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.status\",\"params\":{\"uri\":"
             "\"ipp://127.0.0.1:%d/ipp/print\"}}",
             FAKE_PORT);
    const char *r = rpc(req);
    CHECK(strstr(r,
                 "{\"name\":\"Fake\",\"info\":\"\",\"make\":\"Fake Printer 1\",\"location\":\"\","
                 "\"state\":\"idle\",\"reasons\":\"none\",\"accepting\":true,\"color\":true,"
                 "\"queued\":0,\"copies_max\":0,\"formats\":[\"application/pdf\",\"image/jpeg\"],"
                 "\"media\":[\"iso_a4_210x297mm\"],\"media_default\":\"iso_a4_210x297mm\","
                 "\"sides\":[\"one-sided\"]}"),
          "print.status: %s", r);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.submit\",\"params\":{\"uri\":"
             "\"ipp://127.0.0.1:%d/ipp/print\",\"path\":\"Plan.pdf\",\"copies\":3,\"color\":"
             "\"monochrome\",\"sides\":\"two-sided-short-edge\",\"media\":\"na_letter_8.5x11in\","
             "\"job_name\":\"The plan\"}}",
             FAKE_PORT);
    r = rpc(req);
    CHECK(strstr(r, "{\"job_id\":77,\"state\":\"pending\",\"reasons\":\"none\",\"message\":\"\","
                    "\"pages\":-1}"),
          "print.submit: %s", r);
    CHECK(!strcmp(fake_get("/fake/last"), "print fmt=application/pdf name=The plan copies=3 "
                                          "sides=two-sided-short-edge doc=13:%PDF-1.4"),
          "the printer got: %s", fake_get("/fake/last"));
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.job\",\"params\":{\"uri\":"
             "\"ipp://127.0.0.1:%d/ipp/print\",\"job_id\":77}}",
             FAKE_PORT);
    r = rpc(req);
    CHECK(strstr(r, "{\"job_id\":77,\"state\":\"completed\",\"reasons\":\"\",\"message\":\"\","
                    "\"pages\":2}"),
          "print.job: %s", r);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.cancel\",\"params\":{\"uri\":"
             "\"ipp://127.0.0.1:%d/ipp/print\",\"job_id\":77}}",
             FAKE_PORT);
    CHECK(strstr(rpc(req), "\"state\":\"canceled\""), "print.cancel: %s", OUT.p);
    EXPECT(REQ("1", "print.status", ",\"params\":{\"uri\":\"ipp://8.8.8.8/p\"}"),
           ERR("1", "-32022", "the device is not on the local network"));
    EXPECT(REQ("1", "print.job", ",\"params\":{\"uri\":\"ipp://h/p\",\"job_id\":0}"),
           ERR("1", "-32602", "\\\"job_id\\\" must be 1 to 2147483647"));
    EXPECT(REQ("1", "print.job", ",\"params\":{\"uri\":\"ipp://h/p\",\"job_id\":2147483648}"),
           ERR("1", "-32602", "\\\"job_id\\\" must be 1 to 2147483647"));
    CHECK(has(REQ("1", "print.job", ",\"params\":{\"uri\":\"ftp://h/p\",\"job_id\":2147483647}"),
              "-32022"),
          "the biggest id reaches the device check");
    CHECK(has(REQ("1", "print.job", ",\"params\":{\"uri\":\"ftp://h/p\",\"job_id\":1}"), "-32022"),
          "id 1 reaches the device check");
    EXPECT(REQ("1", "print.submit",
               ",\"params\":{\"uri\":\"ipp://h/p\",\"path\":\"a.pdf\",\"copies\":1000}"),
           ERR("1", "-32602", "\\\"copies\\\" must be 1 to 999"));
    EXPECT(REQ("1", "print.submit",
               ",\"params\":{\"uri\":\"ipp://h/p\",\"path\":\"a.pdf\",\"color\":\"red\"}"),
           ERR("1", "-32602", "\\\"color\\\" must be color or monochrome"));
    EXPECT(REQ("1", "print.submit",
               ",\"params\":{\"uri\":\"ipp://h/p\",\"path\":\"a.pdf\",\"sides\":\"both\"}"),
           ERR("1", "-32602",
               "\\\"sides\\\" must be one-sided, two-sided-long-edge or two-sided-short-edge"));
    CHECK(has(REQ("1", "print.submit",
                  ",\"params\":{\"uri\":\"ftp://h/p\",\"path\":\"a.pdf\",\"copies\":999,"
                  "\"color\":\"color\",\"sides\":\"one-sided\"}"),
              "-32022"),
          "the edges pass: %s", OUT.p);
    CHECK(has(REQ("1", "print.submit",
                  ",\"params\":{\"uri\":\"ftp://h/"
                  "p\",\"path\":\"a.pdf\",\"sides\":\"two-sided-long-edge\"}"),
              "-32022"),
          "long edge");
    CHECK(has(REQ("1", "print.submit", ",\"params\":{\"uri\":\"ipp://h/p\"}"), "-32602"),
          "path required");
    CHECK(has(REQ("1", "print.submit",
                  ",\"params\":{\"uri\":\"ipp://h/p\",\"path\":\"a.pdf\",\"space\":\"nope\"}"),
              "no such space"),
          "space");
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.status\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/eSCL\"}}",
             FAKE_PORT);
    r = rpc(req);
    CHECK(
        strstr(
            r,
            "{\"make\":\"Fake Scanner\",\"platen\":true,\"feeder\":false,\"resolutions\":[300,600],"
            "\"color_modes\":[\"RGB24\",\"Grayscale8\"],\"formats\":[\"image/jpeg\"],"
            "\"max_width\":2550,\"max_height\":3508,\"state\":\"Idle\",\"adf_state\":\"\",\"jobs\":"
            "0}"),
        "scan.status: %s", r);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.start\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/eSCL\",\"folder\":\"Folder\",\"color\":\"gray\",\"dpi\":75}}",
             FAKE_PORT);
    r = rpc(req);
    CHECK(strstr(r, "{\"pages\":2,\"saved\":[\"Folder/Scan 3.jpg\",\"Folder/Scan 4.jpg\"]}"),
          "scan.start: %s", r);
    CHECK(!strcmp(fake_get("/fake/last"), "scan dpi=75</ src=Platen"), "%s",
          fake_get("/fake/last"));
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.start\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/eSCL\",\"source\":\"feeder\",\"color\":\"bw\",\"dpi\":1200,"
             "\"format\":\"application/pdf\"}}",
             FAKE_PORT);
    CHECK(strstr(rpc(req), "{\"pages\":2,\"saved\":[\"Scan 2.pdf\",\"Scan 3.pdf\"]}"), "feeder: %s",
          OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.start\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/eSCL\",\"source\":\"platen\",\"color\":\"color\","
             "\"format\":\"image/png\"}}",
             FAKE_PORT);
    CHECK(strstr(rpc(req), "\"saved\":[\"Scan 1.png\","), "the named defaults: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.start\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/busy\"}}",
             FAKE_PORT);
    CHECK(strstr(rpc(req), "\"code\":-32022,\"message\":\"the scanner is busy\""), "busy: %s",
          OUT.p);
    EXPECT(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"source\":\"tray\"}"),
           ERR("1", "-32602", "\\\"source\\\" must be platen or feeder"));
    EXPECT(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"color\":\"sepia\"}"),
           ERR("1", "-32602", "\\\"color\\\" must be color, gray or bw"));
    EXPECT(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"dpi\":74}"),
           ERR("1", "-32602", "\\\"dpi\\\" must be 75 to 1200"));
    EXPECT(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"dpi\":1201}"),
           ERR("1", "-32602", "\\\"dpi\\\" must be 75 to 1200"));
    EXPECT(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"format\":\"image/tiff\"}"),
           ERR("1", "-32602", "\\\"format\\\" must be image/jpeg, application/pdf or image/png"));
    EXPECT(REQ("1", "scan.status", ",\"params\":{\"url\":\"http://8.8.8.8/eSCL\"}"),
           ERR("1", "-32022", "the device is not on the local network"));
    CHECK(has(REQ("1", "scan.start", ",\"params\":{\"url\":\"http://h/e\",\"space\":\"nope\"}"),
              "no such space"),
          "space");
    /* a scan that fails after saving pages says so, and keeps them */
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scan.start\",\"params\":{\"url\":"
             "\"http://127.0.0.1:%d/half\"}}",
             FAKE_PORT);
    CHECK(strstr(rpc(req),
                 "\"code\":-32022,\"message\":\"the device answered HTTP 500 (1 page saved)\""),
          "half: %s", OUT.p);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/half", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_HTTP && pages == 1 &&
              zxf_stat(saved[0], &e) == 0 && e.size == 4,
          "kept: %u", pages);
    snprintf(uri, sizeof uri, "http://127.0.0.1:%d/stuck", FAKE_PORT);
    CHECK(zph_scan(uri, &s, "", saved, 4, &pages, err, sizeof err) == ZPH_E_HTTP && pages == 0 &&
              strstr(err, "HTTP 503"),
          "busy for ever: %s", err);
    /* what is logged */
    {
        zxvd_log_reset();
        snprintf(req, sizeof req,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.status\",\"params\":{\"uri\":"
                 "\"ipp://127.0.0.1:%d/ipp/print\"}}",
                 FAKE_PORT);
        rpc(req);
        CHECK(zxvd_log_last() == 0, "print.status is not logged");
        snprintf(req, sizeof req,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"print.submit\",\"params\":{\"uri\":"
                 "\"ipp://127.0.0.1:%d/ipp/print\",\"path\":\"Plan.pdf\"}}",
                 FAKE_PORT);
        rpc(req);
        CHECK(zxvd_log_last() == 1, "print.submit is: %llu", (unsigned long long) zxvd_log_last());
    }
    fake_stop();
    zph_set_timeout_ms(0);
    CHECK(rm_tree(root) == 0, "cleanup");
}

/* ---------------- backup.* (zxv_keepsake.h) ---------------- */

static char BK_DEST[200];

static void bk_hex(const void *d, size_t n, char out[65])
{
    uint8_t h[32];
    sha256(d, n, h);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

static bool bk_put(const char *rel, const void *d, size_t n)
{
    uint64_t size;
    return zxf_write(rel, d, (uint32_t) n, ZXF_W_REPLACE, false, &size) == 0;
}

/* a store file's absolute path */
static const char *bk_abs(const char *fmt, const char *a)
{
    static char p[700];
    char tail[400];
    snprintf(tail, sizeof tail, fmt, a);
    snprintf(p, sizeof p, "%s/" ZKS_DIR "/%s", BK_DEST, tail);
    return p;
}

static bool bk_exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

static long bk_read(const char *p, char *buf, size_t cap)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    long n = (long) fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool bk_write(const char *p, const char *d, size_t n)
{
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

/* a snapshot file from lines, with a good end line unless told otherwise */
static bool bk_snap(const char *space, uint32_t id, const char *body, uint32_t files,
                    uint64_t bytes, bool good)
{
    static char text[8192];
    char hex[65], dir[600], name[700];
    int n = snprintf(text, sizeof text, "%s", body);
    bk_hex(text, (size_t) n, hex);
    if (!good) hex[0] = hex[0] == '0' ? '1' : '0';
    catf(text, sizeof text, &n, "end %u %llu %s\n", files, (unsigned long long) bytes, hex);
    snprintf(dir, sizeof dir, "%s/" ZKS_DIR "/spaces/%s", BK_DEST, space);
    mkdir(dir, 0700);
    snprintf(name, sizeof name, "%s/%u.snap", dir, id);
    return bk_write(name, text, (size_t) n);
}

static uint64_t bk_file_size(const char *rel)
{
    zxf_entry_t e;
    return zxf_stat(rel, &e) == 0 ? e.size : UINT64_MAX;
}

static void test_backup(void)
{
    char root[200], err[ZKS_ERR], landed[ZXF_PATH_MAX + 1], req[1200], p[700];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(root, sizeof root, "%s/zxvd-keep-XXXXXX", tmp);
    snprintf(BK_DEST, sizeof BK_DEST, "%s/zxvd-dest-XXXXXX", tmp);
    CHECK(mkdtemp(root) && mkdtemp(BK_DEST), "scratch folders");
    CHECK(zxf_set_root(root) == 0 && zxf_use_space("home") == 0, "root set");
    static uint8_t big[100000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t) (i * 7 + i / 300);
    CHECK(zxf_mkdir("Notes") == 0 && zxf_mkdir("Notes/deep") == 0 &&
              bk_put("Notes/a.txt", "hello", 5) && bk_put("Notes/deep/b.bin", big, sizeof big) &&
              bk_put("top.md", "# top\n", 6) && bk_put("empty.txt", "", 0) &&
              bk_put("README", "read me", 7),
          "files");
    zks_run_t r;
    int zxf = 7;

    /* K1: the destination */
    const char *bad[] = {NULL, "", "rel/dir", "/no/such/zxv/folder"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(zks_run(bad[i], false, &r, &zxf, err, sizeof err) == ZKS_E_DEST && zxf == 0 &&
                  r.snapshot == 0,
              "bad destination %zu: %s", i, err);
    static char longdest[ZKS_DEST_MAX + 2];
    memset(longdest, 'd', ZKS_DEST_MAX + 1);
    longdest[0] = '/';
    CHECK(zks_run(longdest, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST, "too long");
    snprintf(p, sizeof p, "%s/plain", BK_DEST);
    CHECK(bk_write(p, "x", 1) && zks_run(p, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST &&
              !strcmp(err, "no folder at the destination"),
          "a file: %s", err);
    unlink(p);
    snprintf(p, sizeof p, "%s/link", BK_DEST);
    CHECK(symlink(BK_DEST, p) == 0 && zks_run(p, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST &&
              !strcmp(err, "the destination is a link"),
          "a link: %s", err);
    unlink(p);
    snprintf(p, sizeof p, "%s/Notes", root);
    CHECK(zks_run(root, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST &&
              !strcmp(err, "the destination is inside the Hoard") &&
              zks_run(p, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST,
          "inside the Hoard: %s", err);
    snprintf(p, sizeof p, "%s-sibling", root); /* shares a prefix, but is not inside */
    CHECK(mkdir(p, 0700) == 0 && zks_run(p, false, &r, &zxf, err, sizeof err) == ZKS_OK &&
              r.files == 5,
          "a sibling: %s", err);
    char sib[700];
    snprintf(sib, sizeof sib, "%s", p);
    snprintf(p, sizeof p, "%s/blocked", BK_DEST);
    char blocked[800];
    snprintf(blocked, sizeof blocked, "%s/" ZKS_DIR, p);
    CHECK(mkdir(p, 0700) == 0 && bk_write(blocked, "x", 1) &&
              zks_run(p, false, &r, &zxf, err, sizeof err) == ZKS_E_DEST,
          "\"" ZKS_DIR "\" is a file: %s", err);
    uint32_t n, total;
    zks_snap_t snaps[8];
    CHECK(zks_list(p, snaps, 8, &n, &total, err, sizeof err) == ZKS_E_DEST, "list there too");

    /* nothing yet */
    zks_version_t ver[8];
    zks_verify_t v;
    uint32_t files, so, oo;
    uint64_t freed;
    CHECK(zks_list(BK_DEST, snaps, 8, &n, &total, err, sizeof err) == ZKS_OK && n == 0 &&
              total == 0,
          "no list yet");
    CHECK(zks_versions(BK_DEST, "top.md", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 0,
          "no versions yet");
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_E_NOTFOUND &&
              !strcmp(err, "no backups there yet"),
          "nothing to check: %s", err);
    CHECK(zks_prune(BK_DEST, 1, &so, &oo, &freed, err, sizeof err) == ZKS_OK && so == 0 && oo == 0,
          "nothing to prune");
    CHECK(zks_restore(BK_DEST, 0, "top.md", NULL, landed, &files, &zxf, err, sizeof err) ==
              ZKS_E_NOTFOUND,
          "nothing to restore");

    /* the first backup keeps everything */
    char ha[65], hb[65], ht[65], he[65], hr[65], hc[65];
    bk_hex("hello", 5, ha);
    bk_hex(big, sizeof big, hb);
    bk_hex("# top\n", 6, ht);
    bk_hex("", 0, he);
    bk_hex("read me", 7, hr);
    bk_hex("changed!", 8, hc);
    uint64_t all = 5 + sizeof big + 6 + 0 + 7;
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 1 &&
              r.files == 5 && r.new_files == 5 && r.bytes == all && r.new_bytes == all &&
              r.skipped == 0 && r.time > 1600000000 && zxf == 0,
          "first: %u %u %u %s", r.snapshot, r.files, r.new_files, err);
    static char text[300000];
    long tn = bk_read(bk_abs("objects/%.2s/", hb), text, sizeof text);
    snprintf(p, sizeof p, "%s%s", bk_abs("objects/%.2s/", hb), hb);
    tn = bk_read(p, text, sizeof text);
    CHECK(tn == (long) sizeof big && !memcmp(text, big, sizeof big), "the big file kept: %ld", tn);
    tn = bk_read(bk_abs("spaces/%s/1.snap", "home"), text, sizeof text);
    text[tn > 0 ? tn : 0] = 0;
    char want[600];
    snprintf(want, sizeof want, "f %s 5 ", ha);
    CHECK(tn > 0 && !strncmp(text, "zxv-keepsake 1\nspace home\ntime ", 31) && strstr(text, want) &&
              strstr(text, " Notes/a.txt\n") && strstr(text, " Notes/deep/b.bin\n") &&
              strstr(text, "\nend 5 "),
          "the snapshot: %.200s", text);
    CHECK(!bk_exists(bk_abs("spaces/%s/1.tmp", "home")) &&
              !bk_exists(bk_abs("objects/%s", "incoming.tmp")),
          "no temporary files left");

    /* nothing changed: nothing new */
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 2 &&
              r.files == 5 && r.new_files == 0 && r.new_bytes == 0 && r.bytes == all,
          "second: %u new", r.new_files);
    /* a change */
    CHECK(bk_put("Notes/a.txt", "changed!", 8) &&
              zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 3 &&
              r.new_files == 1 && r.new_bytes == 8 && r.bytes == all + 3,
          "third: %u new", r.new_files);
    /* back as it was: read again, but its copy is already kept */
    CHECK(bk_put("Notes/a.txt", "hello", 5) &&
              zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 4 &&
              r.new_files == 0 && r.bytes == all,
          "fourth: %u new", r.new_files);

    /* list */
    CHECK(zks_list(BK_DEST, snaps, 8, &n, &total, err, sizeof err) == ZKS_OK && n == 4 &&
              total == 4 && snaps[0].id == 4 && snaps[3].id == 1 && snaps[0].files == 5 &&
              snaps[0].bytes == all && snaps[1].bytes == all + 3 && !snaps[0].damaged &&
              snaps[0].time > 1600000000,
          "list: %u %u", n, total);
    CHECK(zks_list(BK_DEST, snaps, 2, &n, &total, err, sizeof err) == ZKS_OK && n == 2 &&
              total == 4 && snaps[1].id == 3,
          "list, two");

    /* versions */
    CHECK(zks_versions(BK_DEST, "Notes/a.txt", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 2 &&
              ver[0].snapshot == 4 && !strcmp(ver[0].hash, ha) && ver[0].size == 5 &&
              ver[1].snapshot == 3 && !strcmp(ver[1].hash, hc) && ver[1].size == 8 &&
              ver[0].time >= ver[1].time,
          "versions: %u", n);
    CHECK(zks_versions(BK_DEST, "Notes/a.txt", ver, 1, &n, err, sizeof err) == ZKS_OK && n == 1 &&
              ver[0].snapshot == 4,
          "one version");
    CHECK(zks_versions(BK_DEST, "top.md", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 1 &&
              ver[0].snapshot == 4 && ver[0].size == 6,
          "unchanged: one");
    CHECK(zks_versions(BK_DEST, "nope.txt", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 0,
          "never");
    CHECK(zks_versions(BK_DEST, "", ver, 8, &n, err, sizeof err) == ZKS_E_NOTFOUND &&
              zks_versions(BK_DEST, NULL, ver, 8, &n, err, sizeof err) == ZKS_E_NOTFOUND &&
              zks_versions(BK_DEST, "../x", ver, 8, &n, err, sizeof err) == ZKS_E_NOTFOUND,
          "not a path");
    CHECK(zks_versions(p, "a", ver, 8, &n, err, sizeof err) == ZKS_E_DEST, "bad destination");

    /* verify, then break things */
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_OK && v.snapshot == 4 &&
              v.files == 5 && v.bytes == all && v.bad == 0 && v.missing == 0 && !v.first_bad[0],
          "verified: %u %u %u", v.files, v.bad, v.missing);
    CHECK(zks_verify(BK_DEST, 3, &v, err, sizeof err) == ZKS_OK && v.snapshot == 3 &&
              v.bytes == all + 3,
          "verified 3");
    CHECK(zks_verify(BK_DEST, 9, &v, err, sizeof err) == ZKS_E_NOTFOUND &&
              !strcmp(err, "no such snapshot"),
          "no such snapshot");
    char ob[700], ot[700];
    snprintf(ob, sizeof ob, "%s%s", bk_abs("objects/%.2s/", hb), hb);
    snprintf(ot, sizeof ot, "%s%s", bk_abs("objects/%.2s/", ht), ht);
    tn = bk_read(ob, text, sizeof text);
    text[500] ^= 1; /* one bit, same size */
    CHECK(bk_write(ob, text, (size_t) tn) && unlink(ot) == 0, "damage two copies");
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_OK && v.bad == 1 && v.missing == 1 &&
              !strcmp(v.first_bad, "Notes/deep/b.bin"),
          "found: %u bad, %u missing, first %s", v.bad, v.missing, v.first_bad);
    CHECK(zks_restore(BK_DEST, 0, "Notes", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_DAMAGED &&
              !strcmp(err, "a kept copy of \"Notes/deep/b.bin\" is missing or damaged") &&
              files == 0 && bk_file_size("Notes (restored)") == UINT64_MAX,
          "a damaged copy stops the restore before anything is written: %s", err);
    CHECK(zks_restore(BK_DEST, 0, "", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_DAMAGED &&
              !strcmp(err, "a kept copy of \"Notes/deep/b.bin\" is missing or damaged"),
          "and the whole: %s", err);
    /* a plain backup reuses what looks unchanged; the missing copy comes back */
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.new_files == 1 &&
              r.new_bytes == 6 && bk_exists(ot),
          "the missing copy is kept again: %u", r.new_files);
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_OK && v.snapshot == 5 && v.bad == 1 &&
              v.missing == 0,
          "still one bad");
    /* full re-reads everything and repairs it */
    CHECK(zks_run(BK_DEST, true, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 6 &&
              r.new_files == 1 && r.new_bytes == sizeof big && r.files == 5,
          "full: %u repaired", r.new_files);
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_OK && v.bad == 0 && v.missing == 0 &&
              zks_verify(BK_DEST, 1, &v, err, sizeof err) == ZKS_OK && v.bad == 0,
          "all good again");

    /* restore */
    CHECK(zks_restore(BK_DEST, 3, "Notes/a.txt", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_OK &&
              !strcmp(landed, "Notes/a (restored).txt") && files == 1,
          "beside it: %s %s", landed, err);
    char got[64];
    uint32_t gn;
    uint64_t gs;
    CHECK(zxf_read("Notes/a (restored).txt", 0, (uint8_t *) got, sizeof got, false, &gn, &gs) ==
                  0 &&
              gn == 8 && !memcmp(got, "changed!", 8),
          "the old words");
    CHECK(zks_restore(BK_DEST, 3, "Notes/a.txt", "", landed, &files, &zxf, err, sizeof err) ==
                  ZKS_OK &&
              !strcmp(landed, "Notes/a (restored 2).txt"),
          "and again: %s", landed);
    CHECK(zks_restore(BK_DEST, 1, "README", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_OK &&
              !strcmp(landed, "README (restored)"),
          "no extension: %s", landed);
    CHECK(zks_restore(BK_DEST, 0, "Notes/a.txt", "Back/old/a.txt", landed, &files, &zxf, err,
                      sizeof err) == ZKS_OK &&
              !strcmp(landed, "Back/old/a.txt") && bk_file_size("Back/old/a.txt") == 5,
          "somewhere new, its folders made: %s", err);
    CHECK(zks_restore(BK_DEST, 0, "Notes/a.txt", "Back/old/a.txt", landed, &files, &zxf, err,
                      sizeof err) == ZKS_E_FILE &&
              zxf == ZXF_E_EXISTS && files == 0,
          "never over a file: %d", zxf);
    CHECK(zks_restore(BK_DEST, 0, "Notes/a.txt", "Back", landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_FILE &&
              zxf == ZXF_E_EXISTS,
          "never over a folder");
    CHECK(zks_restore(BK_DEST, 0, "Notes", NULL, landed, &files, &zxf, err, sizeof err) == ZKS_OK &&
              !strcmp(landed, "Notes (restored)") && files == 2 &&
              bk_file_size("Notes (restored)/deep/b.bin") == sizeof big &&
              bk_file_size("Notes (restored)/a.txt") == 5,
          "a folder: %s %u %s", landed, files, err);
    static uint8_t back[100000];
    uint64_t off = 0;
    while (off < sizeof back &&
           zxf_read("Notes (restored)/deep/b.bin", off, back + off, ZXF_READ_MAX, false, &gn,
                    &gs) == 0 &&
           gn)
        off += gn;
    CHECK(off == sizeof big && !memcmp(back, big, sizeof big), "every byte: %llu",
          (unsigned long long) off);
    char trashed[ZXF_PATH_MAX + 1];
    CHECK(zxf_trash("top.md", trashed, sizeof trashed) == 0 &&
              zks_restore(BK_DEST, 0, "top.md", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_OK &&
              !strcmp(landed, "top.md") && bk_file_size("top.md") == 6,
          "back where it was: %s", landed);
    CHECK(zks_restore(BK_DEST, 0, "empty.txt", "e2.txt", landed, &files, &zxf, err, sizeof err) ==
                  ZKS_OK &&
              bk_file_size("e2.txt") == 0,
          "an empty file");
    CHECK(zks_restore(BK_DEST, 2, NULL, NULL, landed, &files, &zxf, err, sizeof err) == ZKS_OK &&
              !strcmp(landed, "Keepsake 2") && files == 5 &&
              bk_file_size("Keepsake 2/Notes/deep/b.bin") == sizeof big,
          "everything: %s %u", landed, files);
    CHECK(zks_restore(BK_DEST, 2, "", NULL, landed, &files, &zxf, err, sizeof err) == ZKS_OK &&
              !strcmp(landed, "Keepsake 2 (restored)"),
          "everything again: %s", landed);
    CHECK(zks_restore(BK_DEST, 0, "Note", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_NOTFOUND &&
              strstr(err, "\"Note\" is not in snapshot 6") &&
              zks_restore(BK_DEST, 0, "Notes/a", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_NOTFOUND,
          "only whole names: %s", err);
    CHECK(zks_restore(BK_DEST, 0, "../x", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_NOTFOUND &&
              zks_restore(BK_DEST, 0, "top.md", "a/../b", landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_NOTFOUND &&
              zks_restore(BK_DEST, 77, "top.md", NULL, landed, &files, &zxf, err, sizeof err) ==
                  ZKS_E_NOTFOUND,
          "bad paths, no such snapshot");
    CHECK(zks_restore(p, 0, "top.md", NULL, landed, &files, &zxf, err, sizeof err) == ZKS_E_DEST,
          "a bad destination");

    /* deep folders are left out and counted */
    char deep[200] = "z";
    CHECK(zxf_mkdir("z") == 0, "deep top");
    for (int i = 1; i < 64; i++) {
        strcat(deep, "/z");
        if (zxf_mkdir(deep) != 0) break;
    }
    strcat(deep, "/f.txt");
    CHECK(bk_put(deep, "deep", 4), "a deep file");
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.skipped == 1,
          "too deep: %u skipped", r.skipped);

    /* damaged snapshots: never trusted */
    char head[200], line[200], body[2000];
    snprintf(head, sizeof head, "zxv-keepsake 1\nspace home\ntime 1700000000\n");
    snprintf(line, sizeof line, "f %s 6 5 top.md\n", ht);
    snprintf(body, sizeof body, "%s%s", head, line);
    CHECK(bk_snap("home", 90, body, 1, 6, true) &&
              zks_verify(BK_DEST, 90, &v, err, sizeof err) == ZKS_OK && v.files == 1 && v.bad == 0,
          "a made snapshot reads: %s", err);
    struct {
        const char *body;
        uint32_t files;
        uint64_t bytes;
        bool good;
    } dmg[] = {
        {"zxv-keepsake 2\nspace home\ntime 1\n", 0, 0, true},
        {"zxv-keepsake 1\nspace other\ntime 1\n", 0, 0, true},
        {"zxv-keepsake 1\nspace home\ntime x\n", 0, 0, true},
        {"zxv-keepsake 1\nspace home\ntime 1 \n", 0, 0, true},
        {"zxv-keepsake 1\nspace home\nwhen 1\n", 0, 0, true},
        {"zxv-keepsake 1\nspace home\n", 0, 0, true},
        {body, 1, 6, false},
        {body, 2, 6, true},
        {body, 1, 7, true},
        {"zxv-keepsake 1\nspace home\ntime 1\nf zz 6 5 top.md\n", 1, 6, true},
        {"zxv-keepsake 1\nspace home\ntime 1\nx\n", 1, 6, true},
    };
    for (size_t i = 0; i < sizeof dmg / sizeof *dmg; i++) {
        CHECK(bk_snap("home", 91, dmg[i].body, dmg[i].files, dmg[i].bytes, dmg[i].good) &&
                  zks_verify(BK_DEST, 91, &v, err, sizeof err) == ZKS_E_DAMAGED &&
                  !strcmp(err, "snapshot 91 is damaged") &&
                  zks_restore(BK_DEST, 91, "top.md", NULL, landed, &files, &zxf, err, sizeof err) ==
                      ZKS_E_DAMAGED,
              "damaged %zu", i);
    }
    /* entry lines that do not add up */
    const char *lines[] = {
        "f %s 6 5 \n",
        "f %s 6 5 ../top.md\n",
        "f %s x 5 top.md\n",
        "f %s 6 x top.md\n",
        "f %s 06 5 top.md\n",
        "f %s 6  top.md\n",
        "f %s 6 5top.md\n",
        "f %sX 6 5 top.md\n",
        "f %s 18446744073709551616 5 top.md\n",
        "f %s 6 9223372036854775808 top.md\n",
    };
    for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
        char l[300], b[600];
        snprintf(l, sizeof l, lines[i], ht);
        snprintf(b, sizeof b, "%s%s", head, l);
        CHECK(bk_snap("home", 91, b, 1, 6, true) &&
                  zks_verify(BK_DEST, 91, &v, err, sizeof err) == ZKS_E_DAMAGED,
              "bad line %zu", i);
    }
    snprintf(body, sizeof body, "%sf %s 6 -5 top.md\n", head, ht);
    CHECK(bk_snap("home", 91, body, 1, 6, true) &&
              zks_versions(BK_DEST, "top.md", ver, 8, &n, err, sizeof err) == ZKS_OK &&
              ver[0].snapshot == 91 && ver[0].mtime == -5 && ver[0].time == 1700000000,
          "a time before 1970: %lld", (long long) ver[0].mtime);
    /* a tail that does not parse shows as damaged in the list */
    snprintf(p, sizeof p, "%s", bk_abs("spaces/%s/91.snap", "home"));
    CHECK(bk_write(p, "zxv-keepsake 1\nspace home\ntime 3\nend x\n", 38) &&
              zks_list(BK_DEST, snaps, 8, &n, &total, err, sizeof err) == ZKS_OK &&
              snaps[0].id == 91 && snaps[0].damaged && snaps[0].time == 3 && snaps[1].id == 90 &&
              !snaps[1].damaged && snaps[1].files == 1 && snaps[1].bytes == 6,
          "listed as damaged");
    CHECK(bk_write(p, "zxv-keepsake 1\nspace home\ntime 3\nend 1 2 x", 41) &&
              zks_list(BK_DEST, snaps, 1, &n, &total, err, sizeof err) == ZKS_OK &&
              snaps[0].damaged,
          "no final newline");
    CHECK(bk_write(p, "nope\n", 5) &&
              zks_list(BK_DEST, snaps, 1, &n, &total, err, sizeof err) == 0 && snaps[0].damaged &&
              snaps[0].time == 0,
          "no header");
    /* versions skip it; a backup builds on the newest good one */
    CHECK(zks_versions(BK_DEST, "top.md", ver, 8, &n, err, sizeof err) == ZKS_OK &&
              ver[0].snapshot == 90,
          "skipped: %u", ver[0].snapshot);
    /* prune refuses while one is damaged, and removes nothing */
    CHECK(zks_prune(BK_DEST, 1, &so, &oo, &freed, err, sizeof err) == ZKS_E_DAMAGED && so == 0 &&
              bk_exists(bk_abs("spaces/%s/1.snap", "home")),
          "prune waits: %s", err);
    unlink(p);
    snprintf(p, sizeof p, "%s", bk_abs("spaces/%s/90.snap", "home"));
    unlink(p);
    /* names that are not snapshots are left alone */
    snprintf(p, sizeof p, "%s", bk_abs("spaces/%s/0.snap", "home"));
    CHECK(bk_write(p, "x", 1), "0.snap");
    snprintf(p, sizeof p, "%s", bk_abs("spaces/%s/1234567.snap", "home"));
    CHECK(bk_write(p, "x", 1), "1234567.snap");
    snprintf(p, sizeof p, "%s", bk_abs("spaces/%s/12.snapx", "home"));
    CHECK(bk_write(p, "x", 1), "12.snapx");
    CHECK(zks_list(BK_DEST, snaps, 8, &n, &total, err, sizeof err) == ZKS_OK && total == 7 &&
              snaps[0].id == 7,
          "ignored: %u, newest %u", total, snaps[0].id);

    /* another space's snapshot keeps its copies through a prune */
    char hx[65];
    bk_hex("other", 5, hx);
    snprintf(p, sizeof p, "%s%s", bk_abs("objects/%.2s/", hx), hx);
    char od[700];
    snprintf(od, sizeof od, "%s", bk_abs("objects/%.2s", hx));
    mkdir(od, 0700);
    snprintf(body, sizeof body, "zxv-keepsake 1\nspace team-x\ntime 5\nf %s 5 5 o.txt\n", hx);
    CHECK(bk_write(p, "other", 5) && bk_snap("team-x", 1, body, 1, 5, true), "another space");
    snprintf(od, sizeof od, "%s", bk_abs("objects/%s", "incoming.tmp"));
    CHECK(bk_write(od, "junk", 4), "a stray");
    snprintf(od, sizeof od, "%s", bk_abs("objects/%s", "README"));
    CHECK(bk_write(od, "not ours", 8), "not an object");
    /* prune: keep the newest two of home's seven */
    char oc[700];
    snprintf(oc, sizeof oc, "%s%s", bk_abs("objects/%.2s/", hc), hc);
    CHECK(bk_exists(oc), "the changed copy");
    CHECK(zks_prune(BK_DEST, 2, &so, &oo, &freed, err, sizeof err) == ZKS_OK && so == 5 &&
              oo == 0 && freed == 0,
          "pruned: %u snapshots, %u objects, %llu bytes: %s", so, oo, (unsigned long long) freed,
          err);
    CHECK(bk_exists(oc) && bk_exists(p) && bk_exists(ob) &&
              !bk_exists(bk_abs("objects/%s", "incoming.tmp")) && bk_exists(od) &&
              !bk_exists(bk_abs("spaces/%s/5.snap", "home")) &&
              bk_exists(bk_abs("spaces/%s/6.snap", "home")) &&
              bk_exists(bk_abs("spaces/%s/7.snap", "home")),
          "what is left");
    CHECK(zks_verify(BK_DEST, 0, &v, err, sizeof err) == ZKS_OK && v.bad == 0 && v.missing == 0,
          "the kept still check");
    CHECK(zks_prune(BK_DEST, 0, &so, &oo, &freed, err, sizeof err) == ZKS_OK && so == 1,
          "keep 0 is keep 1: %u", so);
    CHECK(zks_list(BK_DEST, snaps, 8, &n, &total, err, sizeof err) == ZKS_OK && total == 1 &&
              snaps[0].id == 7,
          "one left");
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 8 &&
              r.new_files == 0,
          "numbers go on: %u", r.snapshot);
    /* a file kept only by a snapshot that goes is swept with it */
    char hg[65], og[700];
    bk_hex("gone soon", 9, hg);
    snprintf(og, sizeof og, "%s%s", bk_abs("objects/%.2s/", hg), hg);
    CHECK(bk_put("gone.txt", "gone soon", 9) &&
              zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.new_files == 1 &&
              bk_exists(og) && zxf_trash("gone.txt", trashed, sizeof trashed) == 0 &&
              zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 10,
          "kept, then deleted");
    CHECK(zks_prune(BK_DEST, 1, &so, &oo, &freed, err, sizeof err) == ZKS_OK && so == 3 &&
              oo == 1 && freed == 9 && !bk_exists(og) && bk_exists(oc),
          "swept: %u %u %llu", so, oo, (unsigned long long) freed);
    CHECK(zks_prune(p, 1, &so, &oo, &freed, err, sizeof err) == ZKS_E_DEST, "bad destination");
    CHECK(zks_prune(sib, 5, &so, &oo, &freed, err, sizeof err) == ZKS_OK && so == 0 && oo == 0,
          "nothing to prune there");
    /* each space has its own snapshots (K6) */
    CHECK(zxf_use_space("other-space") == 0 && bk_put("o.txt", "other", 5), "another space");
    CHECK(zks_run(BK_DEST, false, &r, &zxf, err, sizeof err) == ZKS_OK && r.snapshot == 1 &&
              r.files == 1 && r.new_files == 0 &&
              bk_exists(bk_abs("spaces/%s/1.snap", "other-space")),
          "its own numbers; the copy was already kept: %u %u", r.snapshot, r.new_files);
    CHECK(zks_versions(BK_DEST, "top.md", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 0 &&
              zks_versions(BK_DEST, "o.txt", ver, 8, &n, err, sizeof err) == ZKS_OK && n == 1,
          "its own versions");
    CHECK(zxf_use_space("home") == 0, "home again");

    /* over JSON-RPC */
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.run\",\"params\":{\"dest\":\"%s\"}}",
        BK_DEST);
    const char *o = rpc(req);
    CHECK(strstr(o, "\"result\":{\"space\":\"home\",\"snapshot\":11,\"time\":") &&
              strstr(o, ",\"new_files\":0,\"new_bytes\":0,\"skipped\":1}"),
          "backup.run: %s", o);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.run\",\"params\":{\"dest\":\"%s\","
             "\"full\":true}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"snapshot\":12,"), "backup.run full: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\","
             "\"max\":2}}",
             BK_DEST);
    o = rpc(req);
    CHECK(
        strstr(o, "\"result\":{\"space\":\"home\",\"total\":3,\"snapshots\":[{\"snapshot\":12,") &&
            strstr(o, "},{\"snapshot\":11,") && strstr(o, ",\"damaged\":false}]}"),
        "backup.list: %s", o);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.versions\",\"params\":{\"dest\":"
             "\"%s\",\"path\":\"top.md\"}}",
             BK_DEST);
    snprintf(want, sizeof want, "\"size\":6,\"mtime\":");
    o = rpc(req);
    CHECK(strstr(o, "{\"space\":\"home\",\"versions\":[{\"snapshot\":12,\"time\":") &&
              strstr(o, want) && strstr(o, ht),
          "backup.versions: %s", o);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.verify\",\"params\":{\"dest\":"
             "\"%s\"}}",
             BK_DEST);
    o = rpc(req);
    CHECK(strstr(o, "{\"space\":\"home\",\"snapshot\":12,\"files\":") &&
              strstr(o, ",\"bad\":0,\"missing\":0,\"ok\":true,\"first_bad\":\"\"}"),
          "backup.verify: %s", o);
    snprintf(ot, sizeof ot, "%s%s", bk_abs("objects/%.2s/", ha), ha);
    CHECK(unlink(ot) == 0 &&
              strstr(rpc(req),
                     ",\"bad\":0,\"missing\":5,\"ok\":false,\"first_bad\":\"Back/old/a.txt\"}"),
          "backup.verify, one missing: %s", OUT.p);
    CHECK(bk_write(ot, "hello", 5), "put back");
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.verify\",\"params\":{\"dest\":"
             "\"%s\",\"snapshot\":100001}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"code\":-32602"), "snapshot too big");
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.verify\",\"params\":{\"dest\":"
             "\"%s\",\"snapshot\":3}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "{\"code\":-32018,\"message\":\"no such snapshot\"}"),
          "backup.verify 3: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.restore\",\"params\":{\"dest\":"
             "\"%s\",\"snapshot\":11,\"path\":\"README\",\"to\":\"Old/README\"}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"result\":{\"space\":\"home\",\"to\":\"Old/README\",\"files\":1}"),
          "backup.restore: %s", OUT.p);
    CHECK(strstr(rpc(req),
                 "{\"code\":-32014,\"message\":\"something with that name already exists\"}"),
          "backup.restore, taken: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.restore\",\"params\":{\"dest\":"
             "\"%s\",\"path\":\"README\"}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"to\":\"README (restored 2)\""), "backup.restore beside: %s", OUT.p);
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.prune\",\"params\":{\"dest\":\"%s\","
        "\"keep\":1}}",
        BK_DEST);
    CHECK(strstr(rpc(req), "{\"space\":\"home\",\"snapshots_removed\":2,\"objects_removed\":0,"
                           "\"freed\":0}"),
          "backup.prune: %s", OUT.p);
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.prune\",\"params\":{\"dest\":\"%s\","
        "\"keep\":0}}",
        BK_DEST);
    CHECK(strstr(rpc(req), "\"keep\\\" must be 1 to 100000"), "keep 0: %s", OUT.p);
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.prune\",\"params\":{\"dest\":\"%s\","
        "\"keep\":100001}}",
        BK_DEST);
    CHECK(strstr(rpc(req), "\"code\":-32602"), "keep too many");
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\","
             "\"max\":0}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"max\\\" must be 1 to 100"), "max 0: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\","
             "\"max\":101}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"code\":-32602"), "max 101");
    /* the edges of each range are inside it */
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\","
             "\"max\":1}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"result\":{\"space\":\"home\",\"total\":") &&
              count(OUT.p, "{\"snapshot\":") == 1,
          "max 1, one snapshot: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.versions\",\"params\":{\"dest\":"
             "\"%s\",\"path\":\"o.txt\"}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"result\":{\"space\":\"home\",\"versions\":[]}"),
          "no versions in this space: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.verify\",\"params\":{\"dest\":"
             "\"%s\",\"snapshot\":100000}}",
             BK_DEST);
    CHECK(!strstr(rpc(req), "-32602") && strstr(OUT.p, "\"error\""),
          "snapshot 100000 passes the range check: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.prune\",\"params\":{\"dest\":"
             "\"%s\",\"keep\":100000}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"result\":{\"space\":\"home\","), "keep 100000: %s", OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\","
             "\"max\":100}}",
             BK_DEST);
    CHECK(strstr(rpc(req), "\"total\":1,\"snapshots\":[{\"snapshot\":12,"), "max 100: %s", OUT.p);
    EXPECT(REQ("1", "backup.run", ",\"params\":{}"),
           ERR("1", "-32602", "missing string parameter \\\"dest\\\""));
    EXPECT(REQ("1", "backup.run", ",\"params\":{\"dest\":\"rel\"}"),
           ERR("1", "-32018", "the destination must be a folder's absolute path"));
    EXPECT(REQ("1", "backup.list", ",\"params\":{\"dest\":\"/no/such/zxv\"}"),
           ERR("1", "-32018", "no folder at the destination"));
    EXPECT(REQ("1", "backup.versions", ",\"params\":{\"dest\":\"/no/such/zxv\",\"path\":\"a\"}"),
           ERR("1", "-32018", "no folder at the destination"));
    EXPECT(REQ("1", "backup.versions", ",\"params\":{\"dest\":\"/no/such/zxv\"}"),
           ERR("1", "-32602", "missing string parameter \\\"path\\\""));
    EXPECT(REQ("1", "backup.run", ",\"params\":{\"dest\":\"/x\",\"full\":1}"),
           ERR("1", "-32602", "\\\"full\\\" must be true or false"));
    EXPECT(REQ("1", "backup.restore", ",\"params\":{\"dest\":\"/no/such/zxv\"}"),
           ERR("1", "-32018", "no folder at the destination"));
    EXPECT(REQ("1", "backup.prune", ",\"params\":{\"dest\":\"/no/such/zxv\",\"keep\":1}"),
           ERR("1", "-32018", "no folder at the destination"));
    const char *ms[] = {"backup.run",    "backup.list",    "backup.versions",
                        "backup.verify", "backup.restore", "backup.prune"};
    for (size_t i = 0; i < sizeof ms / sizeof *ms; i++) {
        snprintf(req, sizeof req,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":{\"dest\":\"%s\","
                 "\"space\":\"nope\",\"path\":\"a\",\"keep\":1}}",
                 ms[i], BK_DEST);
        CHECK(strstr(rpc(req), "\"code\":-32013"), "%s: no such space: %s", ms[i], OUT.p);
    }
    unsigned long long before = zxvd_log_last();
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.list\",\"params\":{\"dest\":\"%s\"}}",
        BK_DEST);
    rpc(req);
    CHECK(zxvd_log_last() == before, "backup.list is quiet");
    snprintf(
        req, sizeof req,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"backup.run\",\"params\":{\"dest\":\"%s\"}}",
        BK_DEST);
    rpc(req);
    CHECK(zxvd_log_last() == before + 1, "backup.run is logged");
    CHECK(rm_tree(root) == 0 && rm_tree(BK_DEST) == 0 && rm_tree(sib) == 0, "cleanup");
}

/* ---------------- journal.* (zxv_journal.h) ---------------- */

static char JDIR[256];

static const char *jseg(uint64_t n)
{
    static char p[300];
    snprintf(p, sizeof p, "%s/%llu.jnl", JDIR, (unsigned long long) n);
    return p;
}

static long jread(const char *p, char *buf, size_t cap)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    long n = (long) fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n > 0 ? n : 0] = 0;
    return n;
}

static bool jwrite(const char *p, const char *d)
{
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fputs(d, f) >= 0;
    return fclose(f) == 0 && ok;
}

static uint64_t jlast(void)
{
    zjr_verify_t v;
    zjr_verify(&v);
    return v.last_seq;
}

static void test_journal(void)
{
    static zjr_entry_t e[600];
    uint32_t n;
    bool more;
    zjr_query_t q = {0};
    zjr_verify_t v;
    char req[600];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(JDIR, sizeof JDIR, "%s/zxvd-journal-XXXXXX", tmp);
    CHECK(mkdtemp(JDIR) != NULL, "a folder");
    zjr_close();
    CHECK(!zjr_on() && zjr_query(&q, e, 10, &n, &more) == -1 && n == 0 && zjr_verify(&v) == -1 &&
              !v.ok,
          "off");
    CHECK(strstr(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\"}"),
                 "\"result\":{\"on\":false,\"entries\":[],\"more\":false}"),
          "journal.query, off: %s", OUT.p);
    CHECK(strstr(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.verify\"}"),
                 "\"result\":{\"on\":false,\"ok\":false,\"segments\":0,\"entries\":0,"
                 "\"first_seq\":0,\"last_seq\":0,\"broken_at\":0,\"why\":\"\",\"audit\":0,"
                 "\"sigs_checked\":false,\"key\":null}"),
          "journal.verify, off: %s", OUT.p);
    CHECK(zjr_open(NULL) == 0 && zjr_open("") == 0 && !zjr_on(), "nothing to open");
    static char longdir[ZJR_DIR_MAX + 2];
    memset(longdir, 'j', ZJR_DIR_MAX + 1);
    longdir[0] = '/';
    char afile[300];
    snprintf(afile, sizeof afile, "%s/a-file", JDIR);
    CHECK(zjr_open(longdir) == -1 && jwrite(afile, "x") && zjr_open(afile) == -1 && !zjr_on(),
          "cannot open");
    char sub[300];
    strcat(JDIR, "/j"); /* mkdtemp's name is short */
    snprintf(sub, sizeof sub, "%s", JDIR);

    /* open: a header, then "opened" as seq 1 */
    CHECK(zjr_open(JDIR) == 0 && zjr_on(), "open");
    static char text[1 << 16];
    CHECK(jread(jseg(1), text, sizeof text) > 0 &&
              !strncmp(text,
                       "zxv-journal 1 1 "
                       "0000000000000000000000000000000000000000000000000000000000000000\n1 ",
                       82) &&
              strstr(text, " info journal ") && strstr(text, " opened\n"),
          "the first segment: %s", text);
    CHECK(jread(strcat(strcpy(req, JDIR), "/head"), text, sizeof text) > 0 &&
              !strcmp(text, "1 1\n"),
          "head: %s", text);
    zxvd_log(ZXVD_LOG_INFO, "rpc", "files.list done in 3 ms");
    zxvd_log(ZXVD_LOG_WARN, "rpc", "files.write refused (-32012): Exists");
    zxvd_log(ZXVD_LOG_ERROR, "net", "peer 10.0.0.2 gone");
    zxvd_log(ZXVD_LOG_INFO, "my src", "spaces in the source");
    zxvd_log(ZXVD_LOG_INFO, "", "no source");
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 6 && !more && e[0].seq == 6 &&
              !strcmp(e[0].source, "-") && !strcmp(e[1].source, "my_src") &&
              e[2].level == ZXVD_LOG_ERROR && !strcmp(e[2].text, "peer 10.0.0.2 gone") &&
              e[5].seq == 1 && e[5].ms > 1600000000000ull,
          "all, newest first: %u", n);
    CHECK(zjr_query(&q, e, 2, &n, &more) == 0 && n == 2 && more && e[1].seq == 5, "a page");
    q.before = 5;
    CHECK(zjr_query(&q, e, 2, &n, &more) == 0 && n == 2 && more && e[0].seq == 4 && e[1].seq == 3,
          "the next page");
    q.before = 2;
    CHECK(zjr_query(&q, e, 2, &n, &more) == 0 && n == 1 && !more && e[0].seq == 1, "the last page");
    q.before = 0;
    q.min_level = ZXVD_LOG_WARN;
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 2 && e[0].seq == 4 && e[1].seq == 3,
          "warn and up");
    q.min_level = ZXVD_LOG_ERROR;
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 1 && e[0].seq == 4, "errors");
    q.min_level = 0;
    q.source = "rpc";
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 2, "a source");
    q.source = "rp";
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 0, "exactly");
    q.source = "";
    q.text = "REFUSED";
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 1 && e[0].seq == 3, "text, case ignored");
    q.text = "my_";
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 1 && e[0].seq == 5, "text in the source");
    q.text = "zzz";
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 0, "no such text");
    q.text = NULL;
    q.since_ms = e[0].ms; /* nothing: n was 0 */
    zjr_query_t all = {0};
    zjr_query(&all, e, 10, &n, &more);
    uint64_t t4 = e[2].ms;
    q.since_ms = t4 + 100000000;
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 0, "since the future");
    q.since_ms = 0;
    q.until_ms = 1;
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 0, "until 1970");
    q.until_ms = t4 + 100000000;
    q.since_ms = t4 - 100000000;
    CHECK(zjr_query(&q, e, 10, &n, &more) == 0 && n == 6, "a window");
    q = all;
    CHECK(zjr_verify(&v) == 0 && v.ok && v.segments == 1 && v.entries == 6 && v.first_seq == 1 &&
              v.last_seq == 6 && v.broken_at == 0 && !v.why[0],
          "verified: %llu", (unsigned long long) v.entries);

    /* restart: the chain and the numbers go on in a new segment */
    zjr_close();
    zxvd_log(ZXVD_LOG_INFO, "rpc", "not kept: closed");
    CHECK(zjr_open(JDIR) == 0 && zjr_query(&q, e, 10, &n, &more) == 0 && n == 7 && e[0].seq == 7 &&
              !strcmp(e[0].text, "opened") && e[1].seq == 6,
          "reopened: %u, %llu", n, (unsigned long long) e[0].seq);
    CHECK(jread(jseg(2), text, sizeof text) > 0 && !strncmp(text, "zxv-journal 1 7 ", 16),
          "segment 2: %.40s", text);
    CHECK(zjr_verify(&v) == 0 && v.ok && v.segments == 2 && v.entries == 7, "the chain holds");

    /* a crash mid-line: the torn line is ignored, the chain goes on */
    zjr_close();
    FILE *f = fopen(jseg(2), "ab");
    CHECK(f && fputs("8 1 info rpc 00", f) >= 0 && fclose(f) == 0, "a torn line");
    CHECK(zjr_open(JDIR) == 0 && jlast() == 8 && zjr_verify(&v) == 0 && v.ok && v.segments == 3,
          "after the crash: %llu %s", (unsigned long long) jlast(), v.why);
    /* the head fell behind: the newest is found anyway, nothing overwritten */
    zjr_close();
    CHECK(jwrite(strcat(strcpy(req, JDIR), "/head"), "1\n"), "an old head");
    CHECK(zjr_open(JDIR) == 0 && jread(jseg(4), text, sizeof text) > 0 &&
              !strncmp(text, "zxv-journal 1 9 ", 16) && zjr_verify(&v) == 0 && v.ok,
          "segment 4 after 3: %.30s %s", text, v.why);
    zjr_close();
    CHECK(remove(req) == 0 && zjr_open(JDIR) == 0 && jseg(5) && jlast() == 10, "no head at all");
    zjr_close();
    CHECK(jwrite(req, "nonsense\n") && zjr_open(JDIR) == 0 && jlast() == 11, "a bad head");

    /* tampering shows */
    zjr_close();
    CHECK(jread(jseg(1), text, sizeof text) > 0, "read 1");
    char *gone = strstr(text, "peer 10.0.0.2 gone");
    static char saved[1 << 16];
    strcpy(saved, text);
    CHECK(gone != NULL, "find the line");
    if (gone) gone[0] = 'P';
    CHECK(jwrite(jseg(1), text) && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && !v.ok &&
              v.broken_at == 4 && !strcmp(v.why, "line 4 was changed or is missing") &&
              v.entries == 3,
          "a changed line: %llu %s", (unsigned long long) v.broken_at, v.why);
    CHECK(zjr_query(&q, e, 100, &n, &more) == 0 && n == 9 && e[n - 1].seq == 1 && e[n - 4].seq == 7,
          "query skips the rest of the broken segment: %u", n);
    zjr_close();
    char *l4 = strstr(saved, "\n4 ");
    char cut[1 << 16];
    snprintf(cut, sizeof cut, "%.*s%s", (int) (l4 - saved + 1), saved, strstr(l4 + 1, "\n") + 1);
    CHECK(jwrite(jseg(1), cut) && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && !v.ok &&
              v.broken_at == 4,
          "a removed line");
    zjr_close();
    CHECK(jwrite(jseg(1), saved) && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && v.ok,
          "put back: %s", v.why);
    zjr_close();
    CHECK(jread(jseg(3), text, sizeof text) > 0, "read 3");
    strcpy(saved, text);
    text[20] = text[20] == 'a' ? 'b' : 'a'; /* in the prev hash */
    CHECK(jwrite(jseg(3), text) && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && !v.ok &&
              !strcmp(v.why, "segment 3 does not follow the one before it") && v.broken_at == 8,
          "a header that does not follow: %s %llu", v.why, (unsigned long long) v.broken_at);
    zjr_close();
    CHECK(jwrite(jseg(3), "zxv-journal 1 x\n") && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 &&
              !v.ok && !strcmp(v.why, "segment 3 is missing or its header is damaged"),
          "a bad header: %s", v.why);
    zjr_close();
    CHECK(remove(jseg(3)) == 0 && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && !v.ok &&
              v.segments == 2 && v.first_seq == 1 && v.broken_at == 8 &&
              !strcmp(v.why, "segment 3 is missing or its header is damaged"),
          "a segment gone from the middle: %u %s", v.segments, v.why);
    zjr_close();
    CHECK(jwrite(jseg(3), saved) && zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && v.ok,
          "whole again: %s", v.why);
    zjr_close();
    CHECK(zjr_open(JDIR) == 0 && jlast() > 0, "still whole");

    /* size limits: rotate, then let the oldest go */
    zjr_close();
    zjr_set_limits(300, 2000);
    CHECK(zjr_open(JDIR) == 0, "small limits");
    for (int i = 0; i < 60; i++) zxvd_log(ZXVD_LOG_INFO, "rpc", "event number %d of sixty", i);
    CHECK(!bk_exists(jseg(1)) && zjr_verify(&v) == 0 && v.ok && v.first_seq > 1 && v.segments > 3 &&
              v.last_seq == jlast(),
          "rotated and pruned: %u segments from %llu, %s", v.segments,
          (unsigned long long) v.first_seq, v.why);
    CHECK(zjr_query(&q, e, 600, &n, &more) == 0 && !more && n == v.entries &&
              e[0].seq == v.last_seq && e[n - 1].seq == v.first_seq &&
              !strcmp(e[0].text, "event number 59 of sixty"),
          "the kept, all of them: %u", n);
    zjr_close();
    CHECK(zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && v.ok, "reopened after pruning");
    zjr_set_limits(0, 0);

    /* over JSON-RPC */
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\",\"params\":{\"max\":2,"
             "\"level\":\"info\",\"text\":\"SIXTY\"}}");
    const char *o = rpc(req);
    CHECK(strstr(o, "\"result\":{\"on\":true,\"entries\":[{\"seq\":") &&
              strstr(o,
                     ",\"level\":\"info\",\"source\":\"rpc\",\"text\":\"event number 59 of sixty\","
                     "\"hash\":\"") &&
              strstr(o, "],\"more\":true}"),
          "journal.query: %s", o);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\",\"params\":{\"source\":"
             "\"journal\",\"level\":\"warn\",\"since_ms\":1,\"until_ms\":99999999999999,"
             "\"before\":99999}}");
    CHECK(strstr(rpc(req), "{\"on\":true,\"entries\":[],\"more\":false}"), "nothing at warn: %s",
          OUT.p);
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\",\"params\":{\"source\":"
             "\"journal\",\"level\":\"error\"}}");
    CHECK(strstr(rpc(req), "\"entries\":[]"), "level error: %s", OUT.p);
    CHECK(strstr(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\",\"params\":"
                     "{\"source\":\"journal\",\"max\":500}}"),
                 "\"text\":\"opened\""),
          "opened, at most 500: %s", OUT.p);
    CHECK(strstr(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.verify\"}"),
                 "\"result\":{\"on\":true,\"ok\":true,\"segments\":"),
          "journal.verify: %s", OUT.p);
    CHECK(has(REQ("1", "journal.query", ",\"params\":{\"max\":1}"),
              "\"result\":{\"on\":true,\"entries\":[{") &&
              count(OUT.p, "{\"seq\":") == 1,
          "max 1: %s", OUT.p);
    EXPECT(REQ("1", "journal.query", ",\"params\":{\"max\":0}"),
           ERR("1", "-32602", "\\\"max\\\" must be 1 to 500"));
    EXPECT(REQ("1", "journal.query", ",\"params\":{\"max\":501}"),
           ERR("1", "-32602", "\\\"max\\\" must be 1 to 500"));
    EXPECT(REQ("1", "journal.query", ",\"params\":{\"level\":\"loud\"}"),
           ERR("1", "-32602", "\\\"level\\\" must be info, warn or error"));
    EXPECT(REQ("1", "journal.query", ",\"params\":{\"source\":\"aaaaaaaaaaaaaaaaa\"}"),
           ERR("1", "-32602", "\\\"source\\\" must be a string of at most 15 bytes"));
    EXPECT(REQ("1", "journal.query", ",\"params\":{\"since_ms\":-1}"),
           ERR("1", "-32602", "\\\"since_ms\\\" must not be negative"));
    uint64_t before = jlast();
    rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"journal.query\"}");
    CHECK(jlast() == before, "journal.query is quiet");
    rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"files.root\",\"params\":{}}");
    rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"zxvd.version\"}");
    rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"files.mkdir\",\"params\":{\"path\":\"\"}}");
    CHECK(jlast() == before + 1, "a refusal reaches the journal: %llu",
          (unsigned long long) (jlast() - before));
    zjr_close();
    CHECK(rm_tree(sub) == 0 && remove(afile) == 0, "cleanup");
    *strrchr(sub, '/') = 0;
    CHECK(rmdir(sub) == 0, "cleanup 2");
}

/* ---------------- screen time (zxv_screen.h) ---------------- */

static int64_t SNOW;
static int64_t snow(void)
{
    return SNOW;
}

static void test_screen(void)
{
    char *oldtz = getenv("TZ") ? strdup(getenv("TZ")) : NULL;
    setenv("TZ", "UTC", 1);
    tzset();
    zsc_set_clock(snow);
    SNOW = 1791799200; /* 2026-10-12 10:00 UTC */
    zsc_answer_t a;
    CHECK(zsc_open(NULL) == 0 && zsc_today() == 20261012 && zsc_day_count() == 0, "in memory");

    /* SC1: one minute a tick, not twice in the same minute */
    CHECK(zsc_tick("quillon", &a) == 0 && a.counted && a.used == 1 && a.total == 1 && a.allowed &&
              a.left == -1 && !a.why[0],
          "a minute");
    SNOW += 44;
    CHECK(zsc_tick("glint", &a) == 0 && !a.counted && a.used == 0 && a.total == 1, "too soon");
    SNOW += 1;
    CHECK(zsc_tick("quillon", &a) == 0 && a.counted && a.used == 2 && a.total == 2, "45 s on");
    CHECK(zsc_tick("*", &a) == ZSC_E_ARG && zsc_tick("a b", &a) == ZSC_E_ARG &&
              zsc_tick("", &a) == ZSC_E_ARG && zsc_tick(NULL, &a) == ZSC_E_ARG &&
              zsc_tick("abcdefghijklmnopqrstuvwxyz123456", &a) == ZSC_E_ARG &&
              zsc_check("x*", &a) == ZSC_E_ARG && zsc_check("é", &a) == ZSC_E_ARG &&
              zsc_tick("abcdefghijklmnopqrstuvwxyz12345", &a) == 0,
          "app names");
    CHECK(zsc_day_count() == 1 && zsc_day(0)->n == 1 && zsc_day(0)->total == 2 &&
              zsc_day(0)->a[0].minutes == 2 && !zsc_day(1),
          "today");

    /* SC2, SC5: a daily cap */
    CHECK(zsc_set_limit(NULL, "quillon", 3, 0, 0) == 0 && zsc_check("quillon", &a) == 0 &&
              a.allowed && a.left == 1,
          "one minute left: %d", a.left);
    SNOW += 60;
    CHECK(zsc_tick("quillon", &a) == 0 && a.counted && !a.allowed && a.left == 0 && a.used == 3 &&
              !strcmp(a.why, "today's 3 minutes of quillon are used up"),
          "used up: %s", a.why);
    SNOW += 60;
    CHECK(zsc_tick("quillon", &a) == 0 && a.used == 4 && !a.allowed && a.left == 0,
          "still counted past the cap");
    /* SC4: more time, for today only */
    CHECK(zsc_grant(NULL, "quillon", 2) == 0 && zsc_check("quillon", &a) == 0 && a.allowed &&
              a.left == 1,
          "granted: %d", a.left);
    CHECK(zsc_grant(NULL, "quillon", 1440) == 0 && zsc_limit(0)->extra == 1440, "at most a day");
    CHECK(zsc_grant(NULL, "glint", 5) == ZSC_E_NOTFOUND &&
              zsc_grant(NULL, "quillon", 0) == ZSC_E_ARG &&
              zsc_grant(NULL, "quillon", 1441) == ZSC_E_ARG,
          "grant refusals");
    SNOW += 86400;
    CHECK(zsc_check("quillon", &a) == 0 && a.used == 0 && a.left == 3 && a.allowed &&
              zsc_today() == 20261013,
          "the next day: the grant lapsed");
    CHECK(zsc_grant(NULL, "quillon", 1) == 0 && zsc_check("quillon", &a) == 0 && a.left == 4,
          "a grant on a new day starts over");
    CHECK(zsc_set_limit(NULL, "quillon", 0, 0, 0) == 0 && zsc_check("quillon", &a) == 0 &&
              a.allowed && a.left == 1,
          "0 minutes plus the grant");
    SNOW += 60;
    CHECK(zsc_tick("quillon", &a) == 0 && !a.allowed && a.left == 0 &&
              !strcmp(a.why, "today's 1 minute of quillon is used up"),
          "the grant used: %s", a.why);
    CHECK(zsc_set_limit(NULL, "quillon", -2, 0, 0) == ZSC_E_ARG &&
              zsc_set_limit(NULL, "quillon", 1441, 0, 0) == ZSC_E_ARG &&
              zsc_set_limit(NULL, "quillon", 1, 1440, 0) == ZSC_E_ARG &&
              zsc_set_limit(NULL, "quillon", 1, 0, 1440) == ZSC_E_ARG &&
              zsc_set_limit(NULL, "a b", 1, 0, 0) == ZSC_E_ARG,
          "limit refusals");

    /* hours */
    CHECK(zsc_set_limit(NULL, "glint", -1, 8 * 60, 9 * 60) == 0 && zsc_check("glint", &a) == 0 &&
              !a.allowed && a.left == 0 &&
              !strcmp(a.why, "glint can be used only from 08:00 to 09:00"),
          "outside the hours: %s", a.why);
    /* it is 10:03 */
    CHECK(zsc_set_limit(NULL, "glint", -1, 10 * 60 + 3, 10 * 60 + 4) == 0 &&
              zsc_check("glint", &a) == 0 && a.allowed && a.left == -1,
          "the first minute of the hours");
    CHECK(zsc_set_limit(NULL, "glint", -1, 10 * 60 + 4, 10 * 60 + 9) == 0 &&
              zsc_check("glint", &a) == 0 && !a.allowed,
          "not yet");
    CHECK(zsc_set_limit(NULL, "glint", -1, 9 * 60, 10 * 60 + 3) == 0 &&
              zsc_check("glint", &a) == 0 && !a.allowed,
          "the end is not in the hours");
    CHECK(zsc_set_limit(NULL, "glint", -1, 22 * 60, 11 * 60) == 0 && zsc_check("glint", &a) == 0 &&
              a.allowed,
          "past midnight: in");
    CHECK(zsc_set_limit(NULL, "glint", -1, 22 * 60, 10 * 60) == 0 && zsc_check("glint", &a) == 0 &&
              !a.allowed,
          "past midnight: out");
    SNOW += 12 * 3600; /* 22:00 */
    CHECK(zsc_check("glint", &a) == 0 && a.allowed, "22:00 is in");
    SNOW -= 12 * 3600;
    CHECK(zsc_set_limit(NULL, "glint", 30, 600, 600) == 0 && zsc_check("glint", &a) == 0 &&
              a.allowed && a.left == 30 && zsc_limit_count() == 2,
          "equal hours: any time");

    /* "*": all apps together */
    CHECK(zsc_set_limit(NULL, "*", 2, 0, 0) == 0 && zsc_tick("burrow", &a) == 0 && a.allowed &&
              a.left == 1,
          "one of two");
    SNOW += 60;
    CHECK(zsc_tick("burrow", &a) == 0 && !a.allowed && a.left == 0 &&
              !strcmp(a.why, "today's 2 minutes are used up"),
          "all used up: %s", a.why);
    CHECK(zsc_check("glint", &a) == 0 && !a.allowed && a.left == 0, "for every app");
    CHECK(zsc_set_limit(NULL, "*", -1, 23 * 60, 6 * 60) == 0 && zsc_check("burrow", &a) == 0 &&
              !strcmp(a.why, "the machine can be used only from 23:00 to 06:00"),
          "the machine's hours: %s", a.why);
    CHECK(zsc_clear_limit(NULL, "*") == 0 && zsc_clear_limit(NULL, "*") == ZSC_E_NOTFOUND &&
              zsc_clear_limit(NULL, "") == ZSC_E_ARG && zsc_check("burrow", &a) == 0 && a.allowed,
          "cleared");

    /* SC3: the guardian */
    CHECK(!zsc_locked() && zsc_lock(NULL, "abc") == ZSC_E_ARG && !zsc_locked(), "too short");
    static char longpass[ZSC_PASS_MAX + 2];
    memset(longpass, 'p', ZSC_PASS_MAX + 1);
    CHECK(zsc_lock(NULL, longpass) == ZSC_E_ARG, "too long");
    longpass[ZSC_PASS_MAX] = 0;
    CHECK(zsc_lock(NULL, longpass) == 0 && zsc_locked() && zsc_lock(longpass, "open sesame") == 0,
          "locked, then changed");
    CHECK(zsc_set_limit(NULL, "glint", 1, 0, 0) == ZSC_E_PASS &&
              zsc_set_limit("open", "glint", 1, 0, 0) == ZSC_E_PASS &&
              zsc_clear_limit(longpass, "glint") == ZSC_E_PASS && zsc_wait() == 0,
          "three wrong");
    CHECK(zsc_set_limit("open sesame", "glint", 5, 0, 0) == 0 && zsc_limit(1)->minutes == 5,
          "right: the count starts over");
    for (int i = 0; i < 4; i++) CHECK(zsc_grant("x", "glint", 1) == ZSC_E_PASS, "wrong %d", i);
    CHECK(zsc_wait() == 0 && zsc_grant("y", "glint", 1) == ZSC_E_PASS && zsc_wait() == ZSC_WAIT_S,
          "the fifth: wait %lld", (long long) zsc_wait());
    CHECK(zsc_grant("open sesame", "glint", 1) == ZSC_E_WAIT &&
              zsc_lock("open sesame", "") == ZSC_E_WAIT,
          "even the right one waits");
    SNOW += ZSC_WAIT_S - 1;
    CHECK(zsc_wait() == 1 && zsc_grant("open sesame", "glint", 1) == ZSC_E_WAIT, "a second more");
    SNOW += 1;
    CHECK(zsc_grant("open sesame", "glint", 1) == 0 && zsc_tick("glint", &a) == 0,
          "after the wait");
    CHECK(zsc_lock("nope", "") == ZSC_E_PASS && zsc_lock("open sesame", "") == 0 && !zsc_locked() &&
              zsc_set_limit(NULL, "glint", 5, 0, 0) == 0,
          "unlocked");

    /* SC6: the file */
    char dir[256], path[300], aside[320];
    snprintf(dir, sizeof dir, "%s/zxv-screen-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dir) != NULL, "a folder");
    snprintf(path, sizeof path, "%s/screen.zxst", dir);
    snprintf(aside, sizeof aside, "%s.damaged", path);
    CHECK(zsc_open(path) == 0 && zsc_day_count() == 0 && zsc_limit_count() == 0 && !zsc_locked() &&
              bk_exists(path) && !zsc_was_reset(),
          "a new file");
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600, "0600: %o",
          (unsigned) (st.st_mode & 0777));
    SNOW += 3600;
    CHECK(zsc_tick("quillon", &a) == 0 && zsc_set_limit(NULL, "quillon", 30, 480, 1260) == 0 &&
              zsc_set_limit(NULL, "*", -1, 0, 0) == 0 && zsc_grant(NULL, "quillon", 5) == 0 &&
              zsc_lock(NULL, "open sesame") == 0,
          "changes");
    SNOW -= 86400; /* a tick on the day before */
    CHECK(zsc_tick("glint", &a) == 0 && zsc_day_count() == 2, "yesterday");
    SNOW += 86400;
    zsc_close();
    CHECK(!zsc_locked() && zsc_limit_count() == 0 && zsc_day_count() == 0, "closed");
    CHECK(zsc_open(path) == 0 && !zsc_was_reset() && zsc_locked() && zsc_limit_count() == 2 &&
              zsc_limit(0)->minutes == 30 && zsc_limit(0)->from == 480 &&
              zsc_limit(0)->to == 1260 && zsc_limit(0)->extra == 5 && zsc_limit(1)->minutes == -1 &&
              zsc_day_count() == 2 && zsc_day(0)->date == 20261013 &&
              zsc_day(1)->date == 20261012 && zsc_day(0)->a[0].minutes == 1 &&
              !strcmp(zsc_day(1)->a[0].app, "glint"),
          "reloaded: %u limits %u days", zsc_limit_count(), zsc_day_count());
    CHECK(zsc_check("quillon", &a) == 0 && a.left == 34 &&
              zsc_set_limit("x", "a", 1, 0, 0) == ZSC_E_PASS &&
              zsc_set_limit("open sesame", "a", 1, 0, 0) == 0,
          "the same passphrase");
    zsc_close();
    static char text[1 << 16];
    CHECK(jread(path, text, sizeof text) > 0 && strstr(text, "\nlock ") &&
              strstr(text, "\nlimit quillon 30 480 1260 20261013 5\n") &&
              strstr(text, "\nday 20261012 glint 1\n") && !strstr(text, "open sesame"),
          "the file: %s", text);
    /* damage, in several ways */
    const char *edits[][2] = {{"limit quillon 30", "limit quillon 99"},
                              {"zxv-screen 1", "zxv-screen 2"},
                              {"\nend ", "\nEnd "}};
    for (size_t i = 0; i < sizeof edits / sizeof *edits; i++) {
        CHECK(jread(path, text, sizeof text) > 0, "read");
        char *at = strstr(text, edits[i][0]);
        CHECK(at != NULL, "find %zu", i);
        if (at) memcpy(at, edits[i][1], strlen(edits[i][1]));
        remove(aside);
        CHECK(jwrite(path, text) && zsc_open(path) == 0 && zsc_was_reset() && !zsc_locked() &&
                  zsc_limit_count() == 0 && zsc_day_count() == 0 && bk_exists(aside),
              "damaged %zu: moved aside", i);
        CHECK(zsc_tick("quillon", &a) == 0 && zsc_set_limit(NULL, "quillon", 30, 480, 1260) == 0,
              "starts again");
        zsc_close();
    }
    /* well-formed files whose lines are wrong */
    const char *bodies[] = {
        "limit quillon 30 480 1260 0 0\nlimit quillon 30 480 1260 0 0\n",
        "limit quillon 1441 0 0 0 0\n",
        "limit quillon -2 0 0 0 0\n",
        "limit quillon 1 1440 0 0 0\n",
        "limit quillon 1 0 1440 0 0\n",
        "limit quillon 1 0 0 0 100001\n",
        "limit quillon 1 0 0 99991232 0\n",
        "limit a*b 1 0 0 0 0\n",
        "limit quillon 1 0 0 0 0 x\n",
        "day 20261012 * 3\n",
        "day 20261012 glint 1441\n",
        "day 19691231 glint 1\n",
        "day 20261012 glint 1\nday 20261012 glint 2\n",
        "lock 00 00\n",
        "lock 000000000000000000000000000000zz "
        "0000000000000000000000000000000000000000000000000000000000000000\n",
        "lock 00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000\n"
        "lock 00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000\n",
        "something else\n",
    };
    for (size_t i = 0; i < sizeof bodies / sizeof *bodies; i++) {
        uint8_t h[32];
        char body[1024], hex[65];
        int n = snprintf(body, sizeof body, "zxv-screen 1\n%s", bodies[i]);
        sha256((const uint8_t *) body, (size_t) n, h);
        for (int k = 0; k < 32; k++) snprintf(hex + 2 * k, 3, "%02x", h[k]);
        snprintf(body + n, sizeof body - (size_t) n, "end %s\n", hex);
        remove(aside);
        CHECK(jwrite(path, body) && zsc_open(path) == 0 && zsc_was_reset() && bk_exists(aside),
              "a bad line %zu: %s", i, bodies[i]);
        zsc_close();
    }
    {
        /* a good file written by hand, with days older than kept */
        char body[1 << 12], hex[65];
        uint8_t h[32];
        int n = snprintf(body, sizeof body, "zxv-screen 1\nlimit * 60 0 0 0 0\n");
        for (int d = 1; d <= 9; d++)
            for (int m = 1; m <= 4; m++)
                catf(body, sizeof body, &n, "day 2026%02d%02d a%d %d\n", m, d * 3, d, d);
        sha256((const uint8_t *) body, (size_t) n, h);
        for (int k = 0; k < 32; k++) snprintf(hex + 2 * k, 3, "%02x", h[k]);
        snprintf(body + n, sizeof body - (size_t) n, "end %s\n", hex);
        CHECK(jwrite(path, body) && zsc_open(path) == 0 && !zsc_was_reset() &&
                  zsc_day_count() == ZSC_DAYS && zsc_day(0)->date == 20260427 &&
                  zsc_day(ZSC_DAYS - 1)->date == 20260118 && zsc_limit_count() == 1,
              "the newest 31 days: %u, %u", zsc_day_count(),
              zsc_day(ZSC_DAYS - 1) ? zsc_day(ZSC_DAYS - 1)->date : 0);
        zsc_close();
    }
    CHECK(jwrite(path, "short\n") && zsc_open(path) == 0 && zsc_was_reset(), "too short");
    zsc_close();
    char nodir[320];
    snprintf(nodir, sizeof nodir, "%s/no/such/screen.zxst", dir);
    CHECK(zsc_open(nodir) == ZSC_E_IO && zsc_tick("a", &a) == 0, "no folder: memory only");
    static char longpath[ZSC_PATH_MAX + 2];
    memset(longpath, 'x', ZSC_PATH_MAX + 1);
    CHECK(zsc_open(longpath) == ZSC_E_ARG, "too long a path");
    for (int i = -1; i >= -7; i--) CHECK(zsc_strerror(i)[0], "strerror %d", i);
    CHECK(strcmp(zsc_strerror(ZSC_E_PASS), zsc_strerror(-99)), "unknown");

    /* SC1: the newest 31 days, a new day in front */
    CHECK(zsc_open(path) == 0, "again");
    remove(aside);
    for (int d = 0; d < 40; d++) {
        SNOW += 86400;
        CHECK(zsc_tick("quillon", &a) == 0 && a.counted, "day %d", d);
    }
    CHECK(zsc_day_count() == ZSC_DAYS && zsc_day(0)->date > zsc_day(1)->date &&
              zsc_day(0)->date == zsc_today(),
          "31 days");
    for (int i = 0; i < (int) ZSC_APPS; i++) {
        char app[16];
        snprintf(app, sizeof app, "app%d", i);
        SNOW += 60;
        zsc_tick(app, &a);
    }
    SNOW += 60;
    CHECK(zsc_tick("one-more", &a) == ZSC_E_FULL, "64 apps a day");
    for (int i = (int) zsc_limit_count(); i < (int) ZSC_APPS; i++) {
        char app[16];
        snprintf(app, sizeof app, "app%d", i);
        CHECK(zsc_set_limit(NULL, app, 1, 0, 0) == 0, "limit %d", i);
    }
    CHECK(zsc_set_limit(NULL, "one-more", 1, 0, 0) == ZSC_E_FULL, "64 limits");
    for (int i = 0; i < 3 * 1440; i++) {
        SNOW += 60;
        zsc_tick("app0", &a);
    }
    CHECK(zsc_day(0)->total <= 1440 && zsc_day(1)->total <= 1440, "no more than a day in a day");

    /* over JSON-RPC */
    zsc_close();
    CHECK(zsc_open(NULL) == 0, "memory");
    SNOW = 1791799200;
    CHECK(has(REQ("1", "screen.tick", ",\"params\":{\"app\":\"quillon\"}"),
              "\"result\":{\"app\":\"quillon\",\"counted\":true,\"allowed\":true,\"used\":1,"
              "\"total\":1,\"left\":-1,\"why\":\"\"}"),
          "screen.tick: %s", OUT.p);
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"quillon\",\"minutes\":1}"),
           OK("1", "{\"app\":\"quillon\",\"set\":true}"));
    CHECK(has(REQ("1", "screen.check", ",\"params\":{\"app\":\"quillon\"}"),
              "{\"app\":\"quillon\",\"allowed\":false,\"used\":1,\"total\":1,\"left\":0,\"why\":"
              "\"today's 1 minute of quillon is used up\"}"),
          "screen.check: %s", OUT.p);
    EXPECT(REQ("1", "screen.grant", ",\"params\":{\"app\":\"quillon\",\"minutes\":10}"),
           OK("1", "{\"app\":\"quillon\",\"left\":10}"));
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"*\",\"from\":480,\"to\":1260}"),
           OK("1", "{\"app\":\"*\",\"set\":true}"));
    EXPECT(REQ("1", "screen.grant", ",\"params\":{\"app\":\"*\",\"minutes\":10}"),
           OK("1", "{\"app\":\"*\"}"));
    CHECK(has(REQ("1", "screen.status", ""),
              "\"result\":{\"locked\":false,\"reset\":false,\"wait_s\":0,\"today\":20261012,"
              "\"limits\":[{\"app\":\"quillon\",\"minutes\":1,\"from\":0,\"to\":0,\"extra\":10},"
              "{\"app\":\"*\",\"minutes\":null,\"from\":480,\"to\":1260,\"extra\":10}],\"days\":["
              "{\"date\":20261012,\"total\":1,\"apps\":[{\"app\":\"quillon\",\"minutes\":1}]}]}"),
          "screen.status: %s", OUT.p);
    SNOW += 86400 + 60;
    rpc(REQ("1", "screen.tick", ",\"params\":{\"app\":\"glint\"}"));
    rpc(REQ("1", "screen.tick", ",\"params\":{\"app\":\"burrow\"}"));
    CHECK(has(REQ("1", "screen.status", ",\"params\":{\"days\":1}"),
              "\"extra\":0}],\"days\":[{\"date\":20261013,\"total\":1,\"apps\":[{\"app\":\"glint\","
              "\"minutes\":1}]}]}"),
          "one day, yesterday's grants gone: %s", OUT.p);
    EXPECT(REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"open sesame\"}"),
           OK("1", "{\"locked\":true}"));
    EXPECT(REQ("1", "screen.clear_limit", ",\"params\":{\"app\":\"*\"}"),
           ERR("1", "-32019", "the guardian's passphrase is wrong"));
    EXPECT(REQ("1", "screen.clear_limit",
               ",\"params\":{\"app\":\"*\",\"passphrase\":\"open sesame\"}"),
           OK("1", "{\"app\":\"*\",\"cleared\":true}"));
    EXPECT(REQ("1", "screen.clear_limit",
               ",\"params\":{\"app\":\"*\",\"passphrase\":\"open sesame\"}"),
           ERR("1", "-32019", "no limit for that app"));
    EXPECT(REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"\",\"old\":\"open sesame\"}"),
           OK("1", "{\"locked\":false}"));
    EXPECT(
        REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"abc\"}"),
        ERR("1", "-32602", "\\\"passphrase\\\" must be at least 4 bytes, or \\\"\\\" to unlock"));
    EXPECT(REQ("1", "screen.lock", ""),
           ERR("1", "-32602", "missing string parameter \\\"passphrase\\\""));
    EXPECT(REQ("1", "screen.tick", ",\"params\":{\"app\":\"a b\"}"),
           ERR("1", "-32602", "a bad app name, minutes or hours"));
    EXPECT(REQ("1", "screen.tick", ""), ERR("1", "-32602", "missing string parameter \\\"app\\\""));
    EXPECT(REQ("1", "screen.check", ",\"params\":{\"app\":\"*\"}"),
           ERR("1", "-32602", "a bad app name, minutes or hours"));
    EXPECT(REQ("1", "screen.status", ",\"params\":{\"days\":0}"),
           ERR("1", "-32602", "\\\"days\\\" must be 1 to 31"));
    EXPECT(REQ("1", "screen.status", ",\"params\":{\"days\":32}"),
           ERR("1", "-32602", "\\\"days\\\" must be 1 to 31"));
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"a\",\"minutes\":1441}"),
           ERR("1", "-32602", "\\\"minutes\\\" must be 0 to 1440"));
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"a\",\"from\":1440}"),
           ERR("1", "-32602", "\\\"from\\\" must be 0 to 1439 (minutes after midnight)"));
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"a\",\"to\":-1}"),
           ERR("1", "-32602", "\\\"to\\\" must not be negative"));
    EXPECT(REQ("1", "screen.grant", ",\"params\":{\"app\":\"a\"}"),
           ERR("1", "-32602", "\\\"minutes\\\" must be 1 to 1440"));
    EXPECT(REQ("1", "screen.grant", ",\"params\":{\"app\":\"a\",\"minutes\":1441}"),
           ERR("1", "-32602", "\\\"minutes\\\" must be 1 to 1440"));
    EXPECT(REQ("1", "screen.grant", ",\"params\":{\"app\":\"nolimit\",\"minutes\":5}"),
           ERR("1", "-32019", "no limit for that app"));
    /* the edges of each range are inside it */
    CHECK(has(REQ("1", "screen.status", ",\"params\":{\"days\":31}"), "\"result\":{\"locked\":"),
          "31 days: %s", OUT.p);
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"quillon\",\"minutes\":1440}"),
           OK("1", "{\"app\":\"quillon\",\"set\":true}"));
    EXPECT(REQ("1", "screen.set_limit", ",\"params\":{\"app\":\"edge\",\"from\":1439,\"to\":0}"),
           OK("1", "{\"app\":\"edge\",\"set\":true}"));
    CHECK(has(REQ("1", "screen.grant", ",\"params\":{\"app\":\"quillon\",\"minutes\":1}"),
              "\"result\":{\"app\":\"quillon\",\"left\":"),
          "grant 1: %s", OUT.p);
    CHECK(has(REQ("1", "screen.grant", ",\"params\":{\"app\":\"quillon\",\"minutes\":1440}"),
              "\"result\":{\"app\":\"quillon\",\"left\":"),
          "grant 1440: %s", OUT.p);
    EXPECT(
        REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"a\"}"),
        ERR("1", "-32602", "\\\"passphrase\\\" must be at least 4 bytes, or \\\"\\\" to unlock"));
    EXPECT(REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"abcd\"}"),
           OK("1", "{\"locked\":true}"));
    EXPECT(REQ("1", "screen.lock", ",\"params\":{\"passphrase\":\"\",\"old\":\"abcd\"}"),
           OK("1", "{\"locked\":false}"));
    zsc_close();
    zsc_set_clock(NULL);
    CHECK(zsc_today() > 20260000, "the real clock");
    if (oldtz)
        setenv("TZ", oldtz, 1);
    else
        unsetenv("TZ");
    free(oldtz);
    tzset();
    CHECK(rm_tree(dir) == 0, "cleanup");
}

/* ---------------- location (zxv_place.h) ---------------- */

static int64_t PNOW;
static int64_t pnow(void)
{
    return PNOW;
}

static void test_place(void)
{
    zpl_set_clock(pnow);
    PNOW = 1791799200;
    zpl_here_t h;
    const zpl_point_t paris = {48856600, 2352200}, london = {51507400, -127800};
    CHECK(zpl_open(NULL) == 0 && !zpl_here()->known && !zpl_stale() && zpl_count() == 0,
          "in memory");

    /* P4 */
    uint64_t d = zpl_distance_m(paris, london);
    CHECK(d > 340000 && d < 347000, "Paris to London: %llu m", (unsigned long long) d);
    CHECK(zpl_distance_m(london, paris) == d && zpl_distance_m(paris, paris) == 0, "symmetric");
    CHECK(zpl_distance_m((zpl_point_t){0, 0}, (zpl_point_t){0, 1000000}) == 111320 &&
              zpl_distance_m((zpl_point_t){0, 0}, (zpl_point_t){1000000, 0}) == 111132,
          "a degree at the equator");
    d = zpl_distance_m((zpl_point_t){0, 179900000}, (zpl_point_t){0, -179900000});
    CHECK(d == 22264, "across the date line: %llu", (unsigned long long) d);
    d = zpl_distance_m((zpl_point_t){0, -179900000}, (zpl_point_t){0, 179900000});
    CHECK(d == 22264, "and back: %llu", (unsigned long long) d);
    d = zpl_distance_m((zpl_point_t){60000000, 0}, (zpl_point_t){60000000, 1000000});
    CHECK(d == 55660, "a degree at 60 north: %llu", (unsigned long long) d);
    d = zpl_distance_m((zpl_point_t){-60500000, 0}, (zpl_point_t){-60500000, 1000000});
    CHECK(d > 54700 && d < 54900, "between table rows, south: %llu", (unsigned long long) d);
    CHECK(zpl_distance_m((zpl_point_t){90000000, 0}, (zpl_point_t){90000000, 90000000}) == 0,
          "at the pole");
    CHECK(zpl_valid((zpl_point_t){90000000, 180000000}) &&
              zpl_valid((zpl_point_t){-90000000, -180000000}) &&
              !zpl_valid((zpl_point_t){90000001, 0}) && !zpl_valid((zpl_point_t){-90000001, 0}) &&
              !zpl_valid((zpl_point_t){0, 180000001}) && !zpl_valid((zpl_point_t){0, -180000001}),
          "valid points");

    /* P1 */
    CHECK(zpl_set_here(paris, 0, "owner") == ZPL_E_ARG &&
              zpl_set_here(paris, 1000001, "owner") == ZPL_E_ARG &&
              zpl_set_here(paris, 5, "") == ZPL_E_ARG &&
              zpl_set_here(paris, 5, NULL) == ZPL_E_ARG &&
              zpl_set_here(paris, 5, "sixteen-bytes-xx") == ZPL_E_ARG &&
              zpl_set_here((zpl_point_t){0, 180000001}, 5, "owner") == ZPL_E_ARG &&
              !zpl_here()->known,
          "bad locations");
    CHECK(zpl_set_here(paris, 1000000, "fifteen-bytes-x") == 0 &&
              zpl_set_here(paris, 5, "owner") == 0 && zpl_here()->known &&
              zpl_here()->accuracy_m == 5 && !strcmp(zpl_here()->source, "owner") &&
              zpl_here()->time == PNOW && !zpl_stale(),
          "here");
    PNOW += ZPL_STALE_S;
    CHECK(!zpl_stale(), "ten minutes is fresh");
    PNOW += 1;
    CHECK(zpl_stale(), "then stale");

    /* P2 */
    CHECK(zpl_ask("glint", &h) == ZPL_E_PENDING && !h.known && zpl_app_count() == 1 &&
              zpl_app(0)->pending && zpl_app(0)->asked == PNOW && zpl_app(0)->mode == ZPL_ASK,
          "asks: pending");
    CHECK(zpl_ask("glint", &h) == ZPL_E_PENDING && zpl_app_count() == 1, "one entry per app");
    CHECK(zpl_permit("glint", ZPL_ONCE) == 0 && !zpl_app(0)->pending && zpl_ask("glint", &h) == 0 &&
              h.known && h.at.lat == paris.lat && zpl_ask("glint", &h) == ZPL_E_PENDING,
          "once is once");
    CHECK(zpl_permit("glint", ZPL_ALWAYS) == 0 && zpl_ask("glint", &h) == 0 &&
              zpl_ask("glint", &h) == 0,
          "always");
    zpl_forget_here();
    CHECK(zpl_ask("glint", &h) == ZPL_E_NONE && !h.known, "allowed, not known");
    CHECK(zpl_permit("quillon", ZPL_NEVER) == 0 && zpl_ask("quillon", &h) == ZPL_E_DENIED, "never");
    CHECK(zpl_ask("a b", &h) == ZPL_E_ARG && zpl_ask("", &h) == ZPL_E_ARG &&
              zpl_permit("glint", (zpl_mode_t) 4) == ZPL_E_ARG &&
              zpl_permit("abcdefghijklmnopqrstuvwxyz123456", ZPL_NEVER) == ZPL_E_ARG,
          "bad asks");
    CHECK(zpl_mode_parse("always") == ZPL_ALWAYS && zpl_mode_parse("ask") == ZPL_ASK &&
              zpl_mode_parse("never") == ZPL_NEVER && zpl_mode_parse("once") == ZPL_ONCE &&
              zpl_mode_parse("Always") == -1 && zpl_mode_parse(NULL) == -1 &&
              !strcmp(zpl_mode_name(ZPL_ONCE), "once") &&
              !strcmp(zpl_mode_name((zpl_mode_t) 9), "ask"),
          "modes");
    for (uint32_t i = zpl_app_count(); i < ZPL_APPS; i++) {
        char app[16];
        snprintf(app, sizeof app, "app%u", i);
        CHECK(zpl_ask(app, &h) == ZPL_E_PENDING, "app %u", i);
    }
    CHECK(zpl_ask("one-more", &h) == ZPL_E_FULL && zpl_permit("one-more", ZPL_NEVER) == ZPL_E_FULL,
          "64 apps");

    /* P3 */
    CHECK(zpl_add("Home", paris, "the flat") == 0 && zpl_add("home", london, "") == ZPL_E_EXISTS &&
              zpl_add("Office", london, NULL) == 0 && zpl_count() == 2 &&
              !strcmp(zpl_find("OFFICE")->name, "Office") && !zpl_find("Offic") &&
              !zpl_find(NULL) && !strcmp(zpl_place(1)->note, ""),
          "places");
    CHECK(zpl_add("", paris, "") == ZPL_E_ARG && zpl_add(" lead", paris, "") == ZPL_E_ARG &&
              zpl_add("tab\there", paris, "") == ZPL_E_ARG &&
              zpl_add("x", paris, "line\nbreak") == ZPL_E_ARG &&
              zpl_add("del\x7f", paris, "") == ZPL_E_ARG &&
              zpl_add("x", (zpl_point_t){90000001, 0}, "") == ZPL_E_ARG &&
              zpl_add(NULL, paris, "") == ZPL_E_ARG,
          "bad places");
    static char longname[ZPL_NAME_MAX + 2], longnote[ZPL_NOTE_MAX + 2];
    memset(longname, 'n', ZPL_NAME_MAX + 1);
    memset(longnote, 'o', ZPL_NOTE_MAX + 1);
    CHECK(zpl_add(longname, paris, "") == ZPL_E_ARG && zpl_add("n", paris, longnote) == ZPL_E_ARG,
          "too long");
    longname[ZPL_NAME_MAX] = longnote[ZPL_NOTE_MAX] = 0;
    CHECK(zpl_add(longname, paris, longnote) == 0 && zpl_remove(longname) == 0, "at the limit");
    CHECK(zpl_remove("home") == 0 && zpl_count() == 1 && !strcmp(zpl_place(0)->name, "Office") &&
              !zpl_place(1) && zpl_remove("home") == ZPL_E_NOTFOUND &&
              zpl_remove(NULL) == ZPL_E_ARG,
          "removed");
    for (uint32_t i = zpl_count(); i < ZPL_PLACES; i++) {
        char name[16];
        snprintf(name, sizeof name, "p%u", i);
        CHECK(zpl_add(name, paris, "") == 0, "place %u", i);
    }
    CHECK(zpl_add("one-more", paris, "") == ZPL_E_FULL, "200 places");

    /* P5 */
    char dir[256], path[300], aside[320];
    snprintf(dir, sizeof dir, "%s/zxv-place-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dir) != NULL, "a folder");
    snprintf(path, sizeof path, "%s/places.zxpl", dir);
    snprintf(aside, sizeof aside, "%s.damaged", path);
    CHECK(zpl_open(path) == 0 && zpl_count() == 0 && zpl_app_count() == 0 && bk_exists(path) &&
              !zpl_was_reset(),
          "a new file");
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600, "0600");
    CHECK(zpl_set_here(london, 20, "owner") == 0 && zpl_add("Home", paris, "the flat") == 0 &&
              zpl_add("Café Ω", london, "with a note") == 0 &&
              zpl_permit("glint", ZPL_ALWAYS) == 0 && zpl_permit("quillon", ZPL_NEVER) == 0 &&
              zpl_permit("burrow", ZPL_ONCE) == 0 && zpl_ask("scent", &h) == ZPL_E_PENDING,
          "changes");
    zpl_close();
    CHECK(zpl_open(path) == 0 && !zpl_was_reset() && zpl_count() == 2 &&
              !strcmp(zpl_place(1)->name, "Café Ω") && zpl_place(1)->at.lon == london.lon &&
              !strcmp(zpl_place(0)->note, "the flat") && zpl_app_count() == 2 &&
              zpl_app(0)->mode == ZPL_ALWAYS && zpl_app(1)->mode == ZPL_NEVER && !zpl_here()->known,
          "reloaded: lasting answers only, never the location");
    CHECK(zpl_permit("glint", ZPL_ASK) == 0 && zpl_permit("burrow", ZPL_ONCE) == 0, "changed");
    zpl_close();
    CHECK(zpl_open(path) == 0 && zpl_app_count() == 1 && !strcmp(zpl_app(0)->app, "quillon"),
          "ask again is not kept");
    zpl_close();
    static char text[1 << 16];
    CHECK(jread(path, text, sizeof text) > 0 &&
              strstr(text, "\nplace\t48856600\t2352200\tHome\tthe flat\n") &&
              strstr(text, "\napp\tquillon\tnever\n") && !strstr(text, "owner"),
          "the file: %s", text);
    const char *bodies[] = {
        "place\t1\t2\tA\tx\nplace\t1\t2\ta\ty\n",
        "place\t90000001\t0\tA\t\n",
        "place\t0\t180000001\tA\t\n",
        "place\tx\t0\tA\t\n",
        "place\t0\t0\t\t\n",
        "place\t0\t0\tA\n",
        "place\t0\t0\tA\tb\tc\n",
        "app\tglint\tonce\n",
        "app\tglint\task\n",
        "app\tglint\tsometimes\n",
        "app\ta b\tnever\n",
        "app\tglint\tnever\napp\tglint\talways\n",
        "app\tglint\n",
        "else\n",
        "place\t\t0\tA\t\n",
    };
    for (size_t i = 0; i < sizeof bodies / sizeof *bodies; i++) {
        uint8_t hh[32];
        char body[1024], hex[65];
        int n = snprintf(body, sizeof body, "zxv-places 1\n%s", bodies[i]);
        sha256((const uint8_t *) body, (size_t) n, hh);
        for (int k = 0; k < 32; k++) snprintf(hex + 2 * k, 3, "%02x", hh[k]);
        snprintf(body + n, sizeof body - (size_t) n, "end %s\n", hex);
        remove(aside);
        CHECK(jwrite(path, body) && zpl_open(path) == 0 && zpl_was_reset() && bk_exists(aside) &&
                  zpl_count() == 0 && zpl_app_count() == 0,
              "a bad line %zu", i);
        zpl_close();
    }
    const char *edits[][2] = {
        {"Home", "Hope"}, {"zxv-places 1", "zxv-places 2"}, {"\nend ", "\nEnd "}};
    for (size_t i = 0; i < sizeof edits / sizeof *edits; i++) {
        CHECK(zpl_open(path) == 0 && zpl_add("Home", paris, "") == 0, "fresh %zu", i);
        zpl_close();
        CHECK(jread(path, text, sizeof text) > 0, "read");
        char *at = strstr(text, edits[i][0]);
        CHECK(at != NULL, "find %zu", i);
        if (at) memcpy(at, edits[i][1], strlen(edits[i][1]));
        remove(aside);
        CHECK(jwrite(path, text) && zpl_open(path) == 0 && zpl_was_reset() && zpl_count() == 0,
              "damaged %zu", i);
        zpl_close();
        remove(path);
    }
    CHECK(jwrite(path, "short\n") && zpl_open(path) == 0 && zpl_was_reset(), "too short");
    zpl_close();
    char nodir[320];
    snprintf(nodir, sizeof nodir, "%s/no/such/places.zxpl", dir);
    CHECK(zpl_open(nodir) == ZPL_E_IO && zpl_add("x", paris, "") == 0, "no folder: memory only");
    static char longpath[ZPL_PATH_MAX + 2];
    memset(longpath, 'x', ZPL_PATH_MAX + 1);
    CHECK(zpl_open(longpath) == ZPL_E_ARG, "too long a path");
    for (int i = -1; i >= -8; i--) CHECK(zpl_strerror(i)[0], "strerror %d", i);
    CHECK(strcmp(zpl_strerror(-99), zpl_strerror(ZPL_E_ARG)), "unknown");

    /* over JSON-RPC */
    zpl_close();
    CHECK(zpl_open(NULL) == 0, "memory");
    EXPECT(REQ("1", "place.here", ""), OK("1", "{\"known\":false}"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":48856600,\"lon\":2352200}"),
           OK("1", "{\"known\":true,\"lat\":48856600,\"lon\":2352200,\"accuracy_m\":50,\"source\":"
                   "\"owner\",\"time\":1791799801,\"stale\":false}"));
    EXPECT(REQ("1", "place.set_here",
               ",\"params\":{\"lat\":-1,\"lon\":-2,\"accuracy_m\":9,"
               "\"source\":\"corelocation\"}"),
           OK("1", "{\"known\":true,\"lat\":-1,\"lon\":-2,\"accuracy_m\":9,\"source\":"
                   "\"corelocation\",\"time\":1791799801,\"stale\":false}"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":90000001,\"lon\":0}"),
           ERR("1", "-32602",
               "\\\"lat\\\" and \\\"lon\\\" are millionths of a degree: lat within 90, "
               "lon within 180 degrees"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":0,\"lon\":-180000001}"),
           ERR("1", "-32602",
               "\\\"lat\\\" and \\\"lon\\\" are millionths of a degree: lat within 90, "
               "lon within 180 degrees"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":0,\"lon\":0,\"accuracy_m\":0}"),
           ERR("1", "-32602", "\\\"accuracy_m\\\" must be 1 to 1000000"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":0,\"lon\":0,\"accuracy_m\":1000001}"),
           ERR("1", "-32602", "\\\"accuracy_m\\\" must be 1 to 1000000"));
    EXPECT(REQ("1", "place.set_here",
               ",\"params\":{\"lat\":90000000,\"lon\":180000000,\"accuracy_m\":1000000,"
               "\"source\":\"x\"}"),
           OK("1", "{\"known\":true,\"lat\":90000000,\"lon\":180000000,\"accuracy_m\":1000000,"
                   "\"source\":\"x\",\"time\":1791799801,\"stale\":false}"));
    EXPECT(REQ("1", "place.set_here",
               ",\"params\":{\"lat\":-90000000,\"lon\":-180000000,\"accuracy_m\":1}"),
           OK("1", "{\"known\":true,\"lat\":-90000000,\"lon\":-180000000,\"accuracy_m\":1,"
                   "\"source\":\"owner\",\"time\":1791799801,\"stale\":false}"));
    rpc(REQ("1", "place.set_here",
            ",\"params\":{\"lat\":-1,\"lon\":-2,\"accuracy_m\":9,\"source\":\"corelocation\"}"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":0,\"lon\":0,\"source\":\"a b\"}"),
           ERR("1", "-32602", "a bad name, app or coordinate"));
    EXPECT(REQ("1", "place.set_here", ",\"params\":{\"lat\":0}"),
           ERR("1", "-32602", "missing integer parameter \\\"lon\\\""));
    EXPECT(REQ("1", "place.ask", ",\"params\":{\"app\":\"glint\"}"),
           OK("1", "{\"app\":\"glint\",\"answer\":\"pending\"}"));
    EXPECT(REQ("1", "place.apps", ""),
           OK("1", "{\"apps\":[{\"app\":\"glint\",\"mode\":\"ask\",\"pending\":true,\"asked\":"
                   "1791799801}]}"));
    EXPECT(REQ("1", "place.permit", ",\"params\":{\"app\":\"glint\",\"mode\":\"once\"}"),
           OK("1", "{\"app\":\"glint\",\"mode\":\"once\"}"));
    EXPECT(REQ("1", "place.ask", ",\"params\":{\"app\":\"glint\"}"),
           OK("1",
              "{\"app\":\"glint\",\"answer\":\"allowed\",\"lat\":-1,\"lon\":-2,\"accuracy_m\":9,"
              "\"source\":\"corelocation\",\"time\":1791799801}"));
    EXPECT(REQ("1", "place.permit", ",\"params\":{\"app\":\"glint\",\"mode\":\"never\"}"),
           OK("1", "{\"app\":\"glint\",\"mode\":\"never\"}"));
    EXPECT(REQ("1", "place.ask", ",\"params\":{\"app\":\"glint\"}"),
           OK("1", "{\"app\":\"glint\",\"answer\":\"never\"}"));
    EXPECT(REQ("1", "place.permit", ",\"params\":{\"app\":\"glint\",\"mode\":\"always\"}"),
           OK("1", "{\"app\":\"glint\",\"mode\":\"always\"}"));
    EXPECT(REQ("1", "place.forget_here", ""), OK("1", "{\"known\":false}"));
    EXPECT(REQ("1", "place.ask", ",\"params\":{\"app\":\"glint\"}"),
           OK("1", "{\"app\":\"glint\",\"answer\":\"unknown\"}"));
    EXPECT(REQ("1", "place.ask", ",\"params\":{\"app\":\"a b\"}"),
           ERR("1", "-32602", "a bad name, app or coordinate"));
    EXPECT(REQ("1", "place.permit", ",\"params\":{\"app\":\"glint\",\"mode\":\"maybe\"}"),
           ERR("1", "-32602", "\\\"mode\\\" must be ask, once, always or never"));
    EXPECT(REQ("1", "place.list", ""), OK("1", "{\"reset\":false,\"places\":[]}"));
    EXPECT(REQ("1", "place.add",
               ",\"params\":{\"name\":\"Home\",\"lat\":48856600,\"lon\":2352200,"
               "\"note\":\"the flat\"}"),
           OK("1", "{\"name\":\"Home\",\"added\":true}"));
    EXPECT(REQ("1", "place.add", ",\"params\":{\"name\":\"home\",\"lat\":0,\"lon\":0}"),
           ERR("1", "-32021", "a place has that name"));
    EXPECT(REQ("1", "place.add", ",\"params\":{\"name\":\"\",\"lat\":0,\"lon\":0}"),
           ERR("1", "-32602", "a bad name, app or coordinate"));
    EXPECT(REQ("1", "place.list", ""),
           OK("1",
              "{\"reset\":false,\"places\":[{\"name\":\"Home\",\"lat\":48856600,\"lon\":2352200,"
              "\"note\":\"the flat\",\"distance_m\":null}]}"));
    rpc(REQ("1", "place.set_here", ",\"params\":{\"lat\":51507400,\"lon\":-127800}"));
    CHECK(has(REQ("1", "place.list", ""), "\"distance_m\":34"), "a distance: %s", OUT.p);
    EXPECT(REQ("1", "place.remove", ",\"params\":{\"name\":\"HOME\"}"),
           OK("1", "{\"name\":\"HOME\",\"removed\":true}"));
    EXPECT(REQ("1", "place.remove", ",\"params\":{\"name\":\"HOME\"}"),
           ERR("1", "-32021", "no such place"));
    zpl_close();
    zpl_set_clock(NULL);
    CHECK(rm_tree(dir) == 0, "cleanup");
}

/* ---------------- journal.append: signed audit entries (zxv_journal.h J8-J10) ---------------- */

/* Rewrite the last line of journal segment n: its text becomes `text`, with
 * the hash recomputed so the chain still holds (only a signature can tell). */
static bool jrelast(uint64_t n, const char *text)
{
    static char buf[1 << 20], out[1 << 20];
    if (jread(jseg(n), buf, sizeof buf) <= 0) return false;
    size_t len = strlen(buf);
    if (len < 2 || buf[len - 1] != '\n') return false;
    buf[len - 1] = 0;
    char *last = strrchr(buf, '\n');
    if (!last) return false;
    *last++ = 0;
    /* the previous hash: the line before's 5th field, or the header's last */
    char *pl = strrchr(buf, '\n') ? strrchr(buf, '\n') + 1 : buf;
    uint8_t prev[32];
    char ph[65];
    if (!strncmp(pl, "zxv-journal 1 ", 14)) {
        memcpy(ph, strrchr(pl, ' ') + 1, 64);
    } else {
        const char *q = pl;
        for (int i = 0; i < 4; i++) q = strchr(q, ' ') + 1;
        memcpy(ph, q, 64);
    }
    ph[64] = 0;
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(ph + 2 * i, "%2x", &v);
        prev[i] = (uint8_t) v;
    }
    char f[4][32];
    if (sscanf(last, "%31s %31s %31s %31s", f[0], f[1], f[2], f[3]) != 4) return false;
    char body[1024];
    int bn = snprintf(body, sizeof body, "%s %s %s %s %s", f[0], f[1], f[2], f[3], text);
    sha256_ctx_t c;
    uint8_t h[32];
    sha256_init(&c);
    sha256_update(&c, prev, 32);
    sha256_update(&c, (const uint8_t *) body, (size_t) bn);
    sha256_final(&c, h);
    char hx[65];
    for (int i = 0; i < 32; i++) snprintf(hx + 2 * i, 3, "%02x", h[i]);
    size_t at = strlen(buf);
    memcpy(out, buf, at);
    snprintf(out + at, sizeof out - at, "\n%.31s %.31s %.31s %.31s %.64s %.400s\n", f[0], f[1],
             f[2], f[3], hx, text);
    return jwrite(jseg(n), out);
}

static uint64_t jnewest(void)
{
    uint64_t n = 0;
    for (uint64_t k = 1; k < 100000; k++)
        if (bk_exists(jseg(k))) n = k;
    return n;
}

static void test_audit(void)
{
    zjr_entry_t e[8], a;
    uint32_t n;
    bool more;
    zjr_verify_t v;
    char req[900], key[ZJR_DIR_MAX + 32], keep[300];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    CHECK(zxvd_rpc_books_open() == 0, "books open: the signer is there");
    snprintf(JDIR, sizeof JDIR, "%s/zxvd-audit-XXXXXX", tmp);
    CHECK(mkdtemp(JDIR) != NULL, "a folder");
    snprintf(key, sizeof key, "%s/audit.key", JDIR);
    EXPECT(REQ("1", "journal.append", ",\"params\":{\"actor\":\"owner\",\"text\":\"x\"}"),
           ERR("1", "-32024", "zxvd keeps no journal"));
    CHECK(zjr_open(JDIR) == 0, "open");
    CHECK(!bk_exists(key), "no key until the first audit entry");
    zjr_verify(&v);
    CHECK(v.ok && !v.sigs_checked && v.audit == 0, "nothing to check yet");

    /* J9 over JSON-RPC */
    const char *o = rpc(REQ("1", "journal.append",
                            ",\"params\":{\"actor\":\"owner@home\",\"text\":\"approved the "
                            "transfer of 12 VFV to Ana\",\"level\":\"warn\"}"));
    CHECK(strstr(o, "\"result\":{\"seq\":") &&
              strstr(o, ",\"level\":\"warn\",\"source\":\"audit\",\"text\":\"approved the "
                        "transfer of 12 VFV to Ana\",\"hash\":\"") &&
              strstr(o, "\"actor\":\"owner@home\",\"sig\":\"") && strstr(o, "\"key\":\""),
          "journal.append: %s", o);
    struct stat st;
    CHECK(stat(key, &st) == 0 && (st.st_mode & 0777) == 0600 && st.st_size == 81,
          "J9: the key file, 0600, one line");
    zjr_query_t q = {0};
    q.source = "audit";
    CHECK(zjr_query(&q, e, 8, &n, &more) == 0 && n == 1 && !strcmp(e[0].actor, "owner@home") &&
              !strcmp(e[0].text, "approved the transfer of 12 VFV to Ana") &&
              e[0].level == ZXVD_LOG_WARN,
          "read back as fields: %u %s", n, n ? e[0].text : "");
    q.source = NULL; /* the newest line, whatever wrote it (the call's own log line) */
    CHECK(zjr_query(&q, e + 1, 1, &n, &more) == 0 && n == 1 &&
              zjr_append("owner", "second decision", ZXVD_LOG_INFO, &a) == 0 &&
              a.seq == e[1].seq + 1 && !memcmp(a.prev, e[1].hash, 32),
          "J8: the next entry's prev is the last one's hash");
    zxvd_log(ZXVD_LOG_INFO, "audit", "not an audit entry");
    q.source = NULL;
    CHECK(zjr_query(&q, e, 3, &n, &more) == 0 && n == 3 && !strcmp(e[0].source, "audit_") &&
              !e[0].actor[0] && !strcmp(e[1].actor, "owner") && !memcmp(e[0].prev, e[1].hash, 32) &&
              !memcmp(e[1].prev, e[2].hash, 32),
          "zxvd_log cannot write as audit; every entry links to the one before");
    snprintf(req, sizeof req, "%s",
             REQ("1", "journal.query", ",\"params\":{\"source\":\"audit\",\"max\":5}"));
    o = rpc(req);
    CHECK(strstr(o, "\"actor\":\"owner\",\"sig\":\"") && strstr(o, "\"prev\":\""),
          "journal.query shows actor, sig, hash and prev: %s", o);
    o = rpc(REQ("1", "journal.verify", ""));
    CHECK(strstr(o, "\"ok\":true") && strstr(o, "\"audit\":2,\"sigs_checked\":true,\"key\":\""),
          "J10: verify checks both signatures: %s", o);

    /* refusals */
    EXPECT(
        REQ("1", "journal.append", ",\"params\":{\"actor\":\"two words\",\"text\":\"x\"}"),
        ERR("1", "-32602",
            "\\\"actor\\\" is 1 to 32 of letters, digits and . _ - @ : +; \\\"text\\\" is 1 to 200 "
            "printable bytes"));
    EXPECT(
        REQ("1", "journal.append", ",\"params\":{\"actor\":\"a\",\"text\":\"line\\nbreak\"}"),
        ERR("1", "-32602",
            "\\\"actor\\\" is 1 to 32 of letters, digits and . _ - @ : +; \\\"text\\\" is 1 to 200 "
            "printable bytes"));
    CHECK(has(REQ("1", "journal.append", ",\"params\":{\"text\":\"x\"}"), "\"code\":-32602"),
          "actor is required");
    CHECK(has(REQ("1", "journal.append",
                  ",\"params\":{\"actor\":\"a\",\"text\":\"x\",\"level\":\"loud\"}"),
              "\"code\":-32602"),
          "a bad level");
    memset(keep, 'w', 201);
    keep[201] = 0;
    CHECK(
        zjr_append("a", keep, 0, NULL) == ZJR_E_ARG && zjr_append("", "x", 0, NULL) == ZJR_E_ARG &&
            zjr_append("a", "", 0, NULL) == ZJR_E_ARG && zjr_append("a", "x", 3, NULL) == ZJR_E_ARG,
        "zjr_append's own checks");
    keep[200] = 0;
    CHECK(zjr_append("abcdefghijklmnopqrstuvwxyz012345", keep, 2, &a) == 0 && strlen(a.text) == 200,
          "the longest actor and text fit a line");
    zjr_verify(&v);
    CHECK(v.ok && v.audit == 3, "and verify: %s", v.why);

    /* J10: a forged signature, though the chain is recomputed */
    zjr_close();
    CHECK(zjr_open(JDIR) == 0, "reopen: the key is read again");
    CHECK(zjr_append("owner", "the real one", 0, &a) == 0, "an entry in the new segment");
    uint64_t seg = jnewest();
    char line[400], sh[129];
    for (int i = 0; i < 64; i++) snprintf(sh + 2 * i, 3, "%02x", a.sig[i]);
    snprintf(line, sizeof line, "owner %s a forged one", sh);
    CHECK(jrelast(seg, line), "rewrite it with its chain intact");
    zjr_verify(&v);
    CHECK(!v.ok && v.broken_at == a.seq && strstr(v.why, "signature the audit key did not make"),
          "the forgery is caught: %s", v.why);
    snprintf(line, sizeof line, "owner nothex a malformed one");
    CHECK(jrelast(seg, line), "an audit line that does not read");
    zjr_verify(&v);
    CHECK(!v.ok && v.broken_at == a.seq && strstr(v.why, "does not read"), "caught: %s", v.why);
    snprintf(line, sizeof line, "owner %s the real one", sh);
    CHECK(jrelast(seg, line), "back as it was");
    zjr_close();
    CHECK(zjr_open(JDIR) == 0 && zjr_verify(&v) == 0 && v.ok && v.sigs_checked, "whole again: %s",
          v.why);

    /* a lost key: verify says it cannot check; a new key cannot vouch for old entries */
    zjr_close();
    CHECK(remove(key) == 0 && zjr_open(JDIR) == 0, "the key is gone");
    zjr_verify(&v);
    CHECK(v.ok && !v.sigs_checked && v.audit >= 4, "J10: chain checked, signatures not");
    CHECK(has(REQ("1", "journal.verify", ""), "\"sigs_checked\":false,\"key\":null"), "%s", OUT.p);
    CHECK(zjr_append("owner", "after the loss", 0, NULL) == 0 && bk_exists(key), "a new key");
    zjr_verify(&v);
    CHECK(!v.ok && strstr(v.why, "signature"), "older entries fail under it: %s", v.why);

    /* no signer */
    zjr_set_signer(NULL);
    EXPECT(REQ("1", "journal.append", ",\"params\":{\"actor\":\"a\",\"text\":\"x\"}"),
           ERR("1", "-32024", "the journal's audit key could not be made or read"));
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0 && zjr_append("a", "x", 0, NULL) == 0,
          "M6: the signer returns with the books");
    zjr_close();
    CHECK(rm_tree(JDIR) == 0, "cleanup");
}

/* ---------------- notify.send (zxvd_rpc.h N1-N3) ---------------- */

static zxvd_notice_t LAST_NOTE;
static char LAST_TXT[4][256];
static uint32_t NOTE_SEQ;

static uint32_t note_post(const zxvd_notice_t *n)
{
    LAST_NOTE = *n;
    const char *s[4] = {n->app, n->title, n->body, n->action};
    for (int i = 0; i < 4; i++) snprintf(LAST_TXT[i], sizeof LAST_TXT[i], "%s", s[i]);
    return NOTE_SEQ;
}

static void test_notify(void)
{
    zxvd_rpc_set_notify(NULL);
    EXPECT(REQ("1", "notify.send", ",\"params\":{\"app\":\"coffer\",\"title\":\"t\"}"),
           ERR("1", "-32601", "not available"));
    zxvd_rpc_set_notify(note_post);
    NOTE_SEQ = 7;
    EXPECT(REQ("1", "notify.send", ",\"params\":{\"app\":\"coffer\",\"title\":\"Payment sent\"}"),
           OK("1", "{\"seq\":7,\"stored\":true}"));
    CHECK(!strcmp(LAST_TXT[0], "coffer") && !strcmp(LAST_TXT[1], "Payment sent") &&
              !LAST_TXT[2][0] && !LAST_TXT[3][0] && LAST_NOTE.pri == 1 && LAST_NOTE.key == 0,
          "N1: defaults: normal, no body, no action, no key");
    NOTE_SEQ = 0;
    EXPECT(REQ("1", "notify.send",
               ",\"params\":{\"app\":\"brazier\",\"title\":\"Hot\",\"body\":\"92.5 C\",\"action\":"
               "\"brazier:temps\",\"level\":\"urgent\",\"key\":\"cpu\"}"),
           OK("1", "{\"seq\":0,\"stored\":false}"));
    uint64_t k1 = LAST_NOTE.key;
    CHECK(LAST_NOTE.pri == 3 && !strcmp(LAST_TXT[2], "92.5 C") &&
              !strcmp(LAST_TXT[3], "brazier:temps") && k1 != 0,
          "N1-N2: every field");
    rpc(REQ("1", "notify.send",
            ",\"params\":{\"app\":\"brazier\",\"title\":\"x\",\"key\":\"cpu\"}"));
    CHECK(LAST_NOTE.key == k1, "N2: the same key, the same coalescing key");
    rpc(REQ("1", "notify.send",
            ",\"params\":{\"app\":\"brazier\",\"title\":\"x\",\"key\":\"gpu\"}"));
    CHECK(LAST_NOTE.key != k1, "a different key");
    static const char *const LV[] = {"low", "normal", "high", "urgent"};
    for (int i = 0; i < 4; i++) {
        char r[200];
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"notify.send\",\"params\":{\"app\":"
                 "\"a\",\"title\":\"t\",\"level\":\"%s\"}}",
                 LV[i]);
        rpc(r);
        CHECK(LAST_NOTE.pri == i, "level %s", LV[i]);
    }
    EXPECT(
        REQ("1", "notify.send", ",\"params\":{\"app\":\"a\",\"title\":\"t\",\"level\":\"loud\"}"),
        ERR("1", "-32602", "\\\"level\\\" must be low, normal, high or urgent"));
    EXPECT(REQ("1", "notify.send", ",\"params\":{\"app\":\"\",\"title\":\"t\"}"),
           ERR("1", "-32602", "\\\"app\\\" and \\\"title\\\" must not be empty"));
    CHECK(has(REQ("1", "notify.send", ",\"params\":{\"title\":\"t\"}"), "\"code\":-32602"),
          "app is required");
    CHECK(has(REQ("1", "notify.send",
                  ",\"params\":{\"app\":\"a\",\"title\":"
                  "\"0123456789012345678901234567890123456789012345678901234567890123\"}"),
              "\"code\":-32602"),
          "N1: a 64-byte title is refused, not cut");
    CHECK(has(REQ("1", "notify.send",
                  ",\"params\":{\"app\":\"a\",\"title\":"
                  "\"012345678901234567890123456789012345678901234567890123456789012\"}"),
              "\"seq\""),
          "63 bytes fit");
    zxvd_rpc_set_notify(NULL);
}

/* ---------------- caps.* (zxv_caps.h K1-K4) ---------------- */

static void test_caps(void)
{
    char dir[256], path[300], dmg[320], buf[4096];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(dir, sizeof dir, "%s/zxvd-caps-XXXXXX", tmp);
    CHECK(mkdtemp(dir) != NULL, "a folder");
    snprintf(path, sizeof path, "%s/caps.zxcp", dir);
    snprintf(dmg, sizeof dmg, "%s.damaged", path);
    CHECK(zpl_open(NULL) == 0 && zcp_open(NULL) == 0, "in memory");

    /* K2 */
    CHECK(zcp_check("brazier", "sys.read") == ZCP_OK && zcp_check("brazier", "notify") == ZCP_OK,
          "a capability that is not sensitive: allowed");
    CHECK(zcp_check("flint", "shell") == ZCP_E_PENDING &&
              zcp_check("flint", "shell") == ZCP_E_PENDING,
          "a sensitive one waits for the owner");
    zcp_grant_t g[8];
    CHECK(zcp_list(NULL, g, 8) == 1 && g[0].pending && g[0].mode == ZCP_DEFAULT &&
              !strcmp(g[0].cap, "shell"),
          "one pending ask");
    CHECK(zcp_set("flint", "shell", ZCP_ONCE) == 0 && zcp_check("flint", "shell") == ZCP_OK &&
              zcp_check("flint", "shell") == ZCP_E_PENDING,
          "K1: once, then asked again");
    CHECK(zcp_set("flint", "shell", ZCP_ALWAYS) == 0 && zcp_check("flint", "shell") == ZCP_OK &&
              zcp_check("flint", "shell") == ZCP_OK,
          "always");
    CHECK(zcp_set("brazier", "notify", ZCP_NEVER) == 0 &&
              zcp_check("brazier", "notify") == ZCP_E_DENIED,
          "never, even for one that is not sensitive");
    CHECK(zcp_set("brazier", "notify", ZCP_DEFAULT) == 0 &&
              zcp_check("brazier", "notify") == ZCP_OK &&
              zcp_set("brazier", "notify", ZCP_DEFAULT) == ZCP_E_NOTFOUND,
          "forget: back to the default");
    CHECK(zcp_check("a b", "shell") == ZCP_E_ARG && zcp_check("a", "Shell") == ZCP_E_ARG &&
              zcp_check("a", ".x") == ZCP_E_ARG && zcp_check("a", "x.") == ZCP_E_ARG &&
              zcp_check("a", "abcdefghijklmnopqrstuvwx") == ZCP_E_ARG &&
              zcp_set("a", "shell", (zcp_mode_t) 4) == ZCP_E_ARG && zcp_check("", "x") == ZCP_E_ARG,
          "bad names");

    /* K3: location is place.*'s */
    CHECK(zcp_check("waymark", "location") == ZCP_E_PENDING && zpl_app_count() == 1 &&
              zpl_app(0)->pending,
          "location asks through place");
    CHECK(zcp_set("waymark", "location", ZCP_ALWAYS) == 0 && zpl_app(0)->mode == ZPL_ALWAYS &&
              zcp_check("waymark", "location") == ZCP_OK,
          "and is granted through place (unknown location still counts as allowed)");
    CHECK(zpl_permit("waymark", ZPL_NEVER) == 0 && zcp_check("waymark", "location") == ZCP_E_DENIED,
          "place.permit's answer is caps' answer");
    uint32_t n = zcp_list("waymark", g, 8);
    CHECK(n == 1 && !strcmp(g[0].cap, "location") && g[0].mode == ZCP_NEVER, "listed: %u", n);

    /* over JSON-RPC */
    EXPECT(REQ("1", "caps.check", ",\"params\":{\"app\":\"flint\",\"cap\":\"shell\"}"),
           OK("1", "{\"app\":\"flint\",\"cap\":\"shell\",\"allowed\":true,\"state\":\"allowed\"}"));
    EXPECT(
        REQ("1", "caps.check", ",\"params\":{\"app\":\"kiln\",\"cap\":\"camera\"}"),
        OK("1", "{\"app\":\"kiln\",\"cap\":\"camera\",\"allowed\":false,\"state\":\"pending\"}"));
    EXPECT(REQ("1", "caps.grant",
               ",\"params\":{\"app\":\"kiln\",\"cap\":\"camera\",\"mode\":\"once\"}"),
           OK("1", "{\"app\":\"kiln\",\"cap\":\"camera\",\"mode\":\"once\"}"));
    EXPECT(REQ("1", "caps.grant", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\"}"),
           OK("1", "{\"app\":\"kiln\",\"cap\":\"mic\",\"mode\":\"always\"}"));
    EXPECT(REQ("1", "caps.revoke", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\"}"),
           OK("1", "{\"app\":\"kiln\",\"cap\":\"mic\",\"mode\":\"never\"}"));
    EXPECT(REQ("1", "caps.check", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\"}"),
           OK("1", "{\"app\":\"kiln\",\"cap\":\"mic\",\"allowed\":false,\"state\":\"denied\"}"));
    EXPECT(
        REQ("1", "caps.revoke", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\",\"forget\":true}"),
        OK("1", "{\"app\":\"kiln\",\"cap\":\"mic\",\"mode\":\"default\"}"));
    EXPECT(
        REQ("1", "caps.revoke", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\",\"forget\":true}"),
        ERR("1", "-32025", "no answer to take back"));
    EXPECT(
        REQ("1", "caps.grant", ",\"params\":{\"app\":\"kiln\",\"cap\":\"mic\",\"mode\":\"never\"}"),
        ERR("1", "-32602", "\\\"mode\\\" must be always or once"));
    CHECK(has(REQ("1", "caps.grant", ",\"params\":{\"app\":\"kiln\",\"cap\":\"Mic\"}"),
              "\"code\":-32602") &&
              has(REQ("1", "caps.check", ",\"params\":{\"app\":\"kiln\"}"), "\"code\":-32602"),
          "bad parameters");
    const char *o = rpc(REQ("1", "caps.list", ",\"params\":{\"app\":\"kiln\"}"));
    CHECK(strstr(o, "\"sensitive\":[\"camera\",\"mic\",\"location\",\"shell\",\"ledger.post\","
                    "\"journal.write\",\"caps\",\"proc.control\"],\"grants\":[{\"app\":\"kiln\","
                    "\"cap\":\"camera\","
                    "\"mode\":\"once\",\"pending\":false,\"asked\":") &&
              strstr(o, "],\"more\":false}") && !strstr(o, "flint"),
          "caps.list for one app: %s", o);
    CHECK(has(REQ("1", "caps.list", ""),
              "\"app\":\"waymark\",\"cap\":\"location\",\"mode\":\"never\""),
          "and every app's, location included: %s", OUT.p);

    /* K4: the file keeps lasting answers only */
    CHECK(zcp_open(path) == 0 && !zcp_was_reset(), "a new file");
    CHECK(zcp_set("flint", "shell", ZCP_ALWAYS) == 0 && zcp_set("kiln", "camera", ZCP_ONCE) == 0 &&
              zcp_set("brazier", "notify", ZCP_NEVER) == 0 &&
              zcp_check("glint", "mic") == ZCP_E_PENDING,
          "three answers and an ask");
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600, "0600");
    zcp_close();
    CHECK(zcp_open(path) == 0 && zcp_list(NULL, g, 8) >= 2 &&
              zcp_check("flint", "shell") == ZCP_OK &&
              zcp_check("brazier", "notify") == ZCP_E_DENIED &&
              zcp_check("kiln", "camera") == ZCP_E_PENDING,
          "always and never come back; once and the ask do not");
    FILE *f = fopen(path, "rb");
    size_t len = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[len] = 0;
    CHECK(len > 0 && strstr(buf, "grant\tflint\tshell\talways\n") && !strstr(buf, "camera"),
          "the file: %s", buf);
    char *x = strstr(buf, "always");
    if (x) x[0] = 'A';
    f = fopen(path, "wb");
    if (f) {
        fputs(buf, f);
        fclose(f);
    }
    zcp_close();
    CHECK(zcp_open(path) == 0 && zcp_was_reset() && bk_exists(dmg) &&
              zcp_check("flint", "shell") == ZCP_E_PENDING,
          "an edited file is moved aside; answers start empty");
    zcp_close();
    CHECK(zcp_open(NULL) == 0 && zpl_open(NULL) == 0, "back to memory");
    CHECK(rm_tree(dir) == 0, "cleanup");
}

/* ---------------- shell.* (zxvd_shell.h H1-H7) ---------------- */

/* shell.read from 0 until the job is done (its whole reply), or "" */
static const char *shell_wait(uint32_t job, const char *extra)
{
    char r[300];
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.read\",\"params\":{\"job\":%u%s}}",
             job, extra ? extra : "");
    for (int i = 0; i < 1000; i++) {
        const char *o = rpc(r);
        if (strstr(o, "\"done\":true") || strstr(o, "\"error\"")) return o;
        usleep(5000);
    }
    return "";
}

static uint32_t shell_start(const char *params)
{
    char r[1200];
    snprintf(r, sizeof r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.run\",\"params\":%s}",
             params);
    const char *o = rpc(r), *p = strstr(o, "\"job\":");
    return p ? (uint32_t) strtoul(p + 6, NULL, 10) : 0;
}

static void test_shell(void)
{
    char r[400];
    zxvd_shell_enable("");
    EXPECT(REQ("1", "shell.run", ",\"params\":{\"line\":\"echo hi\"}"),
           ERR("1", "-32023", "shell.* is off in this zxvd"));
    EXPECT(REQ("1", "shell.jobs", ""), OK("1", "{\"on\":false,\"jobs\":[]}"));
    CHECK(zxvd_shell_enable("/bin/sh") == 0, "on, with sh standing in for zxsh (it takes -c too)");

    /* H1, H2, H4: a line, both streams in order, zxsh's exit status */
    uint32_t j = shell_start("{\"line\":\"echo hi; echo err >&2; printf 'tab\\\\there'; exit 4\"}");
    CHECK(j > 0, "started: %s", OUT.p);
    const char *o = shell_wait(j, NULL);
    CHECK(strstr(o, "\"out\":\"hi\\nerr\\ntab\\u0009here\"") &&
              strstr(o, "\"exit\":4,\"signal\":null") && strstr(o, "\"verdict\":\"both\"") &&
              strstr(o, "\"kept\":false"),
          "a zxsh line: %s", o);
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.read\",\"params\":{\"job\":%u}}", j);
    CHECK(strstr(rpc(r), "\"code\":-32023"), "H5: read to the end, it is gone: %s", OUT.p);
    static const struct {
        const char *line, *verdict;
    } V[] = {{"exit 0", "true"},
             {"exit 1", "false"},
             {"exit 3", "forbidden"},
             {"exit 5", "neither"},
             {"exit 2", "false"}};
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        snprintf(r, sizeof r, "{\"line\":\"%s\"}", V[i].line);
        char want[64];
        snprintf(want, sizeof want, "\"verdict\":\"%s\"", V[i].verdict);
        CHECK(strstr(shell_wait(shell_start(r), NULL), want), "H4: %s is %s: %s", V[i].line,
              V[i].verdict, OUT.p);
    }

    /* a program: no shell, PATH searched, zxsh's rule for a process */
    o = shell_wait(shell_start("{\"argv\":[\"printf\",\"%s|%s\",\"a b\",\"$HOME\"]}"), NULL);
    CHECK(strstr(o, "\"out\":\"a b|$HOME\"") && strstr(o, "\"verdict\":\"true\""),
          "argv is passed as is: %s", o);
    o = shell_wait(shell_start("{\"argv\":[\"/no/such/program\"]}"), NULL);
    CHECK(strstr(o, "/no/such/program: No such file or directory") && strstr(o, "\"exit\":127") &&
              strstr(o, "\"verdict\":\"neither\""),
          "it could not run: %s", o);
    o = shell_wait(shell_start("{\"argv\":[\"sh\",\"-c\",\"kill -SEGV $$\"]}"), NULL);
    CHECK(strstr(o, "\"exit\":null,\"signal\":\"SIGSEGV\"") &&
              strstr(o, "\"verdict\":\"forbidden\""),
          "a crash is forbidden: %s", o);
    o = shell_wait(shell_start("{\"argv\":[\"sh\",\"-c\",\"kill -USR1 $$\"]}"), NULL);
    CHECK(strstr(o, "\"signal\":\"other\"") && strstr(o, "\"verdict\":\"neither\""),
          "another signal: neither: %s", o);

    /* H6 */
    o = shell_wait(shell_start("{\"line\":\"pwd; echo $ZXSH_JOB\",\"cwd\":\"/\"}"), NULL);
    CHECK(strstr(o, "\"out\":\"/\\n"), "cwd: %s", o);
    EXPECT(REQ("1", "shell.run", ",\"params\":{\"line\":\"pwd\",\"cwd\":\"/no/such/folder\"}"),
           ERR("1", "-32023", "\\\"cwd\\\" is not a folder"));

    /* H3 */
    j = shell_start("{\"argv\":[\"cat\"]}");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.write\",\"params\":{\"job\":%u,"
             "\"text\":\"one\\ntwo\",\"close\":true}}",
             j);
    CHECK(strstr(rpc(r), "\"written\":7,\"closed\":true"), "write and close: %s", OUT.p);
    CHECK(strstr(rpc(r), "\"code\":-32023"), "input closed: %s", OUT.p);
    o = shell_wait(j, NULL);
    CHECK(strstr(o, "\"out\":\"one\\ntwo\"") && strstr(o, "\"verdict\":\"true\""), "cat: %s", o);

    /* H5: kill, time out */
    j = shell_start("{\"argv\":[\"sleep\",\"30\"]}");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.kill\",\"params\":{\"job\":%u}}", j);
    CHECK(strstr(rpc(r), "\"was_running\":true"), "kill: %s", OUT.p);
    o = shell_wait(j, NULL);
    CHECK(strstr(o, "\"signal\":\"SIGTERM\"") && strstr(o, "\"killed\":true") &&
              strstr(o, "\"timed_out\":false"),
          "ended by kill: %s", o);
    j = shell_start("{\"line\":\"trap '' TERM; sleep 30\",\"timeout_ms\":50}");
    o = shell_wait(j, NULL);
    CHECK(strstr(o, "\"signal\":\"SIGKILL\"") && strstr(o, "\"timed_out\":true"),
          "late, and deaf to SIGTERM: SIGKILL after two seconds: %s", o);
    j = shell_start("{\"argv\":[\"true\"]}");
    shell_wait(j, ",\"max\":1"); /* done, but not read: "" is all, so it is read to the end */
    j = shell_start("{\"line\":\"echo kept\"}");
    o = shell_wait(j, ",\"max\":2");
    CHECK(strstr(o, "\"out\":\"ke\"") && strstr(o, "\"next\":\"2\"") && strstr(o, "\"kept\":true"),
          "part read: kept: %s", o);
    CHECK(has(REQ("1", "shell.jobs", ""),
              "\"what\":\"echo kept\",\"done\":true,\"bytes\":\"5\",\"verdict\":\"true\""),
          "shell.jobs: %s", OUT.p);
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shell.kill\",\"params\":{\"job\":%u}}", j);
    CHECK(strstr(rpc(r), "\"was_running\":false") &&
              !strstr(rpc(REQ("1", "shell.jobs", "")), "echo kept"),
          "killing a finished job lets it go");

    /* H2: the window, dropped bytes, UTF-8 */
    j = shell_start("{\"line\":\"yes a | head -c 300000\"}");
    o = shell_wait(j, ",\"max\":4");
    CHECK(
        strstr(o,
               "\"out\":\"a\\na\\n\",\"from\":\"37856\",\"next\":\"37860\",\"dropped\":\"37856\""),
        "only the last 256 KiB are kept: %s", o);
    o = shell_wait(j, ",\"from\":299999");
    CHECK(strstr(o, "\"out\":\"\\n\",\"from\":\"299999\",\"next\":\"300000\",\"dropped\":\"0\"") &&
              strstr(o, "\"kept\":false"),
          "the end: %s", o);
    j = shell_start("{\"line\":\"printf 'x\\\\303\\\\251y\\\\377z'\"}");
    o = shell_wait(j, ",\"max\":2");
    CHECK(strstr(o, "\"out\":\"x\",\"from\":\"0\",\"next\":\"1\""), "a cut character waits: %s", o);
    o = shell_wait(j, ",\"from\":1");
    CHECK(strstr(o, "\"out\":\"\xc3\xa9y\\ufffdz\""), "then whole; a bad byte is U+FFFD: %s", o);

    /* refusals */
    EXPECT(REQ("1", "shell.run", ",\"params\":{}"),
           ERR("1", "-32602",
               "give \\\"line\\\" (a zxsh line) or \\\"argv\\\" (a program), not both"));
    EXPECT(REQ("1", "shell.run", ",\"params\":{\"line\":\"x\",\"argv\":[\"x\"]}"),
           ERR("1", "-32602",
               "give \\\"line\\\" (a zxsh line) or \\\"argv\\\" (a program), not both"));
    EXPECT(REQ("1", "shell.run", ",\"params\":{\"line\":\"\"}"),
           ERR("1", "-32602", "\\\"line\\\" is empty"));
    CHECK(has(REQ("1", "shell.run", ",\"params\":{\"argv\":[]}"), "\"code\":-32602") &&
              has(REQ("1", "shell.run", ",\"params\":{\"argv\":[\"\"]}"), "\"code\":-32602") &&
              has(REQ("1", "shell.run", ",\"params\":{\"argv\":[1]}"), "\"code\":-32602") &&
              has(REQ("1", "shell.run", ",\"params\":{\"argv\":\"ls\"}"), "\"code\":-32602") &&
              has(REQ("1", "shell.run", ",\"params\":{\"line\":\"x\",\"timeout_ms\":0}"),
                  "\"code\":-32602") &&
              has(REQ("1", "shell.run", ",\"params\":{\"line\":\"x\",\"timeout_ms\":86400001}"),
                  "\"code\":-32602"),
          "bad parameters");
    EXPECT(REQ("1", "shell.read", ",\"params\":{\"job\":999999}"),
           ERR("1", "-32023", "no job 999999 (it ended and was read to the end, or never was)"));
    CHECK(has(REQ("1", "shell.read", ",\"params\":{}"), "\"code\":-32602") &&
              has(REQ("1", "shell.kill", ",\"params\":{\"job\":0}"), "\"code\":-32023"),
          "a job id is needed");

    /* H5: places run out only with running jobs; zxvd_shell_close ends them */
    uint32_t ids[ZXVD_SHELL_JOBS];
    for (uint32_t i = 0; i < ZXVD_SHELL_JOBS; i++)
        ids[i] = shell_start("{\"argv\":[\"sleep\",\"30\"]}");
    CHECK(ids[ZXVD_SHELL_JOBS - 1] > 0, "sixteen at once");
    CHECK(has(REQ("1", "shell.run", ",\"params\":{\"argv\":[\"true\"]}"),
              "\"message\":\"16 jobs are running\""),
          "a seventeenth waits: %s", OUT.p);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    zxvd_shell_close();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(t1.tv_sec - t0.tv_sec < 3 && !has(REQ("1", "shell.jobs", ""), "\"job\""),
          "close ends them all, quickly");
    zxvd_shell_enable("");
}

/* ---------------- dns.* and did.* (zxvd_dns.h D1-D6, I1-I3) ---------------- */

/* dns.resolve or dns.reverse until it is no longer pending (its reply) */
static const char *dns_wait(const char *req)
{
    for (int i = 0; i < 8000; i++) {
        const char *o = rpc(req);
        if (!strstr(o, "\"state\":\"pending\"")) return o;
        usleep(5000);
    }
    return "";
}

static void test_dns(void)
{
    zxvd_dns_enable(false);
    EXPECT(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\"}"),
           ERR("1", "-32026", "dns.* is off in this zxvd"));
    CHECK(has(REQ("1", "dns.reverse", ",\"params\":{\"addr\":\"127.0.0.1\"}"), "\"code\":-32026") &&
              has(REQ("1", "dns.flush", ""), "\"code\":-32026"),
          "D6: off until the host turns it on");
    zxvd_dns_enable(true);

    /* D1: an address is answered at once, in its usual spelling */
    EXPECT(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"10.0.0.1\"}"),
           OK("1", "{\"name\":\"10.0.0.1\",\"family\":\"any\",\"state\":\"done\",\"addrs\":[{"
                   "\"addr\":\"10.0.0.1\",\"family\":\"ipv4\"}],\"ttl_ms\":60000}"));
    EXPECT(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"0:0::1\",\"family\":\"ipv6\"}"),
           OK("1", "{\"name\":\"::1\",\"family\":\"ipv6\",\"state\":\"done\",\"addrs\":[{"
                   "\"addr\":\"::1\",\"family\":\"ipv6\"}],\"ttl_ms\":60000}"));
    EXPECT(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"10.0.0.1\",\"family\":\"ipv6\"}"),
           ERR("1", "-32602", "\\\"10.0.0.1\\\" is an IPv4 address"));

    /* D1, D2: a lookup runs beside the loop; the answer is kept */
    const char *first = rpc(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"LocalHost.\"}"));
    CHECK(strstr(first, "{\"name\":\"localhost\",\"family\":\"any\",\"state\":\"pending\"}"),
          "D1, D4: pending at first, the name in lower case without its dot: %s", first);
    const char *o = dns_wait(
        REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\",\"family\":\"ipv4\"}"));
    CHECK(strstr(o, "\"state\":\"done\",\"addrs\":[{\"addr\":\"127.0.0.1\",\"family\":\"ipv4\"}]"),
          "localhost over IPv4: %s", o);
    o = dns_wait(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\"}"));
    CHECK(strstr(o, "\"state\":\"done\"") && strstr(o, "\"ttl_ms\":") &&
              (strstr(o, "\"127.0.0.1\"") || strstr(o, "\"::1\"")),
          "any family: %s", o);
    o = rpc(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\"}"));
    CHECK(strstr(o, "\"state\":\"done\""), "D2: kept: asked again, answered at once: %s", o);
    o = dns_wait(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"no-such-host.invalid\"}"));
    CHECK(strstr(o, "\"state\":\"failed\",\"error\":\"") && strstr(o, "\"ttl_ms\":"),
          "a name that is not there fails: %s", o);
    o = dns_wait(REQ("1", "dns.reverse", ",\"params\":{\"addr\":\"127.0.0.1\"}"));
    CHECK(strstr(o, "{\"addr\":\"127.0.0.1\",\"state\":\"") &&
              (strstr(o, "\"state\":\"done\",\"name\":\"") || strstr(o, "\"state\":\"failed\"")),
          "D3: a name for an address (or why not): %s", o);
    CHECK(has(REQ("1", "dns.flush", ""), "{\"forgot\":") &&
              has(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\"}"),
                  "\"state\":\"pending\""),
          "D2: flush forgets the answers");
    dns_wait(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"localhost\"}"));

    /* D4: names refused before any lookup */
    static const char *const BAD[] = {
        "a..b",  ".a",
        "-a.b",  "a-.b",
        "a b",   "a/b",
        "",      ".",
        "a.b..", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char r[300];
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"dns.resolve\",\"params\":{\"name\":"
                 "\"%s\"}}",
                 BAD[i]);
        CHECK(strstr(rpc(r), "\"code\":-32602"), "D4: \"%s\" is not a name", BAD[i]);
    }
    char longname[300], r[400];
    memset(longname, 'a', sizeof longname);
    for (int i = 63; i < 254; i += 64) longname[i] = '.';
    longname[253] = 0; /* 253 bytes: the most */
    snprintf(
        r, sizeof r,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"dns.resolve\",\"params\":{\"name\":\"%s\"}}",
        longname);
    CHECK(!strstr(rpc(r), "\"code\":-32602"), "253 bytes is a name: %s", OUT.p);
    longname[253] = 'a';
    longname[254] = 0;
    snprintf(
        r, sizeof r,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"dns.resolve\",\"params\":{\"name\":\"%s\"}}",
        longname);
    CHECK(strstr(rpc(r), "\"code\":-32602"), "254 is not");
    CHECK(has(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"a_b-c.d9\"}"), "\"state\"") &&
              has(REQ("1", "dns.resolve", ",\"params\":{\"name\":\"x\",\"family\":\"ip4\"}"),
                  "\"code\":-32602") &&
              has(REQ("1", "dns.reverse", ",\"params\":{\"addr\":\"localhost\"}"),
                  "\"code\":-32602") &&
              has(REQ("1", "dns.resolve", ""), "\"code\":-32602"),
          "underscores are allowed; a bad family, address or nothing is not");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    zxvd_dns_close();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(t1.tv_sec - t0.tv_sec < 30, "D5: close waits for the lookups running");
    CHECK(has(REQ("1", "dns.flush", ""), "{\"forgot\":0}"), "and forgets every answer");
    zxvd_dns_enable(false);

    /* I1 */
    EXPECT(
        REQ("1", "did.parse",
            ",\"params\":{\"did\":\"did:key:z6MkhaXgBZDvotDkL5257faiztiGiC2QtKLGpbnnEGta2doK\"}"),
        OK("1", "{\"did\":\"did:key:z6MkhaXgBZDvotDkL5257faiztiGiC2QtKLGpbnnEGta2doK\","
                "\"method\":\"key\",\"supported\":true,\"type\":\"ed25519\",\"key\":"
                "\"2e6fcce36701dc791488e0d0b1745cc1e33a4c1c9fcc41c63bd343dbbe0970e6\"}"));
    EXPECT(
        REQ("1", "did.parse",
            ",\"params\":{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\"}"),
        OK("1", "{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\","
                "\"method\":\"key\",\"supported\":true,\"type\":\"ed25519\",\"key\":"
                "\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"}"));
    EXPECT(REQ("1", "did.parse",
               ",\"params\":{\"did\":\"did:zxv:"
               "jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\"}"),
           OK("1", "{\"did\":\"did:zxv:jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\","
                   "\"method\":\"zxv\",\"supported\":true,\"id\":"
                   "\"4b65796eafa98b5daf3ada3e1a4c8cfa3dcf2f8805685ef76e69e8dc7198b762\"}"));
    EXPECT(REQ("1", "did.parse", ",\"params\":{\"did\":\"did:web:example.com:user%20a\"}"),
           OK("1", "{\"did\":\"did:web:example.com:user%20a\",\"method\":\"web\","
                   "\"supported\":false}"));
    static const char *const BADDID[] = {
        "did:key:z6LSbgC4DpuCf7zxewhFPnYcyBm3YgxjEEovsehvWqZzTm8z",     /* X25519 */
        "did:key:z16MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c",    /* a spare zero */
        "did:key:6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c",      /* no multibase */
        "did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9",      /* short */
        "did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc90",     /* 0: not base58 */
        "did:zxv:jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5rb", /* spare bits */
        "did:zxv:jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5r",  /* short */
        "did:zxv:JNSXS3VPVGFV3LZ23I7BUTEM7I646L4IAVUF553ONHUNY4MYW5RA", /* upper case */
        "did:",
        "did:key",
        "did::x",
        "did:Key:x",
        "did:web:",
        "did:web:a b",
        "dad:web:x",
    };
    for (size_t i = 0; i < sizeof BADDID / sizeof BADDID[0]; i++) {
        snprintf(
            r, sizeof r,
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"did.parse\",\"params\":{\"did\":\"%s\"}}",
            BADDID[i]);
        CHECK(strstr(rpc(r), "\"code\":-32026"), "I1: %s refused: %s", BADDID[i], OUT.p);
    }

    /* I2 */
    EXPECT(REQ("1", "did.match",
               ",\"params\":{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\","
               "\"key\":\"000102030405060708090A0B0C0D0E0F101112131415161718191a1b1c1d1e1f\"}"),
           OK("1", "{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\","
                   "\"match\":true}"));
    CHECK(
        has(REQ("1", "did.match",
                ",\"params\":{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\","
                "\"key\":\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1e\"}"),
            "\"match\":false}"),
        "another key: no");
    const char *pk = "00070e151c232a31383f464d545b626970777e858c939aa1a8afb6bdc4cbd2d9e0e7eef5fc"
                     "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff"
                     "060d141b222930373e454c535a61686f767d848b9299a0a7aeb5";
    char req[1200];
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"did.match\",\"params\":{\"did\":\"did:zxv:"
             "jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\",\"key\":\"%s\"}}",
             pk);
    CHECK(strstr(rpc(req), "\"match\":true}"), "did:zxv: its key hashes to the id: %s", OUT.p);
    char *last = strstr(req, "aeb5\"");
    last[3] = '4';
    CHECK(strstr(rpc(req), "\"match\":false}"), "one bit off: no: %s", OUT.p);
    EXPECT(REQ("1", "did.match", ",\"params\":{\"did\":\"did:web:example.com\",\"key\":\"00\"}"),
           ERR("1", "-32026", "did:web keys cannot be checked here"));
    CHECK(
        has(REQ("1", "did.match",
                ",\"params\":{\"did\":\"did:key:z6MkeTGwHmLmuCmgg4ABYhzWVh6ZX7hTwWt8gguAretUfc9c\","
                "\"key\":\"0001\"}"),
            "an Ed25519 \\\"key\\\" is 32 bytes") &&
            has(REQ("1", "did.match",
                    ",\"params\":{\"did\":\"did:zxv:"
                    "jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\",\"key\":\"abc\"}"),
                "\"code\":-32602") &&
            has(REQ("1", "did.match",
                    ",\"params\":{\"did\":\"did:zxv:"
                    "jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\",\"key\":\"zz\"}"),
                "\"code\":-32602") &&
            has(REQ("1", "did.match",
                    ",\"params\":{\"did\":\"did:zxv:"
                    "jnsxs3vpvgfv3lz23i7butem7i646l4iavuf553onhuny4myw5ra\",\"key\":\"\"}"),
                "\"code\":-32602") &&
            has(REQ("1", "did.match", ",\"params\":{\"key\":\"00\"}"), "\"code\":-32602"),
        "bad keys");
}

/* ---------------- schedule.* (zxv_sched.h T1-T5) ---------------- */

static int64_t SCHED_NOW;
static int64_t sched_clock(void)
{
    return SCHED_NOW;
}

static uint32_t SCHED_NOTES;
static char SCHED_NOTE[200];
static uint32_t sched_note(const zxvd_notice_t *n)
{
    SCHED_NOTES++;
    snprintf(SCHED_NOTE, sizeof SCHED_NOTE, "%s|%s|%s|%u", n->app, n->title, n->body,
             (unsigned) n->pri);
    return SCHED_NOTES;
}

static void test_sched(void)
{
    char dir[256], path[300], dmg[320], buf[8192], r[600];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    const char *tz = getenv("TZ");
    char old_tz[64] = "";
    if (tz) snprintf(old_tz, sizeof old_tz, "%s", tz);
    setenv("TZ", "UTC", 1);
    tzset();
    snprintf(dir, sizeof dir, "%s/zxvd-sched-XXXXXX", tmp);
    CHECK(mkdtemp(dir) != NULL, "a folder");
    snprintf(path, sizeof path, "%s/schedule.zxsd", dir);
    snprintf(dmg, sizeof dmg, "%s.damaged", path);
    zsd_set_clock(sched_clock);
    SCHED_NOW = INT64_C(1800000000000); /* Friday 15 January 2027, 08:00 UTC */
    CHECK(zsd_open(NULL) == 0, "in memory");
    zxvd_rpc_set_notify(sched_note);

    /* T2 */
    EXPECT(REQ("1", "schedule.add",
               ",\"params\":{\"app\":\"treadle\",\"name\":\"water\",\"at_ms\":1800000005000,"
               "\"notify\":true,\"note\":\"the ferns\"}"),
           OK("1", "{\"id\":1,\"app\":\"treadle\",\"name\":\"water\",\"kind\":\"at\",\"at_ms\":"
                   "1800000005000,\"notify\":true,\"note\":\"the ferns\",\"next_ms\":1800000005000,"
                   "\"last_ms\":null,\"fired\":0}"));
    EXPECT(REQ("1", "schedule.add",
               ",\"params\":{\"app\":\"treadle\",\"name\":\"tick\",\"every_s\":60}"),
           OK("1", "{\"id\":2,\"app\":\"treadle\",\"name\":\"tick\",\"kind\":\"every\",\"every_s\":"
                   "60,\"start_ms\":1800000060000,\"notify\":false,\"note\":\"\",\"next_ms\":"
                   "1800000060000,\"last_ms\":null,\"fired\":0}"));
    EXPECT(REQ("1", "schedule.add",
               ",\"params\":{\"app\":\"kettle\",\"name\":\"tea\",\"daily\":\"07:30\",\"days\":["
               "\"wed\",\"mon\"]}"),
           OK("1",
              "{\"id\":3,\"app\":\"kettle\",\"name\":\"tea\",\"kind\":\"daily\",\"daily\":"
              "\"07:30\",\"days\":[\"mon\",\"wed\"],\"notify\":false,\"note\":\"\",\"next_ms\":"
              "1800257400000,\"last_ms\":null,\"fired\":0}"));
    EXPECT(REQ("1", "schedule.add",
               ",\"params\":{\"app\":\"kettle\",\"name\":\"late\",\"every_s\":3600,"
               "\"start_ms\":\"1799999000000\"}"),
           OK("1", "{\"id\":4,\"app\":\"kettle\",\"name\":\"late\",\"kind\":\"every\",\"every_s\":"
                   "3600,\"start_ms\":1799999000000,\"notify\":false,\"note\":\"\",\"next_ms\":"
                   "1800002600000,\"last_ms\":null,\"fired\":0}"));
    static const char *const BADP[] = {
        "{\"app\":\"t\",\"name\":\"x\",\"at_ms\":1800000000000}",         /* now: past */
        "{\"app\":\"t\",\"name\":\"x\",\"at_ms\":253402300800000}",       /* year 10000 */
        "{\"app\":\"t\",\"name\":\"x\",\"every_s\":59}",                  /* too often */
        "{\"app\":\"t\",\"name\":\"x\",\"every_s\":31622401}",            /* over a year */
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"24:00\"}",             /* no such minute */
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"7:30\"}",              /* HH:MM */
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"07:30\",\"days\":[]}", /* no day */
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"07:30\",\"days\":[\"mon\",\"mon\"]}",
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"07:30\",\"days\":[\"monday\"]}",
        "{\"app\":\"t\",\"name\":\"x\",\"daily\":\"07:30\",\"days\":\"mon\"}",
        "{\"app\":\"t\",\"name\":\"x\",\"every_s\":60,\"daily\":\"07:30\"}",     /* two kinds */
        "{\"app\":\"t\",\"name\":\"x\"}",                                        /* no kind */
        "{\"app\":\"t\",\"name\":\"x\",\"at_ms\":1900000000000,\"start_ms\":1}", /* start alone */
        "{\"app\":\"t\",\"name\":\"x\",\"every_s\":60,\"days\":[\"mon\"]}",      /* days alone */
        "{\"app\":\"a b\",\"name\":\"x\",\"every_s\":60}",                       /* bad app */
        "{\"app\":\"t\",\"name\":\"\",\"every_s\":60}",                          /* no name */
        "{\"app\":\"t\",\"name\":\"a\\tb\",\"every_s\":60}",                     /* a tab */
        "{\"app\":\"t\",\"name\":\"x\",\"note\":\"a\\nb\",\"every_s\":60}",      /* a line end */
        "{\"app\":\"t\",\"name\":\"x\",\"every_s\":60,\"start_ms\":-1}",
    };
    for (size_t i = 0; i < sizeof BADP / sizeof BADP[0]; i++) {
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"schedule.add\",\"params\":%s}",
                 BADP[i]);
        CHECK(strstr(rpc(r), "\"code\":-32602"), "T2: refused: %s: %s", BADP[i], OUT.p);
    }

    /* T3: firings */
    EXPECT(REQ("1", "schedule.fired", ""), OK("1", "{\"fired\":[],\"next\":0}"));
    SCHED_NOW += 5000;
    zxvd_rpc_tick();
    EXPECT(REQ("1", "schedule.fired", ",\"params\":{\"app\":\"treadle\"}"),
           OK("1", "{\"fired\":[{\"seq\":1,\"id\":1,\"app\":\"treadle\",\"name\":\"water\","
                   "\"due_ms\":1800000005000,\"fired_ms\":1800000005000,\"late\":false,"
                   "\"missed\":0,\"ended\":true}],\"next\":1}"));
    CHECK(SCHED_NOTES == 1 && !strcmp(SCHED_NOTE, "treadle|water|the ferns|1"),
          "T1: a notice in the app's name: %u %s", SCHED_NOTES, SCHED_NOTE);
    CHECK(!has(REQ("1", "schedule.list", ""), "\"water\""), "an `at` schedule ends when it fires");
    EXPECT(REQ("1", "schedule.fired", ",\"params\":{\"after\":1}"),
           OK("1", "{\"fired\":[],\"next\":1}"));
    SCHED_NOW += 10 * 60000 + 30000; /* 10.5 minutes later: tick was due 9 times */
    CHECK(strstr(rpc(REQ("1", "schedule.fired", ",\"params\":{\"after\":1}")),
                 "{\"fired\":[{\"seq\":2,\"id\":2,\"app\":\"treadle\",\"name\":\"tick\","
                 "\"due_ms\":1800000060000,\"fired_ms\":1800000635000,\"late\":true,"
                 "\"missed\":9,\"ended\":false}],\"next\":2}"),
          "T3: missed ones fire once, late, counted: %s", OUT.p);
    CHECK(has(REQ("1", "schedule.list", ",\"params\":{\"app\":\"treadle\"}"),
              "\"next_ms\":1800000660000,\"last_ms\":1800000635000,\"fired\":1}"),
          "and go on from the next time after now: %s", OUT.p);
    SCHED_NOW = INT64_C(1800257400000); /* Monday 07:30 */
    zxvd_rpc_tick();
    CHECK(has(REQ("1", "schedule.list", ",\"params\":{\"app\":\"kettle\"}"),
              "\"name\":\"tea\",\"kind\":\"daily\",\"daily\":\"07:30\",\"days\":[\"mon\",\"wed\"],"
              "\"notify\":false,\"note\":\"\",\"next_ms\":1800430200000,"),
          "daily: Monday's fired, Wednesday's next: %s", OUT.p);
    CHECK(SCHED_NOTES == 1, "no notice unless asked");
    uint64_t next = 0;
    zsd_fired_t fr[8];
    CHECK(zsd_fired("kettle", 0, fr, 8, &next) == 2 && next == 5 && fr[0].seq < fr[1].seq,
          "the ring keeps every app's firings in order (%llu)", (unsigned long long) next);
    CHECK(zsd_fired(NULL, 0, fr, 1, &next) == 1 && next == 1, "a page at a time");

    /* cancel, limits */
    EXPECT(REQ("1", "schedule.cancel", ",\"params\":{\"id\":2}"),
           OK("1", "{\"id\":2,\"cancelled\":true}"));
    EXPECT(REQ("1", "schedule.cancel", ",\"params\":{\"id\":2}"),
           ERR("1", "-32027", "no such schedule"));
    for (uint32_t i = 0; i < ZSD_PER_APP; i++) {
        snprintf(r, sizeof r,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"schedule.add\",\"params\":{\"app\":"
                 "\"busy\",\"name\":\"n%u\",\"every_s\":%u}}",
                 i, 60 + i);
        rpc(r);
    }
    CHECK(strstr(OUT.p, "\"id\":"), "32 for one app");
    CHECK(has(REQ("1", "schedule.add",
                  ",\"params\":{\"app\":\"busy\",\"name\":\"one more\",\"every_s\":60}"),
              "\"code\":-32027,\"message\":\"no room for another schedule\""),
          "T4: a 33rd is refused: %s", OUT.p);

    /* T5: the file */
    CHECK(zsd_open(path) == 0 && !zsd_was_reset(), "a new file");
    zsd_entry_t e;
    memset(&e, 0, sizeof e);
    snprintf(e.app, sizeof e.app, "treadle");
    snprintf(e.name, sizeof e.name, "caf\xc3\xa9 run");
    snprintf(e.note, sizeof e.note, "beans");
    e.kind = ZSD_EVERY;
    e.every_s = 600;
    e.notify = true;
    uint32_t id = 0;
    CHECK(zsd_add(&e, &id) == 0 && id == 1, "added");
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600, "0600");
    SCHED_NOW += 600000;
    CHECK(zsd_tick() == 1, "fired once");
    zsd_close();
    CHECK(zsd_open(path) == 0 && !zsd_was_reset(), "opened again");
    zsd_entry_t l[4];
    CHECK(zsd_list(NULL, l, 4) == 1 && !strcmp(l[0].name, "caf\xc3\xa9 run") && l[0].notify &&
              l[0].fired == 1 && l[0].next_ms == SCHED_NOW + 600000 && !strcmp(l[0].note, "beans"),
          "the schedule came back");
    CHECK(zsd_fired(NULL, 0, fr, 8, &next) == 1 && fr[0].seq == 1 && next == 1, "and its firing");
    SCHED_NOW += 3 * 600000 + 1;
    CHECK(zsd_tick() == 1 && zsd_fired(NULL, 1, fr, 8, &next) == 1 && fr[0].seq == 2 &&
              fr[0].missed == 2 && fr[0].late,
          "numbers go on across runs; while it was off, missed ones count");
    CHECK(zsd_add(&e, &id) == 0 && id == 2, "ids go on too");
    FILE *f = fopen(path, "rb");
    size_t len = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[len] = 0;
    CHECK(len > 0 && strstr(buf, "zxv-sched 1\nnext\t3\t3\n") &&
              strstr(buf, "\tcaf\xc3\xa9 run\t") && strstr(buf, "\nend "),
          "the file: %s", buf);
    char *x = strstr(buf, "beans");
    if (x) x[0] = 'B';
    f = fopen(path, "wb");
    if (f) {
        fputs(buf, f);
        fclose(f);
    }
    zsd_close();
    CHECK(zsd_open(path) == 0 && zsd_was_reset() && bk_exists(dmg) && zsd_list(NULL, l, 4) == 0,
          "an edited file is moved aside; schedules start empty");
    zsd_close();
    CHECK(zsd_open(NULL) == 0, "back to memory");
    zsd_set_clock(NULL);
    zxvd_rpc_set_notify(NULL);
    if (old_tz[0])
        setenv("TZ", old_tz, 1);
    else
        unsetenv("TZ");
    tzset();
    CHECK(rm_tree(dir) == 0, "cleanup");
}

/* ---------------- proc.* (zxvd_proc.h P1-P5) ---------------- */

/* the proc.list entry of `pid` (from its "{"), or NULL */
static const char *proc_entry(long pid, bool all)
{
    static char key[48];
    rpc(all ? REQ("1", "proc.list", ",\"params\":{\"all\":true}") : REQ("1", "proc.list", ""));
    snprintf(key, sizeof key, "{\"pid\":%ld,", pid);
    return strstr(OUT.p, key);
}

static pid_t spawn_sleep(const char *shell_line)
{
    pid_t p = fork();
    if (p == 0) {
        if (chdir("/") != 0) _exit(126);
        if (shell_line)
            execl("/bin/sh", "sh", "-c", shell_line, (char *) NULL);
        else
            execlp("sleep", "sleep", "30", (char *) NULL);
        _exit(127);
    }
    usleep(50000);
    return p;
}

/* proc.list until restart `id` is no longer stopping (its "restarts" part) */
static const char *restart_wait(uint32_t id)
{
    char key[48];
    snprintf(key, sizeof key, "{\"restart\":%u,", id);
    for (int i = 0; i < 1500; i++) {
        zxvd_proc_service();
        rpc(REQ("1", "proc.list", ""));
        const char *p = strstr(OUT.p, key);
        if (p && !strstr(p, "\"state\":\"stopping\"")) return p;
        usleep(10000);
    }
    return "";
}

static void test_proc(void)
{
    char r[200];
    const char *e = proc_entry((long) getpid(), false);
    CHECK(e && strstr(e, "\"name\":\"test_zxvd_rpc") && strstr(e, "\"mine\":true,\"job\":null}") &&
              strstr(e, "\"state\":\"running\"") && strstr(e, "\"session_owner\":"),
          "P1: zxvd's own process: %s", e ? e : OUT.p);
    CHECK(strstr(OUT.p, "],\"count\":") && strstr(OUT.p, "\"control\":false,\"restarts\":[]}"),
          "with the count and control off: %.80s", strstr(OUT.p, "],\"count\":"));
    CHECK(has(REQ("1", "proc.list", ",\"params\":{\"all\":true}"), "{\"pid\":1,"),
          "all: pid 1 too");
    pid_t kid = spawn_sleep(NULL);
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"code\":-32028,\"message\":\"process control is off in this zxvd\""),
          "P5: off until the host turns it on: %s", OUT.p);
    zxvd_proc_enable(true);

    /* P2 */
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld,"
             "\"signal\":\"stop\"}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"signal\":\"stop\",\"sent\":true}"), "stop: %s", OUT.p);
    usleep(50000);
    e = proc_entry((long) kid, false);
    CHECK(e && strstr(e, "\"name\":\"sleep\"") && strstr(e, "\"state\":\"stopped\"") &&
              strstr(e, "\"ppid\":") && strstr(e, "\"mine\":true"),
          "stopped: %s", e ? e : "");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld,"
             "\"signal\":\"cont\"}}",
             (long) kid);
    rpc(r);
    usleep(50000);
    e = proc_entry((long) kid, false);
    /* not stopped: Linux says sleeping; macOS reports SRUN for a sleep(1) */
    CHECK(e && (strstr(e, "\"state\":\"sleeping\"") || strstr(e, "\"state\":\"running\"")),
          "going again: %s", e ? e : "");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld,"
             "\"signal\":\"usr1\"}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"code\":-32602"), "an unknown signal");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"signal\":\"term\",\"sent\":true}"), "term by default");
    int st;
    CHECK(waitpid(kid, &st, 0) == kid && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM,
          "it ended by SIGTERM");
    CHECK(strstr(rpc(r), "\"code\":-32028,\"message\":\"no process"), "P4: then it is gone");

    /* P4 */
    long refused[] = {0, 1, (long) getpid(), (long) getppid()};
    for (size_t i = 0; i < 4; i++) {
        snprintf(
            r, sizeof r,
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.signal\",\"params\":{\"pid\":%ld}}",
            refused[i]);
        CHECK(strstr(rpc(r), "\"code\":-32028") && strstr(OUT.p, "is not one zxvd acts on"),
              "P4: never %ld", refused[i]);
    }
    CHECK(has(REQ("1", "proc.signal", ",\"params\":{}"), "\"code\":-32602"), "a pid is needed");

    /* P1: a shell job's processes name it */
    zxvd_shell_enable("/bin/sh");
    uint32_t j = shell_start("{\"line\":\"sleep 30; true\"}");
    usleep(100000);
    rpc(REQ("1", "proc.list", ""));
    char jk[48];
    snprintf(jk, sizeof jk, "\"name\":\"sleep\"");
    const char *sp = strstr(OUT.p, jk);
    char jobv[32];
    snprintf(jobv, sizeof jobv, "\"job\":%u}", j);
    CHECK(j && sp && strstr(sp, jobv), "a job's child names the job: %s", sp ? sp : OUT.p);
    zxvd_shell_close();
    zxvd_shell_enable("");

    /* P3 */
    kid = spawn_sleep(NULL);
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.restart\",\"params\":{\"pid\":%ld}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"restart\":1,") && strstr(OUT.p, "\"state\":\"stopping\"}"),
          "restart: %s", OUT.p);
    CHECK(strstr(rpc(r), "\"code\":-32028") && strstr(OUT.p, "is already restarting (1)"),
          "once at a time: %s", OUT.p);
    const char *rs = restart_wait(1);
    long np = 0;
    const char *npp = strstr(rs, "\"new_pid\":");
    if (npp) np = strtol(npp + 10, NULL, 10);
    CHECK(strstr(rs, "\"state\":\"started\"") && np > 0 && np != kid, "started again: %.120s", rs);
    waitpid(kid, &st, 0);
    usleep(50000);
    e = proc_entry(np, false);
    CHECK(e && strstr(e, "\"name\":\"sleep\"") && strstr(e, "\"mine\":true"),
          "the same program, on its own: %s", e ? e : "");
    if (np > 0) kill((pid_t) np, SIGKILL);
    kid = spawn_sleep("trap '' TERM; while :; do sleep 1; done");
    snprintf(r, sizeof r,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"proc.restart\",\"params\":{\"pid\":%ld}}",
             (long) kid);
    CHECK(strstr(rpc(r), "\"restart\":2,"), "a process deaf to SIGTERM: %s", OUT.p);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 400 && waitpid(kid, &st, WNOHANG) == 0; i++) {
        zxvd_proc_service();
        usleep(10000);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && t1.tv_sec - t0.tv_sec >= 1,
          "P3: SIGKILL two seconds later");
    rs = restart_wait(2);
    npp = strstr(rs, "\"new_pid\":");
    np = npp ? strtol(npp + 10, NULL, 10) : 0;
    CHECK(strstr(rs, "\"state\":\"started\""), "then started: %.120s", rs);
    if (np > 0) {
        kill((pid_t) -np, SIGKILL); /* its own session and group */
        kill((pid_t) np, SIGKILL);
    }
    CHECK(has(REQ("1", "proc.restart", ",\"params\":{\"pid\":1}"), "is not one zxvd acts on"),
          "P4: restart refuses as signal does");
    zxvd_proc_close();
    CHECK(has(REQ("1", "proc.list", ""), "\"restarts\":[]}"), "close forgets the restarts");
    zxvd_proc_enable(false);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN); /* as zxv_host.c does: a closed peer is an error return */
    test_buf();
    test_protocol();
    test_host_methods();
    test_peer_methods();
    test_organ_method();
    test_phone_methods();
    test_aipi_methods();
    test_files_methods();
    test_spaces_methods();
    test_sys_and_log();
    test_sys_shapes();
    test_search();
    test_print();
    test_backup();
    test_journal();
    test_screen();
    test_place();
    test_people_methods();
    test_books_closed();
    test_books();
    test_earnings();
    test_books_file();
    test_books_vfv_reversal();
    test_mesh();
    test_views();
    test_play();
    test_jurisdiction();
    test_juris_plugins();
    test_groups();
    test_feature_save();
    test_edges();
    test_ext();
    test_audit();
    test_notify();
    test_caps();
    test_shell();
    test_dns();
    test_sched();
    test_proc();
    test_socket();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_rpc: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_rpc: %d checks\n", checks);
    return 0;
}
