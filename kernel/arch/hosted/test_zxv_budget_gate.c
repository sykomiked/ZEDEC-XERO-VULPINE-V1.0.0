/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_budget_gate.c — the swarm budget governs generation
 * (zxv_budget_gate.h B1-B4): with a budget of N tokens, a request for more
 * than N generates exactly N, the budget is 0 afterwards, the next request
 * is refused without running the model, and the next cycle refills it.
 * Checked with a counting generator and, when the forward pass is built
 * (ZXV_HAVE_ZT_GLUE), with the real tensor engine on the tiny qwen2 test
 * model, whose greedy chain must be cut to its first N tokens.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_budget_gate.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0; /* evaluated once: c may have side effects */                       \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

typedef struct {
    uint32_t calls, last_max, natural; /* natural: where the text would end */
} fake_t;

static int32_t fake_gen(void *ctx, uint32_t max_new)
{
    fake_t *f = (fake_t *) ctx;
    f->calls++;
    f->last_max = max_new;
    return (int32_t) (f->natural < max_new ? f->natural : max_new);
}

static int32_t bad_gen(void *ctx, uint32_t max_new)
{
    (void) ctx;
    (void) max_new;
    return -7;
}

static void one_level(swarm_budget_t *b, uint64_t n)
{
    swarm_budget_init(b, 1, n);
    swarm_budget_register(b, 1, 0);
    swarm_budget_begin_cycle(b);
}

static void test_counting(void)
{
    printf("[1] budget gate with a counting generator\n");
    const uint64_t N = 37;
    swarm_budget_t b;
    one_level(&b, N);
    CHECK(swarm_budget_remaining(&b, 1) == N, "the companion starts the cycle with N = %llu",
          (unsigned long long) N);

    fake_t f = {0, 0, 1000};
    zxv_gate_t g;
    int32_t r = zxv_budget_gate(&b, 1, 256, fake_gen, &f, &g);
    CHECK(f.calls == 1 && f.last_max == N, "asked for 256: the model is allowed min(256, N) = %u",
          f.last_max);
    CHECK(r == (int32_t) N && g.generated == N && g.charged == N,
          "exactly N generated and charged");
    CHECK(swarm_budget_remaining(&b, 1) == 0 && g.remaining_after == 0, "budget 0 afterwards");

    r = zxv_budget_gate(&b, 1, 256, fake_gen, &f, &g);
    CHECK(r == ZXV_GATE_EXHAUSTED && g.exhausted && f.calls == 1,
          "next request: exhausted, the model is not run");

    swarm_budget_end_cycle(&b);
    CHECK(b.last_unused == 0, "nothing expired: the cycle was spent");
    swarm_budget_begin_cycle(&b);
    CHECK(swarm_budget_remaining(&b, 1) == N, "the next cycle refills N");

    f.natural = 10; /* the text ends early: only what was written is charged */
    r = zxv_budget_gate(&b, 1, 256, fake_gen, &f, &g);
    CHECK(r == 10 && swarm_budget_remaining(&b, 1) == N - 10, "an early stop charges only 10");
    r = zxv_budget_gate(&b, 1, 5, fake_gen, &f, &g);
    CHECK(r == 5 && f.last_max == 5 && swarm_budget_remaining(&b, 1) == N - 15,
          "a request under the remainder is not raised");

    uint64_t before = swarm_budget_remaining(&b, 1);
    r = zxv_budget_gate(&b, 1, 256, bad_gen, NULL, &g);
    CHECK(r == -7 && swarm_budget_remaining(&b, 1) == before,
          "a failed generation charges nothing");

    swarm_budget_end_cycle(&b);
    r = zxv_budget_gate(&b, 1, 256, fake_gen, &f, &g);
    CHECK(r == ZXV_GATE_EXHAUSTED, "outside an open cycle nothing is generated");
    r = zxv_budget_gate(&b, 99, 256, fake_gen, &f, &g);
    CHECK(r == ZXV_GATE_EXHAUSTED, "an unknown agent has no budget");
}

#if defined(ZXV_HAVE_ZT_GLUE)
#    include "zxv_model_host.h"
#    include "zt_model.h"
#    include "test_model_fixture.h"
#    include "test_tok_fixture.h"
#    include "test_fixture_rebuild.h"

typedef struct {
    const zt_gguf_t *g;
    const zt_tok_t *tok;
    const int32_t *ids;
    uint64_t n;
    char out[4096], err[160];
    uint32_t calls;
} real_t;

static int32_t real_gen(void *ctx, uint32_t max_new)
{
    real_t *j = (real_t *) ctx;
    j->calls++;
    return zxv_zt_generate_n(j->g, j->tok, j->ids, j->n, max_new, j->out, sizeof j->out, j->err,
                             sizeof j->err);
}

static void test_engine(void)
{
    printf("[2] budget gate around the tensor engine (tiny qwen2 test model)\n");
    const mf_variant_t *v = &MF_VARIANTS[0];
    uint8_t *file = rebuild(v);
    zt_gguf_t mg, tg;
    zt_tok_t tok;
    CHECK(file && zt_gguf_open(&mg, file, v->file_len) == 0 &&
              zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN) == 0,
          "model and tokenizer fixtures opened");
    uint64_t tb = zt_tok_arena_bytes(&tg);
    void *tar = malloc((size_t) tb);
    CHECK(tar && zt_tok_load(&tok, &tg, tar, tb) == 0, "tokenizer loaded");
    const char *msg = "Hi there";
    int32_t ids[64];
    uint64_t n = 0, wb = zt_tok_work_bytes(strlen(msg));
    void *work = malloc((size_t) wb);
    zt_tok_encode(&tok, (const uint8_t *) msg, strlen(msg), false, ids, 64, &n, work, wb);
    free(work);

    const uint32_t N = 5; /* less than the 16-token reference chain */
    swarm_budget_t b;
    one_level(&b, N);
    static real_t j;
    j.g = &mg;
    j.tok = &tok;
    j.ids = ids;
    j.n = n;
    zxv_gate_t g;
    int32_t r = zxv_budget_gate(&b, 1, v->n_greedy, real_gen, &j, &g);
    uint8_t ref[4096];
    uint64_t rn = 0;
    zt_tok_decode(&tok, v->greedy, N, ref, sizeof ref - 1, &rn);
    ref[rn] = 0;
    CHECK(r == (int32_t) N && g.max_new == N,
          "asked for %u tokens with a budget of %u: %d generated%s%s", v->n_greedy, N, (int) r,
          j.err[0] ? ": " : "", j.err);
    CHECK(strlen(j.out) == rn && !memcmp(j.out, ref, rn),
          "the reply is the reference chain's first %u tokens", N);
    CHECK(swarm_budget_remaining(&b, 1) == 0, "budget 0 afterwards");
    r = zxv_budget_gate(&b, 1, v->n_greedy, real_gen, &j, &g);
    CHECK(r == ZXV_GATE_EXHAUSTED && j.calls == 1, "next request refused without a forward pass");
    swarm_budget_end_cycle(&b);
    swarm_budget_begin_cycle(&b);
    r = zxv_budget_gate(&b, 1, v->n_greedy, real_gen, &j, &g);
    CHECK(r == (int32_t) N && !strcmp(j.out, (const char *) ref),
          "the next cycle refills it and the same %u tokens come back", N);
    zxv_zt_release();
    free(tar);
    free(file);
}
#endif

int main(void)
{
    test_counting();
#if defined(ZXV_HAVE_ZT_GLUE)
    test_engine();
#else
    printf("[2] skipped: no forward pass in this tree\n");
#endif
    printf(failures ? "budget gate test FAILED (%d)\n" : "budget gate test passed\n", failures);
    return failures ? 1 : 0;
}
