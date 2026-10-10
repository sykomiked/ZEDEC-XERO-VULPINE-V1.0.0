/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_zt_glue.c — the hosted app's forward-pass glue against the tensor
 * engine's reference: the tiny qwen2 test model (rebuilt from
 * test_model_fixture.h) with the qwen2 tokenizer fixture. "Hi there", wrapped
 * by the glue's chat template, must give the reference greedy chain, which
 * kernel/src/tensor/test_zt_model.c checks against float64 llama.cpp
 * semantics for the same template text.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it (with ASan/UBSan where the
 *   compiler has them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_model_host.h"
#include "zt_model.h"
#include "test_model_fixture.h"
#include "test_tok_fixture.h"
#include "test_fixture_rebuild.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) failures++;                                                                      \
        printf((c) ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

int main(void)
{
    const mf_variant_t *v = &MF_VARIANTS[0]; /* qwen2, vocabulary of the tokenizer fixture */
    uint8_t *file = rebuild(v);
    zt_gguf_t mg, tg;
    zt_tok_t tok;
    CHECK(file && zt_gguf_open(&mg, file, v->file_len) == 0, "qwen2 test model rebuilt and opened");
    CHECK(zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN) == 0,
          "tokenizer fixture opened");
    uint64_t tb = zt_tok_arena_bytes(&tg);
    void *tar = malloc((size_t) tb);
    CHECK(tar && zt_tok_load(&tok, &tg, tar, tb) == 0 && tok.n_vocab == v->n_vocab,
          "tokenizer loaded; vocabulary matches the model (%u)", tok.n_vocab);
    CHECK(zxv_zt_can_run(&mg) == 1, "the engine can run the test model");
    CHECK(zxv_zt_can_run(&tg) == 0, "a tokenizer-only file is reported as not runnable");

    /* "Hi there" as the slot tokenizes it (no special parsing) */
    const char *msg = "Hi there";
    int32_t ids[64];
    uint64_t n = 0, wb = zt_tok_work_bytes(strlen(msg));
    void *work = malloc((size_t) wb);
    CHECK(work && zt_tok_encode(&tok, (const uint8_t *) msg, strlen(msg), false, ids, 64, &n, work,
                                wb) == 0,
          "prompt tokenized (%llu ids)", (unsigned long long) n);
    free(work);

    char out[4096], err[160] = "";
    int32_t g = zxv_zt_generate_n(&mg, &tok, ids, n, v->n_greedy, out, sizeof out, err, sizeof err);
    uint8_t ref[4096];
    uint64_t rn = 0;
    zt_tok_decode(&tok, v->greedy, v->n_greedy, ref, sizeof ref - 1, &rn);
    ref[rn] = 0;
    CHECK(g == (int32_t) v->n_greedy && strlen(out) == rn && !memcmp(out, ref, rn),
          "chat-wrapped greedy reply matches the engine's reference chain (%d tokens%s%s)", (int) g,
          err[0] ? ": " : "", err);

    char out2[4096];
    g = zxv_zt_generate_n(&mg, &tok, ids, n, v->n_greedy, out2, sizeof out2, err, sizeof err);
    CHECK(g == (int32_t) v->n_greedy && !strcmp(out, out2),
          "second request reuses the loaded model and gives the same reply");

    char small[8];
    g = zxv_zt_generate_n(&mg, &tok, ids, n, v->n_greedy, small, sizeof small, err, sizeof err);
    CHECK(g == (int32_t) v->n_greedy && strlen(small) <= sizeof small - 1,
          "a short output buffer cuts the reply and stays NUL-terminated");

    int32_t *many = calloc(4096, sizeof *many);
    g = zxv_zt_generate_n(&mg, &tok, many, 4096, 4, out, sizeof out, err, sizeof err);
    CHECK(g < 0 && err[0], "a prompt longer than the context is refused: %s", err);
    free(many);

    int32_t bad[2] = {ids[0], 999999};
    g = zxv_zt_generate_n(&mg, &tok, bad, 2, 4, out, sizeof out, err, sizeof err);
    CHECK(g < 0, "an out-of-range token id is refused: %s", err);

    zxv_zt_release();
    g = zxv_zt_generate_n(&tg, &tok, ids, n, 4, out, sizeof out, err, sizeof err);
    CHECK(g < 0 && err[0], "a file without runnable weights is refused: %s", err);
    zxv_zt_release();

    free(tar);
    free(file);
    printf(failures ? "glue test FAILED (%d)\n" : "glue test passed\n", failures);
    return failures ? 1 : 0;
}
