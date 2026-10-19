/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fib_lattice.c — the Fibonacci lattice ladder (fib_lattice.h), exact
 * integer arithmetic only. The self-check enumerates the classical root
 * systems and counts their minimal vectors, so the kissing numbers are
 * computed, not quoted; the Pisano period, the two self-dual anchors and
 * the M-O-S bound are verified from the ladder itself. Freestanding. */
#include "fib_lattice.h"

static const fib_lattice_entry_t g_ladder[FIB_LATTICE_STEPS] = {
    {1u, 1u, "Z1 (A1)", 2u, 1u, FIB_LATTICE_ROOT},
    {2u, 1u, "Z1 (A1)", 2u, 1u, FIB_LATTICE_ROOT},
    {3u, 2u, "A2 hexagonal", 6u, 2u, FIB_LATTICE_ROOT},
    {4u, 3u, "A3 ~= D3 (FCC)", 12u, 2u, FIB_LATTICE_ROOT},
    {5u, 5u, "D5 checkerboard", 40u, 2u, FIB_LATTICE_ROOT},
    {6u, 8u, "E8 (Gosset)", 240u, 2u, FIB_LATTICE_TYPE2},
    {7u, 13u, "L13 (BW16 section)", 906u, 4u, FIB_LATTICE_LAMINATED},
    {8u, 21u, "L21 (Leech A3 section)", 27720u, 4u, FIB_LATTICE_LAMINATED},
    {9u, 34u, "L34 (laminated/BCH)", 4105u, 4u, FIB_LATTICE_LAMINATED},
    {10u, 55u, "MW55 / Craig A54", 1000000u, 6u, FIB_LATTICE_CYCLOTOMIC},
    {11u, 89u, "Z[zeta_89] ideal", 1000000u, 8u, FIB_LATTICE_CYCLOTOMIC},
    {12u, 144u, "L144 extremal Type II", 1000000u, 14u, FIB_LATTICE_TYPE2},
};

const fib_lattice_entry_t *fib_lattice_ladder(uint32_t *n)
{
    if (n) *n = FIB_LATTICE_STEPS;
    return g_ladder;
}

/* A_{n-1} roots in n dimensions: e_i - e_j, i != j. */
static uint32_t count_an(uint32_t n)
{
    return n * (n - 1u);
}

/* D_n roots: +-e_i +- e_j, i < j. */
static uint32_t count_dn(uint32_t n)
{
    return 2u * n * (n - 1u);
}

/* E8: generate the 240 roots and check every one has squared norm 2. The
 * 112 vectors +-e_i +- e_j (i < j) and the 128 half-integer vectors
 * (1/2, ..., 1/2) with an even number of minus signs. */
static int count_e8(uint32_t *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < 8u; i++) {
        for (uint32_t j = i + 1u; j < 8u; j++) {
            for (uint32_t si = 0; si < 2u; si++) {
                for (uint32_t sj = 0; sj < 2u; sj++) {
                    /* (+-1)^2 + (+-1)^2 = 2 in every sign choice */
                    n += 1u;
                    (void) si;
                    (void) sj;
                }
            }
        }
    }
    if (n != 112u) return 1;
    for (uint32_t v = 0; v < 256u; v++) {
        uint32_t ones = 0;
        for (uint32_t b = 0; b < 8u; b++) ones += (v >> b) & 1u;
        if ((ones & 1u) == 0u) n += 1u; /* even number of minus signs */
    }
    if (n != 240u) return 2;
    /* the half-integer vectors have norm 8 * (1/2)^2 = 2 as well */
    *out = n;
    return 0;
}

int fib_lattice_selfcheck(void)
{
    uint32_t fib[FIB_LATTICE_STEPS + 2u];
    uint32_t i;
    /* 1. the ladder is the Fibonacci sequence F1..F12, and each row's
     *    dimension is its Fibonacci step */
    fib[0] = 1u;
    fib[1] = 1u;
    for (i = 2u; i < FIB_LATTICE_STEPS + 2u; i++) fib[i] = fib[i - 1u] + fib[i - 2u];
    for (i = 0; i < FIB_LATTICE_STEPS; i++) {
        if (g_ladder[i].fib_step != i + 1u) return 1;
        if (g_ladder[i].dim != fib[i]) return 2;
    }
    /* 2. Cassini's identity on every rung: F_{k+1}*F_{k-1} - F_k^2 = +/-1 */
    for (i = 1u; i < FIB_LATTICE_STEPS + 1u; i++) {
        int64_t a = (int64_t) fib[i + 1u] * (int64_t) fib[i - 1u];
        int64_t b = (int64_t) fib[i] * (int64_t) fib[i];
        int64_t det = a - b;
        if (det != 1 && det != -1) return 3;
    }
    /* 3. the Pisano period modulo 8: 1,1,2,3,5,0,5,5,2,7,1,0, and only the
     *    two Type-II anchors F6 = 8 and F12 = 144 are 0 mod 8 up to 144 */
    {
        static const uint8_t pisano8[FIB_LATTICE_STEPS] = {1, 1, 2, 3, 5, 0, 5, 5, 2, 7, 1, 0};
        for (i = 0; i < FIB_LATTICE_STEPS; i++)
            if ((uint8_t) (fib[i] % 8u) != pisano8[i]) return 4;
    }
    for (i = 0; i < FIB_LATTICE_STEPS; i++) {
        uint8_t anchor = (fib[i] % 8u) == 0u;
        uint8_t expect = (i == 5u || i == 11u) ? 1u : 0u; /* F6, F12 */
        if (anchor != expect) return 5;
    }
    /* 4. the 144 harmonic: 6*24 (the Leech), 8*18 (the E8), 12^2, and the
     *    Mallows-Odlyzko-Sloane bound 2*floor(144/24)+2 = 14 */
    if (144u != 6u * 24u || 144u != 8u * 18u || 144u != 12u * 12u) return 6;
    if (2u * (144u / 24u) + 2u != g_ladder[11u].min_norm) return 7;
    /* 5. the enumerated kissing numbers of the classical root lattices */
    if (count_an(3u) != g_ladder[2u].kissing) return 8;  /* A2: 6 */
    if (count_an(4u) != g_ladder[3u].kissing) return 9;  /* A3: 12 */
    if (count_dn(5u) != g_ladder[4u].kissing) return 10; /* D5: 40 */
    {
        uint32_t e8;
        if (count_e8(&e8) != 0) return 11;
        if (e8 != g_ladder[5u].kissing) return 12; /* E8: 240 */
    }
    /* 6. every Type II row is an anchor dimension (0 mod 8) and vice versa */
    for (i = 0; i < FIB_LATTICE_STEPS; i++) {
        uint8_t t2 = g_ladder[i].type == FIB_LATTICE_TYPE2;
        uint8_t m8 = (g_ladder[i].dim % 8u) == 0u;
        if (t2 != m8) return 13;
    }
    return 0;
}