/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* e8_lattice.h — the one E8 root system shared by the icosian E8 of e8.c
 * and the tensor engine's quantisers (src/tensor/zt_lattice.c, zt_holo.c).
 * Header only (tables and small inline functions), so neither module needs
 * a new object file and nothing can be linked against a stale copy.
 *
 * COORDINATES.  E8 in its even coordinate system is
 *     E8 = { v in Z^8 or (Z + 1/2)^8 : sum(v) even }.
 * Everything here is written DOUBLED, v2 = 2v, so it stays integral:
 *     2E8 = { v2 in Z^8 : all v2_i even or all odd, sum(v2) = 0 mod 4 },
 * and the inner product is v . w = (v2 . w2) / 4.
 *
 * WHAT IS TABLED, AND WHY IT IS CANONICAL.
 *   e8l_root2(r)        the 240 roots (norm 2, doubled norm 8) in one fixed
 *                       order: r < 112 is +-2 at a pair of places (28
 *                       pairs, 4 signs), the rest (+-1)^8 with an even
 *                       number of minus signs, in byte order.
 *   E8L_SIMPLE2         a base of simple roots (Bourbaki's labelling).
 *   E8L_CARTAN          their Gram matrix, the Cartan matrix of E8 (the
 *                       Dynkin diagram 1-3-4-5-6-7-8 with 2 on 4):
 *                       determinant 1, diagonal 2, so the lattice is even
 *                       and unimodular.
 *   E8L_ICOSIAN_GRAM    the Gram matrix of e8.c's icosian Z-basis b0..b7
 *                       (moved here from e8.c, which now reads it).
 *   E8L_ICOSIAN2        the images of b0..b7 in doubled coordinates. Their
 *                       inner products are E8L_ICOSIAN_GRAM, so the map
 *                       c -> sum c_i E8L_ICOSIAN2[i] is an isometry from the
 *                       icosian lattice onto these coordinates (E8 is the
 *                       only even unimodular lattice of rank 8, so such a
 *                       map must exist; this is one fixed choice of it).
 *   E8L_FROM2           its inverse: c = E8L_FROM2 v2 / 4, exactly.
 * test_zt_e8.c proves every claim above from the tables themselves.
 * Freestanding: integer only, no libc.
 */
#ifndef ZXV_E8_LATTICE_H
#define ZXV_E8_LATTICE_H

#include <stdint.h>
#include <stdbool.h>

#define E8L_DIM   8u
#define E8L_ROOTS 240u

/* Bourbaki simple roots alpha_1..alpha_8, doubled. */
static const int8_t E8L_SIMPLE2[8][8] = {
    {1, -1, -1, -1, -1, -1, -1, 1}, /* (e1 + e8 - e2 - ... - e7) / 2 */
    {2, 2, 0, 0, 0, 0, 0, 0},       /* e1 + e2 */
    {-2, 2, 0, 0, 0, 0, 0, 0},      /* e2 - e1 */
    {0, -2, 2, 0, 0, 0, 0, 0},      /* e3 - e2 */
    {0, 0, -2, 2, 0, 0, 0, 0},      /* e4 - e3 */
    {0, 0, 0, -2, 2, 0, 0, 0},      /* e5 - e4 */
    {0, 0, 0, 0, -2, 2, 0, 0},      /* e6 - e5 */
    {0, 0, 0, 0, 0, -2, 2, 0},      /* e7 - e6 */
};

/* The Cartan matrix: E8L_CARTAN[i][j] = alpha_i . alpha_j. */
static const int8_t E8L_CARTAN[8][8] = {
    {2, 0, -1, 0, 0, 0, 0, 0},   {0, 2, 0, -1, 0, 0, 0, 0},  {-1, 0, 2, -1, 0, 0, 0, 0},
    {0, -1, -1, 2, -1, 0, 0, 0}, {0, 0, 0, -1, 2, -1, 0, 0}, {0, 0, 0, 0, -1, 2, -1, 0},
    {0, 0, 0, 0, 0, -1, 2, -1},  {0, 0, 0, 0, 0, 0, -1, 2},
};

/* Gram matrix of the icosian basis b0..b7 (e8.c), row-major. */
static const int8_t E8L_ICOSIAN_GRAM[64] = {
    2, 1, 1, 1, 1, 1, 1, 0, /* b0 */
    1, 2, 1, 1, 1, 0, 1, 1, /* b1 */
    1, 1, 2, 0, 1, 0, 1, 0, /* b2 */
    1, 1, 0, 2, 0, 1, 1, 1, /* b3 */
    1, 1, 1, 0, 2, 0, 0, 0, /* b4 */
    1, 0, 0, 1, 0, 2, 0, 0, /* b5 */
    1, 1, 1, 1, 0, 0, 2, 0, /* b6 */
    0, 1, 0, 1, 0, 0, 0, 2, /* b7 */
};

/* b0..b7 in doubled coordinates: E8L_ICOSIAN2[i] . E8L_ICOSIAN2[j] / 4 is
 * E8L_ICOSIAN_GRAM[8 i + j]. Each is a root. */
static const int8_t E8L_ICOSIAN2[8][8] = {
    {2, 2, 0, 0, 0, 0, 0, 0},  {2, 0, 2, 0, 0, 0, 0, 0},     {2, 0, 0, 2, 0, 0, 0, 0},
    {2, 0, 0, -2, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1, 1},     {1, 1, -1, -1, -1, -1, 1, 1},
    {2, 0, 0, 0, 0, 0, -2, 0}, {1, -1, 1, -1, -1, 1, 1, -1},
};

/* Coefficients from doubled coordinates: c_i = sum_j E8L_FROM2[i][j] v2_j / 4,
 * which divides exactly for every lattice point (E8L_FROM2 is the inverse
 * Gram matrix times E8L_ICOSIAN2). */
static const int8_t E8L_FROM2[8][8] = {
    {0, 2, 0, 0, 0, 0, 0, -2},     {0, 0, 2, 0, 0, -2, 0, 0}, {1, -1, -1, 1, 1, -1, 1, -1},
    {1, -1, -1, -1, 3, -1, 1, -1}, {0, 0, 0, 0, 0, 2, 0, 2},  {0, 0, 0, 0, -2, 0, 0, 2},
    {0, 0, 0, 0, -2, 2, -2, 2},    {0, 0, 0, 0, -2, 2, 0, 0},
};

/* Root r (r < 240) of E8 in doubled coordinates; r >= 240 gives zero. */
static inline void e8l_root2(uint32_t r, int8_t e[8])
{
    for (uint32_t i = 0; i < 8; i++) e[i] = 0;
    if (r >= E8L_ROOTS) return;
    if (r < 112) {
        uint32_t pair = r >> 2, a = 0, b = 1;
        for (uint32_t k = 0; k < pair; k++)
            if (++b == 8) {
                a++;
                b = a + 1;
            }
        e[a] = (int8_t) ((r & 1u) ? -2 : 2);
        e[b] = (int8_t) ((r & 2u) ? -2 : 2);
        return;
    }
    uint32_t k = r - 112, m = 0; /* the k-th byte of even weight */
    for (;; m++) {
        uint32_t w = m;
        w ^= w >> 4;
        w ^= w >> 2;
        w ^= w >> 1;
        if (!(w & 1u) && k-- == 0) break;
    }
    for (uint32_t i = 0; i < 8; i++) e[i] = (int8_t) (((m >> i) & 1u) ? -1 : 1);
}

/* Is v2 (doubled coordinates) a point of E8? */
static inline bool e8l_is_point2(const int32_t v2[8])
{
    uint32_t p = (uint32_t) v2[0] & 1u, sum = 0;
    for (uint32_t i = 0; i < 8; i++) {
        if (((uint32_t) v2[i] & 1u) != p) return false;
        sum += (uint32_t) v2[i];
    }
    return (sum & 3u) == 0;
}

/* v2 . w2, which is 4 times the true inner product. */
static inline int64_t e8l_dot2(const int32_t a[8], const int32_t b[8])
{
    int64_t s = 0;
    for (uint32_t i = 0; i < 8; i++) s += (int64_t) a[i] * b[i];
    return s;
}

#endif /* ZXV_E8_LATTICE_H */
