/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_e8.c — the tensor engine and src/e8 describe one E8. Checks, from
 * the shared tables of src/e8/e8_lattice.h alone: the 240 roots, the Cartan
 * matrix, even unimodularity, closure under Weyl reflections, the isometry
 * between e8.c's icosian basis and the doubled coordinates the quantisers
 * use, the nearest-point decoder against a complete Voronoi certificate, and
 * the holographic code on E8 points. Build and run from kernel/:
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Isrc/tensor -Isrc/e8
 *       src/tensor/test_zt_e8.c src/tensor/zt_lattice.c src/tensor/zt.c src/tensor/zt_coil.c
 *       src/tensor/zt_holo.c src/e8/e8.c src/e8/zphi.c -o /tmp/test_zt_e8
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt.h"
#include "zt_lattice.h"
#include "e8.h"
#include "e8_lattice.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x5851F42D4C957F2Dull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static int32_t rnd_fx(int32_t range)
{
    return (int32_t) (rnd() % (uint64_t) (2 * (int64_t) range + 1)) - range;
}

static int8_t book[ZT_E8_CODEBOOK * 8];
static int32_t root[240][8];

static int64_t dot2(const int32_t *a, const int32_t *b)
{
    return e8l_dot2(a, b);
}

/* Index of a root (doubled coordinates) in e8l_root2 order, or -1. */
static int root_index(const int32_t v[8])
{
    for (int r = 0; r < 240; r++)
        if (!memcmp(root[r], v, sizeof root[r])) return r;
    return -1;
}

/* Exact determinant of a small integer matrix (Bareiss). */
static int64_t det8(int64_t m[8][8])
{
    int64_t prev = 1, sign = 1;
    for (int k = 0; k < 7; k++) {
        if (m[k][k] == 0) {
            int s = k + 1;
            while (s < 8 && m[s][k] == 0) s++;
            if (s == 8) return 0;
            for (int j = 0; j < 8; j++) {
                int64_t t = m[k][j];
                m[k][j] = m[s][j];
                m[s][j] = t;
            }
            sign = -sign;
        }
        for (int i = k + 1; i < 8; i++)
            for (int j = k + 1; j < 8; j++)
                m[i][j] = (m[i][j] * m[k][k] - m[i][k] * m[k][j]) / prev;
        prev = m[k][k];
    }
    return sign * m[7][7];
}

static void test_roots(void)
{
    int bad = 0, dup = 0, neg = 0;
    for (uint32_t r = 0; r < 240; r++) {
        int8_t e[8];
        e8l_root2(r, e);
        for (int i = 0; i < 8; i++) root[r][i] = e[i];
        bad += !e8l_is_point2(root[r]) || dot2(root[r], root[r]) != 8;
    }
    int hist[5][240] = {{0}};
    for (int a = 0; a < 240; a++) {
        int32_t m[8];
        for (int i = 0; i < 8; i++) m[i] = -root[a][i];
        neg += root_index(m) < 0;
        for (int b = 0; b < 240; b++) {
            int64_t d = dot2(root[a], root[b]);
            if (a != b && d == 8) dup++;
            if (d % 4 || d < -8 || d > 8)
                bad++;
            else
                hist[d / 4 + 2][a]++;
        }
    }
    CHECK(bad == 0, "240 roots: lattice points of norm 2, inner products in {-2..2} (%d bad)", bad);
    CHECK(dup == 0 && neg == 0, "roots distinct and closed under negation");
    int shape = 0;
    for (int a = 0; a < 240; a++)
        shape += hist[0][a] != 1 || hist[1][a] != 56 || hist[2][a] != 126 || hist[3][a] != 56 ||
                 hist[4][a] != 1;
    CHECK(shape == 0, "each root sees 1/56/126/56/1 roots at inner product -2/-1/0/1/2");
    /* the roots are the codebook's norm-2 shell, in the same lattice */
    uint32_t n = zt_e8_codebook_build(book);
    int inbook = 0;
    for (int r = 0; r < 240; r++) {
        int8_t v[8];
        for (int i = 0; i < 8; i++) v[i] = (int8_t) root[r][i];
        int32_t k = zt_e8_encode(book, v);
        inbook += k >= (int32_t) ZT_E8_SHELL0 && k < (int32_t) ZT_E8_SHELL1;
    }
    CHECK(n == ZT_E8_CODEBOOK && inbook == 240, "roots = codebook shell 1 (%d)", inbook);
}

static void test_cartan(void)
{
    int bad = 0;
    int32_t s[8][8];
    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 8; k++) s[i][k] = E8L_SIMPLE2[i][k];
        bad += root_index(s[i]) < 0;
    }
    int64_t c[8][8], m[8][8];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int64_t d = dot2(s[i], s[j]);
            bad += d % 4 != 0 || d / 4 != E8L_CARTAN[i][j] || E8L_CARTAN[i][j] != E8L_CARTAN[j][i];
            c[i][j] = m[i][j] = E8L_CARTAN[i][j];
        }
    CHECK(bad == 0, "simple roots are roots and their Gram matrix is the Cartan matrix");
    CHECK(det8(m) == 1, "det(Cartan) = 1: E8 is unimodular");
    int even = 1;
    for (int i = 0; i < 8; i++) even &= c[i][i] == 2;
    CHECK(even, "Cartan diagonal 2: E8 is even");
    /* Every root is an integer combination of simple roots with all
     * coefficients of one sign: solve C x = (r . alpha_j) exactly. The
     * inverse of a unimodular matrix is integral; find it by adjugate-free
     * elimination over the rationals, kept in int64 by scaling. */
    int64_t inv[8][8], a[8][16];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 16; j++) a[i][j] = j < 8 ? c[i][j] : (j - 8 == i);
    for (int k = 0; k < 8; k++) { /* Gauss-Jordan with integer row operations */
        int p = k;
        while (a[p][k] == 0) p++;
        for (int j = 0; j < 16; j++) {
            int64_t t = a[k][j];
            a[k][j] = a[p][j];
            a[p][j] = t;
        }
        for (int i = 0; i < 8; i++)
            if (i != k && a[i][k]) {
                int64_t f = a[i][k], g = a[k][k];
                for (int j = 0; j < 16; j++) a[i][j] = a[i][j] * g - a[k][j] * f;
            }
    }
    int integral = 1;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            integral &= a[i][8 + j] % a[i][i] == 0;
            inv[i][j] = a[i][8 + j] / a[i][i];
        }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int64_t t = 0;
            for (int k = 0; k < 8; k++) t += c[i][k] * inv[k][j];
            integral &= t == (i == j);
        }
    CHECK(integral, "Cartan inverse is integral");
    int pos = 0, mixed = 0, height = 0;
    int64_t high[8] = {0};
    for (int r = 0; r < 240; r++) {
        int64_t d[8], x[8];
        for (int j = 0; j < 8; j++) d[j] = dot2(root[r], s[j]) / 4;
        int np = 0, nn = 0, h = 0;
        for (int i = 0; i < 8; i++) {
            x[i] = 0;
            for (int j = 0; j < 8; j++) x[i] += inv[i][j] * d[j];
            np += x[i] > 0, nn += x[i] < 0, h += (int) x[i];
        }
        mixed += np && nn;
        if (!nn) {
            pos++;
            if (h > height) height = h, memcpy(high, x, sizeof x);
        }
    }
    static const int64_t want[8] = {2, 3, 4, 6, 5, 4, 3, 2};
    CHECK(mixed == 0 && pos == 120, "120 positive roots, none of mixed sign (%d, %d)", pos, mixed);
    CHECK(height == 29 && !memcmp(high, want, sizeof want), "highest root 23465432, height 29");
}

/* s_a(v) = v - (v . a) a for a root a (true inner product = dot2 / 4). */
static void reflect(const int32_t v[8], const int32_t a[8], int32_t out[8])
{
    int64_t k = dot2(v, a) / 4;
    for (int i = 0; i < 8; i++) out[i] = (int32_t) (v[i] - k * a[i]);
}

static void test_weyl(void)
{
    int miss = 0, perm = 0;
    for (int a = 0; a < 240; a++) {
        unsigned char seen[240] = {0};
        for (int b = 0; b < 240; b++) {
            int32_t w[8];
            reflect(root[b], root[a], w);
            int k = root_index(w);
            if (k < 0)
                miss++;
            else
                seen[k]++;
        }
        for (int k = 0; k < 240; k++) perm += seen[k] != 1;
    }
    CHECK(miss == 0 && perm == 0, "every Weyl reflection permutes the 240 roots (%d, %d)", miss,
          perm);
    int bad = 0;
    for (int t = 0; t < 20000; t++) {
        int32_t v[8], w[8];
        int p = (int) (rnd() & 1u);
        uint32_t sum;
        do {
            sum = 0;
            for (int i = 0; i < 8; i++) v[i] = 2 * rnd_fx(20) + p, sum += (uint32_t) v[i];
        } while (sum & 3u);
        const int32_t *a = root[rnd() % 240];
        reflect(v, a, w);
        bad += !e8l_is_point2(w) || dot2(w, w) != dot2(v, v);
        int32_t back[8];
        reflect(w, a, back);
        bad += memcmp(back, v, sizeof v) != 0;
    }
    CHECK(bad == 0, "reflections keep lattice points, norms, and are involutions");
}

static void test_icosian_isometry(void)
{
    CHECK(e8_selfcheck() == 0, "e8_selfcheck");
    CHECK(memcmp(e8_gram(), E8L_ICOSIAN_GRAM, 64) == 0, "e8.c reads the shared Gram table");
    int64_t g[8][8];
    int bad = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int32_t a[8], b[8];
            for (int k = 0; k < 8; k++) a[k] = E8L_ICOSIAN2[i][k], b[k] = E8L_ICOSIAN2[j][k];
            bad += dot2(a, b) != 4 * E8L_ICOSIAN_GRAM[8 * i + j];
            g[i][j] = E8L_ICOSIAN_GRAM[8 * i + j];
        }
    CHECK(bad == 0, "icosian basis images have the icosian Gram matrix");
    CHECK(det8(g) == 1, "icosian Gram determinant 1");
    /* All 240 icosian roots, by enumeration of coefficients (every root has
     * coefficients within +-2 in this basis), map to the 240 roots. */
    static icos_t qroot[240];
    CHECK(e8_roots(qroot, 240) == 240, "e8_roots");
    unsigned char hit[240] = {0}, qhit[240] = {0};
    int found = 0, notroot = 0, rt = 0, qbad = 0;
    int64_t c[8];
    for (int idx = 0; idx < 390625; idx++) {
        int v = idx;
        for (int i = 0; i < 8; i++) c[i] = v % 5 - 2, v /= 5;
        bool ok = false;
        if (e8_norm(e8_from_coeffs(c), &ok) != 2 || !ok) continue;
        found++;
        int64_t v2[8];
        int32_t w[8];
        e8_pt_t p = e8_from_coeffs(c), back;
        CHECK(e8_to_coords2(p, v2), "to coords");
        for (int i = 0; i < 8; i++) w[i] = (int32_t) v2[i];
        int k = root_index(w);
        if (k < 0)
            notroot++;
        else
            hit[k]++;
        rt += !e8_from_coords2(v2, &back) || memcmp(back.c, c, sizeof c) != 0;
        icos_t q;
        int m = -1;
        if (e8_expand(p, &q))
            for (int j = 0; j < 240 && m < 0; j++)
                if (icos_eq(q, qroot[j])) m = j;
        if (m < 0)
            qbad++;
        else
            qhit[m]++;
    }
    int onto = 0, qonto = 0;
    for (int k = 0; k < 240; k++) onto += hit[k] == 1, qonto += qhit[k] == 1;
    CHECK(found == 240 && notroot == 0 && onto == 240,
          "the 240 icosian roots map one to one onto the coordinate roots (%d, %d, %d)", found,
          notroot, onto);
    CHECK(rt == 0, "e8_from_coords2 inverts e8_to_coords2 on the roots");
    CHECK(qbad == 0 && qonto == 240, "and they are exactly e8_roots()'s 240 quaternions");
    /* random lattice points: norms and inner products agree */
    int iso = 0;
    for (int t = 0; t < 5000; t++) {
        int64_t a[8], b[8], va[8], vb[8];
        for (int i = 0; i < 8; i++) a[i] = rnd_fx(1000), b[i] = rnd_fx(1000);
        bool ok1, ok2, ok3;
        int64_t na = e8_norm(e8_from_coeffs(a), &ok1), nb = e8_norm(e8_from_coeffs(b), &ok2);
        int64_t s[8];
        for (int i = 0; i < 8; i++) s[i] = a[i] + b[i];
        int64_t ns = e8_norm(e8_from_coeffs(s), &ok3);
        e8_to_coords2(e8_from_coeffs(a), va);
        e8_to_coords2(e8_from_coeffs(b), vb);
        int64_t daa = 0, dab = 0;
        int32_t wa[8];
        for (int i = 0; i < 8; i++)
            daa += va[i] * va[i], dab += va[i] * vb[i], wa[i] = (int32_t) va[i];
        iso += !ok1 || !ok2 || !ok3 || daa != 4 * na || 2 * dab != 4 * (ns - na - nb) ||
               !e8l_is_point2(wa);
    }
    CHECK(iso == 0, "the map is an isometry on random lattice points (%d bad)", iso);
    int64_t off[8] = {1, 0, 0, 0, 0, 0, 0, 1}; /* norm 1/2: not in E8 */
    e8_pt_t p;
    CHECK(!e8_from_coords2(off, &p), "off-lattice coordinates refused");
}

/* Distance from 2x to v s, in units where int64 is exact for these tests. */
static int64_t dist(const zt_fx x[8], zt_fx s, const int32_t v[8])
{
    int64_t d = 0;
    for (int i = 0; i < 8; i++) {
        int64_t e = 2 * (int64_t) x[i] - (int64_t) v[i] * s;
        d += e * e;
    }
    return d;
}

static void test_nearest_certificate(void)
{
    /* A lattice point v is nearest to x exactly when no Voronoi-relevant
     * vector w improves it; for E8 those are the 240 roots and the 2160
     * vectors of norm 4 (codebook shells 1 and 2). */
    int beaten = 0, trials = 0;
    for (int t = 0; t < 3000; t++) {
        zt_fx s = 1 + (zt_fx) (rnd() % 100000), x[8];
        for (int i = 0; i < 8; i++) x[i] = rnd_fx(s * 8);
        int8_t v8[8];
        zt_e8_nearest(x, s, v8);
        int32_t v[8];
        for (int i = 0; i < 8; i++) v[i] = v8[i];
        int64_t dv = dist(x, s, v);
        for (uint32_t k = ZT_E8_SHELL0; k < ZT_E8_SHELL2; k++) {
            int32_t w[8];
            for (int i = 0; i < 8; i++) w[i] = v[i] + book[8 * k + i];
            if (dist(x, s, w) < dv) {
                beaten++;
                break;
            }
        }
        trials++;
    }
    CHECK(beaten == 0, "zt_e8_nearest passes the Voronoi certificate on %d inputs (%d beaten)",
          trials, beaten);
}

static void test_holo_e8(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 6, ZT_WIND_GOLDEN);
    uint32_t n = c.total;
    zt_fx *x = malloc(8 * n * sizeof *x);
    int8_t *v = malloc(8 * n), *d = malloc(8 * n);
    int16_t *r = malloc(16 * n);
    /* tree-shaped data in 8-wide blocks: each place is its parent plus a
     * little, quantised to E8 with one scale */
    const zt_fx s = 4096;
    for (uint32_t j = 0; j < c.size[0]; j++)
        for (int k = 0; k < 8; k++) x[8 * j + k] = rnd_fx(40 * s);
    for (uint32_t sh = 1; sh < ZT_COIL_SHELLS; sh++)
        for (uint32_t j = 0; j < c.size[sh]; j++) {
            zt_place_t p = {sh, j}, q = zt_coil_parent(&c, p);
            uint32_t a = c.offset[sh] + j, b = c.offset[q.shell] + q.slot;
            for (int k = 0; k < 8; k++) {
                int64_t t = (int64_t) x[8 * b + k] + rnd_fx(s / 2);
                x[8 * a + k] = (zt_fx) (t > 50 * s ? 50 * s : t < -50 * s ? -50 * s : t);
            }
        }
    for (uint32_t i = 0; i < n; i++) zt_e8_nearest(x + 8 * i, s, v + 8 * i);
    CHECK(zt_holo_e8_encode(&c, v, r), "encode");
    int notpt = 0, inball = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t w[8];
        int8_t w8[8];
        bool fits = true;
        for (int k = 0; k < 8; k++) {
            w[k] = r[8 * i + k];
            fits &= w[k] >= -128 && w[k] <= 127;
            w8[k] = (int8_t) (fits ? w[k] : 0);
        }
        notpt += !e8l_is_point2(w);
        if (i >= c.size[0] && fits && zt_e8_encode(book, w8) >= 0) inball++;
    }
    CHECK(notpt == 0, "every residual is an E8 point (%d not)", notpt);
    zt_holo_e8_decode(&c, r, ZT_COIL_SHELLS, d);
    CHECK(memcmp(d, v, 8 * n) == 0, "E8 holographic decode is exact");
    double frac = (double) inball / (n - c.size[0]);
    CHECK(frac > 0.9, "residuals of smooth data are codebook points (%.1f%%)", 100 * frac);
    zt_holo_e8_decode(&c, r, 3, d);
    int coarse = 0;
    for (uint32_t i = 0; i < c.offset[3]; i++) coarse += memcmp(d + 8 * i, v + 8 * i, 8) != 0;
    CHECK(coarse == 0, "partial decode is exact on the decoded shells");
    v[3] ^= 1; /* parity broken: not a lattice point */
    CHECK(!zt_holo_e8_encode(&c, v, r), "non-lattice input refused");
    printf("  E8 holographic code: %u places, %.1f%% of residuals are 15-bit codebook indices\n", n,
           100 * frac);
    free(x);
    free(v);
    free(d);
    free(r);
}

int main(void)
{
    test_roots();
    test_cartan();
    test_weyl();
    test_icosian_isometry();
    test_nearest_certificate();
    test_holo_e8();
    printf("test_zt_e8: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
