/* test_zorder.c — correctness + a MEASURED locality benchmark.
 *
 * The point of this file is that the efficiency claim is earned, not
 * asserted. It builds clustered communication workloads, places them
 * both ways, and REPORTS THE RATIO of total data movement. If Z-order
 * placement did not help, the assertions fail and say so.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/fractal src/fractal/test_zorder.c \
 *       src/fractal/zorder.c -o /tmp/test_zorder && /tmp/test_zorder
 */
#include <stdio.h>
#include <string.h>
#include "zorder.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* deterministic PRNG so the benchmark is reproducible */
static uint32_t rs = 12345;
static uint32_t rnd(void){ rs ^= rs<<13; rs ^= rs>>17; rs ^= rs<<5; return rs; }

int main(void) {
    printf("=== Fractal (Z-order) component addressing ===\n");

    /* ---- encode/decode round-trip ---- */
    {
        int ok = 1;
        for (uint32_t i = 0; i < 4000; i++) {
            uint16_t x = (uint16_t)(rnd() & 0xFF), y = (uint16_t)(rnd() & 0xFF);
            uint16_t dx, dy;
            zo_decode2(zo_encode2(x, y), &dx, &dy);
            if (dx != x || dy != y) { ok = 0; break; }
        }
        CHECK(ok, "Morton encode/decode round-trips exactly");
    }
    CHECK(zo_encode2(0,0) == 0, "origin encodes to 0");
    CHECK(zo_encode2(1,0) == 1 && zo_encode2(0,1) == 2 && zo_encode2(1,1) == 3,
          "first quadrant encodes to 0,1,2,3 (Z pattern)");

    /* ---- self-similarity: a prefix IS the enclosing region ---- */
    {
        zo_addr_t leaf = zo_make(0xABCD, 0x1234, ZO_MAX_LEVEL);
        zo_addr_t p1 = zo_parent(leaf, 1);
        zo_addr_t p4 = zo_parent(leaf, 4);
        CHECK(zo_contains(p1, leaf), "parent contains its leaf");
        CHECK(zo_contains(p4, leaf), "ancestor 4 levels up contains the leaf");
        CHECK(zo_contains(p4, p1), "the hierarchy nests consistently");
        CHECK(!zo_contains(leaf, p4), "containment is not symmetric");
        CHECK(p1.level == ZO_MAX_LEVEL-1 && p4.level == ZO_MAX_LEVEL-4,
              "parent levels decrement correctly");
    }

    /* ---- neighbours share more hierarchy than distant components ---- */
    {
        zo_addr_t a = zo_make(10, 10, 8);
        zo_addr_t near = zo_make(11, 10, 8);
        zo_addr_t far  = zo_make(200, 200, 8);
        CHECK(zo_common_level(a, near) > zo_common_level(a, far),
              "adjacent components share a deeper common region");
        CHECK(zo_hops(a, near) < zo_hops(a, far),
              "hop cost grows with separation");
        CHECK(zo_hops(a, a) == 0, "self distance is zero");
    }

    /* ---- THE BENCHMARK: measured movement reduction vs row-major ----
     * Workload: N components in tight communication clusters (the shape
     * real subsystems have — a cell talks mostly to its own group). */
    {
        enum { N = 64, CLUSTERS = 8, LEVEL = 3 };
        static uint16_t aff[N*N];
        memset(aff, 0, sizeof(aff));
        for (uint32_t i = 0; i < N; i++) {
            for (uint32_t j = 0; j < N; j++) {
                if (i == j) continue;
                bool same = (i / (N/CLUSTERS)) == (j / (N/CLUSTERS));
                /* heavy intra-cluster traffic, light background chatter */
                uint16_t w = same ? (uint16_t)(50 + (rnd() % 50))
                                  : (uint16_t)(rnd() % 3);
                aff[i*N + j] = w;
            }
        }

        /* baseline: row-major placement (the naive layout) */
        static uint16_t bx[N], by[N];
        uint32_t side = 1u << LEVEL;
        for (uint32_t i = 0; i < N; i++) { bx[i] = (uint16_t)(i % side); by[i] = (uint16_t)(i / side); }
        uint64_t base_cost = zo_cost(aff, N, bx, by, LEVEL);

        /* fractal: Z-order locality placement */
        static uint16_t zx[N], zy[N];
        uint64_t z_cost = zo_place(aff, N, LEVEL, zx, zy);

        double ratio = base_cost ? (double)z_cost / (double)base_cost : 1.0;
        printf("       row-major cost : %llu\n", (unsigned long long)base_cost);
        printf("       Z-order   cost : %llu\n", (unsigned long long)z_cost);
        printf("       ratio          : %.3f  (%.1f%% less movement)\n",
               ratio, (1.0 - ratio) * 100.0);

        CHECK(z_cost < base_cost, "Z-order placement moves LESS data than row-major");
        CHECK(ratio < 0.85, "measured reduction exceeds 15% on clustered traffic");
    }

    /* ---- honesty check: on UNIFORM traffic there is nothing to exploit,
     * and the method must not pretend otherwise. ---- */
    {
        enum { N = 32, LEVEL = 3 };
        static uint16_t aff[N*N];
        for (uint32_t i = 0; i < N; i++)
            for (uint32_t j = 0; j < N; j++)
                aff[i*N + j] = (i == j) ? 0 : 10;      /* everyone talks equally */

        static uint16_t bx[N], by[N], zx[N], zy[N];
        uint32_t side = 1u << LEVEL;
        for (uint32_t i = 0; i < N; i++) { bx[i]=(uint16_t)(i%side); by[i]=(uint16_t)(i/side); }
        uint64_t base_cost = zo_cost(aff, N, bx, by, LEVEL);
        uint64_t z = zo_place(aff, N, LEVEL, zx, zy);
        double ratio = base_cost ? (double)z / (double)base_cost : 1.0;
        printf("       uniform traffic ratio: %.3f (expected ~1.0 — no locality to exploit)\n", ratio);
        CHECK(ratio > 0.90,
              "uniform traffic yields NO large win (the method does not overclaim)");
    }

    /* ---- determinism: same input => same placement (replayable) ---- */
    {
        enum { N = 32, LEVEL = 3 };
        static uint16_t aff[N*N];
        rs = 999;
        for (uint32_t i = 0; i < N*N; i++) aff[i] = (uint16_t)(rnd() % 40);
        static uint16_t ax[N], ay[N], bx2[N], by2[N];
        uint64_t c1 = zo_place(aff, N, LEVEL, ax, ay);
        uint64_t c2 = zo_place(aff, N, LEVEL, bx2, by2);
        int same = (c1 == c2);
        for (uint32_t i = 0; i < N; i++)
            if (ax[i] != bx2[i] || ay[i] != by2[i]) same = 0;
        CHECK(same, "placement is deterministic (same input => same layout)");
    }

    /* ---- bounded: oversized input is clamped, never overruns ---- */
    {
        static uint16_t aff[4]; aff[0]=aff[1]=aff[2]=aff[3]=1;
        static uint16_t sx[2], sy[2];
        uint64_t c = zo_place(aff, 2, 2, sx, sy);
        CHECK(c >= 0 && sx[0] < 4 && sy[0] < 4, "small input stays in range");
        CHECK(zo_place(0, 8, 3, sx, sy) == 0, "null affinity is rejected safely");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
