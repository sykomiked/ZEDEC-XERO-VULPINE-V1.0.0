/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_zt_tok.c — the byte-level BPE tokenizer against the Hugging Face
 * reference (test_tok_fixture.h, from gen_tok_fixture.py).
 *
 *   gcc -std=c11 -Wall -Werror -Wextra -Isrc/tensor src/tensor/test_zt_tok.c \
 *       src/tensor/zt_tok.c src/tensor/zt_gguf.c src/tensor/zt.c -o /tmp/test_zt_tok \
 *       && /tmp/test_zt_tok
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_tok.h"
#include "test_tok_fixture.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static uint8_t *arena;
static uint8_t work[36 * 70000 + 64];
static int32_t ids[70000];
static uint8_t text[70000];

static bool load(zt_tok_t *t, const uint8_t *buf, uint64_t size, int32_t *err)
{
    zt_gguf_t g;
    *err = zt_gguf_open(&g, buf, size);
    if (*err) return false;
    uint64_t need = zt_tok_arena_bytes(&g);
    free(arena);
    arena = malloc(need ? need : 1);
    *err = zt_tok_load(t, &g, arena, need);
    return *err == ZT_GGUF_OK;
}

static bool roundtrip(const zt_tok_t *t, const uint8_t *s, uint64_t n, bool special)
{
    static uint8_t back[70000];
    uint64_t ni = 0, nb = 0;
    if (zt_tok_encode(t, s, n, special, ids, 70000, &ni, work, sizeof work)) return false;
    if (zt_tok_decode(t, ids, ni, back, sizeof back, &nb)) return false;
    return nb == n && memcmp(back, s, n) == 0;
}

static uint32_t rs = 20261009;
static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static uint8_t *find_bytes(uint8_t *hay, uint64_t n, const char *needle)
{
    uint64_t l = strlen(needle);
    for (uint64_t i = 0; i + l <= n; i++)
        if (!memcmp(hay + i, needle, l)) return hay + i;
    return NULL;
}

int main(void)
{
    printf("=== T19 byte-level BPE tokenizer ===\n");
    const uint8_t *blob = (const uint8_t *) TOK_GGUF;
    zt_tok_t t;
    int32_t err;
    CHECK(load(&t, blob, TOK_GGUF_LEN, &err), "loads the GGUF vocabulary and merges");
    CHECK(t.n_vocab == TOK_N_VOCAB, "vocabulary size matches");
    CHECK(t.n_merges == 941, "all 941 merges indexed");
    CHECK(t.n_special == 3, "three control tokens recognised as special");
    CHECK(t.pre == ZT_TOK_PRE_QWEN2, "pre-tokenizer read as qwen2");
    CHECK(t.bos == 0 && t.eos == 2 && !t.add_bos, "BOS/EOS ids and add_bos read");
    CHECK(zt_tok_find(&t, (const uint8_t *) "<|im_end|>", 10) == t.eos,
          "find: exact string lookup");
    CHECK(zt_tok_find(&t, (const uint8_t *) "no such token here", 18) == -1,
          "find: absent string is -1");

    /* the reference encodings, both pre-tokenizers */
    const struct {
        uint32_t pre;
        const int32_t *ids;
        const uint32_t *nids;
        const char *name;
    } modes[2] = {{ZT_TOK_PRE_QWEN2, TOK_IDS_QWEN2, TOK_NIDS_QWEN2, "qwen2"},
                  {ZT_TOK_PRE_LLAMA3, TOK_IDS_LLAMA_BPE, TOK_NIDS_LLAMA_BPE, "llama-bpe"}};
    for (int m = 0; m < 2; m++) {
        zt_tok_set_pre(&t, modes[m].pre);
        uint32_t off = 0, bad = 0, badrt = 0;
        for (uint32_t k = 0; k < TOK_N_TEXTS; k++) {
            uint64_t ni = 0;
            int32_t r = zt_tok_encode(&t, (const uint8_t *) TOK_TEXTS[k].s, TOK_TEXTS[k].len, true,
                                      ids, 70000, &ni, work, sizeof work);
            bool same = r == 0 && ni == modes[m].nids[k];
            for (uint64_t i = 0; same && i < ni; i++) same = ids[i] == modes[m].ids[off + i];
            if (!same) {
                printf("       %s text %u differs (got %llu ids, want %u):", modes[m].name, k,
                       (unsigned long long) ni, modes[m].nids[k]);
                for (uint64_t i = 0; i < ni && i < 24; i++) printf(" %d", ids[i]);
                printf("\n       want:");
                for (uint32_t i = 0; i < modes[m].nids[k] && i < 24; i++)
                    printf(" %d", modes[m].ids[off + i]);
                printf("\n");
                bad++;
            }
            if (!roundtrip(&t, (const uint8_t *) TOK_TEXTS[k].s, TOK_TEXTS[k].len, true)) badrt++;
            off += modes[m].nids[k];
        }
        char msg[128];
        snprintf(msg, sizeof msg, "%s: all %u texts encode to the reference ids exactly",
                 modes[m].name, TOK_N_TEXTS);
        CHECK(bad == 0, msg);
        snprintf(msg, sizeof msg, "%s: decode(encode(text)) == text for all texts", modes[m].name);
        CHECK(badrt == 0, msg);
    }
    zt_tok_set_pre(&t, ZT_TOK_PRE_QWEN2);

    /* specials as plain text when parse_special is false */
    {
        const char *s = "<|im_start|>hi";
        uint64_t ni = 0;
        zt_tok_encode(&t, (const uint8_t *) s, strlen(s), false, ids, 64, &ni, work, sizeof work);
        bool none = ni > 1;
        for (uint64_t i = 0; i < ni; i++)
            if (ids[i] == 1) none = false;
        CHECK(none, "parse_special=false tokenises special text as ordinary text");
        CHECK(roundtrip(&t, (const uint8_t *) s, strlen(s), false), "and it still round-trips");
    }

    /* robustness: invalid UTF-8, random bytes, long runs */
    {
        const uint8_t bad[] = {'a',  0xFF, 0xC0, 0xAF, 'b', 0xED, 0xA0,
                               0x80, 0xF0, 0x9F, 0x98, ' ', 0xE2, 0x82};
        CHECK(roundtrip(&t, bad, sizeof bad, true),
              "invalid and truncated UTF-8 round-trips byte for byte");
        uint32_t fails = 0;
        for (int it = 0; it < 300; it++) {
            uint32_t n = 1 + rnd() % 400;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t r = rnd();
                text[i] = (r & 3) ? (uint8_t) (32 + (r >> 8) % 95) : (uint8_t) (r >> 16);
            }
            for (int m = 1; m <= 2; m++) {
                zt_tok_set_pre(&t, (uint32_t) m);
                if (!roundtrip(&t, text, n, (it & 1) != 0)) fails++;
            }
        }
        zt_tok_set_pre(&t, ZT_TOK_PRE_QWEN2);
        CHECK(fails == 0, "300 random byte strings round-trip in both modes");
        memset(text, ' ', 60000);
        text[60000] = 'x';
        CHECK(roundtrip(&t, text, 60001, true),
              "a 60000-space run encodes (heap merge, no stall) and round-trips");
        memset(text, 'e', 60000);
        CHECK(roundtrip(&t, text, 60000, true), "a 60000-letter word encodes and round-trips");
    }

    /* limits and errors */
    {
        uint64_t ni = 99;
        const char *s = "Hello world, hello again";
        int32_t r =
            zt_tok_encode(&t, (const uint8_t *) s, strlen(s), true, ids, 2, &ni, work, sizeof work);
        CHECK(r == ZT_TOK_ESPACE && ni == 2,
              "a too-small id buffer reports ESPACE and keeps what fit");
        r = zt_tok_encode(&t, (const uint8_t *) s, strlen(s), true, ids, 64, &ni, work, 16);
        CHECK(r == ZT_TOK_ESPACE, "a too-small work area is refused");
        CHECK(zt_tok_encode(&t, (const uint8_t *) "", 0, true, ids, 0, &ni, NULL, 0) == 0 &&
                  ni == 0,
              "empty text needs no buffers");
        int32_t badid[2] = {5, (int32_t) TOK_N_VOCAB};
        uint8_t out[64];
        uint64_t no;
        CHECK(zt_tok_decode(&t, badid, 2, out, sizeof out, &no) == ZT_TOK_EBADID,
              "decode rejects an out-of-range id");
        int32_t two[2] = {5, 6};
        CHECK(zt_tok_decode(&t, two, 2, out, 1, &no) == ZT_TOK_ESPACE && no == 1,
              "decode honours its output cap");
    }

    /* files the tokenizer must refuse rather than misread */
    {
        uint8_t *copy = malloc(TOK_GGUF_LEN);
        memcpy(copy, blob, TOK_GGUF_LEN);
        zt_tok_t u;
        zt_gguf_t g;
        zt_gguf_open(&g, copy, TOK_GGUF_LEN);
        CHECK(zt_tok_load(&u, &g, arena, 16) == ZT_TOK_ESPACE, "a too-small arena is refused");
        /* each string value follows its key */
        uint8_t *pk = find_bytes(copy, TOK_GGUF_LEN, "tokenizer.ggml.pre");
        uint8_t *pv = pk ? find_bytes(pk, TOK_GGUF_LEN - (uint64_t) (pk - copy), "qwen2") : NULL;
        uint8_t *mk = find_bytes(copy, TOK_GGUF_LEN, "tokenizer.ggml.model");
        uint8_t *q = mk ? find_bytes(mk, TOK_GGUF_LEN - (uint64_t) (mk - copy), "gpt2") : NULL;
        if (pv) pv[4] = 'X';
        CHECK(pv && !load(&u, copy, TOK_GGUF_LEN, &err) && err == ZT_GGUF_EUNSUPPORTED,
              "an unknown pre-tokenizer is reported unsupported");
        if (pv) pv[4] = '2';
        if (q) q[3] = 'X';
        CHECK(q && !load(&u, copy, TOK_GGUF_LEN, &err) && err == ZT_GGUF_EUNSUPPORTED,
              "a non-BPE tokenizer model is reported unsupported");
        free(copy);
    }

    free(arena);
    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
