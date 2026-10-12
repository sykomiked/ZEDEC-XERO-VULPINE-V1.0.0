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
#include "almanac_model.h"
#include "sounding_model.h"
#include "tidewatch_model.h"
#include "wake_model.h"
#include "scent_model.h"
#include "platen_model.h"
#include "keepsake_model.h"
#include "zxv_print_host.h"
#include "test_fake_device.h"
#include "zxvd_log.h"
#include "sha256.h"

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

/* ---------------- the system utilities ---------------- */

static almanac_t AL;
static wake_t WK;
static tidewatch_t TW;
static sounding_t SD;

#define SYS_INFO(t, cpu)                                                                           \
    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":" t ",\"os\":\"linux\","               \
    "\"os_version\":\"6.1\",\"arch\":\"arm64\",\"host\":\"h\",\"cpus\":4,\"mem_total\":"           \
    "8000000000,\"mem_avail\":2000000000,\"uptime_s\":90061,\"load\":[125,50,5],\"zxvd\":"         \
    "{\"pid\":7,\"cpu_user_us\":" cpu ",\"cpu_sys_us\":0,\"rss_bytes\":1000,\"max_rss_bytes\":"    \
    "2000}}}"

static void test_utilities(void)
{
    char t[64];

    /* Almanac over the real dispatcher */
    almanac_init(&AL, &LINK);
    CHECK(almanac_mem_used_x100(&AL) == -1 && almanac_disk_used_x100(&AL) == -1, "nothing yet");
    CHECK(almanac_ai(&AL, ALMANAC_AI_EXPLAIN) == ZXM_E_STATE, "nothing to explain yet");
    CHECK(almanac_refresh(&AL) == ZXM_OK && AL.have && AL.cpus > 0 && AL.os[0] &&
              AL.pid == (uint64_t) getpid() && AL.disk_known && AL.disk_total > 0,
          "almanac: %s", LINK.err);
    CHECK(almanac_disk_used_x100(&AL) >= 0 && almanac_disk_used_x100(&AL) <= 10000, "disk share");
    almanac_uptime_text(90061, t, sizeof t);
    CHECK(!strcmp(t, "1 day, 1 hour"), "%s", t);
    almanac_uptime_text(7260, t, sizeof t);
    CHECK(!strcmp(t, "2 hours, 1 minute"), "%s", t);
    almanac_uptime_text(59, t, sizeof t);
    CHECK(!strcmp(t, "0 minutes"), "%s", t);
    almanac_bytes_text(1, t, sizeof t);
    CHECK(!strcmp(t, "1 byte"), "%s", t);
    almanac_bytes_text(999, t, sizeof t);
    CHECK(!strcmp(t, "999 bytes"), "%s", t);
    almanac_bytes_text(1500000000, t, sizeof t);
    CHECK(!strcmp(t, "1.5 GB"), "%s", t);
    almanac_bytes_text(2000000000000, t, sizeof t);
    CHECK(!strcmp(t, "2 TB"), "%s", t);
    almanac_bytes_text(64000000000, t, sizeof t);
    CHECK(!strcmp(t, "64 GB"), "%s", t);
    almanac_bytes_text(UINT64_MAX, t, sizeof t);
    CHECK(!strcmp(t, "18 EB"), "%s", t);
    almanac_bytes_text(5, t, 0); /* no room: nothing written */
    asked = 0;
    CHECK(almanac_ai(&AL, ALMANAC_AI_EXPLAIN) == ZXM_OK && asked == 1 &&
              AL.held.state == ZXM_HELD_PENDING && strstr(last_prompt, "Memory: ") &&
              strstr(last_prompt, "free for ZXV"),
          "explain: %s", last_prompt);
    CHECK(almanac_ai_accept(&AL) == ZXM_OK && almanac_ai_reject(&AL) == ZXM_E_STATE, "accept");
    CHECK(almanac_ai(&AL, 1) == ZXM_E_ARG && almanac_ai(&AL, -1) == ZXM_E_ARG, "actions");
    /* canned: load known, a disk zxvd cannot read, a damaged reply keeps the old facts */
    almanac_init(&AL, &FAKE);
    CANNED = SYS_INFO("1", "0");
    CHECK(almanac_refresh(&AL) == ZXM_E_PROTOCOL && !AL.have, "sys.disk reply is not a disk");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1,\"os\":\"linux\"}}";
    CHECK(almanac_refresh(&AL) == ZXM_E_PROTOCOL && !AL.have, "short reply");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32012,\"message\":\"no\"}}";
    CHECK(almanac_refresh(&AL) == ZXM_E_REMOTE && !AL.have, "refused");
    CANNED = NULL;
    almanac_init(&AL, &LINK);
    CHECK(almanac_refresh(&AL) == ZXM_OK, "again");
    almanac_t keep = AL;
    AL.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"load\":7}}";
    CHECK(almanac_refresh(&AL) == ZXM_E_PROTOCOL && AL.pid == keep.pid && AL.have, "kept");
    CANNED = NULL;

    /* Tidewatch: canned samples give exact shares */
    tidewatch_init(&TW, &FAKE);
    CHECK(!tidewatch_latest(&TW) && !tidewatch_at(&TW, 0) && tidewatch_peak_cpu_x100(&TW) == 0,
          "empty");
    CANNED = SYS_INFO("1000", "0");
    CHECK(tidewatch_sample(&TW) == ZXM_OK && tidewatch_latest(&TW)->zxvd_cpu_x100 == -1 &&
              tidewatch_latest(&TW)->mem_used_x100 == 7500 &&
              tidewatch_latest(&TW)->load1_x100 == 125 && TW.cpus == 4,
          "first sample: cpu %d mem %d", tidewatch_latest(&TW)->zxvd_cpu_x100,
          tidewatch_latest(&TW)->mem_used_x100);
    CANNED = SYS_INFO("2000", "250000"); /* 0.25 s of CPU in 1 s */
    CHECK(tidewatch_sample(&TW) == ZXM_OK && tidewatch_latest(&TW)->zxvd_cpu_x100 == 2500,
          "25%%: %d", tidewatch_latest(&TW)->zxvd_cpu_x100);
    CANNED = SYS_INFO("2000", "300000"); /* no time passed: no share */
    CHECK(tidewatch_sample(&TW) == ZXM_OK && tidewatch_latest(&TW)->zxvd_cpu_x100 == -1, "dt 0");
    CANNED = SYS_INFO("2500", "1300000"); /* 1 s of CPU in 0.5 s: two CPUs' worth */
    CHECK(tidewatch_sample(&TW) == ZXM_OK && tidewatch_latest(&TW)->zxvd_cpu_x100 == 20000 &&
              tidewatch_peak_cpu_x100(&TW) == 20000 && tidewatch_count(&TW) == 4,
          "200%%: %d", tidewatch_latest(&TW)->zxvd_cpu_x100);
    for (int i = 0; i < 130; i++) {
        CANNED = SYS_INFO("3000", "0");
        CHECK(tidewatch_sample(&TW) == ZXM_OK, "sample %d", i);
    }
    CHECK(tidewatch_count(&TW) == TIDEWATCH_SAMPLES && tidewatch_peak_cpu_x100(&TW) == 0 &&
              tidewatch_at(&TW, TIDEWATCH_SAMPLES) == NULL,
          "the ring keeps the newest");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{}}";
    CHECK(tidewatch_sample(&TW) == ZXM_E_PROTOCOL && tidewatch_count(&TW) == TIDEWATCH_SAMPLES,
          "a damaged reply adds nothing");
    CANNED = NULL;
    tidewatch_init(&TW, &LINK);
    CHECK(tidewatch_sample(&TW) == ZXM_OK && tidewatch_sample(&TW) == ZXM_OK &&
              tidewatch_latest(&TW)->zxvd_rss > 0,
          "live: %s", LINK.err);

    /* Wake follows the daemon's own log */
    zxvd_log_reset();
    wake_init(&WK, &LINK);
    uint32_t added;
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 0 && wake_count(&WK) == 0, "quiet");
    CHECK(wake_ai(&WK, WAKE_AI_EXPLAIN) == ZXM_E_STATE, "nothing to explain");
    CHECK(raw_call("spaces.get", "{\"space\":\"nowhere\"}") == ZXM_E_REMOTE, "a refusal");
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Wake Test\"}") == ZXM_OK,
          "a change");
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 2 && WK.next == 2, "two: %u", added);
    /* Wake's own log.tail calls are quiet, so following does not feed itself */
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 0, "no echo");
    CHECK(wake_event(&WK, 0)->level == WAKE_WARN &&
              strstr(wake_event(&WK, 0)->text, "spaces.get refused") &&
              wake_event(&WK, 1)->level == WAKE_INFO && !wake_event(&WK, 2),
          "events");
    CHECK(wake_filter(&WK, WAKE_WARN, "") == ZXM_OK && wake_count(&WK) == 1, "warnings only");
    CHECK(wake_filter(&WK, WAKE_INFO, "CREATE") == ZXM_OK && wake_count(&WK) == 1 &&
              strstr(wake_event(&WK, 0)->text, "spaces.create"),
          "find, case ignored");
    CHECK(wake_filter(&WK, WAKE_INFO, "RPC") == ZXM_OK && wake_count(&WK) == 2, "by source");
    CHECK(wake_filter(&WK, WAKE_ERROR + 1, "") == ZXM_E_ARG &&
              wake_filter(&WK, WAKE_INFO,
                          "0123456789012345678901234567890123456789012345678901234567890123") ==
                  ZXM_E_ARG,
          "bad filter");
    CHECK(wake_filter(&WK, WAKE_INFO, "") == ZXM_OK, "all");
    asked = 0;
    CHECK(wake_ai(&WK, WAKE_AI_EXPLAIN) == ZXM_OK && asked == 1 &&
              strstr(last_prompt, "rpc: spaces.get refused") && !strstr(last_prompt, "create"),
          "explain warnings: %s", last_prompt);
    CHECK(wake_ai_reject(&WK) == ZXM_OK && wake_ai_accept(&WK) == ZXM_E_STATE, "reject");
    CHECK(wake_ai(&WK, 3) == ZXM_E_ARG, "action");
    wake_pause(&WK, true);
    zxvd_log(ZXVD_LOG_ERROR, "net", "link down");
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 0, "paused");
    wake_pause(&WK, false);
    /* the AI question was logged too (swarm.ask is not a poll) */
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 2 &&
              strstr(wake_event(&WK, 2)->text, "swarm.ask done") &&
              wake_event(&WK, 3)->level == WAKE_ERROR && !strcmp(wake_event(&WK, 3)->source, "net"),
          "resumed: %u", added);
    wake_clear(&WK);
    CHECK(wake_count(&WK) == 0 && WK.next == 4, "cleared, still following");
    /* more than one read's worth, and more than the daemon keeps */
    for (int i = 0; i < 700; i++) zxvd_log(ZXVD_LOG_INFO, "test", "e%d", i);
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 512 && WK.missed == 188 &&
              WK.n == WAKE_KEEP && !strcmp(wake_event(&WK, 0)->text, "e188") &&
              !strcmp(wake_event(&WK, 511)->text, "e699"),
          "added %u missed %llu first %s", added, (unsigned long long) WK.missed,
          wake_event(&WK, 0) ? wake_event(&WK, 0)->text : "-");
    for (int i = 0; i < 10; i++) zxvd_log(ZXVD_LOG_INFO, "test", "f%d", i);
    CHECK(wake_follow(&WK, &added) == ZXM_OK && added == 10 && WK.n == WAKE_KEEP &&
              !strcmp(wake_event(&WK, 0)->text, "e198") &&
              !strcmp(wake_event(&WK, 511)->text, "f9"),
          "the model's ring");
    wake_init(&WK, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"seq\":1,\"time_ms\":1,"
             "\"level\":\"loud\",\"source\":\"x\",\"text\":\"y\"}],\"next\":1,\"dropped\":0}}";
    CHECK(wake_follow(&WK, &added) == ZXM_E_PROTOCOL, "bad level");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"seq\":5,\"time_ms\":1,"
             "\"level\":\"info\",\"source\":\"x\",\"text\":\"y\"}],\"next\":4,\"dropped\":0}}";
    CHECK(wake_follow(&WK, &added) == ZXM_E_PROTOCOL, "seq past next");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[],\"next\":0}}";
    CHECK(wake_follow(&WK, &added) == ZXM_E_PROTOCOL, "no dropped");
    CANNED = NULL;

    /* Sounding: canned counters give exact rates */
    sounding_init(&SD, &FAKE);
#define NET(t, rx, tx, req)                                                                        \
    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":" t ",\"supported\":true,"             \
    "\"interfaces\":[{\"name\":\"lo\",\"up\":true,\"loopback\":true,\"addresses\":[\"127.0.0.1\"]" \
    ","                                                                                            \
    "\"rx_bytes\":" rx ",\"tx_bytes\":" tx ",\"rx_packets\":1,\"tx_packets\":1},{\"name\":"        \
    "\"en0\",\"up\":true,\"loopback\":false,\"addresses\":[\"10.0.0.2\",\"fe80::1\"],"             \
    "\"rx_bytes\":" rx ",\"tx_bytes\":" tx ",\"rx_packets\":1,\"tx_packets\":1},{\"name\":"        \
    "\"tun0\",\"up\":false,\"loopback\":false,\"addresses\":[],\"rx_bytes\":null,"                 \
    "\"tx_bytes\":null,\"rx_packets\":null,\"tx_packets\":null}],\"rpc\":{\"requests\":" req       \
    ",\"errors\":1,\"bytes_in\":10,\"bytes_out\":20,\"busy_us\":5}}}"
    CANNED = NET("1000", "100", "50", "10");
    CHECK(sounding_refresh(&SD) == ZXM_OK && SD.n == 3 && SD.supported &&
              sounding_find(&SD, "en0")->naddr == 2 && sounding_find(&SD, "en0")->rx_rate == -1 &&
              !sounding_find(&SD, "tun0")->counters && SD.rpc_rate == -1 &&
              sounding_history(&SD, 0)->rx_rate == -1 && !sounding_find(&SD, "wlan9"),
          "first read");
    CANNED = NET("3000", "4100", "2050", "30");
    CHECK(sounding_refresh(&SD) == ZXM_OK && sounding_find(&SD, "en0")->rx_rate == 2000 &&
              sounding_find(&SD, "en0")->tx_rate == 1000 && SD.rpc_rate == 10 &&
              sounding_history(&SD, 1)->rx_rate == 2000 &&
              sounding_history(&SD, 1)->tx_rate == 1000 && sounding_history_len(&SD) == 2 &&
              !sounding_history(&SD, 2),
          "2000 B/s in, loopback left out of the total: %lld",
          (long long) sounding_history(&SD, 1)->rx_rate);
    CANNED = NET("4000", "10", "10", "31"); /* counters reset */
    CHECK(sounding_refresh(&SD) == ZXM_OK && sounding_find(&SD, "en0")->rx_rate == -1 &&
              sounding_history(&SD, 2)->rx_rate == -1 && SD.rpc_rate == 1,
          "a reset gives no rate");
    for (int i = 0; i < 130; i++) {
        CANNED = NET("4000", "10", "10", "31");
        CHECK(sounding_refresh(&SD) == ZXM_OK, "read %d", i);
    }
    CHECK(sounding_history_len(&SD) == SOUNDING_SAMPLES, "history ring");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1,\"supported\":true,"
             "\"interfaces\":[{\"name\":\"x\"}],\"rpc\":{\"requests\":1,\"errors\":0,"
             "\"bytes_in\":0,\"bytes_out\":0}}}";
    CHECK(sounding_refresh(&SD) == ZXM_E_PROTOCOL && SD.n == 3, "damaged: kept");
    CANNED = NULL;
#undef NET
    sounding_init(&SD, &LINK);
    CHECK(sounding_refresh(&SD) == ZXM_OK && SD.rpc_requests > 0, "live: %s", LINK.err);
#if defined(__linux__) || defined(__APPLE__)
    CHECK(SD.supported && SD.n >= 1, "interfaces listed");
#endif
}

static scent_t SC;

static void test_scent(void)
{
    uint64_t size;
    CHECK(zxf_mkdir("scent") == 0, "folder");
    CHECK(zxf_write("scent/ledger notes.md", (const uint8_t *) "quincunx orchard\nrows", 21,
                    ZXF_W_REPLACE, false, &size) == 0 &&
              zxf_write("scent/quincunx.png", (const uint8_t *) "\x89PNG\0\0", 6, ZXF_W_REPLACE,
                        false, &size) == 0,
          "files");
    scent_init(&SC, &LINK);
    int before = sent;
    CHECK(scent_search(&SC, "  \t") == ZXM_OK && SC.n == 0 && sent == before, "blank: not asked");
    CHECK(scent_search(&SC, "quincunx") == ZXM_OK && SC.n == 2 && SC.total == 2 && SC.sel == -1,
          "found: %u %s", SC.n, LINK.err);
    const scent_hit_t *a = &SC.hit[0], *b = &SC.hit[1];
    CHECK(!strcmp(a->path, "scent/quincunx.png") && a->in_name && a->score == 1000 &&
              a->size == 6 && !a->snippet[0] && scent_opener(a) == SCENT_OPEN_GLINT,
          "the picture first: %s %u", a->path, a->score);
    CHECK(!strcmp(b->path, "scent/ledger notes.md") && !b->in_name && b->score == 1 &&
              !strcmp(b->snippet, "quincunx orchard rows") && scent_opener(b) == SCENT_OPEN_QUILLON,
          "then the note: %s [%s]", b->path, b->snippet);
    char f[16];
    scent_folder(b, f, sizeof f);
    CHECK(!strcmp(f, "scent"), "its folder: %s", f);
    scent_folder(b, f, 3);
    CHECK(!strcmp(f, "sc"), "cut: %s", f);
    scent_folder(b, f, 0);
    scent_hit_t top = {.path = "x.bin"};
    scent_folder(&top, f, sizeof f);
    CHECK(!f[0] && scent_opener(&top) == SCENT_OPEN_BURROW, "the top; others in Burrow");
    CHECK(scent_selected(&SC) == NULL && scent_select(&SC, 1) == ZXM_OK &&
              scent_selected(&SC) == &SC.hit[1] && scent_select(&SC, 2) == ZXM_E_ARG &&
              scent_select(&SC, -2) == ZXM_E_ARG && scent_select(&SC, -1) == ZXM_OK &&
              scent_selected(&SC) == NULL,
          "selection");
    uint32_t files = 0;
    CHECK(scent_reindex(&SC, &files) == ZXM_OK && files >= 2, "reindex: %u", files);
    CHECK(scent_reindex(&SC, NULL) == ZXM_OK, "reindex, count not wanted");
    CHECK(strstr(LINK.req, "\"method\":\"search.refresh\"") && !strstr(LINK.req, "space"), "home");
    CHECK(scent_search(&SC, "quincunx orchard") == ZXM_OK && SC.n == 1 && SC.hit[0].score == 2,
          "both words");
    /* spaces */
    CHECK(scent_set_space(&SC, "studio") == ZXM_OK && SC.n == 0 &&
              scent_search(&SC, "quincunx") == ZXM_OK && SC.total == 0 &&
              strstr(LINK.req, "\"space\":\"studio\""),
          "another space: %s", LINK.req);
    CHECK(scent_reindex(&SC, &files) == ZXM_OK && strstr(LINK.req, "\"space\":\"studio\""),
          "reindex there");
    CHECK(scent_set_space(&SC, "nowhere") == ZXM_OK &&
              scent_search(&SC, "quincunx") == ZXM_E_REMOTE && LINK.err_code == -32013 &&
              scent_reindex(&SC, &files) == ZXM_E_REMOTE,
          "no such space");
    static char longid[64];
    memset(longid, 'a', 49);
    CHECK(scent_set_space(&SC, longid) == ZXM_E_ARG && !strcmp(SC.space, "nowhere"), "id too long");
    CHECK(scent_set_space(&SC, "home") == ZXM_OK && !SC.space[0] &&
              scent_set_space(&SC, NULL) == ZXM_OK && !SC.space[0],
          "home");
    static char longq[SCENT_QUERY + 2];
    memset(longq, 'q', SCENT_QUERY + 1);
    CHECK(scent_search(&SC, longq) == ZXM_E_ARG && scent_search(&SC, NULL) == ZXM_E_ARG,
          "too long");
    CHECK(scent_search(&SC, "-- ;") == ZXM_E_REMOTE && LINK.err_code == -32602, "no words");

    /* replies that do not add up keep the old hits */
    CHECK(scent_search(&SC, "quincunx") == ZXM_OK && SC.n == 2, "again");
    scent_t F;
    scent_init(&F, &FAKE);
    F = SC;
    F.link = &FAKE;
    const char *bad[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":{},\"total\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[1],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":1,"
        "\"mtime\":2,\"in_name\":true,\"score\":3,\"snippet\":\"\"}],\"total\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[],\"total\":4294967296}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"\",\"size\":1,"
        "\"mtime\":2,\"in_name\":true,\"score\":3,\"snippet\":\"\"}],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":1,"
        "\"mtime\":2,\"in_name\":true,\"score\":4294967296,\"snippet\":\"\"}],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":1,"
        "\"mtime\":2,\"in_name\":1,\"score\":3,\"snippet\":\"\"}],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":1,"
        "\"mtime\":2,\"in_name\":true,\"score\":3}],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":-1,"
        "\"mtime\":2,\"in_name\":true,\"score\":3,\"snippet\":\"\"}],\"total\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a\",\"size\":1,"
        "\"mtime\":\"x\",\"in_name\":true,\"score\":3,\"snippet\":\"\"}],\"total\":1}}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        CANNED = bad[i];
        CHECK(scent_search(&F, "x") == ZXM_E_PROTOCOL && F.n == 2 && F.total == 2 &&
                  !strcmp(F.hit[0].path, "scent/quincunx.png"),
              "bad reply %zu kept the hits", i);
    }
    CANNED =
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"hits\":[{\"path\":\"a/b.txt\",\"size\":7,"
        "\"mtime\":-5,\"in_name\":false,\"score\":12,\"snippet\":\"s\"}],\"total\":9}}";
    F.sel = 0;
    CHECK(scent_search(&F, "x") == ZXM_OK && F.n == 1 && F.total == 9 && F.sel == -1 &&
              !strcmp(F.hit[0].path, "a/b.txt") && F.hit[0].size == 7 && F.hit[0].mtime == -5 &&
              !F.hit[0].in_name && F.hit[0].score == 12 && !strcmp(F.hit[0].snippet, "s") &&
              strstr(last_req, "\"q\":\"x\",\"max\":50"),
          "a made-up reply: %s", last_req);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"files\":4294967296}}";
    CHECK(scent_reindex(&F, &files) == ZXM_E_PROTOCOL, "too many files");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"files\":4294967295}}";
    CHECK(scent_reindex(&F, &files) == ZXM_OK && files == 4294967295u, "the most files");
    CANNED = NULL;
    CHECK(scent_search(&F, "x") == ZXM_E_TRANSPORT && F.n == 1, "no daemon");

    /* AI: a sharper search, run only when accepted */
    scent_t E;
    scent_init(&E, &LINK);
    CHECK(scent_ai(&E, SCENT_AI_SHARPEN) == ZXM_E_STATE, "nothing searched");
    CHECK(scent_ai(&SC, -1) == ZXM_E_ARG && scent_ai(&SC, SCENT_AI_COUNT) == ZXM_E_ARG, "range");
    CHECK(scent_ai_accept(&SC) == ZXM_E_STATE, "nothing held");
    ANSWER = "quincunx orchard\nor try rows";
    asked = 0;
    CHECK(scent_ai(&SC, SCENT_AI_SHARPEN) == ZXM_OK && SC.held.state == ZXM_HELD_PENDING &&
              asked == 1 && SC.n == 2 &&
              strstr(last_prompt, "Search: quincunx\nFound 2. Names of the best:\n"
                                  "scent/quincunx.png\nscent/ledger notes.md\n") &&
              strstr(last_prompt, "one better search"),
          "held: %s", last_prompt);
    CHECK(scent_ai_accept(&SC) == ZXM_OK && SC.held.state == ZXM_HELD_ACCEPTED &&
              !strcmp(SC.query, "quincunx orchard") && SC.n == 1,
          "accepted: searched [%s] %u", SC.query, SC.n);
    ANSWER = "nothing useful";
    CHECK(scent_ai(&SC, SCENT_AI_SHARPEN) == ZXM_OK && scent_ai_reject(&SC) == ZXM_OK &&
              SC.held.state == ZXM_HELD_REJECTED && !strcmp(SC.query, "quincunx orchard"),
          "rejected: nothing searched");
    /* a long answer: cut to a query on a whole character */
    static char longans[400];
    memset(longans, 'w', sizeof longans - 1);
    longans[SCENT_QUERY - 1] = (char) 0xc3;
    longans[SCENT_QUERY] = (char) 0xa9;
    ANSWER = longans;
    CHECK(scent_ai(&SC, SCENT_AI_SHARPEN) == ZXM_OK && scent_ai_accept(&SC) == ZXM_OK &&
              strlen(SC.query) == SCENT_QUERY - 1,
          "cut answer: %zu", strlen(SC.query));
    ANSWER = "ok";
}

/* ---------------- Platen ---------------- */

static platen_t PL;

static void test_platen(void)
{
    uint64_t size;
    char uri[96], bad[96], scanner[96], half[96];
    CHECK(fake_start(), "the fake printer and scanner");
    zph_set_timeout_ms(3000);
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    snprintf(bad, sizeof bad, "ipp://127.0.0.1:%d/ipp/garbage", FAKE_PORT);
    snprintf(scanner, sizeof scanner, "http://127.0.0.1:%d/eSCL", FAKE_PORT);
    snprintf(half, sizeof half, "http://127.0.0.1:%d/half", FAKE_PORT);
    CHECK(zxf_mkdir("platen") == 0 && zxf_mkdir("platen/scans") == 0 &&
              zxf_write("platen/letter.pdf", (const uint8_t *) "%PDF-1.4 x", 10, ZXF_W_REPLACE,
                        false, &size) == 0 &&
              zxf_write("platen/photo.png", (const uint8_t *) "\x89PNG", 4, ZXF_W_REPLACE, false,
                        &size) == 0,
          "files");
    platen_init(&PL, &LINK);

    /* formats */
    CHECK(!strcmp(platen_format_of("a.PDF"), "application/pdf") &&
              !strcmp(platen_format_of("b.jpeg"), "image/jpeg") &&
              !strcmp(platen_format_of("c.txt"), "text/plain") && !platen_format_of("d") &&
              !platen_format_of("e.pdfx") && !platen_format_of("f.pd") &&
              !platen_format_of("g.pdf/h") && !strcmp(platen_format_of("x.y.urf"), "image/urf"),
          "formats");

    /* printers */
    CHECK(platen_add_printer(&PL, NULL) == ZXM_E_ARG && platen_add_printer(&PL, "") == ZXM_E_ARG,
          "no address");
    static char longuri[PLATEN_URI + 2];
    memset(longuri, 'u', PLATEN_URI + 1);
    CHECK(platen_add_printer(&PL, longuri) == ZXM_E_ARG, "address too long");
    CHECK(platen_add_printer(&PL, bad) == ZXM_E_REMOTE && LINK.err_code == -32014 &&
              PL.nprinters == 0,
          "a printer that does not answer is not added: %d", LINK.err_code);
    CHECK(platen_add_printer(&PL, "ipp://8.8.8.8/ipp/print") == ZXM_E_REMOTE && PL.nprinters == 0,
          "not local");
    CHECK(platen_add_printer(&PL, uri) == ZXM_OK && PL.nprinters == 1, "added: %s", LINK.err);
    const platen_printer_t *pr = &PL.printer[0];
    CHECK(pr->known && !strcmp(pr->name, "Fake") && !strcmp(pr->make, "Fake Printer 1") &&
              !strcmp(pr->state, "idle") && !strcmp(pr->reasons, "none") && pr->accepting &&
              pr->color && pr->nformats == 2 && !strcmp(pr->formats[0], "application/pdf") &&
              !strcmp(pr->formats[1], "image/jpeg") && pr->nsides == 1 &&
              !strcmp(pr->sides[0], "one-sided"),
          "what it is: %s %s %s %u", pr->name, pr->state, pr->reasons, pr->nformats);
    CHECK(platen_add_printer(&PL, uri) == ZXM_E_STATE && PL.nprinters == 1, "already there");
    CHECK(platen_takes(&PL, 0, "platen/letter.pdf") && platen_takes(&PL, 0, "x.JPG") &&
              !platen_takes(&PL, 0, "platen/photo.png") && !platen_takes(&PL, 0, "noext") &&
              !platen_takes(&PL, 1, "a.pdf"),
          "what it takes");
    CHECK(platen_refresh_printer(&PL, 0) == ZXM_OK && pr->known &&
              platen_refresh_printer(&PL, 1) == ZXM_E_ARG,
          "refresh");

    /* printing */
    int before = sent;
    CHECK(platen_print(&PL, 1, "platen/letter.pdf", NULL) == ZXM_E_ARG &&
              platen_print(&PL, 0, NULL, NULL) == ZXM_E_ARG &&
              platen_print(&PL, 0, "", NULL) == ZXM_E_ARG && sent == before,
          "nothing to print: not asked");
    CHECK(platen_print(&PL, 0, "platen/photo.png", NULL) == ZXM_E_REMOTE &&
              LINK.err_code == -32014 && PL.njobs == 0,
          "a format it does not take");
    CHECK(platen_print(&PL, 0, "platen/none.pdf", NULL) == ZXM_E_REMOTE && PL.njobs == 0,
          "no such file");
    CHECK(platen_print(&PL, 0, "platen/letter.pdf", NULL) == ZXM_OK && PL.njobs == 1, "printed: %s",
          LINK.err);
    const platen_job_t *j = &PL.job[0];
    CHECK(j->id == 77 && j->printer == 0 && !strcmp(j->state, "pending") && j->pages == -1 &&
              !strcmp(j->path, "platen/letter.pdf") && !platen_job_done(j),
          "the job: %d %s %d", j->id, j->state, j->pages);
    CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
              strstr(fake_get("/fake/last"), "doc=10:%PDF-1.4"),
          "what the printer got: %s", fake_get("/fake/last"));
    platen_opts_t o = {.copies = 2, .sides = "one-sided", .mono = true};
    CHECK(platen_print(&PL, 0, "platen/letter.pdf", &o) == ZXM_OK && PL.njobs == 2 &&
              strstr(LINK.req, "\"copies\":2") && strstr(LINK.req, "\"sides\":\"one-sided\"") &&
              strstr(LINK.req, "\"color\":\"monochrome\""),
          "options: %s", LINK.req);
    CHECK(strstr(fake_get("/fake/last"), "copies=2 sides=one-sided"), "options reached it: %s",
          fake_get("/fake/last"));
    platen_opts_t none = {0};
    CHECK(platen_print(&PL, 0, "platen/letter.pdf", &none) == ZXM_OK && PL.njobs == 3 &&
              !strstr(LINK.req, "copies") && !strstr(LINK.req, "sides") &&
              !strstr(LINK.req, "color"),
          "defaults: %s", LINK.req);
    o.sides = "both";
    CHECK(platen_print(&PL, 0, "platen/letter.pdf", &o) == ZXM_E_REMOTE &&
              LINK.err_code == -32602 && PL.njobs == 3,
          "bad sides");
    /* cancel one, then follow the rest */
    CHECK(platen_cancel(&PL, 3) == ZXM_E_ARG, "no such job");
    CHECK(platen_cancel(&PL, 1) == ZXM_OK && !strcmp(PL.job[1].state, "canceled") &&
              platen_job_done(&PL.job[1]) && platen_cancel(&PL, 1) == ZXM_E_STATE,
          "canceled: %s", PL.job[1].state);
    before = sent;
    CHECK(platen_follow(&PL) == ZXM_OK && sent == before + 2 &&
              !strcmp(PL.job[0].state, "completed") && PL.job[0].pages == 2 &&
              !strcmp(PL.job[2].state, "completed") && !strcmp(PL.job[1].state, "canceled"),
          "followed: %s %d, %d asked", PL.job[0].state, PL.job[0].pages, sent - before);
    before = sent;
    CHECK(platen_follow(&PL) == ZXM_OK && sent == before, "nothing left to follow");
    platen_job_t aborted = {.state = "aborted"}, held = {.state = "held"};
    CHECK(platen_job_done(&aborted) && !platen_job_done(&held), "done states");
    /* a job the printer does not know: follow reports it but goes on */
    CHECK(platen_print(&PL, 0, "platen/letter.pdf", NULL) == ZXM_OK &&
              platen_print(&PL, 0, "platen/letter.pdf", NULL) == ZXM_OK && PL.njobs == 5,
          "two more");
    PL.job[3].id = 5;
    CHECK(platen_follow(&PL) == ZXM_E_REMOTE && !strcmp(PL.job[3].state, "pending") &&
              !strcmp(PL.job[4].state, "completed"),
          "one unknown, the other followed: %s", PL.job[4].state);
    /* a long path is cut on a whole character */
    static char longpath[400];
    memset(longpath, 'p', 300);
    memcpy(longpath + 300, ".pdf", 5);
    longpath[254] = (char) 0xc3;
    longpath[255] = (char) 0xa9;
    PL.njobs = 0;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":9,\"state\":\"pending\","
             "\"reasons\":\"none\",\"message\":\"\",\"pages\":-1}}";
    PL.link = &FAKE;
    CHECK(platen_print(&PL, 0, longpath, NULL) == ZXM_OK && strlen(PL.job[0].path) == 254 &&
              PL.job[0].pages == -1,
          "cut path: %zu", strlen(PL.job[0].path));
    /* the list keeps the newest */
    for (int i = 0; i < (int) PLATEN_JOBS + 2; i++) {
        CANNED = i % 2 ? "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":11,\"state\":"
                         "\"pending\",\"reasons\":\"\",\"message\":\"\",\"pages\":0}}"
                       : "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":10,\"state\":"
                         "\"pending\",\"reasons\":\"\",\"message\":\"\",\"pages\":0}}";
        platen_print(&PL, 0, "a.pdf", NULL);
    }
    CHECK(PL.njobs == PLATEN_JOBS && PL.job[PLATEN_JOBS - 1].id == 11 && PL.job[0].id == 10 &&
              PL.job[1].id == 11,
          "full: %u, last %d", PL.njobs, PL.job[PLATEN_JOBS - 1].id);
    /* replies that do not add up change nothing */
    const char *badjob[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"state\":\"pending\",\"message\":\"\","
        "\"pages\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":2147483648,\"state\":\"pending\","
        "\"message\":\"\",\"pages\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":1,\"message\":\"\",\"pages\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":1,\"state\":\"pending\",\"pages\":0}"
        "}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":1,\"state\":\"pending\",\"message\":"
        "\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":1,\"state\":\"pending\",\"message\":"
        "\"\","
        "\"pages\":-2}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":1,\"state\":\"pending\",\"message\":"
        "\"\","
        "\"pages\":2147483648}}",
    };
    for (size_t i = 0; i < sizeof badjob / sizeof *badjob; i++) {
        CANNED = badjob[i];
        CHECK(platen_print(&PL, 0, "a.pdf", NULL) == ZXM_E_PROTOCOL && PL.njobs == PLATEN_JOBS &&
                  PL.job[PLATEN_JOBS - 1].id == 11,
              "bad job reply %zu", i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":2147483647,\"state\":"
             "\"processing\",\"reasons\":\"\",\"message\":\"m\",\"pages\":2147483647}}";
    CHECK(platen_cancel(&PL, 0) == ZXM_OK && PL.job[0].id == INT32_MAX &&
              PL.job[0].pages == INT32_MAX && !strcmp(PL.job[0].message, "m"),
          "the largest job");
    CANNED = NULL;
    CHECK(platen_cancel(&PL, 1) == ZXM_E_TRANSPORT && PL.job[1].id == 11, "no daemon");
    const char *badstatus[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"idle\",\"reasons\":"
        "\"\","
        "\"accepting\":true,\"color\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"state\":\"idle\",\"reasons\":"
        "\"\","
        "\"accepting\":true,\"color\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"reasons\":\"\","
        "\"accepting\":true,\"color\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"accepting\":true,\"color\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"color\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"formats\":[],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"color\":true,\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"color\":true,\"formats\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"color\":true,\"formats\":[1],\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"color\":true,\"formats\":{},\"sides\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"\",\"make\":\"\",\"state\":\"idle\","
        "\"reasons\":\"\",\"accepting\":true,\"color\":true,\"formats\":[\""
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"],\"sides\":[]}}",
    };
    for (size_t i = 0; i < sizeof badstatus / sizeof *badstatus; i++) {
        CANNED = badstatus[i];
        PL.printer[0].known = true;
        CHECK(platen_refresh_printer(&PL, 0) == ZXM_E_PROTOCOL && !PL.printer[0].known &&
                  !strcmp(PL.printer[0].name, "Fake") && PL.printer[0].nformats == 2,
              "bad status %zu kept what was known", i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"name\":\"N\",\"make\":\"M\",\"state\":"
             "\"stopped\",\"reasons\":\"media-empty-error\",\"accepting\":false,\"color\":false,"
             "\"formats\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"application/octet-stream\","
             "\"i\"],\"sides\":[]}}";
    CHECK(platen_refresh_printer(&PL, 0) == ZXM_OK && pr->known && !pr->accepting && !pr->color &&
              pr->nformats == PLATEN_WORDS && pr->nsides == 0 && !strcmp(pr->state, "stopped") &&
              platen_takes(&PL, 0, "any.txt") && !platen_takes(&PL, 0, "noext"),
          "eight formats kept; anything goes: %u", pr->nformats);
    CANNED = NULL;
    CHECK(platen_refresh_printer(&PL, 0) == ZXM_E_TRANSPORT && !pr->known, "gone quiet");
    CHECK(platen_add_printer(&PL, "ipp://x/y") == ZXM_E_TRANSPORT && PL.nprinters == 1,
          "not added when quiet");
    PL.link = &LINK;

    /* AI explains a printer's trouble */
    CHECK(platen_ai(&PL, -1, 0) == ZXM_E_ARG && platen_ai(&PL, PLATEN_AI_COUNT, 0) == ZXM_E_ARG &&
              platen_ai(&PL, PLATEN_AI_EXPLAIN, 1) == ZXM_E_ARG,
          "range");
    ANSWER = "The paper tray is empty. Add paper.";
    asked = 0;
    CHECK(platen_ai(&PL, PLATEN_AI_EXPLAIN, 0) == ZXM_OK && asked == 1 &&
              PL.held.state == ZXM_HELD_PENDING &&
              strstr(last_prompt, "Printer: N (M)\nState: stopped\nReasons: media-empty-error\n") &&
              strstr(last_prompt, "plain words"),
          "held: %s", last_prompt);
    CHECK(platen_ai_accept(&PL) == ZXM_OK && PL.held.state == ZXM_HELD_ACCEPTED, "read");
    CHECK(platen_ai(&PL, PLATEN_AI_EXPLAIN, 0) == ZXM_OK && platen_ai_reject(&PL) == ZXM_OK &&
              PL.held.state == ZXM_HELD_REJECTED,
          "dismissed");
    ANSWER = "ok";
    PL.printer[0].reasons[0] = 0;
    CHECK(platen_ai(&PL, PLATEN_AI_EXPLAIN, 0) == ZXM_E_STATE, "nothing wrong (empty)");
    memcpy(PL.printer[0].reasons, "none", 5);
    CHECK(platen_ai(&PL, PLATEN_AI_EXPLAIN, 0) == ZXM_E_STATE, "nothing wrong (none)");
    memset(PL.printer[0].name, 'n', sizeof PL.printer[0].name - 1);
    memset(PL.printer[0].make, 'm', sizeof PL.printer[0].make - 1);
    memset(PL.printer[0].reasons, 'r', sizeof PL.printer[0].reasons - 1);
    CHECK(platen_ai(&PL, PLATEN_AI_EXPLAIN, 0) == ZXM_OK, "long fields fit: %s", LINK.err);

    /* removing a printer leaves its jobs, pointing nowhere */
    CHECK(platen_add_printer(&PL, uri) == ZXM_E_STATE, "still there");
    PL.njobs = 3;
    PL.job[0].printer = 0;
    PL.job[1].printer = 1;
    PL.job[2].printer = PLATEN_DEVICES;
    memcpy(PL.job[0].state, "pending", 8);
    PL.nprinters = 2;
    memcpy(PL.printer[1].uri, "ipp://y", 8);
    CHECK(platen_remove_printer(&PL, 2) == ZXM_E_ARG && platen_remove_printer(&PL, 0) == ZXM_OK &&
              PL.nprinters == 1 && !strcmp(PL.printer[0].uri, "ipp://y") &&
              PL.job[0].printer == PLATEN_DEVICES && PL.job[1].printer == 0 &&
              PL.job[2].printer == PLATEN_DEVICES,
          "removed: %u %u %u", PL.job[0].printer, PL.job[1].printer, PL.job[2].printer);
    CHECK(platen_cancel(&PL, 0) == ZXM_E_STATE, "its printer is gone");
    before = sent;
    PL.njobs = 1;
    CHECK(platen_follow(&PL) == ZXM_OK && sent == before, "not followed");
    /* a full list */
    PL.nprinters = PLATEN_DEVICES;
    for (uint32_t i = 0; i < PLATEN_DEVICES; i++) snprintf(PL.printer[i].uri, 16, "ipp://p%u", i);
    CHECK(platen_add_printer(&PL, uri) == ZXM_E_STATE && PL.nprinters == PLATEN_DEVICES, "full");
    CHECK(platen_remove_printer(&PL, PLATEN_DEVICES - 1) == ZXM_OK &&
              PL.nprinters == PLATEN_DEVICES - 1,
          "the last removed");

    /* scanners */
    CHECK(platen_add_scanner(&PL, NULL) == ZXM_E_ARG && platen_add_scanner(&PL, "") == ZXM_E_ARG &&
              platen_add_scanner(&PL, longuri) == ZXM_E_ARG,
          "no scanner address");
    CHECK(platen_add_scanner(&PL, scanner) == ZXM_OK && PL.nscanners == 1, "scanner: %s", LINK.err);
    const platen_scanner_t *sc = &PL.scanner[0];
    CHECK(sc->known && !strcmp(sc->make, "Fake Scanner") && !strcmp(sc->state, "Idle") &&
              sc->platen && !sc->feeder && sc->ndpi == 2 && sc->dpi[0] == 300 &&
              sc->dpi[1] == 600 && !strcmp(sc->url, scanner),
          "what it is: %s %s %u", sc->make, sc->state, sc->ndpi);
    CHECK(platen_add_scanner(&PL, scanner) == ZXM_E_STATE, "already there");
    CHECK(platen_scan(&PL, 1, "platen/scans", NULL) == ZXM_E_ARG, "no such scanner");
    CHECK(platen_set_space(&PL, "nowhere") == ZXM_OK &&
              platen_scan(&PL, 0, "platen/scans", NULL) == ZXM_E_REMOTE && LINK.err_code == -32013,
          "no such space");
    CHECK(platen_set_space(&PL, "home") == ZXM_OK && !PL.space[0], "home");
    platen_scan_t ss = {.dpi = 600};
    CHECK(platen_scan(&PL, 0, "platen/scans", &ss) == ZXM_OK && PL.nsaved == 2 &&
              !strcmp(PL.saved[0], "platen/scans/Scan 1.jpg") &&
              !strcmp(PL.saved[1], "platen/scans/Scan 2.jpg"),
          "scanned: %u %s", PL.nsaved, PL.saved[0]);
    CHECK(strstr(fake_get("/fake/last"), "dpi=600< src=Platen"), "what the scanner got: %s",
          fake_get("/fake/last"));
    zxf_entry_t fe;
    CHECK(zxf_stat("platen/scans/Scan 2.jpg", &fe) == 0 && fe.size == 10, "saved");
    ss = (platen_scan_t){.feeder = true, .color = "gray", .format = "image/jpeg"};
    CHECK(platen_scan(&PL, 0, NULL, &ss) == ZXM_OK && PL.nsaved == 2 &&
              !strcmp(PL.saved[0], "Scan 1.jpg") && strstr(LINK.req, "\"source\":\"feeder\"") &&
              strstr(LINK.req, "\"color\":\"gray\"") && !strstr(LINK.req, "folder"),
          "feeder, at the top: %s", PL.saved[0]);
    CHECK(strstr(fake_get("/fake/last"), "dpi=300< src=Feeder"), "defaults: %s",
          fake_get("/fake/last"));
    CHECK(platen_scan(&PL, 0, "", NULL) == ZXM_OK && !strstr(LINK.req, "folder") &&
              !strstr(LINK.req, "source") && !strcmp(PL.saved[0], "Scan 3.jpg"),
          "no settings: %s", PL.saved[0]);
    ss = (platen_scan_t){.dpi = 50};
    CHECK(platen_scan(&PL, 0, "", &ss) == ZXM_E_REMOTE && LINK.err_code == -32602 &&
              !strcmp(PL.saved[0], "Scan 3.jpg"),
          "bad dpi");
    PL.nscanners = 2;
    memcpy(PL.scanner[1].url, half, strlen(half) + 1);
    CHECK(platen_scan(&PL, 1, "platen/scans", NULL) == ZXM_E_REMOTE && LINK.err_code == -32014 &&
              strstr(LINK.err, "(1 page saved)") && !strcmp(PL.saved[0], "Scan 3.jpg"),
          "half a scan: %s", LINK.err);
    CHECK(platen_remove_scanner(&PL, 2) == ZXM_E_ARG && platen_remove_scanner(&PL, 0) == ZXM_OK &&
              PL.nscanners == 1 && !strcmp(PL.scanner[0].url, half),
          "removed");
    PL.nscanners = PLATEN_DEVICES;
    for (uint32_t i = 0; i < PLATEN_DEVICES; i++) snprintf(PL.scanner[i].url, 16, "http://s%u", i);
    CHECK(platen_add_scanner(&PL, scanner) == ZXM_E_STATE, "full");
    PL.nscanners = 0;
    CHECK(platen_add_scanner(&PL, half) == ZXM_E_REMOTE && PL.nscanners == 0, "not a scanner");

    /* made-up scanner replies */
    PL.link = &FAKE;
    const char *badscan[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"state\":\"Idle\",\"platen\":true,"
        "\"feeder\":false,\"resolutions\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"platen\":true,"
        "\"feeder\":false,\"resolutions\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\","
        "\"feeder\":false,\"resolutions\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"resolutions\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"feeder\":false}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"feeder\":false,\"resolutions\":{}}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"feeder\":false,\"resolutions\":[0]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"feeder\":false,\"resolutions\":[100001]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "true,"
        "\"feeder\":false,\"resolutions\":[\"x\"]}}",
    };
    for (size_t i = 0; i < sizeof badscan / sizeof *badscan; i++) {
        CANNED = badscan[i];
        CHECK(platen_add_scanner(&PL, "http://s/eSCL") == ZXM_E_PROTOCOL && PL.nscanners == 0,
              "bad scanner %zu", i);
    }
    CANNED =
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"make\":\"\",\"state\":\"Idle\",\"platen\":"
        "false,\"feeder\":true,\"resolutions\":[1,2,3,4,5,6,7,8,100000]}}";
    CHECK(platen_add_scanner(&PL, "http://s/eSCL") == ZXM_OK && PL.nscanners == 1 &&
              PL.scanner[0].ndpi == 8 && PL.scanner[0].dpi[7] == 8 && PL.scanner[0].feeder,
          "eight resolutions kept");
    const char *badsaved[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"pages\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"saved\":{}}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"saved\":[\"a\",2]}}",
    };
    for (size_t i = 0; i < sizeof badsaved / sizeof *badsaved; i++) {
        CANNED = badsaved[i];
        CHECK(platen_scan(&PL, 0, "", NULL) == ZXM_E_PROTOCOL && !strcmp(PL.saved[0], "Scan 3.jpg"),
              "bad saved %zu", i);
    }
    static char many[PLATEN_PAGES * 6 + 200];
    int k = snprintf(many, sizeof many, "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":{\"saved\":[");
    for (uint32_t i = 0; i <= PLATEN_PAGES; i++)
        k += snprintf(many + k, sizeof many - (size_t) k, "%s\"p\"", i ? "," : "");
    snprintf(many + k, sizeof many - (size_t) k, "]}}");
    CANNED = many;
    CHECK(platen_scan(&PL, 0, "", NULL) == ZXM_E_PROTOCOL && PL.nsaved == 2, "too many pages");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"pages\":0,\"saved\":[]}}";
    CHECK(platen_scan(&PL, 0, "", NULL) == ZXM_OK && PL.nsaved == 0, "nothing scanned");
    CANNED = NULL;
    CHECK(platen_scan(&PL, 0, "", NULL) == ZXM_E_TRANSPORT &&
              platen_add_scanner(&PL, "http://t") == ZXM_E_TRANSPORT && PL.nscanners == 1,
          "no daemon");
    PL.link = &LINK;

    /* spaces */
    static char longid[64];
    memset(longid, 'a', 49);
    CHECK(platen_set_space(&PL, longid) == ZXM_E_ARG && platen_set_space(&PL, "studio") == ZXM_OK &&
              !strcmp(PL.space, "studio") && platen_set_space(&PL, NULL) == ZXM_OK && !PL.space[0],
          "space");
    zph_set_timeout_ms(0);
    fake_stop();
}

/* ---------------- Keepsake ---------------- */

static keepsake_t KS;

static void test_keepsake(void)
{
    char dest[256];
    snprintf(dest, sizeof dest, "%s/zxv-keep-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dest) != NULL, "a destination");
    uint64_t size;
    CHECK(zxf_mkdir("keep") == 0 && zxf_write("keep/plan.txt", (const uint8_t *) "first", 5,
                                              ZXF_W_REPLACE, false, &size) == 0,
          "files");
    keepsake_init(&KS, &LINK);
    int before = sent;
    CHECK(keepsake_backup(&KS, false) == ZXM_E_STATE && keepsake_refresh(&KS) == ZXM_E_STATE &&
              keepsake_versions(&KS, "a") == ZXM_E_STATE && keepsake_check(&KS, 0) == ZXM_E_STATE &&
              keepsake_restore(&KS, 0, "a", NULL) == ZXM_E_STATE &&
              keepsake_prune(&KS, 1) == ZXM_E_STATE && sent == before,
          "no destination yet: nothing asked");
    static char longd[KEEPSAKE_DEST + 2];
    memset(longd, 'd', KEEPSAKE_DEST + 1);
    CHECK(keepsake_set_dest(&KS, NULL) == ZXM_E_ARG && keepsake_set_dest(&KS, "") == ZXM_E_ARG &&
              keepsake_set_dest(&KS, longd) == ZXM_E_ARG && !KS.dest[0],
          "bad destinations");
    CHECK(keepsake_set_dest(&KS, "relative") == ZXM_OK &&
              keepsake_backup(&KS, false) == ZXM_E_REMOTE && LINK.err_code == -32015,
          "zxvd refuses a relative one");
    CHECK(keepsake_set_dest(&KS, dest) == ZXM_OK, "set");
    CHECK(keepsake_refresh(&KS) == ZXM_OK && KS.nsnaps == 0 && KS.total == 0, "none yet");
    CHECK(!keepsake_tested(&KS), "not tested");

    /* back up, change, back up */
    CHECK(keepsake_backup(&KS, false) == ZXM_OK && KS.last.snapshot == 1 && KS.last.files >= 2 &&
              KS.last.new_files >= 1 && KS.last.new_files <= KS.last.files && KS.nsnaps == 1 &&
              KS.total == 1 && KS.snap[0].id == 1 && !KS.snap[0].damaged &&
              KS.snap[0].time == KS.last.time,
          "first: %u files %s", KS.last.files, LINK.err);
    uint32_t files = KS.last.files;
    CHECK(!keepsake_tested(&KS), "not checked yet");
    CHECK(keepsake_check(&KS, 0) == ZXM_OK && KS.check.done && KS.check.snapshot == 1 &&
              KS.check.files == files && KS.check.bad == 0 && KS.check.missing == 0 &&
              !KS.check.first_bad[0] && keepsake_tested(&KS),
          "checked");
    CHECK(keepsake_ai(&KS, KEEPSAKE_AI_EXPLAIN) == ZXM_E_STATE, "nothing to explain");
    CHECK(zxf_write("keep/plan.txt", (const uint8_t *) "second!", 7, ZXF_W_REPLACE, false, &size) ==
                  0 &&
              keepsake_backup(&KS, false) == ZXM_OK && KS.last.snapshot == 2 &&
              KS.last.new_files == 1 && KS.nsnaps == 2 && KS.snap[0].id == 2 &&
              KS.snap[1].id == 1 && !KS.check.done && !keepsake_tested(&KS),
          "second: %u new", KS.last.new_files);
    /* a damaged copy: a plain backup trusts it, a full one repairs it */
    char hex[65], obj[600];
    uint8_t h[32];
    sha256((const uint8_t *) "second!", 7, h);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    snprintf(obj, sizeof obj, "%s/ZXV Keepsake/objects/%.2s/%s", dest, hex, hex);
    FILE *of = fopen(obj, "wb");
    CHECK(of && fwrite("SECOND!", 1, 7, of) == 7 && fclose(of) == 0, "damage a copy");
    CHECK(keepsake_backup(&KS, false) == ZXM_OK && KS.last.new_files == 0 &&
              strstr(LINK.req, "\"max\":50"),
          "third, then listed: %s", LINK.req);
    CHECK(keepsake_check(&KS, 0) == ZXM_OK && KS.check.bad == 1 &&
              !strcmp(KS.check.first_bad, "keep/plan.txt") && !keepsake_tested(&KS),
          "the check finds it: %s", KS.check.first_bad);
    CHECK(keepsake_backup(&KS, true) == ZXM_OK && KS.last.snapshot == 4 && KS.last.new_files == 1,
          "full repairs it: %u", KS.last.new_files);
    CHECK(keepsake_check(&KS, 1) == ZXM_OK && KS.check.snapshot == 1 && !keepsake_tested(&KS) &&
              strstr(LINK.req, "\"snapshot\":1"),
          "an older one checked is not the newest");
    CHECK(keepsake_check(&KS, 0) == ZXM_OK && !strstr(LINK.req, "snapshot") && keepsake_tested(&KS),
          "the newest");

    /* versions */
    CHECK(keepsake_versions(&KS, NULL) == ZXM_E_ARG && keepsake_versions(&KS, "") == ZXM_E_ARG,
          "no path");
    CHECK(keepsake_versions(&KS, "keep/plan.txt") == ZXM_OK && KS.nvers == 2 &&
              KS.ver[0].snapshot == 4 && KS.ver[0].size == 7 && KS.ver[1].snapshot == 1 &&
              KS.ver[1].size == 5 && strlen(KS.ver[0].hash) == 64 &&
              !strcmp(KS.vpath, "keep/plan.txt"),
          "two versions: %u", KS.nvers);

    /* restore the first words beside the file */
    CHECK(keepsake_restore(&KS, 1, "keep/plan.txt", NULL) == ZXM_OK &&
              !strcmp(KS.landed, "keep/plan (restored).txt") && KS.restored == 1 &&
              strstr(LINK.req, "\"snapshot\":1") && !strstr(LINK.req, "\"to\""),
          "restored: %s", KS.landed);
    char got[16];
    uint32_t gn;
    CHECK(zxf_read("keep/plan (restored).txt", 0, (uint8_t *) got, sizeof got, false, &gn, &size) ==
                  0 &&
              gn == 5 && !memcmp(got, "first", 5),
          "the first words");
    CHECK(keepsake_restore(&KS, 0, "keep", "keep2") == ZXM_OK && !strcmp(KS.landed, "keep2") &&
              strstr(LINK.req, "\"to\":\"keep2\"") && !strstr(LINK.req, "snapshot"),
          "a folder somewhere new: %s", KS.landed);
    CHECK(keepsake_restore(&KS, 0, "keep", "keep2") == ZXM_E_REMOTE && LINK.err_code == -32012 &&
              !strcmp(KS.landed, "keep2"),
          "never over it");
    CHECK(keepsake_restore(&KS, 0, "nope.txt", NULL) == ZXM_E_REMOTE && LINK.err_code == -32015,
          "not backed up");
    static char longp[KEEPSAKE_PATH + 2];
    memset(longp, 'p', KEEPSAKE_PATH + 1);
    CHECK(keepsake_restore(&KS, 0, longp, NULL) == ZXM_E_ARG &&
              keepsake_restore(&KS, 0, "a", longp) == ZXM_E_ARG &&
              keepsake_versions(&KS, longp) == ZXM_E_ARG,
          "too long");
    CHECK(keepsake_restore(&KS, 0, NULL, NULL) == ZXM_OK && !strcmp(KS.landed, "Keepsake 4") &&
              KS.restored >= files && !strstr(LINK.req, "path"),
          "everything: %s %u", KS.landed, KS.restored);

    /* prune */
    CHECK(keepsake_prune(&KS, 0) == ZXM_E_ARG, "keep none");
    CHECK(keepsake_prune(&KS, 1) == ZXM_OK && KS.pruned_snaps == 3 && KS.nsnaps == 1 &&
              KS.snap[0].id == 4 && KS.total == 1,
          "pruned: %u, %u objects", KS.pruned_snaps, KS.pruned_objects);

    /* spaces and destinations forget what was listed */
    CHECK(keepsake_set_space(&KS, "studio") == ZXM_OK && KS.nsnaps == 0 && !KS.check.done &&
              !KS.vpath[0] && keepsake_refresh(&KS) == ZXM_OK && KS.total == 0 &&
              keepsake_set_space(&KS, "nowhere") == ZXM_OK &&
              keepsake_refresh(&KS) == ZXM_E_REMOTE && LINK.err_code == -32013,
          "another space: %d", LINK.err_code);
    static char longid[64];
    memset(longid, 'a', 49);
    CHECK(keepsake_set_space(&KS, longid) == ZXM_E_ARG && !strcmp(KS.space, "nowhere"),
          "id too long");
    CHECK(keepsake_set_space(&KS, "home") == ZXM_OK && !KS.space[0] &&
              keepsake_set_space(&KS, NULL) == ZXM_OK && keepsake_refresh(&KS) == ZXM_OK &&
              KS.nsnaps == 1,
          "home");

    /* made-up replies */
    keepsake_t F;
    keepsake_init(&F, &FAKE);
    keepsake_set_dest(&F, "/d");
    const char *badlist[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshots\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":0,\"snapshots\":{}}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":4294967296,\"snapshots\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":0,\"snapshots\":[{\"snapshot\":1,"
        "\"time\":1,\"files\":1,\"bytes\":1,\"damaged\":false}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[1]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"time\":1,"
        "\"files\":1,\"bytes\":1,\"damaged\":false}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"snapshot\":1,"
        "\"files\":1,\"bytes\":1,\"damaged\":false}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"snapshot\":1,"
        "\"time\":1,\"bytes\":1,\"damaged\":false}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"snapshot\":1,"
        "\"time\":1,\"files\":1,\"damaged\":false}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"snapshot\":1,"
        "\"time\":1,\"files\":1,\"bytes\":1}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1,\"snapshots\":[{\"snapshot\":"
        "4294967296,"
        "\"time\":1,\"files\":1,\"bytes\":1,\"damaged\":false}]}}",
    };
    F.nsnaps = 1;
    F.snap[0].id = 9;
    for (size_t i = 0; i < sizeof badlist / sizeof *badlist; i++) {
        CANNED = badlist[i];
        CHECK(keepsake_refresh(&F) == ZXM_E_PROTOCOL && F.nsnaps == 1 && F.snap[0].id == 9,
              "bad list %zu", i);
    }
    static char many[KEEPSAKE_SNAPS * 80 + 200];
    int m = snprintf(many, sizeof many,
                     "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":{\"total\":99,\"snapshots\":[");
    for (uint32_t i = 0; i <= KEEPSAKE_SNAPS; i++)
        m += snprintf(many + m, sizeof many - (size_t) m,
                      "%s{\"snapshot\":%u,\"time\":1,\"files\":1,\"bytes\":1,\"damaged\":false}",
                      i ? "," : "", i + 1);
    snprintf(many + m, sizeof many - (size_t) m, "]}}");
    CANNED = many;
    CHECK(keepsake_refresh(&F) == ZXM_E_PROTOCOL && F.nsnaps == 1, "too many");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":7,\"snapshots\":[{\"snapshot\":"
             "4294967295,\"time\":-2,\"files\":4294967295,\"bytes\":18446744073709551615,"
             "\"damaged\":true}]}}";
    CHECK(keepsake_refresh(&F) == ZXM_OK && F.nsnaps == 1 && F.total == 7 &&
              F.snap[0].id == 4294967295u && F.snap[0].time == -2 && F.snap[0].damaged &&
              F.snap[0].bytes == 18446744073709551615ull,
          "the largest");
    const char *badrun[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time\":1,\"files\":1,\"new_files\":0,"
        "\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":0,\"time\":1,\"files\":1,"
        "\"new_files\":0,\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":1,\"new_files\":0,"
        "\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"new_files\":0,"
        "\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"new_files\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"new_files\":0,\"skipped\":0,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"new_files\":0,\"skipped\":0,\"bytes\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"new_files\":2,\"skipped\":0,\"bytes\":1,\"new_bytes\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"time\":1,\"files\":1,"
        "\"new_files\":1,\"skipped\":0,\"bytes\":1,\"new_bytes\":2}}",
    };
    F.last.snapshot = 5;
    for (size_t i = 0; i < sizeof badrun / sizeof *badrun; i++) {
        CANNED = badrun[i];
        CHECK(keepsake_backup(&F, false) == ZXM_E_PROTOCOL && F.last.snapshot == 5, "bad run %zu",
              i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":6,\"time\":1,\"files\":2,"
             "\"new_files\":2,\"skipped\":3,\"bytes\":9,\"new_bytes\":9}}";
    F.check.done = true;
    CHECK(keepsake_backup(&F, false) == ZXM_E_PROTOCOL && F.last.snapshot == 6 &&
              F.last.skipped == 3 && F.last.new_bytes == 9 && !F.check.done,
          "made, but the list after it does not parse");
    const char *badver[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[1]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[{\"snapshot\":1,\"time\":1,"
        "\"size\":1,\"mtime\":1,\"hash\":\"ab\"}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[{\"time\":1,\"size\":1,"
        "\"mtime\":1,\"hash\":\"ab\"}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[{\"snapshot\":1,\"size\":1,"
        "\"mtime\":1,\"hash\":\"ab\"}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[{\"snapshot\":1,\"time\":1,"
        "\"mtime\":1,\"hash\":\"ab\"}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"versions\":[{\"snapshot\":1,\"time\":1,"
        "\"size\":1,\"hash\":\"ab\"}]}}",
    };
    F.nvers = 0;
    for (size_t i = 0; i < sizeof badver / sizeof *badver; i++) {
        CANNED = badver[i];
        CHECK(keepsake_versions(&F, "a") == ZXM_E_PROTOCOL && F.nvers == 0 && !F.vpath[0],
              "bad versions %zu", i);
    }
    static char vmany[KEEPSAKE_VERS * 160 + 200];
    m = snprintf(vmany, sizeof vmany, "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":{\"versions\":[");
    for (uint32_t i = 0; i < KEEPSAKE_VERS + 3; i++)
        m += snprintf(vmany + m, sizeof vmany - (size_t) m,
                      "%s{\"snapshot\":%u,\"time\":1,\"size\":2,\"mtime\":-3,\"hash\":\"%064u\"}",
                      i ? "," : "", i + 1, i);
    snprintf(vmany + m, sizeof vmany - (size_t) m, "]}}");
    CANNED = vmany;
    CHECK(keepsake_versions(&F, "a") == ZXM_OK && F.nvers == KEEPSAKE_VERS &&
              F.ver[KEEPSAKE_VERS - 1].snapshot == KEEPSAKE_VERS && F.ver[0].mtime == -3 &&
              !strcmp(F.vpath, "a"),
          "the first fifty kept: %u", F.nvers);
    const char *badcheck[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":0,"
        "\"missing\":0,\"bytes\":1,\"ok\":false,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":1,"
        "\"missing\":0,\"bytes\":1,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":3,"
        "\"missing\":0,\"bytes\":1,\"ok\":false,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":1,"
        "\"missing\":2,\"bytes\":1,\"ok\":false,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"files\":2,\"bad\":0,"
        "\"missing\":0,\"bytes\":1,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"bad\":0,"
        "\"missing\":0,\"bytes\":1,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,"
        "\"missing\":0,\"bytes\":1,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":0,"
        "\"bytes\":1,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":0,"
        "\"missing\":0,\"ok\":true,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":0,"
        "\"missing\":0,\"bytes\":1,\"first_bad\":\"\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":1,\"files\":2,\"bad\":0,"
        "\"missing\":0,\"bytes\":1,\"ok\":true}}",
    };
    F.check.done = false;
    for (size_t i = 0; i < sizeof badcheck / sizeof *badcheck; i++) {
        CANNED = badcheck[i];
        CHECK(keepsake_check(&F, 0) == ZXM_E_PROTOCOL && !F.check.done, "bad check %zu", i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":4,\"files\":3,\"bad\":1,"
             "\"missing\":2,\"bytes\":10,\"ok\":false,\"first_bad\":\"Notes/a.txt\"}}";
    F.nsnaps = 1;
    F.snap[0].id = 4;
    CHECK(keepsake_check(&F, 4) == ZXM_OK && F.check.done && F.check.bad == 1 &&
              F.check.missing == 2 && F.check.bytes == 10 &&
              !strcmp(F.check.first_bad, "Notes/a.txt") && !keepsake_tested(&F),
          "a failed check");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshot\":4,\"files\":3,\"bad\":0,"
             "\"missing\":0,\"bytes\":10,\"ok\":true,\"first_bad\":\"\"}}";
    keepsake_check_t keepc = F.check;
    CHECK(keepsake_check(&F, 4) == ZXM_OK && keepsake_tested(&F), "a good one");
    F.nsnaps = 0;
    CHECK(!keepsake_tested(&F), "nothing listed: not tested");
    F.nsnaps = 1;
    F.check = keepc;
    const char *badrestore[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"files\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"to\":\"\",\"files\":1}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"to\":\"x\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"to\":\"x\",\"files\":0}}",
    };
    F.landed[0] = 0;
    for (size_t i = 0; i < sizeof badrestore / sizeof *badrestore; i++) {
        CANNED = badrestore[i];
        CHECK(keepsake_restore(&F, 0, "a", NULL) == ZXM_E_PROTOCOL && !F.landed[0],
              "bad restore %zu", i);
    }
    const char *badprune[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"objects_removed\":0,\"freed\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshots_removed\":0,\"freed\":0}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshots_removed\":0,\"objects_removed\":0}"
        "}",
    };
    F.pruned_snaps = 77;
    for (size_t i = 0; i < sizeof badprune / sizeof *badprune; i++) {
        CANNED = badprune[i];
        CHECK(keepsake_prune(&F, 1) == ZXM_E_PROTOCOL && F.pruned_snaps == 77, "bad prune %zu", i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"snapshots_removed\":2,"
             "\"objects_removed\":3,\"freed\":18446744073709551615}}";
    CHECK(keepsake_prune(&F, 1) == ZXM_E_PROTOCOL && F.pruned_snaps == 2 && F.pruned_objects == 3 &&
              F.freed == 18446744073709551615ull,
          "pruned, the list after it does not parse");
    CANNED = NULL;
    CHECK(keepsake_refresh(&F) == ZXM_E_TRANSPORT &&
              keepsake_backup(&F, false) == ZXM_E_TRANSPORT &&
              keepsake_versions(&F, "a") == ZXM_E_TRANSPORT &&
              keepsake_check(&F, 0) == ZXM_E_TRANSPORT &&
              keepsake_restore(&F, 0, "a", NULL) == ZXM_E_TRANSPORT &&
              keepsake_prune(&F, 1) == ZXM_E_TRANSPORT,
          "no daemon");

    /* AI explains a failed check */
    F.link = &LINK;
    CHECK(keepsake_ai(&F, -1) == ZXM_E_ARG && keepsake_ai(&F, KEEPSAKE_AI_COUNT) == ZXM_E_ARG,
          "range");
    ANSWER = "Two copies are gone; run a full backup.";
    asked = 0;
    CHECK(keepsake_ai(&F, KEEPSAKE_AI_EXPLAIN) == ZXM_OK && asked == 1 &&
              F.held.state == ZXM_HELD_PENDING &&
              strstr(last_prompt, "Backup 4 was checked: 3 files, 1 read back wrong, 2 missing.\n"
                                  "First problem: Notes/a.txt\n") &&
              strstr(last_prompt, "full backup"),
          "held: %s", last_prompt);
    CHECK(keepsake_ai_accept(&F) == ZXM_OK && F.held.state == ZXM_HELD_ACCEPTED, "read");
    F.check.bad = 0;
    CHECK(keepsake_ai(&F, KEEPSAKE_AI_EXPLAIN) == ZXM_OK && keepsake_ai_reject(&F) == ZXM_OK &&
              F.held.state == ZXM_HELD_REJECTED,
          "only missing: still explained, then dismissed");
    F.check.missing = 0;
    CHECK(keepsake_ai(&F, KEEPSAKE_AI_EXPLAIN) == ZXM_E_STATE, "nothing wrong");
    F.check.missing = 1;
    F.check.done = false;
    CHECK(keepsake_ai(&F, KEEPSAKE_AI_EXPLAIN) == ZXM_E_STATE, "no check yet");
    F.check.done = true;
    memset(F.check.first_bad, 'f', sizeof F.check.first_bad - 1);
    CHECK(keepsake_ai(&F, KEEPSAKE_AI_EXPLAIN) == ZXM_OK, "a long path fits");
    ANSWER = "ok";
    CHECK(rm_tree(dest) == 0, "cleanup");
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
    test_utilities();
    test_scent();
    test_platen();
    test_keepsake();
    free(OUT.p);
    if (rm_tree(root) != 0) printf("  (could not remove %s)\n", root);
    if (fails) {
        printf("[FAIL] test_apps: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_apps: %d checks\n", checks);
    return 0;
}
