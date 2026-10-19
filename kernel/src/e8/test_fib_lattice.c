/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fib_lattice.c — host unit test of the Fibonacci lattice ladder
 * (fib_lattice.c): the self-check must pass (the ladder, Cassini, the
 * Pisano mod-8 period, the F6=8 / F12=144 Type-II anchors, the 144
 * harmonic and the M-O-S bound, and the enumerated A2/A3/D5/E8 kissing
 * numbers), and the ladder table must agree with the mathematical facts:
 * A2 = 6, A3 = 12, D5 = 40, E8 = 240, L144 minimum norm 14. */
#include "fib_lattice.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                           \
    } while (0)

int main(void)
{
    uint32_t n = 0;
    const fib_lattice_entry_t *l = fib_lattice_ladder(&n);
    CHECK(n == FIB_LATTICE_STEPS);
    CHECK(l != 0);
    CHECK(fib_lattice_selfcheck() == 0);
    CHECK(l[2].dim == 2 && l[2].kissing == 6 && l[2].min_norm == 2);   /* A2 */
    CHECK(l[3].dim == 3 && l[3].kissing == 12 && l[3].min_norm == 2);  /* A3 */
    CHECK(l[4].dim == 5 && l[4].kissing == 40 && l[4].min_norm == 2);  /* D5 */
    CHECK(l[5].dim == 8 && l[5].kissing == 240 && l[5].min_norm == 2); /* E8 */
    CHECK(l[11].dim == 144 && l[11].min_norm == 14);                   /* L144 */
    CHECK(l[5].type == FIB_LATTICE_TYPE2 && l[11].type == FIB_LATTICE_TYPE2);
    /* the two Type-II anchors are the only 0-mod-8 Fibonacci dimensions */
    for (uint32_t i = 0; i < n; i++) {
        uint8_t m8 = (l[i].dim % 8u) == 0u;
        uint8_t anchor = (i == 5u || i == 11u);
        if (m8 != anchor) {
            printf("  FAIL line %d: F%d dim %u m8=%u anchor=%u\n", __LINE__,
                   l[i].fib_step, l[i].dim, m8, anchor);
            fails++;
        }
    }
    printf("test_fib_lattice: %s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}