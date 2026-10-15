/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pmm.c — host tests for the four-state frame allocator. */
#include <stdio.h>
#include <string.h>
#include "pmm.h"

static int failures, passes;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            passes++;                                                                              \
        } else {                                                                                   \
            failures++;                                                                            \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
        }                                                                                          \
    } while (0)

#define BASE 0x40000000ull
#define F(i) (BASE + (uint64_t) (i) * PMM_FRAME)

static void counters_add_up(const pmm_t *p, const char *m)
{
    CHECK(p->n_free + p->n_owned + p->n_fenced + p->n_unusable == p->nframes, m);
}

int main(void)
{
    pmm_t p;
    uint8_t bits[PMM_BITS_BYTES(10)];
    uint64_t pa = 0;

    /* ---- init ---- */
    CHECK(pmm_init(NULL, BASE, 10, bits) == -1, "init NULL pool");
    CHECK(pmm_init(&p, BASE, 10, NULL) == -1, "init NULL bits");
    CHECK(pmm_init(&p, BASE, 0, bits) == -1, "init zero frames");
    CHECK(pmm_init(&p, BASE + 1, 10, bits) == -1, "init misaligned base");
    CHECK(pmm_init(&p, BASE, 10, bits) == 0, "init 10 frames");
    CHECK(p.n_free == 10 && p.n_owned == 0 && p.n_fenced == 0 && p.n_unusable == 0,
          "all frames start free");
    CHECK(sizeof(bits) == 3, "10 frames take 3 bytes");
    CHECK(pmm_state(&p, F(0)) == PMM_N && pmm_state(&p, F(9)) == PMM_N, "first and last free");
    CHECK(pmm_state(&p, F(10)) == PMM_F, "one past the end is not ours");
    CHECK(pmm_state(&p, BASE - PMM_FRAME) == PMM_F, "below the base is not ours");
    CHECK(pmm_state(&p, F(3) + 8) == PMM_F, "unaligned address is not a frame");
    CHECK(pmm_owns_range(&p, F(9)) && !pmm_owns_range(&p, F(10)) && !pmm_owns_range(&p, F(1) + 1),
          "owns_range bounds");
    CHECK((bits[2] >> 4) == 0, "tail bits past frame 9 are F, never handed out");

    /* ---- alloc everything, each frame exactly once ---- */
    int seen[10] = {0};
    for (int i = 0; i < 10; i++) {
        CHECK(pmm_alloc(&p, &pa) == LPRES_STATE_TRUE, "alloc while free remain");
        CHECK(pmm_owns_range(&p, pa), "allocated frame is in the pool");
        seen[(pa - BASE) / PMM_FRAME]++;
    }
    int once = 1;
    for (int i = 0; i < 10; i++) once &= seen[i] == 1;
    CHECK(once, "every frame handed out exactly once");
    CHECK(p.n_free == 0 && p.n_owned == 10, "pool fully owned");
    pa = 0x1234;
    CHECK(pmm_alloc(&p, &pa) == LPRES_STATE_NEITHER, "empty pool says NEITHER (wait)");
    CHECK(pa == 0x1234, "NEITHER leaves the output alone");
    CHECK(pmm_alloc(&p, NULL) == LPRES_STATE_FALSE, "alloc with no output is refused");
    CHECK(pmm_alloc(NULL, &pa) == LPRES_STATE_FALSE, "alloc on NULL pool is refused");
    counters_add_up(&p, "counters add up when full");

    /* ---- free, then reuse ---- */
    CHECK(pmm_free(&p, F(4)) == LPRES_STATE_TRUE, "free an owned frame");
    CHECK(pmm_state(&p, F(4)) == PMM_N, "freed frame is free");
    CHECK(p.n_free == 1 && p.n_owned == 9, "counters after free");
    CHECK(pmm_alloc(&p, &pa) == LPRES_STATE_TRUE && pa == F(4), "the freed frame is reused");
    CHECK(pmm_free(&p, F(4)) == LPRES_STATE_TRUE, "free it again after reuse");

    /* ---- double free: BOTH, fenced for life ---- */
    CHECK(pmm_free(&p, F(4)) == LPRES_STATE_BOTH, "double free is BOTH");
    CHECK(pmm_state(&p, F(4)) == PMM_B, "double-freed frame is fenced");
    CHECK(p.n_fenced == 1 && p.n_free == 0, "fence moves it out of free");
    CHECK(pmm_alloc(&p, &pa) == LPRES_STATE_NEITHER, "a fenced frame is never handed out");
    CHECK(pmm_free(&p, F(4)) == LPRES_STATE_FALSE, "freeing a fenced frame changes nothing");
    CHECK(pmm_claim(&p, F(4)) == LPRES_STATE_FALSE, "claiming a fenced frame is refused");
    CHECK(pmm_state(&p, F(4)) == PMM_B, "fenced stays fenced (absorbing)");
    counters_add_up(&p, "counters add up after a fence");

    /* ---- bad frees change nothing ---- */
    uint32_t before = p.n_owned;
    CHECK(pmm_free(&p, F(10)) == LPRES_STATE_FALSE, "free past the end refused");
    CHECK(pmm_free(&p, F(2) + 16) == LPRES_STATE_FALSE, "free unaligned refused");
    CHECK(pmm_free(&p, BASE - PMM_FRAME) == LPRES_STATE_FALSE, "free below base refused");
    CHECK(pmm_free(NULL, F(2)) == LPRES_STATE_FALSE, "free on NULL pool refused");
    CHECK(p.n_owned == before && pmm_state(&p, F(2)) == PMM_T, "bad frees left frame 2 owned");

    /* ---- claim ---- */
    pmm_t q;
    uint8_t qb[PMM_BITS_BYTES(8)];
    CHECK(pmm_init(&q, BASE, 8, qb) == 0, "init 8 frames (exact bytes)");
    CHECK(pmm_claim(&q, F(3)) == LPRES_STATE_TRUE, "claim a free frame");
    CHECK(pmm_state(&q, F(3)) == PMM_T && q.n_owned == 1 && q.n_free == 7, "claimed is owned");
    CHECK(pmm_claim(&q, F(3)) == LPRES_STATE_BOTH, "second claim is BOTH");
    CHECK(pmm_state(&q, F(3)) == PMM_B && q.n_fenced == 1 && q.n_owned == 0,
          "doubly claimed frame is fenced");
    CHECK(pmm_free(&q, F(3)) == LPRES_STATE_FALSE, "the first owner's free leaves the fence");
    CHECK(pmm_claim(&q, F(8)) == LPRES_STATE_FALSE, "claim past the end refused");
    for (int i = 0; i < 7; i++) CHECK(pmm_alloc(&q, &pa) == LPRES_STATE_TRUE, "alloc rest");
    CHECK(pmm_alloc(&q, &pa) == LPRES_STATE_NEITHER, "claim + fence + allocs exhaust the pool");
    counters_add_up(&q, "counters add up after claims");

    /* ---- reserve ---- */
    CHECK(pmm_init(&q, BASE, 8, qb) == 0, "re-init");
    CHECK(pmm_reserve(&q, F(0)) == LPRES_STATE_TRUE, "reserve a free frame");
    CHECK(pmm_state(&q, F(0)) == PMM_F && q.n_unusable == 1 && q.n_free == 7, "reserved is F");
    CHECK(pmm_reserve(&q, F(0)) == LPRES_STATE_FALSE, "reserve twice refused");
    CHECK(pmm_free(&q, F(0)) == LPRES_STATE_FALSE, "an unusable frame cannot be freed");
    CHECK(pmm_claim(&q, F(0)) == LPRES_STATE_FALSE, "an unusable frame cannot be claimed");
    CHECK(pmm_alloc(&q, &pa) == LPRES_STATE_TRUE && pa != F(0), "alloc skips the reserved frame");
    CHECK(pmm_reserve(&q, pa) == LPRES_STATE_FALSE, "an owned frame cannot be reserved");
    CHECK(pmm_reserve(&q, F(8)) == LPRES_STATE_FALSE, "reserve past the end refused");
    counters_add_up(&q, "counters add up after reserve");

    /* ---- the hint rotates: a long-lived pool reuses freed frames anywhere ---- */
    pmm_t r;
    uint8_t rb[PMM_BITS_BYTES(64)];
    CHECK(pmm_init(&r, BASE, 64, rb) == 0, "init 64");
    for (int i = 0; i < 64; i++) pmm_alloc(&r, &pa);
    CHECK(r.n_free == 0, "64 allocated");
    CHECK(pmm_free(&r, F(1)) == LPRES_STATE_TRUE, "free a frame below the hint");
    CHECK(pmm_alloc(&r, &pa) == LPRES_STATE_TRUE && pa == F(1), "wrap-around finds it");
    for (int round = 0; round < 1000; round++) {
        uint64_t a = 0;
        pmm_free(&r, F(round % 64));
        if (pmm_alloc(&r, &a) != LPRES_STATE_TRUE || a != F(round % 64)) {
            CHECK(0, "free/alloc churn returns the same frame");
            break;
        }
    }
    CHECK(r.n_owned == 64 && r.n_fenced == 0, "1000 rounds of churn leak nothing");

    /* ---- a counter/bitmap disagreement is BOTH, not a wrong frame ---- */
    pmm_t s;
    uint8_t sb[PMM_BITS_BYTES(4)];
    pmm_init(&s, BASE, 4, sb);
    sb[0] = 0xFF; /* all owned, but n_free still says 4 */
    CHECK(pmm_alloc(&s, &pa) == LPRES_STATE_BOTH, "counters disagreeing with bits is BOTH");

    printf("[PASS] test_pmm: %d checks, %d failures\n", passes, failures);
    return failures ? 1 : 0;
}
