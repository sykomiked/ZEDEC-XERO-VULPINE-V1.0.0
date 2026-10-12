/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_apps.c — the app models (zxm.h, Burrow, Quillon, Glint, Spindle)
 * against zxvd's real dispatcher in this process: files.* runs on a scratch
 * ZXV folder and swarm.ask is a stub that returns a set answer. A second
 * transport hands zxm made-up replies to cover its error paths. */
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
#include "quillon_model.h"
#include "spindle_model.h"

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
static const zxvd_host_ops_t OPS = {.ask = h_ask, .app_version = "test"};

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
    CHECK(burrow_import(&B, "one.png", png, sizeof png) == ZXM_OK && B.n == 1 &&
              B.e[0].size == sizeof png && B.e[0].kind == BURROW_K_IMAGE,
          "import in three parts: %s", LINK.err);
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
    CHECK(glint_load(&G, buf, sizeof buf, &got) == ZXM_OK && got == sizeof png &&
              !memcmp(buf, png, sizeof png),
          "load: %s", LINK.err);
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
    free(OUT.p);
    if (rm_tree(root) != 0) printf("  (could not remove %s)\n", root);
    if (fails) {
        printf("[FAIL] test_apps: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_apps: %d checks\n", checks);
    return 0;
}
