/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_q16.c — the portable (no __int128) Q16.16 rounding of
 * zxv_rat_to_q16 (src/zxv_q16.h), checked two ways:
 *   1. built with -DZXV_NO_INT128, so zxv_q16_round IS the portable path,
 *      against fixed expectations (the vectors.json q16 cases and the
 *      int32 edges);
 *   2. where the compiler has __int128, against the 128-bit reference on
 *      edge values and a deterministic pseudo-random sweep.
 * It also checks the shared library's zxv_rat_to_q16 agrees, so both
 * builds of the library give the same bits. */
#include <stdint.h>
#include <stdio.h>

#include "zxv_api.h"
#include "zxv_q16.h"

#ifdef ZXV_Q16_HAVE_INT128
#    error "test_q16.c must be built with -DZXV_NO_INT128 so the portable path is the one under test"
#endif

static int checks, failures;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

/* The 128-bit reference, written out here because zxv_q16.h hides it
 * under ZXV_NO_INT128. */
#ifdef __SIZEOF_INT128__
static int ref_q16(int64_t num, int64_t den, int32_t *out)
{
    __int128 scaled = (__int128) num * 65536;
    __int128 q = scaled / den, rem = scaled % den;
    if (rem < 0) rem = -rem;
    if (2 * rem >= (__int128) den) q += scaled < 0 ? -1 : 1;
    if (q > INT32_MAX || q < INT32_MIN) return 0;
    *out = (int32_t) q;
    return 1;
}
#endif

static int64_t gcd64(int64_t a, int64_t b)
{
    if (a < 0) a = -a;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static void one(int64_t num, int64_t den)
{
    int64_t g = gcd64(num, den);
    num /= g;
    den /= g;
    int32_t p = 0, l = 0;
    int okp = zxv_q16_round(num, den, &p);
    int32_t rc = zxv_rat_to_q16(num, den, &l);
    CHECK(okp == (rc == ZXV_OK) && (!okp || p == l), "%lld/%lld: portable %d/%d, library %s/%d",
          (long long) num, (long long) den, okp, p, zxv_status_name(rc), l);
#ifdef __SIZEOF_INT128__
    int32_t r = 0;
    int okr = ref_q16(num, den, &r);
    CHECK(okp == okr && (!okp || p == r), "%lld/%lld: portable %d/%d, int128 %d/%d", (long long) num,
          (long long) den, okp, p, okr, r);
#endif
}

int main(void)
{
    static const struct {
        int64_t n, d;
        int ok;
        int32_t q;
    } fixed[] = {
        {7, 10, 1, 45875},          {-1, 2, 1, -32768},       {1, 3, 1, 21845},
        {-1, 3, 1, -21845},         {32767, 1, 1, 2147418112}, {32768, 1, 0, 0},
        {-32768, 1, 1, INT32_MIN},  {-32769, 1, 0, 0},         {1, 131072, 1, 1},
        {-1, 131072, 1, -1},        {1, 131073, 1, 0},         {0, 1, 1, 0},
        {INT64_MAX, 1, 0, 0},       {-INT64_MAX, 1, 0, 0},     {1, INT64_MAX, 1, 0},
        {INT64_MAX, INT64_MAX - 1, 1, 65536},
        {(int64_t) 2147483647, 65536, 1, INT32_MAX},           /* exactly INT32_MAX */
        {(int64_t) 4294967295LL, 131072, 1, INT32_MAX},       /* INT32_MAX + 1/2 rounds up? */
    };
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        int32_t q = 0;
        int ok = zxv_q16_round(fixed[i].n, fixed[i].d, &q);
        if (i == sizeof fixed / sizeof fixed[0] - 1) {
            /* (2^32 - 1) / 2^17 * 2^16 = 2^31 - 1/2: rounds away to 2^31, out of range. */
            CHECK(!ok, "INT32_MAX + 1/2 is out of range");
            continue;
        }
        CHECK(ok == fixed[i].ok && (!ok || q == fixed[i].q), "fixed %zu: %lld/%lld -> %d %d", i,
              (long long) fixed[i].n, (long long) fixed[i].d, ok, q);
    }
    /* Edges around every power of two in numerator and denominator. */
    for (int a = 0; a < 63; a++)
        for (int b = 0; b < 63; b += 3) {
            int64_t n = (int64_t) 1 << a, d = (int64_t) 1 << b;
            one(n, d);
            one(-n, d);
            one(n - 1 > 0 ? n - 1 : 1, d + 1);
            one(-(n + (a < 62)), d > 1 ? d - 1 : 1);
        }
    /* Half-way cases: (2k + 1) / 2^17 lands exactly on half a Q16 unit. */
    for (int64_t k = -50; k <= 50; k++) one(2 * k + 1, 131072);
    /* Pseudo-random sweep across magnitudes. */
    for (int i = 0; i < 200000; i++) {
        int sh = (int) (next() % 63), sd = (int) (next() % 63);
        int64_t n = (int64_t) (next() >> (1 + sh));
        int64_t d = (int64_t) (next() >> (1 + sd));
        if (d == 0) d = 1;
        if (next() & 1) n = -n;
        one(n, d);
    }
    printf("test_q16 (portable path, ZXV_NO_INT128): %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
