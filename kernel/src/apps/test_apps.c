/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_apps.c — the app models (zxm.h, Burrow, Quillon, Glint, Spindle,
 * Murmuration) against zxvd's real dispatcher in this process: files.* runs
 * on a scratch ZXV folder, swarm.ask is a stub that returns a set answer and
 * swarm.state and model.info return set JSON. A second transport hands zxm
 * made-up replies to cover its error paths; a third answers each method
 * from a script. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700 /* nftw */
#define _DARWIN_C_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zxvd_rpc.h"
#include "zxv_files.h"
#include "burrow_model.h"
#include "glint_model.h"
#include "murmuration_model.h"
#include "quillon_model.h"
#include "spindle_model.h"
#include "swarm_emotion.h"

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

/* ---------------- the stub host and the two transports ---------------- */

static const char *ANSWER = "ok";
static char last_prompt[8192];
static int asked;

static void h_ask(const char *q, zxvd_buf_t *o)
{
    asked++;
    snprintf(last_prompt, sizeof last_prompt, "%s", q);
    zxvd_buf_json_str(o, ANSWER);
}

/* swarm.state and model.info: these JSON objects as they are */
static const char *STATE = "{}";
static const char *MODEL = "{}";
static void h_state(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, STATE, strlen(STATE));
}
static void h_model(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, MODEL, strlen(MODEL));
}
static const zxvd_host_ops_t OPS = {
    .state = h_state, .ask = h_ask, .model = h_model, .app_version = "test"};

static zxvd_buf_t OUT;
static int sent;

static int32_t inproc(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    (void) ctx;
    sent++;
    if (!zxvd_rpc_handle_line(req, len, &OUT)) return -1;
    memcpy(resp, OUT.p, OUT.len < cap ? OUT.len : cap);
    return (int32_t) OUT.len;
}

/* made up: the reply is CANNED with "%u" replaced by the request's id */
static const char *CANNED;
static char last_req[ZXM_REQ_MAX + 1];

static int32_t canned(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    memcpy(last_req, req, len);
    last_req[len] = 0;
    if (!CANNED) return -1;
    int n = snprintf(resp, cap, CANNED, l->seq);
    return n;
}

static zxm_link_t LINK, FAKE;

/* what a view's progress bar would be told */
typedef struct {
    uint32_t calls;
    uint64_t done, total;
    bool rising; /* done never went back */
} prog_t;
static prog_t PROG = {.rising = true};

static void on_progress(void *ctx, uint64_t done, uint64_t total)
{
    prog_t *p = ctx;
    if (p->calls && done < p->done) p->rising = false;
    p->calls++;
    p->done = done;
    p->total = total;
}

/* ---------------- zxm ---------------- */

static void test_zxm(void)
{
    char out[64];
    CHECK(zxm_escape("a\"\\\n\t\x01z", 7, out, sizeof out) == 16 &&
              !strcmp(out, "a\\\"\\\\\\n\\t\\u0001z"),
          "escape: %s", out);
    CHECK(zxm_escape("abc", 3, out, 2) == -1, "escape that does not fit");
    CHECK(zxm_escape("\x1f", 1, out, 6) == 6 && !memcmp(out, "\\u001f", 6), "escape at cap");
    CHECK(zxm_utf8_cut("ab\xc3\xa9", 4) == 4, "whole two-byte char kept");
    CHECK(zxm_utf8_cut("ab\xc3\xa9", 3) == 2, "half a char cut");
    CHECK(zxm_utf8_cut("a\xe2\x82\xac", 3) == 1, "two of three bytes cut");
    CHECK(zxm_utf8_cut("a\xf0\x9f\x98\x80", 5) == 5, "four-byte char kept");
    CHECK(zxm_utf8_cut("\x80\x80", 2) == 0, "only continuation bytes");
    CHECK(zxm_utf8_cut("abc", 0) == 0, "nothing");

    zxm_link_init(&FAKE, canned, &FAKE);
    zxm_begin(&FAKE, "x.y");
    zxm_p_str(&FAKE, "s", "q\"");
    zxm_p_u64(&FAKE, "n", 18446744073709551615ull);
    zxm_p_u64(&FAKE, "z", 0);
    zxm_p_bool(&FAKE, "t", true);
    zxm_p_bool(&FAKE, "f", false);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"a\":\"b\",\"n\":-3,\"u\":5,\"ok\":true,"
             "\"arr\":[1,2]}}";
    CHECK(zxm_call(&FAKE) == ZXM_OK, "call: %s", FAKE.err);
    CHECK(!strcmp(last_req,
                  "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x.y\",\"params\":{\"s\":"
                  "\"q\\\"\",\"n\":18446744073709551615,\"z\":0,\"t\":true,\"f\":false}}"),
          "request: %s", last_req);
    char s[8];
    int64_t i;
    uint64_t u;
    bool b;
    CHECK(zxm_get_str(&FAKE, FAKE.result, "a", s, sizeof s) == ZXM_OK && !strcmp(s, "b"), "str");
    CHECK(zxm_get_str(&FAKE, FAKE.result, "n", s, sizeof s) == ZXM_E_PROTOCOL && !s[0],
          "a number is not a string");
    CHECK(zxm_get_str(&FAKE, FAKE.result, "a", s, 1) == ZXM_E_TOOBIG, "string too long");
    CHECK(zxm_get_i64(&FAKE, FAKE.result, "n", &i) == ZXM_OK && i == -3, "i64");
    CHECK(zxm_get_u64(&FAKE, FAKE.result, "u", &u) == ZXM_OK && u == 5, "u64");
    CHECK(zxm_get_u64(&FAKE, FAKE.result, "n", &u) == ZXM_E_PROTOCOL, "negative is not u64");
    CHECK(zxm_get_bool(&FAKE, FAKE.result, "ok", &b) == ZXM_OK && b, "bool");
    CHECK(zxm_get_bool(&FAKE, FAKE.result, "zz", &b) == ZXM_E_PROTOCOL, "missing bool");
    int32_t arr = zxm_get(&FAKE, FAKE.result, "arr");
    CHECK(zxm_len(&FAKE, arr) == 2 && zxm_at(&FAKE, arr, 1) > arr, "array");
    CHECK(zxm_len(&FAKE, FAKE.result) == 0 && zxm_at(&FAKE, FAKE.result, 0) < 0,
          "an object is not an array");
    CHECK(zxm_get(&FAKE, arr, "a") < 0 && zxm_get(&FAKE, -1, "a") < 0 &&
              zxm_get(&FAKE, FAKE.ntok, "a") < 0,
          "get on a non-object");
    CHECK(zxm_len(&FAKE, -1) == 0 && zxm_at(&FAKE, FAKE.ntok, 0) < 0, "array bounds");

    zxm_begin(&FAKE, "m");
    zxm_p_raw(&FAKE, "{\"k\":[1]}", 9);
    CHECK(zxm_call(&FAKE) == ZXM_OK &&
              !strcmp(last_req,
                      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"m\",\"params\":{\"k\":[1]}}"),
          "raw params: %s", last_req);
    zxm_begin(&FAKE, "m");
    zxm_p_raw(&FAKE, "{}", 2);
    zxm_p_u64(&FAKE, "a", 1);
    CHECK(zxm_call(&FAKE) == ZXM_OK &&
              !strcmp(last_req,
                      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"m\",\"params\":{\"a\":1}}"),
          "empty raw params, then a key: %s", last_req);
    zxm_begin(&FAKE, "m");
    zxm_p_u64(&FAKE, "a", 1);
    zxm_p_raw(&FAKE, "{}", 2);
    CHECK(zxm_call(&FAKE) == ZXM_E_TOOBIG, "raw after a key is refused");
    zxm_begin(&FAKE, "m");
    zxm_p_raw(&FAKE, "[1]", 3);
    CHECK(zxm_call(&FAKE) == ZXM_E_TOOBIG, "raw that is not an object");

    static char big[ZXM_REQ_MAX];
    memset(big, 'a', sizeof big);
    int before = FAKE.seq;
    last_req[0] = 0;
    zxm_begin(&FAKE, "m");
    zxm_p_strn(&FAKE, "s", big, sizeof big);
    zxm_p_u64(&FAKE, "n", 1);
    CHECK(zxm_call(&FAKE) == ZXM_E_TOOBIG && !last_req[0] && (int) FAKE.seq == before + 1,
          "an oversized request is not sent");
    zxm_begin(&FAKE, "m");
    zxm_p_strn(&FAKE, "s", big, ZXM_REQ_MAX - 60u);
    CHECK(zxm_call(&FAKE) == ZXM_OK && strlen(last_req) <= ZXM_REQ_MAX, "a request at the limit");

    CANNED = NULL;
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_TRANSPORT && FAKE.result < 0 && FAKE.err[0], "transport");
    CANNED = "nope";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "not JSON");
    CANNED = "[1]";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "not an object");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":999%u,\"result\":{}}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "another request's id");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":\"%u\",\"result\":{}}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "a string id");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "no result");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"message\":\"x\"}}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_PROTOCOL, "an error with no code");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32012,\"message\":\"no such\"}}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_REMOTE && FAKE.err_code == -32012 &&
              !strcmp(FAKE.err, "no such") && FAKE.result < 0,
          "a remote error");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-1}}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_E_REMOTE && FAKE.err_code == -1 && !FAKE.err[0],
          "a remote error with no message");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":1}";
    zxm_begin(&FAKE, "m");
    CHECK(zxm_call(&FAKE) == ZXM_OK && !FAKE.err[0] && FAKE.err_code == 0,
          "success clears the last error");
}

/* the reply larger than the link holds */
static int32_t huge(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    (void) ctx, (void) req, (void) len, (void) resp;
    return (int32_t) cap + 1;
}

static void test_zxm_ai(void)
{
    zxm_held_t h;
    static const zxm_ai_action_t A = {"Act", "Writer-with-a-very-long-agent-name-xx", 10};
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"answer\":\"hi\",\"tokens\":77}}";
    CHECK(zxm_ai_ask(&FAKE, &A, 3, "Do.", "in", 2, &h) == ZXM_OK && h.state == ZXM_HELD_PENDING &&
              h.action == 3 && !h.cut && h.cost.exact && h.cost.tokens == 77 && h.len == 2 &&
              !strcmp(h.text, "hi") && strlen(h.cost.agent) == ZXM_AGENT_MAX - 1,
          "exact tokens, agent cut to fit");
    CHECK(strstr(last_req, "\"prompt\":\"Do.\\n\\nin\""), "prompt: %s", last_req);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"answer\":\"abcd\"}}";
    CHECK(zxm_ai_ask(&FAKE, &A, 0, "12345678", "1234", 4, &h) == ZXM_OK && !h.cost.exact &&
              h.cost.tokens == (14u + 4u + 3u) / 4u,
          "estimate: %llu", (unsigned long long) h.cost.tokens);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"answer\":7}}";
    CHECK(zxm_ai_ask(&FAKE, &A, 0, "i", "x", 1, &h) == ZXM_E_PROTOCOL, "answer not a string");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{}}";
    CHECK(zxm_ai_ask(&FAKE, &A, 0, "i", "x", 1, &h) == ZXM_E_PROTOCOL, "no answer");
    static char longi[ZXM_ASK_MAX];
    memset(longi, 'i', sizeof longi - 1);
    CHECK(zxm_ai_ask(&FAKE, &A, 0, longi, "x", 1, &h) == ZXM_E_ARG, "instruction too long");

    /* input cut on a character boundary */
    static char in[ZXM_ASK_MAX * 2];
    for (uint32_t i = 0; i + 1 < sizeof in; i += 2) memcpy(in + i, "\xc3\xa9", 2);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"answer\":\"a\"}}";
    CHECK(zxm_ai_ask(&FAKE, &A, 0, "abc", in, sizeof in, &h) == ZXM_OK && h.cut, "input cut");
    const char *p = strstr(last_req, "\\n\\n") + 4;
    size_t pl = strlen(p) - 3; /* less the closing "}} */
    CHECK(pl % 2 == 0 && 5 + pl <= ZXM_ASK_MAX && 5 + pl + 2 > ZXM_ASK_MAX, "cut at %zu", pl);

    /* an answer longer than the held text is cut, not lost */
    static char reply[ZXM_ANSWER_MAX * 4];
    int o = sprintf(reply, "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":{\"answer\":\"");
    for (uint32_t i = 0; i < ZXM_ANSWER_MAX + 2u; i += 2) o += sprintf(reply + o, "\\u00e9");
    sprintf(reply + o, "\"}}");
    CANNED = reply;
    CHECK(zxm_ai_ask(&FAKE, &A, 0, "abc", "x", 1, &h) == ZXM_OK && h.cut &&
              h.len <= ZXM_ANSWER_MAX && h.len >= ZXM_ANSWER_MAX - 1 && h.len % 2 == 0 &&
              h.text[h.len] == 0,
          "long answer cut to %u", h.len);
    zxm_link_t big;
    zxm_link_init(&big, huge, NULL);
    CHECK(zxm_ai_ask(&big, &A, 0, "abc", "x", 1, &h) == ZXM_E_TOOBIG, "a reply too large");
}

/* ---------------- Burrow and Glint ---------------- */

static burrow_t B;
static glint_t G;

static int find(const burrow_t *b, const char *name)
{
    for (uint32_t i = 0; i < b->n; i++)
        if (!strcmp(b->e[i].name, name)) return (int) i;
    return -1;
}

static void test_burrow(const char *root)
{
    CHECK(burrow_kind("a.PNG") == BURROW_K_IMAGE && burrow_kind("b.Mp3") == BURROW_K_AUDIO &&
              burrow_kind("c.webm") == BURROW_K_VIDEO && burrow_kind("d.md") == BURROW_K_TEXT &&
              burrow_kind("README") == BURROW_K_TEXT && burrow_kind(".zshrc") == BURROW_K_TEXT &&
              burrow_kind("e.bin") == BURROW_K_OTHER && burrow_kind("f.pngx") == BURROW_K_OTHER,
          "kinds");
    CHECK(burrow_init(&B, &LINK) == ZXM_OK && !strcmp(B.root, root) && B.n == 0 && B.sel == -1 &&
              !B.dir[0],
          "init: %s", LINK.err);
    CHECK(burrow_up(&B) == ZXM_E_STATE, "no folder above the ZXV folder");
    CHECK(burrow_new_text(&B, "a.txt") == ZXM_OK && B.n == 1 && !B.e[0].dir &&
              B.e[0].kind == BURROW_K_TEXT && B.e[0].size == 0,
          "new text");
    CHECK(burrow_new_text(&B, "a.txt") == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_FILES,
          "never overwrites");
    CHECK(burrow_new_text(&B, "") == ZXM_E_ARG, "no name");
    CHECK(burrow_mkdir(&B, "Pics") == ZXM_OK && B.n == 2, "mkdir");
    CHECK(burrow_mkdir(&B, "../x") == ZXM_E_REMOTE, "a bad name is zxvd's to refuse");
    int pics = find(&B, "Pics");
    CHECK(pics >= 0 && B.e[pics].dir && B.e[pics].kind == BURROW_K_DIR, "a folder entry");
    CHECK(burrow_enter(&B, (uint32_t) find(&B, "a.txt")) == ZXM_E_STATE, "enter a file");
    CHECK(burrow_enter(&B, 99) == ZXM_E_STATE, "enter past the end");
    CHECK(burrow_enter(&B, (uint32_t) pics) == ZXM_OK && !strcmp(B.dir, "Pics") && B.n == 0,
          "enter");

    static uint8_t png[20000];
    for (uint32_t i = 0; i < sizeof png; i++) png[i] = (uint8_t) (i * 7u + (i >> 8));
    zxm_link_progress(&LINK, on_progress, &PROG);
    CHECK(burrow_import(&B, "one.png", png, sizeof png) == ZXM_OK && B.n == 1 &&
              B.e[0].size == sizeof png && B.e[0].kind == BURROW_K_IMAGE,
          "import in three parts: %s", LINK.err);
    CHECK(PROG.calls == 3 && PROG.rising && PROG.done == sizeof png && PROG.total == sizeof png,
          "progress after each part: %u calls, %llu of %llu", PROG.calls,
          (unsigned long long) PROG.done, (unsigned long long) PROG.total);
    zxm_link_progress(&LINK, NULL, NULL);
    PROG = (prog_t){.rising = true};
    CHECK(burrow_import(&B, "two.jpg", png, 1) == ZXM_OK, "one byte");
    CHECK(burrow_import(&B, "three.mp3", png, 2) == ZXM_OK, "two bytes");
    CHECK(burrow_import(&B, "four.gif", png, 0) == ZXM_OK, "no bytes");
    CHECK(burrow_import(&B, "notes.md", (const uint8_t *) "# hi\n", 5) == ZXM_OK, "text");
    CHECK(burrow_import(&B, "one.png", png, 3) == ZXM_E_REMOTE, "an import never overwrites");
    CHECK(B.n == 5, "five files: %u", B.n);
    char p[64];
    CHECK(burrow_path(&B, (uint32_t) find(&B, "one.png"), p, sizeof p) == ZXM_OK &&
              !strcmp(p, "Pics/one.png"),
          "path");
    CHECK(burrow_path(&B, 0, p, 5) == ZXM_E_ARG && burrow_path(&B, 9, p, sizeof p) == ZXM_E_ARG,
          "path bounds");

    /* Glint over the same folder */
    glint_scan(&G, &B);
    CHECK(G.n == 4 && G.cur == -1, "four media items: %u", G.n);
    uint8_t buf[sizeof png];
    uint32_t got = 1;
    CHECK(glint_load(&G, buf, sizeof buf, &got) == ZXM_E_STATE && got == 0, "nothing viewed");
    CHECK(glint_next(&G) == ZXM_E_STATE && glint_prev(&G) == ZXM_E_STATE, "no current item");
    int one = -1;
    for (uint32_t i = 0; i < G.n; i++)
        if (!strcmp(G.item[i].name, "one.png")) one = (int) i;
    CHECK(glint_view(&G, one) == ZXM_OK, "view");
    CHECK(glint_view(&G, (int32_t) G.n) == ZXM_E_ARG && glint_view(&G, -2) == ZXM_E_ARG,
          "view bounds");
    zxm_link_progress(&LINK, on_progress, &PROG);
    CHECK(glint_load(&G, buf, sizeof buf, &got) == ZXM_OK && got == sizeof png &&
              !memcmp(buf, png, sizeof png),
          "load: %s", LINK.err);
    CHECK(PROG.calls >= 1 && PROG.rising && PROG.done == sizeof png && PROG.total == sizeof png,
          "load progress: %u calls, %llu of %llu", PROG.calls, (unsigned long long) PROG.done,
          (unsigned long long) PROG.total);
    zxm_link_progress(&LINK, NULL, NULL);
    CHECK(glint_load(&G, buf, sizeof png - 1, &got) == ZXM_E_TOOBIG && got == 0, "too small");
    for (uint32_t i = 0; i < G.n; i++) {
        CHECK(glint_view(&G, (int32_t) i) == ZXM_OK, "view %u", i);
        uint32_t want = !strcmp(G.item[i].name, "two.jpg")     ? 1
                        : !strcmp(G.item[i].name, "three.mp3") ? 2
                        : !strcmp(G.item[i].name, "four.gif")  ? 0
                                                               : sizeof png;
        CHECK(glint_load(&G, buf, sizeof buf, &got) == ZXM_OK && got == want &&
                  !memcmp(buf, png, want),
              "%s: %u bytes", G.item[i].name, got);
    }
    CHECK(glint_view(&G, 0) == ZXM_OK && glint_prev(&G) == ZXM_E_STATE &&
              glint_next(&G) == ZXM_OK && G.cur == 1 && glint_prev(&G) == ZXM_OK && G.cur == 0,
          "next and prev");
    CHECK(glint_view(&G, (int32_t) G.n - 1) == ZXM_OK && glint_next(&G) == ZXM_E_STATE,
          "no next at the end");
    CHECK(glint_view(&G, -1) == ZXM_OK && G.cur == -1, "close the viewer");
    CHECK(!strcmp(glint_mime("a.JPG"), "image/jpeg") &&
              !strcmp(glint_mime("b.mov"), "video/quicktime") &&
              !strcmp(glint_mime("c.opus"), "audio/ogg") &&
              !strcmp(glint_mime("d"), "application/octet-stream") &&
              !strcmp(glint_mime(".png"), "application/octet-stream") &&
              !strcmp(glint_mime("e.pngg"), "application/octet-stream"),
          "mime");
    ANSWER = "Spring Walk";
    asked = 0;
    CHECK(glint_ai(&G, GLINT_AI_COUNT) == ZXM_E_ARG && glint_ai(&G, -1) == ZXM_E_ARG && !asked,
          "bad action");
    CHECK(glint_ai(&G, GLINT_AI_NAME_ALBUM) == ZXM_OK && G.held.state == ZXM_HELD_PENDING &&
              !strcmp(G.held.text, "Spring Walk") && !strcmp(G.held.cost.agent, "Writer") &&
              !G.held.cost.exact && G.held.cost.tokens > 0,
          "album name held");
    CHECK(strstr(last_prompt, "one.png\n") && !strstr(last_prompt, "notes.md"),
          "only media names go to the swarm: %s", last_prompt);
    CHECK(glint_ai_accept(&G) == ZXM_OK && G.held.state == ZXM_HELD_ACCEPTED &&
              glint_ai_accept(&G) == ZXM_E_STATE && glint_ai_reject(&G) == ZXM_E_STATE,
          "accept once");
    CHECK(glint_ai(&G, 0) == ZXM_OK && glint_ai_reject(&G) == ZXM_OK &&
              G.held.state == ZXM_HELD_REJECTED && !G.held.len && !G.held.text[0],
          "reject drops it");
    glint_t empty = {0};
    CHECK(glint_ai(&empty, 0) == ZXM_E_STATE, "no media");

    /* selection survives a refresh by name */
    CHECK(burrow_select(&B, (int32_t) find(&B, "notes.md")) == ZXM_OK, "select");
    CHECK(burrow_select(&B, (int32_t) B.n) == ZXM_E_ARG && burrow_select(&B, -2) == ZXM_E_ARG,
          "select bounds");
    CHECK(burrow_refresh(&B) == ZXM_OK && B.sel == find(&B, "notes.md"), "kept selected");
    CHECK(burrow_rename(&B, (uint32_t) find(&B, "notes.md"), "renamed.md") == ZXM_OK &&
              find(&B, "renamed.md") >= 0 && find(&B, "notes.md") < 0,
          "rename");
    CHECK(burrow_select(&B, (int32_t) find(&B, "renamed.md")) == ZXM_OK &&
              burrow_rename(&B, 99, "x") == ZXM_E_ARG,
          "rename bounds");
    CHECK(burrow_rename(&B, (uint32_t) find(&B, "renamed.md"), "") == ZXM_E_ARG, "no new name");

    /* the Trash */
    CHECK(burrow_trash(&B, (uint32_t) find(&B, "renamed.md")) == ZXM_OK && B.n == 4 && B.sel == -1,
          "to the Trash");
    CHECK(burrow_trash(&B, 99) == ZXM_E_ARG, "trash bounds");
    CHECK(burrow_up(&B) == ZXM_OK && !B.dir[0] && B.n == 2, "up: %u", B.n);
    CHECK(!burrow_in_trash(&B), "not in the Trash");
    CHECK(burrow_open(&B, ".Trash") == ZXM_OK && burrow_in_trash(&B) && B.n == 1 &&
              !strcmp(B.e[0].name, "renamed.md"),
          "the Trash holds it");
    CHECK(burrow_mkdir(&B, "x") == ZXM_E_STATE && burrow_new_text(&B, "x") == ZXM_E_STATE &&
              burrow_import(&B, "x", png, 1) == ZXM_E_STATE &&
              burrow_rename(&B, 0, "y") == ZXM_E_STATE,
          "no new things in the Trash");
    CHECK(burrow_put_back(&B, 0, "back.md") == ZXM_OK && B.n == 0, "put back");
    CHECK(burrow_put_back(&B, 0, "x") == ZXM_E_ARG, "put back bounds");
    CHECK(burrow_open(&B, "") == ZXM_OK && find(&B, "back.md") >= 0, "it is back");
    CHECK(burrow_put_back(&B, (uint32_t) find(&B, "back.md"), "y") == ZXM_E_STATE,
          "put back outside the Trash");
    CHECK(burrow_trash(&B, (uint32_t) find(&B, "back.md")) == ZXM_OK &&
              burrow_trash(&B, (uint32_t) find(&B, "a.txt")) == ZXM_OK,
          "two in the Trash");
    uint32_t removed = 9;
    CHECK(burrow_empty_trash(&B, &removed) == ZXM_OK && removed == 2 && !burrow_in_trash(&B),
          "empty: %u", removed);
    CHECK(burrow_open(&B, ".Trash") == ZXM_OK && B.n == 0, "empty Trash");
    CHECK(burrow_empty_trash(&B, &removed) == ZXM_OK && removed == 0 && burrow_in_trash(&B),
          "empty again, from the Trash");
    CHECK(burrow_open(&B, "nope") == ZXM_E_REMOTE && burrow_in_trash(&B),
          "a failed open keeps the folder");
    CHECK(burrow_open(&B, ".Trashy") == ZXM_E_REMOTE, "not the Trash");

    /* AI over the names */
    CHECK(burrow_open(&B, "") == ZXM_OK, "back to the ZXV folder");
    ANSWER = "Pictures and notes.";
    CHECK(burrow_ai(&B, BURROW_AI_COUNT) == ZXM_E_ARG, "bad action");
    CHECK(burrow_ai(&B, BURROW_AI_DESCRIBE) == ZXM_OK && B.held.state == ZXM_HELD_PENDING &&
              !strcmp(B.held.cost.agent, "Companion") && B.held.action == BURROW_AI_DESCRIBE,
          "describe");
    CHECK(strstr(last_prompt, "Pics/\n"), "folders end in a slash: %s", last_prompt);
    CHECK(burrow_ai_accept(&B) == ZXM_OK && burrow_ai_accept(&B) == ZXM_E_STATE, "accept");
    CHECK(burrow_ai(&B, BURROW_AI_SUGGEST_FOLDERS) == ZXM_OK &&
              !strcmp(B.held.cost.agent, "Planner") && burrow_ai_reject(&B) == ZXM_OK &&
              B.held.state == ZXM_HELD_REJECTED && burrow_ai_reject(&B) == ZXM_E_STATE,
          "reject");
    CHECK(burrow_open(&B, ".Trash") == ZXM_OK && burrow_ai(&B, 0) == ZXM_E_STATE,
          "nothing to describe");
    CHECK(burrow_open(&B, "") == ZXM_OK, "home");
}

/* ---------------- Quillon ---------------- */

static quillon_t Q, Q2;

static void test_quillon(void)
{
    quillon_new(&Q, &LINK);
    CHECK(!quillon_dirty(&Q) && Q.counts.words == 0 && Q.counts.lines == 0, "new");
    CHECK(quillon_save(&Q) == ZXM_E_STATE, "nowhere to save");
    CHECK(quillon_set_text(&Q, "Hello  wide\nw\xc3\xb6rld\n", 19) == ZXM_OK && quillon_dirty(&Q),
          "set");
    CHECK(Q.counts.words == 3 && Q.counts.chars == 18 && Q.counts.lines == 3, "counts %u %u %u",
          Q.counts.words, Q.counts.chars, Q.counts.lines);
    CHECK(quillon_set_text(&Q, "a\xc3", 2) == ZXM_E_ARG &&
              quillon_set_text(&Q, "a\0b", 3) == ZXM_E_ARG,
          "not UTF-8 or a NUL");
    CHECK(quillon_set_text(&Q, "\xed\xa0\x80", 3) == ZXM_E_ARG &&
              quillon_set_text(&Q, "\xe0\x80\x80", 3) == ZXM_E_ARG &&
              quillon_set_text(&Q, "\xf4\x90\x80\x80", 4) == ZXM_E_ARG &&
              quillon_set_text(&Q, "\xc0\x80", 2) == ZXM_E_ARG,
          "surrogates, overlong, past U+10FFFF");
    CHECK(Q.len == 19, "a refused edit changes nothing");
    CHECK(quillon_save_as(&Q, "", "Pics/doc.md") == ZXM_OK && !quillon_dirty(&Q) &&
              !strcmp(Q.path, "Pics/doc.md"),
          "save as: %s", LINK.err);
    CHECK(quillon_save_as(&Q, "", "") == ZXM_E_ARG, "save as nowhere");
    CHECK(quillon_save_as(&Q, "", "nope/x.md") == ZXM_E_REMOTE && !strcmp(Q.path, "Pics/doc.md"),
          "a failed save keeps the path");

    /* replace on character boundaries only */
    CHECK(quillon_replace(&Q, 14, 15, "o", 1) == ZXM_E_ARG, "inside a character");
    CHECK(quillon_replace(&Q, 5, 4, "", 0) == ZXM_E_ARG &&
              quillon_replace(&Q, 0, 99, "", 0) == ZXM_E_ARG,
          "bad range");
    CHECK(quillon_replace(&Q, 13, 15, "o", 1) == ZXM_OK &&
              !strcmp(Q.text, "Hello  wide\nworld\n") && quillon_dirty(&Q),
          "replace: %s", Q.text);
    CHECK(quillon_replace(&Q, 18, 18, "!", 1) == ZXM_OK && Q.len == 19, "append");
    CHECK(quillon_replace(&Q, 18, 19, "", 0) == ZXM_OK && Q.len == 18, "delete");
    CHECK(quillon_save(&Q) == ZXM_OK && !quillon_dirty(&Q), "save");
    quillon_new(&Q2, &LINK);
    CHECK(quillon_open(&Q2, "", "Pics/doc.md") == ZXM_OK && Q2.len == 18 &&
              !strcmp(Q2.text, "Hello  wide\nworld\n") && !quillon_dirty(&Q2),
          "open: %s", LINK.err);
    CHECK(quillon_open(&Q2, "", "Pics/one.png") == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_FILES &&
              !strcmp(Q2.path, "Pics/doc.md") && Q2.len == 18,
          "a picture is not text, and the document stays");
    CHECK(quillon_open(&Q2, "", "missing.md") == ZXM_E_REMOTE && Q2.len == 18, "missing");

    /* a large document with everything that needs escaping, in many parts */
    static char big[300000];
    uint32_t n = 0;
    static const char *const BITS[] = {"plain ",
                                       "\"q\" ",
                                       "back\\slash ",
                                       "\xc3\xa9t\xc3\xa9 ",
                                       "\xe2\x82\xac ",
                                       "\xf0\x9f\x98\x80 ",
                                       "\x01\x1f",
                                       "\n",
                                       "\t"};
    for (uint32_t i = 0; n < sizeof big - 16; i++) {
        const char *s = BITS[(i * 7u) % 9u];
        size_t k = strlen(s);
        memcpy(big + n, s, k);
        n += (uint32_t) k;
    }
    CHECK(quillon_set_text(&Q, big, n) == ZXM_OK, "big text");
    sent = 0;
    CHECK(quillon_save_as(&Q, "", "big.txt") == ZXM_OK && sent > 30, "big save: %s (%d parts)",
          LINK.err, sent);
    CHECK(quillon_open(&Q2, "", "big.txt") == ZXM_OK && Q2.len == n && !memcmp(Q2.text, big, n) &&
              !quillon_dirty(&Q2) && !strcmp(Q2.path, "big.txt"),
          "big round trip: %u of %u", Q2.len, n);
    CHECK(Q2.counts.chars == Q.counts.chars && Q2.counts.words == Q.counts.words, "counts match");

    /* every byte a control character: the largest escaped read */
    static char ctl[QUILLON_TEXT_MAX + 1];
    memset(ctl, 1, 100000);
    CHECK(quillon_set_text(&Q, ctl, 100000) == ZXM_OK &&
              quillon_save_as(&Q, "", "ctl.txt") == ZXM_OK &&
              quillon_open(&Q2, "", "ctl.txt") == ZXM_OK && Q2.len == 100000 &&
              !memcmp(Q2.text, ctl, 100000),
          "escaped round trip: %s", LINK.err);
    memset(ctl, 'a', sizeof ctl);
    CHECK(quillon_set_text(&Q, ctl, QUILLON_TEXT_MAX) == ZXM_OK &&
              quillon_replace(&Q, 0, 0, "b", 1) == ZXM_E_TOOBIG &&
              quillon_set_text(&Q, ctl, QUILLON_TEXT_MAX + 1u) == ZXM_E_TOOBIG,
          "the size limit");

    /* AI: held until accepted; replace or insert by action */
    CHECK(quillon_set_text(&Q, "one two. three four.", 20) == ZXM_OK, "reset");
    ANSWER = "TWO";
    CHECK(quillon_ai(&Q, QUILLON_AI_COUNT, 0, 0) == ZXM_E_ARG, "bad action");
    CHECK(quillon_ai(&Q, 0, 3, 99) == ZXM_E_ARG, "bad range");
    CHECK(quillon_ai(&Q, QUILLON_AI_REWRITE, 4, 7) == ZXM_OK && Q.held.state == ZXM_HELD_PENDING &&
              !strcmp(Q.text, "one two. three four."),
          "held, the text unchanged");
    CHECK(strstr(last_prompt, "\n\ntwo") && !strstr(last_prompt, "three"), "only the selection: %s",
          last_prompt);
    CHECK(quillon_ai_accept(&Q) == ZXM_OK && !strcmp(Q.text, "one TWO. three four.") &&
              Q.sel_start == 4 && Q.sel_end == 7 && Q.held.state == ZXM_HELD_ACCEPTED,
          "rewrite replaces: %s", Q.text);
    CHECK(quillon_ai_accept(&Q) == ZXM_E_STATE && quillon_ai_reject(&Q) == ZXM_E_STATE, "once");
    ANSWER = "Five.";
    CHECK(quillon_ai(&Q, QUILLON_AI_CONTINUE, 0, 0) == ZXM_OK && Q.sel_end == Q.len &&
              quillon_ai_accept(&Q) == ZXM_OK && !strcmp(Q.text, "one TWO. three four.\n\nFive."),
          "continue inserts after the whole text: %s", Q.text);
    ANSWER = "Summary";
    CHECK(quillon_ai(&Q, QUILLON_AI_SUMMARIZE, 0, 8) == ZXM_OK && quillon_ai_accept(&Q) == ZXM_OK &&
              !strncmp(Q.text, "one TWO.\n\nSummary three", 23),
          "summary after the selection: %s", Q.text);
    CHECK(quillon_ai(&Q, QUILLON_AI_PROOFREAD, 0, 0) == ZXM_OK &&
              quillon_replace(&Q, 0, 0, "x", 1) == ZXM_OK && Q.held.state == ZXM_HELD_REJECTED &&
              quillon_ai_accept(&Q) == ZXM_E_STATE,
          "an edit drops a held answer");
    CHECK(quillon_ai(&Q, QUILLON_AI_PROOFREAD, 0, 0) == ZXM_OK && quillon_ai_reject(&Q) == ZXM_OK &&
              Q.held.state == ZXM_HELD_REJECTED && Q.text[0] == 'x',
          "reject keeps the text");
    ANSWER = "bad\x01";
    CHECK(quillon_ai(&Q, QUILLON_AI_REWRITE, 0, 1) == ZXM_OK && quillon_ai_accept(&Q) == ZXM_OK &&
              !strncmp(Q.text, "bad\x01", 4),
          "control characters are text");
    quillon_new(&Q2, &LINK);
    CHECK(quillon_ai(&Q2, 0, 0, 0) == ZXM_E_STATE, "nothing to work on");
    asked = 0;
    CHECK(quillon_ai(&Q, QUILLON_AI_REWRITE, 0, 0) == ZXM_OK && asked == 1, "whole text");
    CHECK(quillon_set_text(&Q, "a", 1) == ZXM_OK && Q.held.state == ZXM_HELD_REJECTED,
          "set_text drops a held answer");
    quillon_new(&Q2, &LINK);
    CHECK(Q2.held.state == ZXM_HELD_NONE && !Q2.path[0], "new clears");
}

/* ---------------- Spindle ---------------- */

static spindle_t S;

static void test_spindle(void)
{
    spindle_init(&S, &LINK);
    CHECK(spindle_history_len(&S) == 0 && !spindle_entry(&S, 0), "empty history");
    CHECK(spindle_ai(&S, 0, 0) == ZXM_E_STATE, "nothing to explain");
    CHECK(spindle_discover(&S) == ZXM_OK && S.n_methods > 40 &&
              !strcmp(S.methods[0], "rpc.discover") && S.protocol[0],
          "discover: %u methods, %s", S.n_methods, S.protocol);
    bool has = false;
    for (uint32_t i = 0; i < S.n_methods; i++) has |= !strcmp(S.methods[i], "files.empty_trash");
    CHECK(has, "files.empty_trash listed");

    CHECK(spindle_call(&S, "files.list", "{\"path\":\"Pics\"}") == ZXM_OK, "call: %s", LINK.err);
    const spindle_entry_t *e = spindle_entry(&S, 0);
    CHECK(e && !strcmp(e->method, "files.list") && !strncmp(e->reply, "{\"path\":\"Pics\"", 14) &&
              e->rc == ZXM_OK && e->reply_len == strlen(e->reply) && !e->reply_cut,
          "kept: %s", e ? e->reply : "");
    CHECK(spindle_call(&S, "files.root", "") == ZXM_OK && spindle_history_len(&S) == 2 &&
              !strcmp(spindle_entry(&S, 0)->method, "files.root") &&
              !strcmp(spindle_entry(&S, 1)->method, "files.list"),
          "newest first");
    CHECK(spindle_call(&S, "files.list", "{\"path\":\"..\"}") == ZXM_E_REMOTE, "refused");
    e = spindle_entry(&S, 0);
    CHECK(e->rc == ZXM_E_REMOTE && e->err_code == ZXVD_E_FILES &&
              !strcmp(e->reply, "not a valid name or path in the ZXV folder"),
          "a refusal is kept: %s", e->reply);
    CHECK(spindle_call(&S, "no.such", "") == ZXM_E_REMOTE &&
              spindle_entry(&S, 0)->err_code == -32601,
          "unknown method");
    sent = 0;
    CHECK(spindle_call(&S, "", "") == ZXM_E_ARG && spindle_call(&S, "a b", "") == ZXM_E_ARG &&
              spindle_call(&S, "a\"", "") == ZXM_E_ARG &&
              spindle_call(&S, "files.list", "{") == ZXM_E_ARG &&
              spindle_call(&S, "files.list", "[]") == ZXM_E_ARG &&
              spindle_call(&S, "files.list", "{} {}") == ZXM_E_ARG &&
              spindle_call(&S, "files.list", "\"x\"") == ZXM_E_ARG && sent == 0 &&
              spindle_history_len(&S) == 4,
          "refused before sending");
    static char m[SPINDLE_METHOD_MAX + 2];
    memset(m, 'a', SPINDLE_METHOD_MAX + 1);
    CHECK(spindle_call(&S, m, "") == ZXM_E_ARG, "method name too long");
    m[SPINDLE_METHOD_MAX] = 0;
    CHECK(spindle_call(&S, m, "") == ZXM_E_REMOTE, "the longest method name is sent");
    static char pbig[SPINDLE_PARAMS_MAX + 2];
    memset(pbig, ' ', sizeof pbig - 1);
    pbig[0] = '{';
    pbig[SPINDLE_PARAMS_MAX] = '}';
    CHECK(spindle_call(&S, "files.root", pbig) == ZXM_E_ARG, "params too long");
    pbig[SPINDLE_PARAMS_MAX - 1] = '}';
    pbig[SPINDLE_PARAMS_MAX] = 0;
    CHECK(spindle_call(&S, "files.root", pbig) == ZXM_OK &&
              strlen(spindle_entry(&S, 0)->params) == SPINDLE_PARAMS_MAX,
          "the longest params: %s", LINK.err);

    /* a string result keeps its quotes; a long reply is cut and says so */
    zxm_link_t *real = S.link;
    S.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":\"hi\"}";
    CHECK(spindle_call(&S, "x", "") == ZXM_OK && !strcmp(spindle_entry(&S, 0)->reply, "\"hi\""),
          "string result: %s", spindle_entry(&S, 0)->reply);
    static char reply[SPINDLE_REPLY_MAX * 2];
    int o = sprintf(reply, "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":[");
    while (o < (int) SPINDLE_REPLY_MAX + 100) o += sprintf(reply + o, "\"\xc3\xa9\",");
    sprintf(reply + o, "0]}");
    CANNED = reply;
    e = NULL;
    CHECK(spindle_call(&S, "x", "") == ZXM_OK && (e = spindle_entry(&S, 0)) && e->reply_cut &&
              e->reply_len <= SPINDLE_REPLY_MAX && e->reply_len >= SPINDLE_REPLY_MAX - 1 &&
              e->reply[0] == '[',
          "cut reply");
    CANNED = NULL;
    CHECK(spindle_call(&S, "x", "") == ZXM_E_TRANSPORT &&
              !strcmp(spindle_entry(&S, 0)->reply, "no connection to zxvd"),
          "a transport failure is kept");
    static char hugep[SPINDLE_PARAMS_MAX + 1];
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":1}";
    S.link = real;

    /* the ring */
    uint32_t before = S.n_hist;
    for (uint32_t i = 0; i < SPINDLE_HISTORY + 3; i++) {
        snprintf(hugep, sizeof hugep, "{\"path\":\"%u\"}", i);
        spindle_call(&S, "files.stat", hugep);
    }
    CHECK(S.n_hist == before + SPINDLE_HISTORY + 3 && spindle_history_len(&S) == SPINDLE_HISTORY,
          "ring length");
    snprintf(hugep, sizeof hugep, "{\"path\":\"%u\"}", SPINDLE_HISTORY + 2);
    CHECK(!strcmp(spindle_entry(&S, 0)->params, hugep), "newest: %s", spindle_entry(&S, 0)->params);
    snprintf(hugep, sizeof hugep, "{\"path\":\"%u\"}", 3);
    CHECK(!strcmp(spindle_entry(&S, SPINDLE_HISTORY - 1)->params, hugep), "oldest kept: %s",
          spindle_entry(&S, SPINDLE_HISTORY - 1)->params);
    CHECK(!spindle_entry(&S, SPINDLE_HISTORY), "past the end");

    /* AI */
    CHECK(spindle_call(&S, "files.root", "") == ZXM_OK, "a call to explain");
    ANSWER = "It says where the ZXV folder is.";
    CHECK(spindle_ai(&S, SPINDLE_AI_COUNT, 0) == ZXM_E_ARG && spindle_ai(&S, 0, 99) == ZXM_E_STATE,
          "bad action or entry");
    CHECK(spindle_ai(&S, SPINDLE_AI_EXPLAIN, 0) == ZXM_OK && S.held.state == ZXM_HELD_PENDING &&
              !strcmp(S.held.cost.agent, "Companion") &&
              !strcmp(S.held.text, "It says where the ZXV folder is."),
          "explain");
    CHECK(strstr(last_prompt, "Method: files.root\nParameters: {}\nReply: {\"root\":"),
          "prompt: %s", last_prompt);
    CHECK(spindle_ai_accept(&S) == ZXM_OK && spindle_ai_accept(&S) == ZXM_E_STATE, "accept");
    CHECK(spindle_call(&S, "files.list", "{\"path\":\"..\"}") == ZXM_E_REMOTE &&
              spindle_ai(&S, 0, 0) == ZXM_OK && strstr(last_prompt, "\nError: not a valid"),
          "explain an error: %s", last_prompt);
    CHECK(spindle_ai_reject(&S) == ZXM_OK && S.held.state == ZXM_HELD_REJECTED && !S.held.len &&
              spindle_ai_reject(&S) == ZXM_E_STATE,
          "reject");
}

/* ---------------- spaces ---------------- */

static int raw_call(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

static void test_spaces(void)
{
    static burrow_t SB;
    static glint_t SG;
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Studio\"}") == ZXM_OK,
          "office: %s", LINK.err);
    CHECK(raw_call("spaces.join", "{\"id\":\"crew\",\"kind\":\"team\",\"name\":\"Crew\","
                                  "\"owner\":\"alice\",\"role\":\"guest\"}") == ZXM_OK,
          "joined as a guest: %s", LINK.err);
    CHECK(raw_call("spaces.create", "{\"kind\":\"cloudtop\",\"name\":\"Front\"}") == ZXM_OK,
          "cloud top");
    CHECK(burrow_init(&SB, &LINK) == ZXM_OK && !SB.space[0], "home");
    uint32_t home_n = SB.n;
    CHECK(burrow_set_space(&SB, "studio") == ZXM_OK && !strcmp(SB.space, "studio") && SB.n == 0 &&
              !SB.dir[0],
          "into the office");
    CHECK(burrow_set_space(&SB, "nowhere") == ZXM_E_REMOTE && LINK.err_code == -32013 &&
              !strcmp(SB.space, "studio"),
          "a missing space leaves the model where it was");
    static char longid[ZXM_SPACE_MAX + 2];
    memset(longid, 'a', ZXM_SPACE_MAX + 1);
    CHECK(burrow_set_space(&SB, longid) == ZXM_E_ARG, "id too long");
    CHECK(burrow_new_text(&SB, "plan.md") == ZXM_OK && SB.n == 1, "a file in the office");
    CHECK(burrow_mkdir(&SB, "Shots") == ZXM_OK && burrow_enter(&SB, 0) == ZXM_OK, "a folder");
    static const uint8_t png[] = {0x89, 'P', 'N', 'G', 0, 1, 2, 3};
    CHECK(burrow_import(&SB, "a.png", png, sizeof png) == ZXM_OK && SB.n == 1, "import");
    glint_scan(&SG, &SB);
    uint8_t buf[16];
    uint32_t got;
    CHECK(SG.n == 1 && glint_view(&SG, 0) == ZXM_OK &&
              glint_load(&SG, buf, sizeof buf, &got) == ZXM_OK && got == sizeof png &&
              !memcmp(buf, png, sizeof png),
          "Glint reads from the space: %s", LINK.err);
    CHECK(burrow_rename(&SB, 0, "b.png") == ZXM_OK && burrow_trash(&SB, 0) == ZXM_OK && SB.n == 0,
          "rename and trash in the space");
    CHECK(burrow_open(&SB, ".Trash") == ZXM_OK && SB.n == 1 && burrow_in_trash(&SB),
          "the space's own Trash");
    uint32_t removed;
    CHECK(burrow_empty_trash(&SB, &removed) == ZXM_OK && removed == 1, "empty it");
    CHECK(burrow_set_space(&SB, "home") == ZXM_OK && !SB.space[0] && SB.n == home_n,
          "home is untouched");

    /* Quillon remembers the space it saved to */
    quillon_new(&Q, &LINK);
    CHECK(quillon_set_text(&Q, "notes", 5) == ZXM_OK &&
              quillon_save_as(&Q, "studio", "plan.md") == ZXM_OK && !strcmp(Q.space, "studio"),
          "save in the office");
    CHECK(quillon_replace(&Q, 5, 5, "!", 1) == ZXM_OK && quillon_save(&Q) == ZXM_OK, "save again");
    quillon_new(&Q2, &LINK);
    CHECK(quillon_open(&Q2, "studio", "plan.md") == ZXM_OK && !strcmp(Q2.text, "notes!") &&
              !strcmp(Q2.space, "studio"),
          "open from the office");
    CHECK(quillon_open(&Q2, "", "plan.md") == ZXM_E_REMOTE && !strcmp(Q2.space, "studio"),
          "not at home");
    CHECK(quillon_open(&Q2, longid, "plan.md") == ZXM_E_ARG, "space id too long");
    CHECK(quillon_save_as(&Q2, "crew", "x.md") == ZXM_E_REMOTE && LINK.err_code == -32013 &&
              strstr(LINK.err, "guest"),
          "a guest may not write: %s", LINK.err);
    CHECK(quillon_save_as(&Q2, "home", "from-office.md") == ZXM_OK && !Q2.space[0],
          "\"home\" is home");

    /* crossings: into a public space is held, home is not */
    CHECK(burrow_set_space(&SB, "studio") == ZXM_OK, "office");
    uint32_t ticket = 9;
    int i = -1;
    for (uint32_t k = 0; k < SB.n; k++)
        if (!strcmp(SB.e[k].name, "plan.md")) i = (int) k;
    CHECK(i >= 0 && burrow_cross(&SB, (uint32_t) i, "front", "plan.md", &ticket) == ZXM_OK &&
              ticket > 0,
          "held: %u", ticket);
    CHECK(burrow_cross_confirm(&SB, 0) == ZXM_E_ARG, "no ticket");
    CHECK(burrow_cross_confirm(&SB, ticket) == ZXM_OK, "confirmed: %s", LINK.err);
    CHECK(burrow_cross_confirm(&SB, ticket) == ZXM_E_REMOTE, "only once");
    CHECK(burrow_cross(&SB, (uint32_t) i, "", "copied.md", &ticket) == ZXM_OK && ticket == 0,
          "home at once");
    CHECK(burrow_cross(&SB, (uint32_t) i, "crew", "x.md", &ticket) == ZXM_E_REMOTE && ticket == 0,
          "never into a space it may not change");
    CHECK(burrow_cross(&SB, 99, "", "x.md", &ticket) == ZXM_E_ARG, "bounds");
    CHECK(burrow_mkdir(&SB, "Dir") == ZXM_OK, "a folder");
    for (uint32_t k = 0; k < SB.n; k++)
        if (SB.e[k].dir) CHECK(burrow_cross(&SB, k, "", "Dir", &ticket) == ZXM_E_ARG, "files only");
    CHECK(burrow_set_space(&SB, "front") == ZXM_OK && SB.n == 1 && !strcmp(SB.e[0].name, "plan.md"),
          "it reached the cloud top");
    CHECK(burrow_set_space(&SB, "") == ZXM_OK, "home");
    bool found = false;
    for (uint32_t k = 0; k < SB.n; k++) found |= !strcmp(SB.e[k].name, "copied.md");
    CHECK(found, "and home");

    /* a made-up reply to files.cross */
    burrow_t *fb = &SB;
    uint32_t f = (uint32_t) find(fb, "copied.md");
    zxm_link_t *real = fb->link;
    fb->link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"held\":true}}";
    CHECK(burrow_cross(fb, f, "x", "y", &ticket) == ZXM_E_PROTOCOL, "held with no ticket");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"held\":true,\"ticket\":0}}";
    CHECK(burrow_cross(fb, f, "x", "y", &ticket) == ZXM_E_PROTOCOL, "ticket 0");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{}}";
    CHECK(burrow_cross(fb, f, "x", "y", &ticket) == ZXM_E_PROTOCOL, "no held");
    CHECK(strstr(last_req, "\"from_space\":\"home\"") && strstr(last_req, "\"to_space\":\"x\""),
          "home by name: %s", last_req);
    fb->link = real;
}

/* ---------------- Murmuration ---------------- */

#define E_NEUTRAL "\xf0\x9f\x98\x90" /* U+1F610 */
#define E_JOY     "\xf0\x9f\x98\x84" /* U+1F604 */
#define E_CURIOUS "\xf0\x9f\xa4\x94" /* U+1F914 */
#define E_SAD     "\xf0\x9f\x98\xa2" /* U+1F622 */

/* what zxv_host.c's state_json writes, for three agents */
static const char STATE_FULL[] =
    "{\"version\":\"1.0.0\",\"host\":\"hive\",\"remote\":false,\"cores\":8,\"cores_milli\":7000,"
    "\"mem_total_mb\":16384,\"mem_mb\":2048,\"levels\":3,\"tokens_per_cycle\":1000,\"tick\":4242,"
    "\"fundamental\":5,\"cycle\":17,\"due_mask\":0,\"settled\":16,\"held\":1,"
    "\"money_supply\":3000,\"pot\":0,\"imag\":125,\"saved\":0,\"mood\":\"" E_JOY " 3\","
    "\"ledger_postings\":96,\"ledger_synced\":17,\"ledger_halt\":0,\"levied\":0,\"fees\":0,"
    "\"bounties_returned\":0,\"audit\":\"active\",\"audit_records\":12,\"audit_events\":0,"
    "\"ledger_audited\":96,\"ledger_issues\":0,\"quest\":{\"contributed\":0,\"refused\":0,"
    "\"skills\":[{\"skill\":\"craft\",\"level\":1,\"points\":2}]},\"agents\":["
    "{\"id\":1,\"name\":\"Companion\",\"level\":0,\"emoji\":\"" E_NEUTRAL "\",\"intensity\":0,"
    "\"re\":400,\"mk\":50,\"im\":0,\"money\":1000,\"social\":3,\"gen\":0,\"witness\":1,"
    "\"witness_by\":\"Coder-2\"},"
    "{\"id\":2,\"name\":\"Coder-2\",\"level\":1,\"emoji\":\"" E_SAD "\",\"intensity\":4,"
    "\"re\":200,\"mk\":0,\"im\":100,\"money\":990,\"social\":2,\"gen\":1,\"witness\":2,"
    "\"witness_by\":\"Critic-3\"},"
    "{\"id\":3,\"name\":\"Critic-3\",\"level\":2,\"emoji\":\"" E_CURIOUS "\",\"intensity\":2,"
    "\"re\":150,\"mk\":75,\"im\":25,\"money\":7,\"social\":9,\"gen\":4,\"witness\":0,"
    "\"witness_by\":\"Companion\"}],"
    "\"model\":{\"loaded\":true,\"tokenizer_ok\":true,\"can_generate\":false,\"name\":\"tiny."
    "gguf\","
    "\"arch\":\"llama\",\"bytes\":5,\"layers\":2,\"context\":512,\"vocab\":32000,"
    "\"status\":\"loaded, no tokenizer\"},\"companion\":{\"cycle_open\":true,\"allotted\":450,"
    "\"used\":0,\"remaining\":450,\"exhausted\":false,\"max_per_answer\":256,\"asks\":0,"
    "\"exhausted_asks\":0,\"quarantined\":0,\"quarantine_ok\":true},\"net\":{\"mode\":\"off\"},"
    "\"update\":{\"checked\":false},\"notes\":{\"unread\":0,\"os\":\"none\"}}";

/* the smallest state zxvd could send: required fields only */
#define MIN_ST(top, agents)                                                                        \
    "{\"levels\":1,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":1" top ",\"agents\":[" agents "]}"
#define AGT(id, name, emoji, inten, re, wit, extra)                                                \
    "{\"id\":" id ",\"name\":" name ",\"level\":1,\"emoji\":" emoji ",\"intensity\":" inten        \
    ",\"re\":" re ",\"mk\":0,\"im\":0,\"witness\":" wit extra "}"
#define AOK AGT("5", "\"A\"", "\"x\"", "1", "10", "1", "")
#define ST_CYCLE(c)                                                                                \
    "{\"levels\":1,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":" c ",\"agents\":[" AOK "]}"

/* a third transport: each method answers from its own script, "\"result\":..."
 * or "\"error\":..."; NULL fails the send. sc_log lists the methods asked. */
static const char *SC_STATE, *SC_MODEL, *SC_EARN;
static char sc_log[512];

static int32_t scripted(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) len;
    const char *m = strstr(req, "\"method\":\"");
    const char *body = NULL;
    if (m) {
        m += 10;
        size_t k = strcspn(m, "\""), o = strlen(sc_log);
        if (o + k + 2 < sizeof sc_log) {
            memcpy(sc_log + o, m, k);
            sc_log[o + k] = ';';
            sc_log[o + k + 1] = 0;
        }
        body = !strncmp(m, "swarm.state\"", 12)      ? SC_STATE
               : !strncmp(m, "model.info\"", 11)     ? SC_MODEL
               : !strncmp(m, "swarm.earnings\"", 15) ? SC_EARN
                                                     : NULL;
    }
    if (!body) return -1;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,%s}", l->seq, body);
}

#define EARN_R(rate, bal, posts, cycles, extra)                                                    \
    "\"result\":{\"currency\":\"VFV\",\"rate_minor\":" rate                                        \
    ",\"source_account\":1,\"owner_account\":2,\"balance_minor\":" bal ",\"postings\":" posts      \
    ",\"cycles\":" cycles extra "}"
#define NOT_AVAILABLE "\"error\":{\"code\":-32601,\"message\":\"not available\"}"

static murmuration_t M, MB;
static zxm_link_t SCR;

/* a failed refresh leaves the model exactly as it was (R2) */
static int refresh_kept(murmuration_t *m, int want)
{
    memcpy(&MB, m, sizeof MB);
    int rc = murm_refresh(m);
    return rc == want && !memcmp(&MB, m, sizeof MB);
}

static void test_murmuration_parse(void)
{
    /* the state's required members, each left out, then given the wrong type */
    static const char *const TK[] = {"\"levels\":1", "\"tokens_per_cycle\":10", "\"tick\":1",
                                     "\"cycle\":1", "\"agents\":[]"};
    static const char *const TKBAD[] = {"\"levels\":\"1\"", "\"tokens_per_cycle\":\"10\"",
                                        "\"tick\":-1", "\"cycle\":1.5", "\"agents\":{}"};
    static const char *const AK[] = {"\"id\":5",        "\"name\":\"A\"",  "\"level\":1",
                                     "\"emoji\":\"x\"", "\"intensity\":1", "\"re\":10",
                                     "\"mk\":0",        "\"im\":0",        "\"witness\":1"};
    static const char *const AKBAD[] = {"\"id\":\"5\"", "\"name\":7",          "\"level\":\"1\"",
                                        "\"emoji\":7",  "\"intensity\":\"1\"", "\"re\":\"10\"",
                                        "\"mk\":1.5",   "\"im\":-1",           "\"witness\":true"};
    static char st[1024];
    for (uint32_t bad = 0; bad < 2; bad++)
        for (uint32_t k = 0; k < 5; k++) {
            uint32_t o = (uint32_t) snprintf(st, sizeof st, "{");
            for (uint32_t j = 0; j < 5; j++) {
                if (j == k && !bad) continue;
                o += (uint32_t) snprintf(st + o, sizeof st - o, "%s%s", o > 1 ? "," : "",
                                         j == k ? TKBAD[j] : TK[j]);
            }
            snprintf(st + o, sizeof st - o, "}");
            STATE = st;
            CHECK(refresh_kept(&M, ZXM_E_PROTOCOL), "state %s: %s", bad ? "bad" : "without", TK[k]);
        }
    for (uint32_t bad = 0; bad < 2; bad++)
        for (uint32_t k = 0; k < 9; k++) {
            uint32_t o =
                (uint32_t) snprintf(st, sizeof st,
                                    "{\"levels\":1,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":1,"
                                    "\"agents\":[{");
            bool first = true;
            for (uint32_t j = 0; j < 9; j++) {
                if (j == k && !bad) continue;
                o += (uint32_t) snprintf(st + o, sizeof st - o, "%s%s", first ? "" : ",",
                                         j == k ? AKBAD[j] : AK[j]);
                first = false;
            }
            snprintf(st + o, sizeof st - o, "}]}");
            STATE = st;
            CHECK(refresh_kept(&M, ZXM_E_PROTOCOL), "agent %s: %s", bad ? "bad" : "without", AK[k]);
        }

    /* ranges and optional members of the wrong type (R3) */
    static const char *const BAD[] = {
        "[1]",
        "\"state\"",
        MIN_ST(",\"levels\":4294967296", AOK), /* a duplicate key is no key */
        "{\"levels\":4294967296,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":1,\"agents\":[]}",
        "{\"levels\":1,\"tokens_per_cycle\":18446744073709551616,\"tick\":1,\"cycle\":1,"
        "\"agents\":[]}",
        MIN_ST(",\"cores\":1.5", AOK),
        MIN_ST(",\"cores\":4294967296", AOK),
        MIN_ST(",\"version\":2", AOK),
        MIN_ST(",\"host\":null", AOK),
        MIN_ST(",\"mood\":5", AOK),
        MIN_ST(",\"mood\":\"" E_NEUTRAL "\"", AOK),
        MIN_ST(",\"mood\":\"" E_NEUTRAL " 6\"", AOK),
        MIN_ST(",\"mood\":\"" E_NEUTRAL " 33\"", AOK),
        MIN_ST(",\"mood\":\"" E_NEUTRAL " \"", AOK),
        MIN_ST(",\"mood\":\"" E_NEUTRAL " /\"", AOK),
        MIN_ST(",\"mood\":\" 3\"", AOK),
        MIN_ST(",\"mood\":\"0123456789abcdef 1\"", AOK),
        MIN_ST(",\"audit\":1", AOK),
        MIN_ST(",\"audit_records\":-1", AOK),
        MIN_ST(",\"ledger_postings\":\"9\"", AOK),
        MIN_ST(",\"ledger_halt\":2147483648", AOK),
        MIN_ST(",\"ledger_halt\":-2147483649", AOK),
        MIN_ST(",\"ledger_halt\":\"-4\"", AOK),
        MIN_ST(",\"ledger_halt\":true", AOK),
        MIN_ST(",\"model\":\"x\"", AOK),
        MIN_ST(",\"model\":null", AOK),
        MIN_ST(",\"model\":{\"loaded\":1,\"can_generate\":false}", AOK),
        MIN_ST(",\"model\":{\"can_generate\":false}", AOK),
        MIN_ST(",\"model\":{\"loaded\":true}", AOK),
        MIN_ST(",\"model\":{\"loaded\":true,\"can_generate\":\"no\"}", AOK),
        MIN_ST(",\"model\":{\"loaded\":true,\"can_generate\":false,\"status\":3}", AOK),
        MIN_ST(",\"model\":{\"loaded\":true,\"can_generate\":false,\"name\":[]}", AOK),
        MIN_ST(",\"model\":{\"loaded\":true,\"can_generate\":false,\"arch\":{}}", AOK),
        MIN_ST("", "1"),
        MIN_ST("", AOK ",null"),
        MIN_ST("", AOK ",[]"),
        MIN_ST("", AGT("-1", "\"A\"", "\"x\"", "1", "10", "1", "")),
        MIN_ST("", AGT("4294967296", "\"A\"", "\"x\"", "1", "10", "1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "6", "10", "1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "-1", "10", "1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "18446744073709551616", "1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "1e3", "1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "3", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "-1", "")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1", ",\"level\":4294967296")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1", ",\"money\":\"5\"")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1", ",\"social\":-2")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1", ",\"gen\":4294967296")),
        MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1", ",\"witness_by\":7")),
        MIN_ST("", AOK "," AGT("6", "\"B\"", "\"x\"", "1", "10", "9", "")), /* the last agent */
    };
    for (uint32_t i = 0; i < sizeof BAD / sizeof *BAD; i++) {
        STATE = BAD[i];
        CHECK(refresh_kept(&M, ZXM_E_PROTOCOL), "bad state %u: %s", i, BAD[i]);
    }
    /* sanity: the same builders make a good state */
    STATE = MIN_ST(",\"model\":{\"loaded\":false,\"can_generate\":false}", AOK);
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.n == 1, "the good one: %s", LINK.err);
}

static void test_murmuration(void)
{
    MODEL = "{\"loaded\":false,\"can_generate\":false,\"name\":\"from info\",\"arch\":\"\","
            "\"n_layers\":0,\"n_vocab\":0,\"status\":\"no model\"}";
    murm_init(&M, &LINK);
    CHECK(M.link == &LINK && M.refreshes == 0 && M.sel == -1 && M.sw.n == 0 && !M.have_model &&
              !M.have_earnings && M.held.state == ZXM_HELD_NONE,
          "init");
    CHECK(!strcmp(MURM_AI[MURM_AI_EXPLAIN].name, "Explain the swarm") &&
              !strcmp(MURM_AI[MURM_AI_EXPLAIN].agent, "Companion") &&
              !strcmp(MURM_AI[MURM_AI_WHY_UNHAPPY].name, "Why is this agent unhappy") &&
              !strcmp(MURM_AI[MURM_AI_WHY_UNHAPPY].agent, "Companion") &&
              MURM_AI[0].est_tokens > 0 && MURM_AI[1].est_tokens > 0,
          "the palette");
    asked = 0;
    sent = 0;
    CHECK(murm_ai(&M, MURM_AI_EXPLAIN) == ZXM_E_STATE && !asked && !sent, "no snapshot to explain");
    CHECK(murm_select(&M, 0) == ZXM_E_ARG && murm_select(&M, -1) == ZXM_OK && M.sel == -1,
          "nothing to select");
    CHECK(murm_cycle_share(&M, 0) == 0 && murm_find(&M, "") == -1, "empty");

    /* the whole state; the books are not open, so no earnings */
    STATE = STATE_FULL;
    sent = 0;
    CHECK(murm_refresh(&M) == ZXM_OK, "refresh: %s", LINK.err);
    CHECK(sent == 2, "swarm.state and swarm.earnings, model from the state: %d", sent);
    const murm_swarm_t *s = &M.sw;
    CHECK(!strcmp(s->version, "1.0.0") && !strcmp(s->host, "hive") && s->cores == 8 &&
              s->levels == 3 && s->tokens_per_cycle == 1000 && s->tick == 4242 && s->cycle == 17,
          "totals");
    CHECK(!strcmp(s->mood, E_JOY) && s->mood_codepoint == 0x1F604u && s->mood_intensity == 3,
          "mood: %s %u", s->mood, s->mood_intensity);
    CHECK(!strcmp(s->audit, "active") && s->audit_records == 12 && s->ledger_postings == 96 &&
              s->ledger_halt == 0,
          "audit and ledger");
    CHECK(s->n == 3 && s->n_total == 3 && !s->truncated && s->allotted == 1000, "agents: %u %llu",
          s->n, (unsigned long long) s->allotted);
    const murm_agent_t *a = &s->agent[0];
    CHECK(a->id == 1 && !strcmp(a->name, "Companion") && a->level == 0 &&
              !strcmp(a->emoji, E_NEUTRAL) && a->codepoint == 0x1F610u && a->intensity == 0 &&
              a->re == 400 && a->mk == 50 && a->im == 0 && a->money == 1000 && a->social == 3 &&
              a->gen == 0 && a->witness == MURM_WIT_TRUE && !strcmp(a->witness_by, "Coder-2"),
          "the companion");
    a = &s->agent[1];
    CHECK(a->id == 2 && !strcmp(a->name, "Coder-2") && a->level == 1 && !strcmp(a->emoji, E_SAD) &&
              a->codepoint == 0x1F622u && a->intensity == 4 && a->re == 200 && a->mk == 0 &&
              a->im == 100 && a->money == 990 && a->social == 2 && a->gen == 1 &&
              a->witness == MURM_WIT_GLUT && !strcmp(a->witness_by, "Critic-3"),
          "a contested agent");
    a = &s->agent[2];
    CHECK(a->id == 3 && a->level == 2 && a->codepoint == 0x1F914u && a->intensity == 2 &&
              a->re == 150 && a->mk == 75 && a->im == 25 && a->money == 7 && a->social == 9 &&
              a->gen == 4 && a->witness == MURM_WIT_FALSE && !strcmp(a->witness_by, "Companion"),
          "a refused allotment");
    CHECK(M.have_model && M.model.loaded && !M.model.can_generate &&
              !strcmp(M.model.name, "tiny.gguf") && !strcmp(M.model.arch, "llama") &&
              !strcmp(M.model.status, "loaded, no tokenizer") && M.model_code == 0,
          "model");
    CHECK(!M.have_earnings && M.earn_code == -32603 && !M.earn.currency[0] && M.refreshes == 1,
          "books not open: earnings absent (%d)", M.earn_code);
    CHECK(murm_cycle_share(&M, 0) == 450 && murm_cycle_share(&M, 1) == 300 &&
              murm_cycle_share(&M, 2) == 250 && murm_cycle_share(&M, 3) == 0,
          "shares %u %u %u", murm_cycle_share(&M, 0), murm_cycle_share(&M, 1),
          murm_cycle_share(&M, 2));
    CHECK(murm_emotion(s->agent[0].codepoint) == SWARM_EMO_NEUTRAL &&
              murm_emotion(s->agent[1].codepoint) == SWARM_EMO_SADNESS &&
              murm_emotion(s->agent[2].codepoint) == SWARM_EMO_CURIOUS &&
              murm_emotion(s->mood_codepoint) == SWARM_EMO_JOY &&
              murm_emotion(0x1F620u) == SWARM_EMO_FRUSTRATION && murm_emotion(0x1F600u) == -1 &&
              murm_emotion(0) == -1,
          "emotions");
    CHECK(murm_find(&M, "Companion") == 0 && murm_find(&M, "Coder-2") == 1 &&
              murm_find(&M, "Critic-3") == 2 && murm_find(&M, "coder-2") == -1 &&
              murm_find(&M, "Coder") == -1 && murm_find(&M, "") == -1,
          "find");
    M.sw.n = 2; /* past the end, though the entry is there */
    CHECK(murm_cycle_share(&M, 2) == 0 && murm_cycle_share(&M, 1) == 300, "only what is kept");
    M.sw.n = 3;
    CHECK(murm_select(&M, 3) == ZXM_E_ARG && murm_select(&M, -2) == ZXM_E_ARG && M.sel == -1,
          "select bounds");
    CHECK(murm_select(&M, 2) == ZXM_OK && M.sel == 2, "select");

    /* the books open: earnings arrive (zxvd_rpc.h E1-E4) */
    CHECK(zxvd_rpc_books_open() == 0 && zxvd_rpc_earnings_sync(1000) == 0, "books");
    CHECK(murm_refresh(&M) == ZXM_OK && M.have_earnings && M.earn_code == 0 &&
              !strcmp(M.earn.currency, "VFV") && M.earn.rate_minor == 1 &&
              M.earn.balance_minor == 1000 && M.earn.postings == 1 && M.earn.cycles == 1 &&
              !M.earn.resumed && M.refreshes == 2,
          "earnings: %s", LINK.err);
    CHECK(M.sel == 2, "the selection stays");

    /* AI: held, never applied (M3) */
    ANSWER = "The swarm is busy.";
    asked = 0;
    CHECK(murm_ai(&M, MURM_AI_COUNT) == ZXM_E_ARG && murm_ai(&M, -1) == ZXM_E_ARG && !asked,
          "bad action");
    CHECK(murm_select(&M, -1) == ZXM_OK, "clear the selection");
    sent = 0;
    CHECK(murm_ai(&M, MURM_AI_WHY_UNHAPPY) == ZXM_E_STATE && !asked && !sent &&
              M.held.state == ZXM_HELD_NONE,
          "why unhappy needs an agent, and asks nothing");
    CHECK(murm_ai(&M, MURM_AI_EXPLAIN) == ZXM_OK && asked == 1 &&
              M.held.state == ZXM_HELD_PENDING && M.held.action == MURM_AI_EXPLAIN &&
              !strcmp(M.held.text, "The swarm is busy.") && M.held.len == 18 &&
              !strcmp(M.held.cost.agent, "Companion") && !M.held.cost.exact &&
              M.held.cost.tokens > 0,
          "explain held");
    const char *in = strstr(last_prompt, "\n\n");
    CHECK(in && !strcmp(in + 2,
                        "Cycle 17, 1000 tokens per cycle, 3 levels, mood " E_JOY
                        " 3, audit active, ledger running, 3 agents\n"
                        "Companion (level 0): feeling " E_NEUTRAL
                        " 0, tokens re 400 mk 50 im 0 (450 per mille of the cycle), witness true "
                        "by Coder-2\n"
                        "Coder-2 (level 1): feeling " E_SAD
                        " 4, tokens re 200 mk 0 im 100 (300 per mille of the cycle), witness "
                        "contested by Critic-3\n"
                        "Critic-3 (level 2): feeling " E_CURIOUS
                        " 2, tokens re 150 mk 75 im 25 (250 per mille of the cycle), witness "
                        "false by Companion\n"),
          "explain prompt: %s", last_prompt);
    CHECK(!strncmp(last_prompt, "Explain in plain words what this ZXV swarm", 42), "instruction");
    sent = 0;
    memcpy(&MB, &M, sizeof MB);
    CHECK(murm_ai_accept(&M) == ZXM_OK && M.held.state == ZXM_HELD_ACCEPTED && !sent &&
              !strcmp(M.held.text, "The swarm is busy.") && M.refreshes == MB.refreshes &&
              !memcmp(&M.sw, &MB.sw, sizeof M.sw),
          "accept only marks it read");
    CHECK(murm_ai_accept(&M) == ZXM_E_STATE && murm_ai_reject(&M) == ZXM_E_STATE, "once");
    CHECK(murm_select(&M, 1) == ZXM_OK && murm_ai(&M, MURM_AI_WHY_UNHAPPY) == ZXM_OK &&
              M.held.state == ZXM_HELD_PENDING && M.held.action == MURM_AI_WHY_UNHAPPY,
          "why unhappy");
    in = strstr(last_prompt, "\n\n");
    static const char WHY[] = "Agent: Coder-2 (level 1): feeling " E_SAD
                              " 4, tokens re 200 mk 0 im 100 (300 per mille of the cycle), "
                              "witness contested by Critic-3\n\nThe swarm: Cycle 17, 1000 tokens";
    CHECK(in && !strncmp(in + 2, WHY, sizeof WHY - 1) &&
              strstr(last_prompt, "Critic-3 (level 2)") &&
              !strncmp(last_prompt, "Explain in plain words why this agent", 37),
          "why prompt: %s", last_prompt);
    CHECK(murm_refresh(&M) == ZXM_OK && M.held.state == ZXM_HELD_PENDING && M.sel == 1,
          "a refresh keeps the held answer");
    CHECK(murm_ai_reject(&M) == ZXM_OK && M.held.state == ZXM_HELD_REJECTED && !M.held.len &&
              !M.held.text[0] && murm_ai_reject(&M) == ZXM_E_STATE &&
              murm_ai_accept(&M) == ZXM_E_STATE,
          "reject drops it");

    /* the selection follows the agent's id (R4) */
    STATE = MIN_ST("", AGT("5", "\"A\"", "\"x\"", "1", "10", "1",
                           "") "," AGT("6", "\"B\"", "\"x\"", "1", "10", "1",
                                       "") "," AGT("7", "\"C\"", "\"x\"", "1", "10", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && M.sel == -1 && murm_select(&M, 2) == ZXM_OK, "A B C");
    STATE = MIN_ST("", AGT("7", "\"C\"", "\"x\"", "1", "10", "1", "") "," AOK);
    CHECK(murm_refresh(&M) == ZXM_OK && M.sel == 0, "C moved to the front: %d", M.sel);
    STATE = MIN_ST("", AOK);
    CHECK(murm_refresh(&M) == ZXM_OK && M.sel == -1, "C gone");
    STATE = MIN_ST("", AGT("0", "\"Z\"", "\"x\"", "1", "10", "1", "") "," AOK);
    CHECK(murm_refresh(&M) == ZXM_OK && M.sel == -1, "no selection stays none");
    CHECK(murm_select(&M, 0) == ZXM_OK, "select id 0");
    STATE = MIN_ST("", AOK "," AGT("0", "\"Z\"", "\"x\"", "1", "10", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && M.sel == 1, "id 0 followed: %d", M.sel);

    /* the smallest state: optional members absent, the model from model.info */
    STATE = MIN_ST(",\"ledger_halt\":-4", AOK);
    sent = 0;
    CHECK(murm_refresh(&M) == ZXM_OK && sent == 3, "three calls: %d", sent);
    CHECK(M.have_model && !M.model.loaded && !strcmp(M.model.name, "from info") &&
              !strcmp(M.model.status, "no model") && !M.model.arch[0],
          "model.info");
    s = &M.sw;
    CHECK(!s->version[0] && !s->host[0] && s->cores == 0 && !s->mood[0] && s->mood_codepoint == 0 &&
              s->mood_intensity == 0 && !s->audit[0] && s->audit_records == 0 &&
              s->ledger_postings == 0 && s->ledger_halt == -4 && s->levels == 1 &&
              s->tokens_per_cycle == 10 && s->tick == 1 && s->cycle == 1,
          "optional totals absent, the ledger halted");
    a = &s->agent[0];
    CHECK(s->n == 1 && a->id == 5 && !strcmp(a->name, "A") && !strcmp(a->emoji, "x") &&
              a->codepoint == 'x' && a->money == 0 && a->social == 0 && a->gen == 0 &&
              !a->witness_by[0] && murm_emotion(a->codepoint) == -1,
          "optional agent members absent");
    CHECK(murm_cycle_share(&M, 0) == 1000, "one agent has it all");
    ANSWER = "Halted.";
    CHECK(murm_ai(&M, MURM_AI_EXPLAIN) == ZXM_OK, "explain a halt");
    in = strstr(last_prompt, "\n\n");
    CHECK(in && !strcmp(in + 2, "Cycle 1, 10 tokens per cycle, 1 levels, mood unknown, audit "
                                "unknown, ledger halted (-4), 1 agents\n"
                                "A (level 1): feeling x 1, tokens re 10 mk 0 im 0 (1000 per mille "
                                "of the cycle), witness true by kernel\n"),
          "halt prompt: %s", last_prompt);

    /* the edges of what is accepted */
    STATE = MIN_ST(",\"mood\":\"" E_SAD " 5\",\"ledger_halt\":-2147483648,\"cores\":4294967295",
                   AGT("4294967295", "\"\\u00e9\"", "\"\\ud83d\\ude20\"", "5",
                       "18446744073709551615", "2", ",\"gen\":4294967295,\"witness_by\":\"\""));
    CHECK(murm_refresh(&M) == ZXM_OK, "the edges: %s", LINK.err);
    a = &M.sw.agent[0];
    CHECK(a->id == 4294967295u && !strcmp(a->name, "\xc3\xa9") &&
              !strcmp(a->emoji, "\xf0\x9f\x98\xa0") && a->codepoint == 0x1F620u &&
              a->intensity == 5 && a->re == UINT64_MAX && a->witness == MURM_WIT_GLUT &&
              a->gen == 4294967295u && M.sw.ledger_halt == INT32_MIN && M.sw.cores == 4294967295u &&
              M.sw.mood_intensity == 5 && M.sw.mood_codepoint == 0x1F622u,
          "escaped emoji and the largest values");
    CHECK(murm_emotion(a->codepoint) == SWARM_EMO_FRUSTRATION, "frustration");
    STATE = MIN_ST(",\"mood\":\"\xe2\x82\xac 0\"",
                   AGT("5", "\"A\"", "\"\xc3\xa9!\"", "1", "10", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.agent[0].codepoint == 0xE9u &&
              M.sw.mood_codepoint == 0x20ACu && M.sw.mood_intensity == 0 &&
              !strcmp(M.sw.mood, "\xe2\x82\xac"),
          "two- and three-byte code points: %x %x", M.sw.agent[0].codepoint, M.sw.mood_codepoint);
    STATE = MIN_ST("", AGT("5", "\"A\"", "\"\\u0000\"", "1", "10", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.agent[0].codepoint == 0 && !M.sw.agent[0].emoji[0],
          "an empty emoji");
    STATE = MIN_ST(",\"mood\":\"\\u007f 1\"",
                   AGT("5", "\"A\"", "\"\xe0\xa0\x80\"", "1", "10", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.agent[0].codepoint == 0x800u &&
              M.sw.mood_codepoint == 0x7Fu,
          "U+0800 and U+007F: %x %x", M.sw.agent[0].codepoint, M.sw.mood_codepoint);

    /* text longer than its field is cut on a character boundary */
    STATE = MIN_ST(
        ",\"version\":\"0123456789012345678901234567890123456789012345678901234567890123456789\","
        "\"model\":{\"loaded\":true,\"can_generate\":true,\"status\":"
        "\""
        "0123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789\"}",
        AGT("5",
            "\"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3"
            "\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\"",
            "\"" E_SAD E_SAD E_SAD E_SAD "\"", "1", "10", "1",
            ",\"witness_by\":\"abcdefghijabcdefghijabcdefghijabcdefghij\""));
    CHECK(murm_refresh(&M) == ZXM_OK, "long text: %s", LINK.err);
    a = &M.sw.agent[0];
    CHECK(strlen(a->name) == 30 && !memcmp(a->name, "\xc3\xa9\xc3\xa9", 4), "name cut: %zu",
          strlen(a->name));
    CHECK(strlen(a->emoji) == 12 && a->codepoint == 0x1F622u, "emoji cut: %zu", strlen(a->emoji));
    CHECK(strlen(a->witness_by) == MURM_NAME_MAX && strlen(M.sw.version) == MURM_TEXT_MAX &&
              strlen(M.model.status) == MURM_STATUS_MAX && M.model.can_generate,
          "ascii cut to the field");

    /* more agents than MURM_AGENTS: the first are kept, every one counts */
    static char big[32768];
    uint32_t o = (uint32_t) snprintf(big, sizeof big,
                                     "{\"levels\":2,\"tokens_per_cycle\":700,\"tick\":9,\"cycle\":"
                                     "8,\"agents\":[");
    for (uint32_t i = 0; i < MURM_AGENTS + 6u; i++)
        o += (uint32_t) snprintf(big + o, sizeof big - o,
                                 "%s{\"id\":%u,\"name\":\"A%u\",\"level\":1,\"emoji\":\"" E_NEUTRAL
                                 "\",\"intensity\":0,\"re\":%u,\"mk\":0,\"im\":0,\"witness\":1}",
                                 i ? "," : "", i + 100u, i, i < MURM_AGENTS ? 10u : 30u);
    snprintf(big + o, sizeof big - o, "]}");
    STATE = big;
    CHECK(murm_select(&M, 0) == ZXM_OK, "select before");
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.n == MURM_AGENTS && M.sw.n_total == MURM_AGENTS + 6u &&
              M.sw.truncated && M.sw.allotted == MURM_AGENTS * 10u + 6u * 30u && M.sel == -1,
          "truncated: %u of %u, %llu", M.sw.n, M.sw.n_total, (unsigned long long) M.sw.allotted);
    CHECK(murm_find(&M, "A63") == 63 && murm_find(&M, "A64") == -1 &&
              M.sw.agent[MURM_AGENTS - 1].id == 163,
          "the first kept");
    CHECK(murm_cycle_share(&M, 0) == 10u * 1000u / (MURM_AGENTS * 10u + 180u) &&
              murm_cycle_share(&M, MURM_AGENTS) == 0,
          "share of the whole: %u", murm_cycle_share(&M, 0));
    CHECK(murm_select(&M, MURM_AGENTS - 1) == ZXM_OK && murm_select(&M, MURM_AGENTS) == ZXM_E_ARG,
          "select within what is kept");
    ANSWER = "Many.";
    CHECK(murm_ai(&M, MURM_AI_EXPLAIN) == ZXM_OK &&
              strstr(last_prompt, "Cycle 8, 700 tokens per cycle, 2 levels, mood unknown") &&
              strstr(last_prompt, "ledger running, 70 agents\nA0 (level 1)") &&
              strstr(last_prompt, "\nA30 (level 1)") && M.held.cut &&
              strlen(last_prompt) <= ZXM_ASK_MAX,
          "every agent counted, as many listed as fit");
    o = (uint32_t) snprintf(big, sizeof big,
                            "{\"levels\":2,\"tokens_per_cycle\":640,\"tick\":9,\"cycle\":8,"
                            "\"agents\":[");
    for (uint32_t i = 0; i < MURM_AGENTS; i++)
        o += (uint32_t) snprintf(big + o, sizeof big - o,
                                 "%s{\"id\":%u,\"name\":\"A%u\",\"level\":1,\"emoji\":\"x\","
                                 "\"intensity\":0,\"re\":10,\"mk\":0,\"im\":0,\"witness\":1}",
                                 i ? "," : "", i + 100u, i);
    snprintf(big + o, sizeof big - o, "]}");
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.n == MURM_AGENTS && M.sw.n_total == MURM_AGENTS &&
              !M.sw.truncated && M.sel == (int32_t) (MURM_AGENTS - 1),
          "exactly MURM_AGENTS");
    STATE = MIN_ST("", "");
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.n == 0 && M.sw.n_total == 0 && M.sw.allotted == 0 &&
              murm_cycle_share(&M, 0) == 0 && M.sel == -1,
          "no agents");
    ANSWER = "Empty.";
    CHECK(murm_ai(&M, MURM_AI_EXPLAIN) == ZXM_OK && strstr(last_prompt, ", 0 agents\n"),
          "explain an empty swarm");

    /* allotments near 2^64 saturate; shares stay exact enough */
    STATE = MIN_ST("", AGT("1", "\"Big\"", "\"x\"", "1", "18446744073709551615", "1",
                           ",\"mk\":1") "," AGT("2", "\"Half\"", "\"x\"", "1",
                                                "9223372036854775808", "1", ""));
    CHECK(refresh_kept(&M, ZXM_E_PROTOCOL), "a duplicated mk is refused");
    STATE = "{\"levels\":1,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":1,\"agents\":["
            "{\"id\":1,\"name\":\"Big\",\"level\":1,\"emoji\":\"x\",\"intensity\":1,"
            "\"re\":18446744073709551615,\"mk\":1,\"im\":1,\"witness\":1},"
            "{\"id\":2,\"name\":\"Half\",\"level\":1,\"emoji\":\"x\",\"intensity\":1,"
            "\"re\":9223372036854775808,\"mk\":0,\"im\":0,\"witness\":1}]}";
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.allotted == UINT64_MAX &&
              murm_cycle_share(&M, 0) == 1000 && murm_cycle_share(&M, 1) == 500,
          "saturated: %u %u", murm_cycle_share(&M, 0), murm_cycle_share(&M, 1));
    STATE = "{\"levels\":1,\"tokens_per_cycle\":10,\"tick\":1,\"cycle\":1,\"agents\":["
            "{\"id\":1,\"name\":\"P\",\"level\":1,\"emoji\":\"x\",\"intensity\":1,"
            "\"re\":6148914691236517205,\"mk\":0,\"im\":0,\"witness\":1},"
            "{\"id\":2,\"name\":\"Q\",\"level\":1,\"emoji\":\"x\",\"intensity\":1,"
            "\"re\":6148914691236517205,\"mk\":6148914691236517205,\"im\":0,\"witness\":1}]}";
    CHECK(murm_refresh(&M) == ZXM_OK && M.sw.allotted == 18446744073709551615u &&
              murm_cycle_share(&M, 0) == 333 && murm_cycle_share(&M, 1) == 666,
          "thirds: %u %u", murm_cycle_share(&M, 0), murm_cycle_share(&M, 1));
    STATE = MIN_ST("", AGT("1", "\"P\"", "\"x\"", "1", "1", "1", "") "," AGT("2", "\"Q\"", "\"x\"",
                                                                             "1", "2", "1", ""));
    CHECK(murm_refresh(&M) == ZXM_OK && murm_cycle_share(&M, 0) == 333 &&
              murm_cycle_share(&M, 1) == 666,
          "rounded down: %u %u", murm_cycle_share(&M, 0), murm_cycle_share(&M, 1));

    test_murmuration_parse();

    /* no swarm.state, no model.info: zxvd's refusal is the link's (M2) */
    static const zxvd_host_ops_t BARE = {.ask = h_ask, .app_version = "test"};
    zxvd_rpc_set_ops(&BARE);
    CHECK(refresh_kept(&M, ZXM_E_REMOTE) && LINK.err_code == -32601 &&
              !strcmp(LINK.err, "not available"),
          "no state: %d %s", LINK.err_code, LINK.err);
    static const zxvd_host_ops_t NO_MODEL = {.state = h_state, .ask = h_ask, .app_version = "t"};
    zxvd_rpc_set_ops(&NO_MODEL);
    STATE = MIN_ST("", AOK);
    CHECK(murm_refresh(&M) == ZXM_OK && !M.have_model && M.model_code == -32601 &&
              !M.model.name[0] && !M.model.status[0] && M.have_earnings,
          "no model.info: absent, not a failure");
    zxvd_rpc_set_ops(&OPS);
    CHECK(murm_refresh(&M) == ZXM_OK && M.have_model && M.model_code == 0, "model back");

    /* a scripted zxvd: earnings as zxvd_rpc.h M3 sends them */
    zxm_link_init(&SCR, scripted, &SCR);
    murm_init(&M, &SCR);
    SC_STATE = "\"result\":" MIN_ST("", AOK);
    SC_MODEL = "\"result\":{\"loaded\":true,\"can_generate\":true,\"name\":\"m\"}";
    SC_EARN = EARN_R("\"250\"", "\"-9223372036854775808\"", "\"18446744073709551615\"", "\"0\"",
                     ",\"resumed\":true");
    sc_log[0] = 0;
    CHECK(murm_refresh(&M) == ZXM_OK && M.have_earnings && M.earn.balance_minor == INT64_MIN &&
              M.earn.postings == UINT64_MAX && M.earn.cycles == 0 && M.earn.rate_minor == 250 &&
              M.earn.resumed && !strcmp(M.earn.currency, "VFV"),
          "the extremes: %s", SCR.err);
    CHECK(!strcmp(sc_log, "swarm.state;model.info;swarm.earnings;"), "order: %s", sc_log);
    SC_EARN = EARN_R("7", "\"9223372036854775807\"", "\"0012\"", "3", "");
    CHECK(murm_refresh(&M) == ZXM_OK && M.earn.balance_minor == INT64_MAX &&
              M.earn.postings == 12 && M.earn.cycles == 3 && M.earn.rate_minor == 7 &&
              !M.earn.resumed,
          "JSON integers too");
    SC_EARN = EARN_R("\"-0\"", "\"-1\"", "\"-0\"", "\"1\"", "");
    CHECK(murm_refresh(&M) == ZXM_OK && M.earn.balance_minor == -1 && M.earn.rate_minor == 0 &&
              M.earn.postings == 0,
          "signs");
    static const char *const EBAD[] = {
        EARN_R("\"1\"", "\"9223372036854775808\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"-9223372036854775809\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"18446744073709551616\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"0\"", "\"99999999999999999999\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"-1\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"0\"", "\"-5\"", ""),
        EARN_R("\"1.5\"", "\"0\"", "\"0\"", "\"0\"", ""),
        EARN_R("1.5", "\"0\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"1e3\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"-\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\" 1\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"1 \"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"+1\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"9:\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"/\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"--1\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"\\u0031\"", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "true", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "null", "\"0\"", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "[]", "\"0\"", ""),
        EARN_R("\"1\"", "\"0\"", "\"0\"", "\"0\"", ",\"resumed\":\"yes\""),
        "\"result\":{\"rate_minor\":\"1\",\"balance_minor\":\"0\",\"postings\":\"0\","
        "\"cycles\":\"0\"}",
        "\"result\":{\"currency\":5,\"rate_minor\":\"1\",\"balance_minor\":\"0\",\"postings\":"
        "\"0\",\"cycles\":\"0\"}",
        "\"result\":{\"currency\":\"VFV\",\"balance_minor\":\"0\",\"postings\":\"0\","
        "\"cycles\":\"0\"}",
        "\"result\":{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"postings\":\"0\","
        "\"cycles\":\"0\"}",
        "\"result\":{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"balance_minor\":\"0\","
        "\"cycles\":\"0\"}",
        "\"result\":{\"currency\":\"VFV\",\"rate_minor\":\"1\",\"balance_minor\":\"0\","
        "\"postings\":\"0\"}",
        "\"result\":[]",
        "\"result\":\"VFV\"",
    };
    SC_STATE = "\"result\":" ST_CYCLE("99"); /* would be new: never taken */
    for (uint32_t i = 0; i < sizeof EBAD / sizeof *EBAD; i++) {
        SC_EARN = EBAD[i];
        CHECK(refresh_kept(&M, ZXM_E_PROTOCOL) && M.sw.cycle == 1 && M.earn.balance_minor == -1,
              "bad earnings %u: %s", i, EBAD[i]);
    }
    SC_EARN = NULL;
    CHECK(refresh_kept(&M, ZXM_E_TRANSPORT), "earnings: no connection");
    SC_EARN = EARN_R("\"1\"", "\"5\"", "\"1\"", "\"1\"", "");
    SC_MODEL = NULL;
    CHECK(refresh_kept(&M, ZXM_E_TRANSPORT), "model.info: no connection");
    SC_MODEL = "\"result\":{\"loaded\":true}";
    CHECK(refresh_kept(&M, ZXM_E_PROTOCOL), "model.info: malformed");
    SC_MODEL = "\"error\":{\"code\":-32000,\"message\":\"model busy\"}";
    SC_EARN = NOT_AVAILABLE;
    sc_log[0] = 0;
    CHECK(murm_refresh(&M) == ZXM_OK && !M.have_model && M.model_code == -32000 &&
              !M.have_earnings && M.earn_code == -32601 && !M.earn.currency[0] &&
              M.earn.balance_minor == 0 && M.sw.cycle == 99 && !M.model.name[0],
          "both absent, the swarm still shown");
    CHECK(!strcmp(sc_log, "swarm.state;model.info;swarm.earnings;"), "asked all: %s", sc_log);
    SC_STATE = "\"error\":{\"code\":-32000,\"message\":\"busy\"}";
    CHECK(refresh_kept(&M, ZXM_E_REMOTE) && SCR.err_code == -32000 && !strcmp(SCR.err, "busy"),
          "a refused state");
    SC_STATE = NULL;
    sc_log[0] = 0;
    CHECK(refresh_kept(&M, ZXM_E_TRANSPORT) && !strcmp(sc_log, "swarm.state;"),
          "no connection: nothing more asked (%s)", sc_log);
    SC_STATE = "\"result\":" ST_CYCLE("100");
    SC_EARN = "\"error\":{\"code\":-32603,\"message\":\"the books are not open\"}";
    SC_MODEL = "\"result\":{\"loaded\":false,\"can_generate\":false}";
    CHECK(murm_refresh(&M) == ZXM_OK && M.have_model && !M.have_earnings && M.earn_code == -32603 &&
              M.sw.cycle == 100,
          "books closed");
    zxvd_rpc_books_close();
}

int main(void)
{
    char root[256];
    snprintf(root, sizeof root, "%s/zxv-apps-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(root) || zxf_set_root(root) != 0) {
        printf("[FAIL] test_apps: no scratch folder\n");
        return 1;
    }
    zxvd_rpc_set_ops(&OPS);
    zxm_link_init(&LINK, inproc, NULL);
    test_zxm();
    test_zxm_ai();
    test_burrow(root);
    test_quillon();
    test_spindle();
    test_spaces();
    test_murmuration();
    free(OUT.p);
    if (rm_tree(root) != 0) printf("  (could not remove %s)\n", root);
    if (fails) {
        printf("[FAIL] test_apps: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_apps: %d checks\n", checks);
    return 0;
}
