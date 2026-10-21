/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_ai_engine.c MODEL.gguf [MAX_NEW] — the browser's engine
 * (zxv_ai_engine.c), built natively, answers exactly as the app does: for
 * each prompt below, the reply streamed piece by piece is the app's reply
 * (zxv_zt_glue.c, zxv_zt_generate_n) token for token, each piece is whole
 * UTF-8, and the logits digest is the same on the C reference as on the
 * engine the determiner chose. It prints one ZW line per prompt (ids and
 * digest), which the browser builds must reproduce (test_zxv_ai.mjs). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L /* setenv */
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_gguf.h"
#include "zt_tok.h"
#include "zxv_model_host.h"

/* zxv_ai_engine.c */
int32_t zw_load(const uint8_t *buf, uint32_t len);
const char *zw_error(void);
const char *zw_device(void);
const char *zw_record(void);
int32_t zw_start(const char *prompt, uint32_t max_new);
int32_t zw_next(void);
const char *zw_text(void);
const char *zw_stop(void);
uint32_t zw_n_gen(void);
const char *zw_digest(void);
void zw_free(void);
uint64_t zw_whole_utf8(const char *s, uint64_t n);

/* the prompts every build answers (test_zxv_ai.mjs has the same list) */
static const char *const PROMPTS[] = {
    "Hello! Who are you?",
    "Integer math is exact.",
    "Gr\xc3\xbc\xc3\x9f"
    "e aus K\xc3\xb6ln, \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\xa6\x8a",
};
#define N_PROMPTS (sizeof PROMPTS / sizeof PROMPTS[0])

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0;                                                                     \
        checks++;                                                                                  \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static uint8_t *read_all(const char *path, uint64_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t) n)) &&
            fread(b, 1, (size_t) n, f) != (size_t) n) {
            free(b);
            b = NULL;
        }
        *len = n > 0 ? (uint64_t) n : 0;
    }
    fclose(f);
    return b;
}

/* whether a cut of s at b falls inside a whole, valid UTF-8 character */
static bool splits_char(const uint8_t *s, uint64_t len, uint64_t b)
{
    if (b == 0 || b >= len || (s[b] & 0xC0u) != 0x80u) return false;
    for (uint64_t j = b; j-- > 0 && b - j <= 3;) {
        uint8_t l = s[j];
        if ((l & 0xC0u) == 0x80u) continue;
        uint64_t need = l >= 0xF8u ? 0 : l >= 0xF0u ? 4 : l >= 0xE0u ? 3 : l >= 0xC0u ? 2 : 0;
        if (!need || j + need <= b || j + need > len) return false;
        for (uint64_t k = j + 1; k < j + need; k++)
            if ((s[k] & 0xC0u) != 0x80u) return false;
        return true;
    }
    return false;
}

typedef struct {
    char text[8192];
    int32_t ids[512];
    uint32_t n;
    int32_t n_prompt;
    char digest[17], stop[8];
    uint64_t cuts[512];
    uint32_t n_cuts;
} reply_t;

static void zw_reply(const char *prompt, uint32_t max_new, reply_t *r)
{
    memset(r, 0, sizeof *r);
    r->n_prompt = zw_start(prompt, max_new);
    if (r->n_prompt < 0) return;
    for (;;) {
        int32_t t = zw_next();
        if (t >= 0 && r->n < 512) r->ids[r->n++] = t;
        const char *piece = zw_text();
        strncat(r->text, piece, sizeof r->text - strlen(r->text) - 1);
        if (r->n_cuts < 512) r->cuts[r->n_cuts++] = strlen(r->text);
        if (t < 0) break;
    }
    snprintf(r->digest, sizeof r->digest, "%s", zw_digest());
    snprintf(r->stop, sizeof r->stop, "%s", zw_stop());
}

/* the app's reply to prompt: its encoding (zxv_model_host.c), then the glue */
static int32_t app_reply(const zt_gguf_t *g, const zt_tok_t *tok, const char *prompt,
                         uint32_t max_new, char *out, size_t cap)
{
    uint64_t len = strlen(prompt), n = 0, off = 0, wb = zt_tok_work_bytes(len);
    int32_t *ids = malloc((size_t) (len + 2) * sizeof *ids);
    void *work = malloc((size_t) (wb ? wb : 1));
    int32_t r = -1;
    char err[200] = "";
    if (ids && work) {
        if (tok->add_bos && tok->bos >= 0) ids[off++] = tok->bos;
        if (zt_tok_encode(tok, (const uint8_t *) prompt, len, false, ids + off, len + 2 - off, &n,
                          work, wb) == 0)
            r = zxv_zt_generate_n(g, tok, ids, n + off, max_new, out, cap, err, sizeof err);
    }
    if (r < 0) printf("  (app: %s)\n", err);
    free(ids);
    free(work);
    return r;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_zxv_ai_engine MODEL.gguf [MAX_NEW]\n");
        return 2;
    }
    uint32_t max_new = argc > 2 ? (uint32_t) strtoul(argv[2], NULL, 10) : 24u;
    uint64_t len = 0;
    uint8_t *buf = read_all(argv[1], &len);
    CHECK(buf && len > 0 && len <= UINT32_MAX, "%s read (%llu bytes)", argv[1],
          (unsigned long long) len);
    if (!buf) return 1;
    setenv("ZT_CHOICE_CACHE", "off", 1);

    /* a character a token leaves unfinished waits for the next token */
    static const struct {
        const char *s;
        uint64_t n, out;
    } U[] = {
        {"", 0, 0},          {"abc", 3, 3},           {"a\xC3", 2, 1},
        {"a\xC3\xA9", 3, 3}, {"\xE6\x9D", 2, 0},      {"\xE6\x9D\xB1", 3, 3},
        {"x\xF0", 2, 1},     {"x\xF0\x9F\xA6", 4, 1}, {"x\xF0\x9F\xA6\x8A", 5, 5},
        {"\x80\x80", 2, 2}, /* stray continuation bytes: nothing to wait for */
    };
    bool held = true;
    for (size_t i = 0; i < sizeof U / sizeof U[0]; i++)
        if (zw_whole_utf8(U[i].s, U[i].n) != U[i].out) {
            held = false;
            printf("    zw_whole_utf8 case %zu: %llu, want %llu\n", i,
                   (unsigned long long) zw_whole_utf8(U[i].s, U[i].n),
                   (unsigned long long) U[i].out);
        }
    CHECK(held, "an unfinished UTF-8 character is held back, a whole one given out (%zu cases)",
          sizeof U / sizeof U[0]);

    /* refusals */
    CHECK(zw_start("hi", 4) < 0 && zw_error()[0], "a request before any model is refused (%s)",
          zw_error());
    uint8_t junk[64] = "not a model at all";
    CHECK(zw_load(junk, sizeof junk) < 0 && zw_error()[0],
          "bytes that are not a GGUF are refused (%s)", zw_error());

    CHECK(zw_load(buf, (uint32_t) len) == 0, "the model loads (%s)", zw_error());
    printf("[INFO] device: %s\n[INFO] choice: %s\n", zw_device(), zw_record());
    CHECK(!strncmp(zw_record(), "zt-choice/1 cpu=", 16), "the determiner chose its engine");
    {
        static char longp[1 << 16];
        memset(longp, 'a', sizeof longp - 1);
        CHECK(zw_start(longp, 4) < 0, "a prompt longer than the context is refused (%s)",
              zw_error());
    }

    /* the app's replies, from the same file */
    zt_gguf_t g;
    zt_tok_t tok;
    uint64_t tb = 0;
    void *tar = NULL;
    bool app_ok = zt_gguf_open(&g, buf, len) == 0 && (tb = zt_tok_arena_bytes(&g)) > 0 &&
                  (tar = malloc((size_t) tb)) && zt_tok_load(&tok, &g, tar, tb) == 0;
    CHECK(app_ok, "the app's tokenizer reads the file");

    static reply_t mine[N_PROMPTS];
    for (uint32_t i = 0; i < N_PROMPTS && app_ok; i++) {
        reply_t *r = &mine[i];
        zw_reply(PROMPTS[i], max_new, r);
        static char app[8192];
        int32_t an = app_reply(&g, &tok, PROMPTS[i], max_new, app, sizeof app);
        /* the ids' text: as the app writes it (a C string, cut at a NUL), and
         * as the stream gives it (NUL bytes left out) */
        static uint8_t dec[8192];
        static char as_app[8192], as_stream[8192];
        uint64_t dn = 0, sn = 0;
        zt_tok_decode(&tok, r->ids, r->n, dec, sizeof dec - 1, &dn);
        dec[dn] = 0;
        snprintf(as_app, sizeof as_app, "%s", (const char *) dec);
        for (uint64_t k = 0; k < dn; k++)
            if (dec[k]) as_stream[sn++] = (char) dec[k];
        as_stream[sn] = 0;
        CHECK(r->n_prompt > 0 && an >= 0 && (uint32_t) an == r->n && !strcmp(app, as_app),
              "prompt %u: the reply is the app's (%u tokens, stop %s)", (unsigned) i,
              (unsigned) r->n, r->stop);
        if (strcmp(app, as_app)) printf("    app  \"%s\"\n    mine \"%s\"\n", app, as_app);
        bool whole = !strcmp(r->text, as_stream);
        for (uint32_t k = 0; k < r->n_cuts; k++)
            if (splits_char((const uint8_t *) r->text, strlen(r->text), r->cuts[k])) whole = false;
        CHECK(whole, "prompt %u: the pieces streamed make the reply, none cut inside a character",
              (unsigned) i);
        printf("ZW %u n_prompt=%d n_gen=%u stop=%s digest=%s ids=", (unsigned) i, (int) r->n_prompt,
               (unsigned) r->n, r->stop, r->digest);
        for (uint32_t k = 0; k < r->n; k++) printf("%s%d", k ? "," : "", (int) r->ids[k]);
        printf("\n");
    }

    /* the same digests on the C reference alone */
    zw_free();
    setenv("ZT_CHOICE", "cpu=c/1 gpu=none", 1);
    CHECK(zw_load(buf, (uint32_t) len) == 0 && strstr(zw_record(), "cpu=c/1 gpu=none"),
          "reloaded on the C reference (%s)", zw_device());
    bool same = true;
    for (uint32_t i = 0; i < N_PROMPTS && app_ok; i++) {
        reply_t r;
        zw_reply(PROMPTS[i], max_new, &r);
        if (strcmp(r.digest, mine[i].digest) || r.n != mine[i].n ||
            memcmp(r.ids, mine[i].ids, r.n * sizeof r.ids[0]))
            same = false;
    }
    CHECK(same, "the C reference gives the same ids and logits digests as the chosen engine");
    unsetenv("ZT_CHOICE");
    zw_free();
    zxv_zt_release();
    free(tar);
    free(buf);
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
