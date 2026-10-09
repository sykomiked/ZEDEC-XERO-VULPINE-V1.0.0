/* test_e8.c — host tests for ℤ[φ] and the icosian E8 construction.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "e8.h"

static int fails = 0;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: ");                                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* Complete enumeration of lattice vectors up to a norm bound, by
 * Cholesky-free integer search over a coefficient box. The box is wide enough
 * for norm 2 with this basis (verified against an exact bounded enumeration
 * offline); the point of the test is the COUNT, which is the lattice's
 * fingerprint. */
static long count_norm(int64_t target, int box)
{
    long n = 0;
    int64_t c[E8_DIM];
    for (c[0] = -box; c[0] <= box; c[0]++)
        for (c[1] = -box; c[1] <= box; c[1]++)
            for (c[2] = -box; c[2] <= box; c[2]++)
                for (c[3] = -box; c[3] <= box; c[3]++)
                    for (c[4] = -box; c[4] <= box; c[4]++)
                        for (c[5] = -box; c[5] <= box; c[5]++)
                            for (c[6] = -box; c[6] <= box; c[6]++)
                                for (c[7] = -box; c[7] <= box; c[7]++) {
                                    bool ok = false;
                                    if (e8_norm(e8_from_coeffs(c), &ok) == target && ok) n++;
                                }
    return n;
}

int main(void)
{
    printf("=== zphi: exact golden integers ===\n");
    {
        uint32_t b = zphi_selfcheck();
        CHECK(b == 0, "zphi_selfcheck reported %u problems", b);
        printf("  phi^2 == phi+1, both roots, norm multiplicative, "
               "phi^k == F(k)phi+F(k-1) to k=20, overflow reported: %s\n",
               b == 0 ? "OK" : "BROKEN");
    }

    printf("\n=== the 600-cell (icosians) ===\n");
    {
        CHECK(e8_icosian_count() == 120, "icosian count is %u, want 120", e8_icosian_count());
        int units = 0, distinct = 1;
        for (uint32_t i = 0; i < 120; i++) {
            icos_t q;
            if (!e8_icosian(i, &q)) continue;
            if (zphi_eq(icos_norm4(q), zphi_int(4))) units++;
            for (uint32_t j = i + 1; j < 120; j++) {
                icos_t r;
                if (e8_icosian(j, &r) && icos_eq(q, r)) distinct = 0;
            }
        }
        CHECK(units == 120, "only %d of 120 are unit quaternions", units);
        CHECK(distinct, "the 120 icosians are not distinct");
        printf("  120 distinct unit quaternions, all with sum h^2 == 4: OK\n");
    }

    printf("\n=== 2I closure (binary icosahedral group) ===\n");
    {
        int closed = 1;
        for (uint32_t i = 0; i < 120 && closed; i += 3) {
            for (uint32_t j = 0; j < 120 && closed; j += 5) {
                icos_t a, b, p;
                int found = 0;
                e8_icosian(i, &a);
                e8_icosian(j, &b);
                p = icos_mul(a, b);
                for (uint32_t k = 0; k < 120 && !found; k++) {
                    icos_t c;
                    e8_icosian(k, &c);
                    if (icos_eq(p, c)) found = 1;
                }
                if (!found) closed = 0;
            }
        }
        CHECK(closed, "icosians are NOT closed under quaternion multiplication");
        printf("  every sampled product is again an icosian: OK\n");
    }

    printf("\n=== the E8 Gram matrix ===\n");
    {
        const int8_t *G = e8_gram();
        printf("  ");
        for (uint32_t i = 0; i < 8; i++) {
            for (uint32_t j = 0; j < 8; j++) printf("%2d ", G[i * 8 + j]);
            printf(i == 7 ? "\n" : "\n  ");
        }
        int even = 1, sym = 1;
        for (uint32_t i = 0; i < 8; i++) {
            if (G[i * 8 + i] % 2) even = 0;
            for (uint32_t j = 0; j < 8; j++)
                if (G[i * 8 + j] != G[j * 8 + i]) sym = 0;
        }
        CHECK(even, "diagonal is not even");
        CHECK(sym, "Gram is not symmetric");
    }

    printf("\n=== theta series — the lattice's fingerprint ===\n");
    {
        long n2 = count_norm(2, 2);
        CHECK(n2 == 240, "norm-2 vectors: %ld, want 240", n2);
        printf("  vectors of norm 2 = %ld  (E8 kissing number, proven optimal)\n", n2);
    }

    printf("\n=== the phi shell identity ===\n");
    {
        icos_t roots[240];
        uint32_t n = e8_roots(roots, 240);
        CHECK(n == 240, "root count %u, want 240", n);
        /* inner shell norm4 == 4(2-phi); outer == 4; inner * phi^2 == outer */
        zphi_t g2 = zphi_mul(zphi_golden(), zphi_golden());
        int idok = 1;
        for (uint32_t i = 0; i < 120; i++) {
            zphi_t outer = icos_norm4(roots[i]);
            zphi_t inner = icos_norm4(roots[i + 120]);
            if (!zphi_eq(zphi_mul(inner, g2), outer)) idok = 0;
            /* both shells must consist of roots: Q = (U+V)/2 == 2 */
            if ((outer.a + outer.b) / 2 != 2) idok = 0;
            if ((inner.a + inner.b) / 2 != 2) idok = 0;
        }
        CHECK(idok, "the phi^2 shell-radius identity does not hold");
        printf("  240 roots = 120 icosians + 120 scaled by 1/phi\n");
        printf("  ratio of squared radii == phi^2 EXACTLY (identity in Z[phi]): %s\n",
               idok ? "OK" : "BROKEN");
    }

    printf("\n=== ISOMETRY_LIFT_M8 ===\n");
    {
        const int64_t m5[5] = {3, -1, 4, 1, -5};
        e8_pt_t p;
        int64_t back[5];
        CHECK(e8_lift_m5(m5, &p), "lift failed");
        CHECK(e8_project_m5(p, back), "project failed");
        int same = 1;
        for (int i = 0; i < 5; i++)
            if (back[i] != m5[i]) same = 0;
        CHECK(same, "lift/project is not an exact round trip");

        bool ok = false;
        int64_t n = e8_norm(p, &ok);
        CHECK(ok, "norm overflowed");
        CHECK(n % 2 == 0, "lifted point has odd norm %lld (E8 is an even lattice)", (long long) n);
        printf("  lift(3,-1,4,1,-5) -> E8 point of norm %lld (even), "
               "round-trip exact\n",
               (long long) n);

        /* refuses points outside the rank-5 image rather than truncating */
        int64_t outside[8] = {1, 1, 1, 1, 1, 0, 0, 7};
        CHECK(!e8_project_m5(e8_from_coeffs(outside), back),
              "project accepted a point outside the M5 sublattice");
        printf("  a point outside the image is refused, not truncated: OK\n");

        /* the induced metric is positive definite on a sweep */
        int pd = 1;
        for (int64_t a = -2; a <= 2; a++)
            for (int64_t b = -2; b <= 2; b++)
                for (int64_t c = -2; c <= 2; c++)
                    for (int64_t d = -2; d <= 2; d++)
                        for (int64_t e = -2; e <= 2; e++) {
                            const int64_t v[5] = {a, b, c, d, e};
                            bool o = false;
                            int64_t q = e8_m5_norm(v, &o);
                            if (!o) {
                                pd = 0;
                                break;
                            }
                            int zero = (a | b | c | d | e) == 0;
                            if (zero ? q != 0 : q <= 0) {
                                pd = 0;
                                break;
                            }
                        }
        CHECK(pd, "the induced M5 metric is NOT positive definite");
        printf("  induced M5 metric positive definite over 5^5 sweep: %s\n", pd ? "OK" : "BROKEN");

        /* distance is symmetric and zero only for equal states */
        const int64_t x[5] = {1, 2, 3, 4, 5}, y[5] = {5, 4, 3, 2, 1};
        bool o1 = false, o2 = false;
        CHECK(e8_m5_dist2(x, y, &o1) == e8_m5_dist2(y, x, &o2) && o1 && o2,
              "distance is not symmetric");
        bool o3 = false;
        CHECK(e8_m5_dist2(x, x, &o3) == 0 && o3, "d(x,x) != 0");
    }

    printf("\n=== full selfcheck ===\n");
    {
        uint32_t b = e8_selfcheck();
        CHECK(b == 0, "e8_selfcheck reported %u problems", b);
        printf("  e8_selfcheck(): %u problems\n", b);
    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
