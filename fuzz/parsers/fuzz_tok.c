/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_tok.c — the byte-level BPE tokenizer (tensor/zt_tok.c) on a hostile
 * vocabulary and hostile text. fuzz_gguf_loader covers the GGUF container;
 * here the container is always well formed and what is hostile is what it
 * says: token strings, token types, merges, special tokens, BOS/EOS ids.
 *
 * The input builds a GGUF file in memory (exact-size heap buffer, so ASan
 * sees any read past it): the 256 GPT-2 byte-alphabet tokens, rotated to a
 * fuzz-chosen start id, then up to 192 extra tokens and merges:
 *   - a concatenation of two existing tokens, with the merge "A B" (the
 *     merges come out in a fuzz-chosen order, with duplicates);
 *   - a special token (CONTROL or USER_DEFINED) whose text starts with a raw
 *     control byte 0x01..0x08, which never occurs in a byte-alphabet
 *     spelling, so it can only enter the ids through special matching;
 *   - in HOSTILE mode: raw strings of any bytes and any type, empty tokens,
 *     duplicate strings, merges naming unknown tokens, a short token_type
 *     array, BOS/EOS ids from fz_edge64.
 * The rest of the input makes the text: mostly whole tokens' bytes (so the
 * merges fire) mixed with literal bytes, or (mode bit 2) the raw rest.
 *
 * Invariants checked:
 *   K1  zt_tok_load either refuses or builds tables inside an arena of
 *       exactly zt_tok_arena_bytes; bos/eos are -1 or a valid id
 *   K2  zt_tok_encode with a work area of exactly 36 * len bytes and an id
 *       buffer of exactly the needed size: OK, every id in [0, n_vocab),
 *       deterministic (twice, same ids)
 *   K3  round trip (not HOSTILE): decode(encode(text)) == text, with and
 *       without special-token parsing (zt_tok.h: "every input round-trips")
 *   K4  a too-small id buffer gives ZT_TOK_ESPACE with *n_ids <= cap and a
 *       prefix of the full encoding; a too-small work area gives
 *       ZT_TOK_ESPACE and writes nothing
 *   K5  decode into an exact-size buffer works and one byte less gives
 *       ZT_TOK_ESPACE with a prefix; ids from the input (any int32) are
 *       either decoded or refused with ZT_TOK_EBADID, never read out of
 *       bounds
 *   K6  zt_tok_find of every token string that is the first with its
 *       spelling returns its id */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_in.h"
#include "zt_gguf.h"
#include "zt_tok.h"

#define EXTRA_MAX 192u
#define TOK_MAX   (256u + EXTRA_MAX)
#define STR_MAX   48u

/* ------------------------------------------------------------ GGUF writer */
typedef struct {
    uint8_t *p;
    size_t n, cap;
} wbuf_t;

static void w_raw(wbuf_t *w, const void *d, size_t n)
{
    if (w->n + n > w->cap) {
        size_t c = w->cap ? w->cap * 2 : 4096;
        while (c < w->n + n) c *= 2;
        w->p = (uint8_t *) realloc(w->p, c);
        if (!w->p) abort();
        w->cap = c;
    }
    memcpy(w->p + w->n, d, n);
    w->n += n;
}
static void w_u32(wbuf_t *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
    w_raw(w, b, 4);
}
static void w_u64(wbuf_t *w, uint64_t v)
{
    w_u32(w, (uint32_t) v);
    w_u32(w, (uint32_t) (v >> 32));
}
static void w_str(wbuf_t *w, const void *s, size_t n)
{
    w_u64(w, n);
    w_raw(w, s, n);
}
static void w_key(wbuf_t *w, const char *k, uint32_t type)
{
    w_str(w, k, strlen(k));
    w_u32(w, type);
}

/* ------------------------------------------------------------ the vocab */
typedef struct {
    uint8_t s[STR_MAX];
    uint32_t len;
    uint8_t type;
} vtok_t;

static vtok_t g_tok[TOK_MAX];
static uint8_t g_raw[TOK_MAX][STR_MAX]; /* the text bytes each token stands for */
static uint32_t g_rawlen[TOK_MAX];
static uint32_t g_nt;
static uint8_t g_mtxt[2 * TOK_MAX][2 * STR_MAX + 1];
static uint32_t g_mlen[2 * TOK_MAX], g_nm;

static bool byte_is_self(uint32_t b)
{
    return (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || b >= 174;
}

static uint32_t enc_cp(uint32_t c, uint8_t *o)
{
    if (c < 0x80) return o[0] = (uint8_t) c, 1;
    o[0] = (uint8_t) (0xC0 | (c >> 6));
    o[1] = (uint8_t) (0x80 | (c & 0x3F));
    return 2;
}

static void add_merge(const vtok_t *a, const vtok_t *b)
{
    if (g_nm >= 2 * TOK_MAX) return;
    uint8_t *m = g_mtxt[g_nm];
    memcpy(m, a->s, a->len);
    m[a->len] = ' ';
    memcpy(m + a->len + 1, b->s, b->len);
    g_mlen[g_nm++] = a->len + 1 + b->len;
}

/* a merge part: often a letter or an earlier extra, so merges chain and fire
 * inside one pre-split piece */
static uint32_t pick_part(fz_in *in, uint32_t rot)
{
    uint8_t c = fz_u8(in);
    if (c < 0x60u) return (uint32_t) (('a' + c % 26u - rot) & 255u);
    if (c < 0xC0u && g_nt > 256) return 256u + fz_u8(in) % (g_nt - 256u);
    return fz_u16(in) % g_nt;
}

static uint8_t *build_gguf(fz_in *in, bool hostile, size_t *out_n)
{
    uint8_t flags = fz_u8(in);
    vtok_t alpha[256];
    uint32_t extra = 0;
    for (uint32_t b = 0; b < 256; b++) {
        uint32_t cp = byte_is_self(b) ? b : 256 + extra++;
        alpha[b].len = enc_cp(cp, alpha[b].s);
        alpha[b].type = 1;
    }
    /* the alphabet at ids rot .. rot+255 (mod), extras fill in after */
    g_nt = 0;
    g_nm = 0;
    uint32_t n_extra = fz_u8(in) % (EXTRA_MAX + 1u);
    uint32_t rot = fz_u8(in);
    for (uint32_t b = 0; b < 256; b++) {
        g_raw[g_nt][0] = (uint8_t) ((b + rot) & 255u);
        g_rawlen[g_nt] = 1;
        g_tok[g_nt++] = alpha[(b + rot) & 255u];
    }
    for (uint32_t e = 0; e < n_extra && in->n > 0; e++) {
        vtok_t *t = &g_tok[g_nt];
        uint8_t kind = fz_u8(in);
        if (!hostile || kind < 0xA0u) {
            if ((kind & 7u) == 7u) { /* special */
                t->s[0] = (uint8_t) (1u + fz_u8(in) % 8u);
                t->len = 1u + (uint32_t) fz_bytes(in, t->s + 1, fz_u8(in) % 8u);
                t->type = (kind & 8u) ? 3 : 4;
                for (uint32_t q = 0; q < t->len; q++) /* a merge line splits at its first space */
                    if (t->s[q] == ' ') t->s[q] = '!';
                memcpy(g_raw[g_nt], t->s, t->len);
                g_rawlen[g_nt] = t->len;
            } else { /* a concatenation + its merge */
                uint32_t ia = pick_part(in, rot), ib = pick_part(in, rot);
                const vtok_t *a = &g_tok[ia], *b = &g_tok[ib];
                if (a->len + b->len > STR_MAX) continue;
                memcpy(g_raw[g_nt], g_raw[ia], g_rawlen[ia]);
                memcpy(g_raw[g_nt] + g_rawlen[ia], g_raw[ib], g_rawlen[ib]);
                g_rawlen[g_nt] = g_rawlen[ia] + g_rawlen[ib];
                memcpy(t->s, a->s, a->len);
                memcpy(t->s + a->len, b->s, b->len);
                t->len = a->len + b->len;
                t->type = 1;
                add_merge(a, b);
                if ((kind & 0x30u) == 0x30u) add_merge(a, b); /* duplicate merge */
            }
        } else {
            t->len = (uint32_t) fz_bytes(in, t->s, fz_u8(in) % 12u);
            memcpy(g_raw[g_nt], t->s, t->len);
            g_rawlen[g_nt] = t->len;
            t->type = fz_u8(in) % 8u;
            if (kind & 1u) { /* a hostile merge string */
                const vtok_t *a = &g_tok[fz_u16(in) % g_nt];
                add_merge(a, t);
            }
        }
        g_nt++;
    }
    /* shuffle the merge ranks a little */
    for (uint32_t i = 0; i + 1 < g_nm && (fz_u8(in) & 1u); i++) {
        uint32_t j = i + fz_u8(in) % (g_nm - i);
        uint8_t tmp[2 * STR_MAX + 1];
        uint32_t tl = g_mlen[i];
        memcpy(tmp, g_mtxt[i], tl);
        memcpy(g_mtxt[i], g_mtxt[j], g_mlen[j]);
        g_mlen[i] = g_mlen[j];
        memcpy(g_mtxt[j], tmp, tl);
        g_mlen[j] = tl;
    }

    wbuf_t w = {0};
    uint64_t nkv = 5 + ((flags & 2u) ? 3 : 0);
    w_raw(&w, "GGUF", 4);
    w_u32(&w, 3);
    w_u64(&w, 0);
    w_u64(&w, nkv);
    w_key(&w, "tokenizer.ggml.model", ZT_GGUF_STRING);
    w_str(&w, "gpt2", 4);
    w_key(&w, "tokenizer.ggml.pre", ZT_GGUF_STRING);
    if (flags & 1u)
        w_str(&w, "llama-bpe", 9);
    else
        w_str(&w, "qwen2", 5);
    w_key(&w, "tokenizer.ggml.tokens", ZT_GGUF_ARRAY);
    w_u32(&w, ZT_GGUF_STRING);
    w_u64(&w, g_nt);
    for (uint32_t i = 0; i < g_nt; i++) w_str(&w, g_tok[i].s, g_tok[i].len);
    w_key(&w, "tokenizer.ggml.merges", ZT_GGUF_ARRAY);
    w_u32(&w, ZT_GGUF_STRING);
    w_u64(&w, g_nm);
    for (uint32_t i = 0; i < g_nm; i++) w_str(&w, g_mtxt[i], g_mlen[i]);
    w_key(&w, "tokenizer.ggml.token_type", ZT_GGUF_ARRAY);
    bool short_types = hostile && (flags & 4u);
    if (flags & 8u) {
        w_u32(&w, ZT_GGUF_I32);
        w_u64(&w, short_types ? g_nt - 1 : g_nt);
        for (uint32_t i = 0; i < g_nt - (short_types ? 1u : 0u); i++) w_u32(&w, g_tok[i].type);
    } else {
        w_u32(&w, ZT_GGUF_U8);
        w_u64(&w, short_types ? g_nt - 1 : g_nt);
        for (uint32_t i = 0; i < g_nt - (short_types ? 1u : 0u); i++) w_raw(&w, &g_tok[i].type, 1);
    }
    if (short_types)
        for (uint32_t i = 0; i < g_nt; i++) g_tok[i].type = 1; /* the loader ignores them */
    if (flags & 2u) {
        uint64_t bos = hostile ? fz_edge64(in) : fz_u16(in) % (g_nt + 2u);
        uint64_t eos = hostile ? fz_edge64(in) : fz_u16(in) % (g_nt + 2u);
        w_key(&w, "tokenizer.ggml.bos_token_id", ZT_GGUF_I64);
        w_u64(&w, bos);
        w_key(&w, "tokenizer.ggml.eos_token_id", ZT_GGUF_U32);
        w_u32(&w, (uint32_t) eos);
        w_key(&w, "tokenizer.ggml.add_bos_token", ZT_GGUF_BOOL);
        uint8_t ab = fz_u8(in) & 1u;
        w_raw(&w, &ab, 1);
    }
    while (w.n % 32u) w_raw(&w, "", 1); /* data_off must lie inside the file */
    uint8_t *file = fz_dup(w.p, w.n);
    free(w.p);
    *out_n = w.n;
    return file;
}

/* ------------------------------------------------------------ checks */
static int32_t *encode_exact(const zt_tok_t *t, const uint8_t *text, uint64_t n, bool sp,
                             uint64_t *n_ids)
{
    /* first pass to learn the count (work area exactly 36 * n) */
    uint8_t *work = (uint8_t *) malloc(36u * n + 1u);
    int32_t *big = (int32_t *) malloc(sizeof(int32_t) * (n + 1u));
    if (!work || !big) abort();
    uint64_t k = 0;
    int32_t r = zt_tok_encode(t, text, n, sp, big, n + 1u, &k, n ? work : NULL, 36u * n);
    if (r != ZT_GGUF_OK) abort(); /* K2 */
    if (k > n) abort(); /* never more ids than bytes */
    for (uint64_t i = 0; i < k; i++)
        if (big[i] < 0 || (uint32_t) big[i] >= t->n_vocab) abort();
    /* again into an exact buffer: same ids */
    int32_t *ids = (int32_t *) malloc(sizeof(int32_t) * (k ? k : 1u));
    uint64_t k2 = 0;
    if (!ids) abort();
    r = zt_tok_encode(t, text, n, sp, ids, k, &k2, n ? work : NULL, 36u * n);
    if (r != ZT_GGUF_OK || k2 != k || memcmp(ids, big, sizeof(int32_t) * k) != 0) abort();
    /* K4: one id short */
    if (k > 0) {
        uint64_t k3 = 99;
        r = zt_tok_encode(t, text, n, sp, big, k - 1u, &k3, work, 36u * n);
        if (r != ZT_TOK_ESPACE || k3 > k - 1u || memcmp(big, ids, sizeof(int32_t) * k3) != 0)
            abort();
    }
    /* K4: work one byte short writes nothing */
    if (n > 0) {
        uint64_t k4 = 99;
        int32_t canary = 0x5A5A5A5A;
        big[0] = canary;
        r = zt_tok_encode(t, text, n, sp, big, n + 1u, &k4, work, 36u * n - 1u);
        if (r != ZT_TOK_ESPACE || k4 != 0 || big[0] != canary) abort();
    }
    free(work);
    free(big);
    *n_ids = k;
    return ids;
}

static void check_decode(const zt_tok_t *t, const int32_t *ids, uint64_t k, const uint8_t *want,
                         uint64_t want_n)
{
    /* the longest token is STR_MAX bytes of alphabet: at most STR_MAX bytes out */
    uint64_t cap = k * 64u + 1u, n = 0;
    uint8_t *out = (uint8_t *) malloc(cap);
    if (!out) abort();
    int32_t r = zt_tok_decode(t, ids, k, out, cap, &n);
    if (r != ZT_GGUF_OK && r != ZT_TOK_EBADID) abort();
    if (n > cap) abort();
    if (want && (r != ZT_GGUF_OK || n != want_n || memcmp(out, want, n) != 0)) abort(); /* K3 */
    if (r == ZT_GGUF_OK) {
        uint8_t *ex = fz_dup(out, n);
        uint64_t n2 = 0;
        if (zt_tok_decode(t, ids, k, ex, n, &n2) != ZT_GGUF_OK || n2 != n ||
            memcmp(ex, out, n) != 0)
            abort();
        if (n > 0) {
            uint64_t n3 = 0;
            if (zt_tok_decode(t, ids, k, ex, n - 1u, &n3) != ZT_TOK_ESPACE || n3 > n - 1u ||
                memcmp(ex, out, n3) != 0)
                abort(); /* K5 */
        }
        free(ex);
    }
    free(out);
}

static uint8_t *g_arena;
static unsigned long long g_st_load, g_st_refused, g_st_bytes, g_st_ids, g_st_special;

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS"))
        fprintf(stderr,
                "fuzz_tok: %llu vocabularies loaded, %llu refused, %llu text bytes -> %llu ids "
                "(%llu specials)\n",
                g_st_load, g_st_refused, g_st_bytes, g_st_ids, g_st_special);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 65536) return 0;
    fz_in in;
    fz_init(&in, data, size);
    uint8_t mode = fz_u8(&in);
    bool hostile = (mode & 0x80u) != 0;
    size_t fn = 0;
    uint8_t *file = build_gguf(&in, hostile, &fn);

    zt_gguf_t g;
    zt_tok_t t;
    if (zt_gguf_open(&g, file, fn) != ZT_GGUF_OK) abort(); /* the container is well formed */
    uint64_t need = zt_tok_arena_bytes(&g);
    if (!need) abort();
    g_arena = (uint8_t *) malloc(need);
    if (!g_arena) abort();
    /* one byte short is refused (8-aligned malloc: the slack is not used) */
    if (zt_tok_load(&t, &g, g_arena, need - 9u) != ZT_TOK_ESPACE) abort();
    int32_t lr = zt_tok_load(&t, &g, g_arena, need);
    if (lr != ZT_GGUF_OK) {
        if (!hostile) {
            fprintf(stderr, "honest vocabulary refused: %d\n", lr);
            abort();
        }
        g_st_refused++;
        goto done;
    }
    g_st_load++;
    /* K1 */
    if (t.n_vocab != g_nt || t.n_merges > g_nm) abort();
#ifndef FZ_TOK_TOLERATE_NEG_BOS /* exploration builds only: the known negative-id defect */
    if (t.bos < -1 || t.bos >= (int32_t) t.n_vocab || t.eos < -1 || t.eos >= (int32_t) t.n_vocab) {
        fprintf(stderr, "bos %d eos %d n_vocab %u\n", t.bos, t.eos, t.n_vocab);
        abort();
    }
#endif
    /* K6 */
    for (uint32_t i = 0; i < g_nt; i++) {
        bool first = true;
        for (uint32_t j = 0; j < i && first; j++)
            if (g_tok[j].len == g_tok[i].len && !memcmp(g_tok[j].s, g_tok[i].s, g_tok[i].len))
                first = false;
        if (first && zt_tok_find(&t, g_tok[i].s, g_tok[i].len) != (int32_t) i) abort();
    }

    {
        /* the text: mostly whole tokens' bytes (so merges fire), some literal
         * bytes; or (mode bit 2) the rest of the input as it is */
        uint64_t n = 0;
        uint8_t *text;
        if (mode & 4u) {
            n = in.n;
            text = fz_dup(in.p, n);
        } else {
            uint8_t *tb = (uint8_t *) malloc(in.n * STR_MAX + 1u);
            if (!tb) abort();
            while (in.n > 0) {
                uint8_t c = fz_u8(&in);
                if (c & 0x80u) {
                    tb[n++] = fz_u8(&in);
                } else {
                uint32_t k = ((uint32_t) c | (uint32_t) fz_u8(&in) << 7) % g_nt;
                    if ((c & 3u) && g_nt > 256) k = 256u + k % (g_nt - 256u); /* extras */
                    memcpy(tb + n, g_raw[k], g_rawlen[k]);
                    n += g_rawlen[k];
                }
            }
            text = fz_dup(tb, n);
            free(tb);
        }
        if (!hostile && (mode & 1u)) /* sprinkle in the specials' text */
            for (uint64_t i = 0; i + 1 < n; i += 7u)
                if (text[i] < 0x10u) text[i] = (uint8_t) (1u + text[i] % 8u);
        for (int sp = 0; sp < 2; sp++) {
            uint64_t k = 0;
            int32_t *ids = encode_exact(&t, text, n, sp != 0, &k);
            g_st_bytes += n;
            g_st_ids += k;
            for (uint64_t q = 0; q < k; q++)
                g_st_special += t.ttype[ids[q]] == ZT_TOK_CONTROL || t.ttype[ids[q]] == ZT_TOK_USER;
            check_decode(&t, ids, k, hostile ? NULL : text, n);
            free(ids);
        }
        /* K5: the text read as int32 ids */
        uint64_t ni = n / 4u;
        int32_t *raw = (int32_t *) malloc(sizeof(int32_t) * (ni ? ni : 1u));
        if (!raw) abort();
        for (uint64_t i = 0; i < ni; i++) {
            uint32_t v;
            memcpy(&v, text + 4u * i, 4);
            raw[i] = (mode & 2u) ? (int32_t) (v % (t.n_vocab + 2u)) - 1 : (int32_t) v;
        }
        check_decode(&t, raw, ni, NULL, 0);
        free(raw);
        free(text);
    }
done:
    free(g_arena);
    free(file);
    return 0;
}
