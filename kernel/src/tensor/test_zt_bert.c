/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_bert.c — T26 encoder cells (zt_bert): the WordPiece tokenizer
 * against the Hugging Face tokenizers library's ids, and the encoder and its
 * heads against a float64 numpy reference, on three tiny random files
 * (test_bert_fixture.h, from gen_bert_fixture.py): BERT with a pooler
 * classifier and every normaliser flag on, ELECTRA with its GELU head and a
 * cased vocabulary, and a mean-pooled, length-divided embedder. Built with
 * -DZT_TEST_SIMD and zt_simd*.c it also checks that every kernel set this CPU
 * runs, with and without threads, gives the C reference's bits. Hosted test
 * (libc, libm for the reference values). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_bert.h"
#include "test_bert_fixture.h"
#ifdef ZT_TEST_SIMD
#    include "zt_simd.h"
#endif

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("[FAIL] ");                                                                     \
        } else                                                                                     \
            printf("[PASS] ");                                                                     \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static const char *const NAMES[BF_NFILES] = {"BERT classifier, every flag", "ELECTRA, cased",
                                             "mean-pooled embedder, no clean_text"};

typedef struct {
    zt_gguf_t g;
    zt_bert_t b;
    void *arena;
    uint64_t abytes;
} enc_t;

static int load(enc_t *e, int f)
{
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    int32_t r = zt_gguf_open(&e->g, BF_GGUF[f], BF_GGUF_SIZE[f]);
    e->abytes = r ? 0 : zt_bert_arena_bytes(&e->g);
    e->arena = e->abytes ? malloc(e->abytes) : 0;
    r = e->arena ? zt_bert_load(&e->b, &e->g, e->arena, e->abytes, &err) : -1;
    CHECK(r == 0, "file %d (%s) loads (%d %s %s)", f, NAMES[f], r, r ? err.what : "",
          r ? err.name : "");
    return r;
}

static const uint8_t *text(int32_t i, uint64_t *len)
{
    *len = BF_TEXT_LEN[i];
    return BF_TEXT + BF_TEXT_OFF[i];
}

static void test_tokenizer(int f, enc_t *e)
{
    int32_t ids[BF_CTX];
    uint8_t types[BF_CTX];
    for (int special = 1; special >= 0; special--) {
        uint32_t bad = 0, first = 0, total = 0;
        for (uint32_t c = 0; c < BF_NCASE; c++) {
            const int32_t *cs = BF_CASE + 3 * c;
            uint64_t la, lb = 0;
            const uint8_t *a = text(cs[0], &la), *b = cs[1] >= 0 ? text(cs[1], &lb) : 0;
            uint64_t wb = zt_bert_tok_work_bytes(la, lb);
            void *w = malloc(wb);
            uint32_t n = 0;
            int32_t r = zt_bert_tokenize(&e->b, a, la, b, lb, special, (uint32_t) cs[2], ids, types,
                                         &n, w, wb);
            free(w);
            uint32_t row = (uint32_t) (f * 2 + (1 - special)) * BF_NCASE + c;
            uint32_t o = BF_IDS_OFF[row], m = BF_IDS_OFF[row + 1] - o;
            bool ok = r == 0 && n == m && !memcmp(ids, BF_IDS + o, m * 4);
            /* type ids: all 0 for one text; for a pair 0 then 1, the 1s
             * starting after a [SEP] and ending with one */
            for (uint32_t i = 0; ok && i < n; i++)
                ok = b ? types[i] <= 1 && (!i || types[i] >= types[i - 1]) &&
                             (types[i] == 0 || types[i - 1] == 1 || ids[i - 1] == e->b.cfg.sep_id)
                       : types[i] == 0;
            if (ok && b) ok = types[0] == 0 && types[n - 1] == 1 && ids[n - 1] == e->b.cfg.sep_id;
            if (!ok && !bad++) first = c;
            total += m;
        }
        CHECK(!bad,
              "file %d: the library's ids on all %u cases (%u tokens), special-token text %s%s", f,
              BF_NCASE, total, special ? "parsed" : "as words",
              bad ? " — first differing case below" : "");
        if (bad) printf("       %u cases differ, first case %u\n", bad, first);
    }
}

static void test_forward(int f, enc_t *e)
{
    const zt_bert_cfg_t *c = &e->b.cfg;
    uint32_t nout = c->n_labels ? c->n_labels : c->n_embd;
    double emax = 0;
    uint32_t sat = 0, nmax = 0;
    for (uint32_t k = 0; k < BF_NFWD; k++) {
        uint32_t o = BF_FWD_OFF[f * BF_NFWD + k], n = BF_FWD_OFF[f * BF_NFWD + k + 1] - o;
        uint32_t eo = BF_FWD_EOFF[f * BF_NFWD + k];
        if (n > nmax) nmax = n;
        uint64_t wb = zt_bert_work_bytes(&e->b, n);
        void *w = malloc(wb);
        zt_fx out[BF_NE + 8], out2[BF_NE + 8];
        uint32_t ns = 0;
        int32_t r =
            c->n_labels
                ? zt_bert_classify(&e->b, BF_FWD_IDS + o, BF_FWD_TYPES + o, n, out, w, wb, &ns)
                : zt_bert_embed(&e->b, BF_FWD_IDS + o, BF_FWD_TYPES + o, n, out, w, wb, &ns);
        if (r) emax = 1e9;
        sat += ns;
        for (uint32_t i = 0; i < nout; i++) {
            double d = fabs((out[i] - BF_FWD_EXPECT[eo + i]) / 65536.0);
            if (d > emax) emax = d;
        }
        if (!c->n_labels) {
            double len = 0;
            for (uint32_t i = 0; i < nout; i++) len += pow(out[i] / 65536.0, 2);
            if (fabs(sqrt(len) - 1) > 1e-4) emax = 1e9;
        }
#ifdef ZT_TEST_SIMD
        const zt_kern_t *ks[8];
        uint32_t nk = zt_simd_list(ks, 8);
        zt_pool_t *pool = zt_pool_new(3);
        bool same = true;
        for (uint32_t i = 0; i < nk; i++)
            for (int th = 0; th < 2; th++) {
                zt_kern_t kt;
                zt_kern_threaded(&kt, ks[i], th ? pool : 0);
                e->b.kern = &kt;
                memset(out2, 0, sizeof out2);
                if (c->n_labels)
                    zt_bert_classify(&e->b, BF_FWD_IDS + o, BF_FWD_TYPES + o, n, out2, w, wb, 0);
                else
                    zt_bert_embed(&e->b, BF_FWD_IDS + o, BF_FWD_TYPES + o, n, out2, w, wb, 0);
                same &= !memcmp(out, out2, nout * 4);
            }
        e->b.kern = 0;
        zt_pool_free(pool);
        if (k == 0)
            CHECK(same,
                  "file %d: every kernel set (%u), with and without threads, gives the C "
                  "reference's bits",
                  f, nk);
#else
        (void) out2;
#endif
        free(w);
    }
    /* Q8_0 weights are exact; F16 ones are kept to about 2^-15 of each
     * block's largest value; activations are Q16 and 16-bit blocks */
    CHECK(emax < (c->n_labels ? 0.003 : 0.0005) && sat == 0,
          "file %d: %s vs float64 on %u inputs (up to %u tokens, a pair, a cut pair): max abs err "
          "%.5f (< %s)%s",
          f, c->n_labels ? "logits" : "unit-length embeddings", BF_NFWD, nmax, emax,
          c->n_labels ? "0.003" : "0.0005", c->n_labels ? "" : ", length 1 within 1e-4");
}

/* the same embedder read with the length division off: the plain mean */
static void test_mean(enc_t *e)
{
    double emax = 0;
    e->b.cfg.normalize = false;
    for (uint32_t k = 0; k < BF_NFWD; k++) {
        uint32_t o = BF_FWD_OFF[2 * BF_NFWD + k], n = BF_FWD_OFF[2 * BF_NFWD + k + 1] - o;
        uint64_t wb = zt_bert_work_bytes(&e->b, n);
        void *w = malloc(wb);
        zt_fx out[BF_NE];
        if (zt_bert_embed(&e->b, BF_FWD_IDS + o, BF_FWD_TYPES + o, n, out, w, wb, 0)) emax = 1e9;
        for (uint32_t i = 0; i < BF_NE; i++) {
            double d = fabs((out[i] - BF_FWD_MEAN[k * BF_NE + i]) / 65536.0);
            if (d > emax) emax = d;
        }
        free(w);
    }
    e->b.cfg.normalize = true;
    CHECK(emax < 0.0005,
          "file 2 without the length division: the mean vs float64, max abs err %.5f (< 0.0005)",
          emax);
}

static void test_refusals(enc_t *e)
{
    zt_model_err_t err;
    int32_t ids[BF_CTX];
    uint8_t types[BF_CTX];
    uint32_t n;
    uint8_t w[64 << 10];
    const uint8_t *s = (const uint8_t *) "hello world";
    CHECK(zt_bert_tokenize(&e->b, s, 11, 0, 0, false, 2, ids, types, &n, w, sizeof w) == 0 &&
              n == 2 &&
              zt_bert_tokenize(&e->b, s, 11, s, 11, false, 2, ids, types, &n, w, sizeof w) ==
                  ZT_NN_EARG &&
              zt_bert_tokenize(&e->b, s, 11, 0, 0, false, BF_CTX + 1, ids, types, &n, w,
                               sizeof w) == ZT_NN_EARG &&
              zt_bert_tokenize(&e->b, s, 11, 0, 0, false, 8, ids, types, &n, w, 64) == ZT_NN_ESPACE,
          "max_tokens below the template or above the context, or too little work memory, is "
          "refused; two tokens leave [CLS] [SEP]");
    int32_t one[2] = {2, (int32_t) e->b.cfg.n_vocab};
    uint8_t ty[2] = {0, 0}, ty2[2] = {0, 2};
    int32_t ok_ids[2] = {2, 3};
    zt_fx out[BF_NE];
    uint64_t wb = zt_bert_work_bytes(&e->b, 2);
    void *wk = malloc(wb);
    CHECK(zt_bert_classify(&e->b, one, ty, 2, out, wk, wb, 0) == ZT_NN_EARG &&
              zt_bert_classify(&e->b, ok_ids, ty2, 2, out, wk, wb, 0) == ZT_NN_EARG &&
              zt_bert_classify(&e->b, ok_ids, ty, 2, out, wk, wb - 4096, 0) == ZT_NN_ESPACE &&
              zt_bert_work_bytes(&e->b, BF_CTX + 1) == 0 &&
              zt_bert_load(&e->b, &e->g, e->arena, e->abytes - 64, &err) == ZT_NN_ESPACE,
          "an id past the vocabulary, a token type past the table, too little work memory or "
          "arena, or more tokens than the context, are refused");
    free(wk);
    zt_bert_load(&e->b, &e->g, e->arena, e->abytes, &err);
    zt_gguf_str_t lab;
    CHECK(zt_bert_label(&e->b, 2, &lab) == 0 && zt_gguf_str_eq(lab, "contradiction") &&
              zt_bert_label(&e->b, 3, &lab) != 0,
          "labels are read from the file");
    static const uint8_t cls[] = "[CLS]", hello[] = "\xe2\x96\x81hello", s_[] = "s";
    CHECK(zt_bert_find(&e->b, cls, 5) == e->b.cfg.cls_id && zt_bert_find(&e->b, hello, 8) > 0 &&
              zt_bert_find(&e->b, s_, 1) > 0 && zt_bert_find(&e->b, hello + 3, 5) < 0,
          "the vocabulary is llama.cpp's form: U+2581 before a word start, '##' pieces bare");
    /* a file missing a normaliser flag, or of another architecture, is refused */
    uint8_t *bad = malloc(BF_GGUF_SIZE[0]);
    zt_gguf_t gb;
    zt_bert_cfg_t cb;
    static const char *const keys[2] = {"zxv.wordpiece.lowercase", "general.architecture"};
    for (int k = 0; k < 2; k++) {
        memcpy(bad, BF_GGUF[0], BF_GGUF_SIZE[0]);
        uint64_t kl = strlen(keys[k]);
        for (uint64_t i = 0; i + kl < BF_GGUF_SIZE[0]; i++)
            if (!memcmp(bad + i, keys[k], kl)) {
                bad[i + kl - 1] ^= 0x20;
                break;
            }
        memset(&err, 0, sizeof err);
        int32_t r =
            zt_gguf_open(&gb, bad, BF_GGUF_SIZE[0]) == 0 ? zt_bert_config(&gb, &cb, &err) : 0;
        CHECK(r == (k ? ZT_NN_ECONFIG : ZT_NN_EMISSING) && zt_bert_arena_bytes(&gb) == 0,
              "a file %s is refused (%s)",
              k ? "of another architecture" : "without a normaliser flag",
              err.what ? err.what : "");
    }
    free(bad);
}

int main(void)
{
    printf("=== T26 encoder cells (BERT, ELECTRA) ===\n");
    for (int f = 0; f < BF_NFILES; f++) {
        enc_t e;
        if (load(&e, f)) continue;
        CHECK(e.b.cfg.n_labels == BF_NLABELS[f] && e.b.cfg.n_embd == BF_NE &&
                  e.b.cfg.arch == (f == 1 ? ZT_BERT_ELECTRA : ZT_BERT_BERT) &&
                  e.b.cfg.pooling == (f == 2 ? ZT_BERT_POOL_MEAN : ZT_BERT_POOL_CLS) &&
                  e.b.n_special == 5,
              "file %d: config (%u labels, width %u, %s pooling, 5 special tokens)", f,
              e.b.cfg.n_labels, e.b.cfg.n_embd, f == 2 ? "mean" : "first-token");
        test_tokenizer(f, &e);
        test_forward(f, &e);
        if (f == 0) test_refusals(&e);
        if (f == 2) test_mean(&e);
        free(e.arena);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
