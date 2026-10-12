/* test_rational_narrow.c — the no-__int128 ("narrow") path of rational.c.
 *
 * 32-bit targets (arm32, riscv32) have no 128-bit integer type. Before
 * 2026-10-12 rational.c fell back to plain 64-bit arithmetic there with no
 * overflow check, so a large sum or product wrapped and came back with
 * valid == true. This test is built with -DRAT_NO_WIDE, which forces the
 * narrow path on a 64-bit host, and checks it against an __int128 oracle:
 *
 *   - add, sub, mul, div, cmp, parse, to_string and to_fixed must agree
 *     with the oracle exactly, including refusing every result that does
 *     not fit in 64 bits and accepting every result that does;
 *   - nothing may ever return a wrong value with valid == true.
 *
 * The tier 1 and tier 3 rational suites (tests/01_root_axioms,
 * tests/03_metamorphic) also pass with -DRAT_NO_WIDE.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "rational.h"

#if !defined(RAT_NO_WIDE)
#    error "build this test with -DRAT_NO_WIDE (see the kernel Makefile)"
#endif
#if !defined(__SIZEOF_INT128__)
#    error "the oracle needs a host with __int128"
#endif

typedef __int128 i128;
static const int64_t MAX = 0x7FFFFFFFFFFFFFFFLL;
static int F = 0;
static long checks = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            F++;                                                                                   \
        }                                                                                          \
    } while (0)

/* ---- oracle: reduce an exact 128-bit fraction, report whether it fits ---- */
static bool oracle(i128 n, i128 d, rat_t *out)
{
    if (d == 0) return false;
    if (d < 0) {
        n = -n;
        d = -d;
    }
    i128 a = n < 0 ? -n : n, b = d;
    while (b) {
        i128 t = a % b;
        a = b;
        b = t;
    }
    if (a == 0) a = 1;
    n /= a;
    d /= a;
    if (n == 0) d = 1;
    if (n > MAX || n < -(i128) MAX || d > MAX) return false;
    out->num = (int64_t) n;
    out->den = (int64_t) d;
    out->valid = true;
    return true;
}

static bool same(rat_t x, rat_t y)
{
    return x.valid && y.valid && x.num == y.num && x.den == y.den;
}

/* ---- deterministic generator (xorshift64, masked so no shift drops a
 * set bit: this test also runs under -fsanitize=integer) ---- */
static uint64_t seed = 0x5A58560000000001ULL;
static uint64_t next(void)
{
    uint64_t x = seed;
    x ^= (x & (UINT64_MAX >> 13)) << 13;
    x ^= x >> 7;
    x ^= (x & (UINT64_MAX >> 17)) << 17;
    return seed = x;
}
/* magnitudes spread across every bit width, so both tiny and huge values occur */
static int64_t rnd_mag(void)
{
    unsigned bits = (unsigned) (next() % 63) + 1;
    uint64_t v = next() >> (64 - bits);
    return (int64_t) v;
}
static rat_t rnd_rat(void)
{
    for (;;) {
        int64_t n = rnd_mag(), d = rnd_mag();
        if (d == 0) continue;
        if (next() & 1) n = -n;
        rat_t r = rat_make(n, d);
        if (r.valid) return r;
    }
}

static void fixed_cases(void)
{
    char b[96];
    bool ex;
    rat_t big = rat_from_int(MAX);
    /* the silent-wrap cases the old fallback got wrong */
    CK(!rat_mul(big, rat_from_int(2)).valid, "I64_MAX * 2 refuses");
    CK(!rat_add(big, rat_from_int(1)).valid, "I64_MAX + 1 refuses");
    CK(!rat_sub(rat_from_int(-MAX), rat_from_int(2)).valid, "-I64_MAX - 2 refuses");
    CK(!rat_add(rat_make(1, MAX), rat_make(1, MAX - 1)).valid,
       "1/I64_MAX + 1/(I64_MAX-1) refuses (denominator too big)");
    CK(!rat_make(-MAX - 1, 1).valid, "INT64_MIN numerator refuses");
    CK(!rat_make(1, -MAX - 1).valid, "INT64_MIN denominator refuses");
    CK(same(rat_make(-MAX - 1, 2), rat_from_int(-(MAX / 2) - 1)), "INT64_MIN/2 reduces to -2^62");
    CK(same(rat_make(6, -MAX - 1), rat_make(-3, 1LL << 62)), "6/INT64_MIN reduces to -3/2^62");
    /* sums whose unreduced numerator needs more than 64 bits */
    CK(same(rat_add(rat_make(MAX - 1, 6), rat_make(MAX - 1, 3)), rat_make(MAX - 1, 2)),
       "(I64_MAX-1)/6 + (I64_MAX-1)/3 = (I64_MAX-1)/2");
    CK(same(rat_from_string("4611686018427387903.5"), rat_make(MAX, 2)),
       "parse 4611686018427387903.5 = I64_MAX/2");
    CK(!rat_from_string("922337203685477580.9").valid, "decimal just past I64_MAX refuses");
    CK(same(rat_from_string("922337203685477580.7"), rat_make(MAX, 10)),
       "decimal exactly I64_MAX/10 parses");
    /* exact results that fit must still come back */
    CK(same(rat_add(rat_make(1, 3), rat_make(1, 6)), rat_make(1, 2)), "1/3 + 1/6 = 1/2");
    CK(same(rat_add(rat_from_string("0.1"), rat_from_string("0.2")), rat_from_string("0.3")),
       "0.1 + 0.2 = 0.3");
    CK(same(rat_mul(rat_make(MAX, 3), rat_make(3, MAX)), rat_from_int(1)),
       "cross-reduction keeps I64_MAX/3 * 3/I64_MAX exact");
    CK(same(rat_add(big, rat_neg(big)), rat_zero()), "I64_MAX - I64_MAX = 0/1");
    CK(same(rat_sub(rat_make(5, MAX), rat_make(5, MAX)), rat_zero()),
       "5/I64_MAX - 5/I64_MAX = 0/1 (denominator renormalised)");
    /* comparisons whose cross products need 128 bits */
    CK(rat_cmp(rat_make(MAX, MAX - 1), rat_make(MAX - 1, MAX - 2)) == -1,
       "cmp near 1 with I64_MAX-sized terms");
    CK(rat_cmp(rat_make(-MAX, 3), rat_make(-MAX, 7)) == -1, "cmp of large negatives");
    /* rendering: integer part split avoids the wrap on every target */
    rat_to_string(rat_make(MAX, 1LL << 18), b, sizeof b);
    CK(strcmp(b, "35184372088831.999996185302734375") == 0,
       "to_string I64_MAX/2^18 is exact (old 64-bit scale would wrap)");
    rat_to_string(rat_make(-MAX, 3814697265625LL), b, sizeof b);
    CK(strcmp(b, "-2417851.639229258349150208") == 0, "to_string -I64_MAX/5^18 is exact");
    rat_to_fixed(rat_make(MAX, 3), 18, b, sizeof b, &ex);
    CK(strcmp(b, "3074457345618258602.333333333333333333") == 0 && !ex,
       "to_fixed I64_MAX/3 at 18 places");
    rat_to_fixed(rat_make(-MAX, MAX - 1), 18, b, sizeof b, &ex);
    CK(strcmp(b, "-1.000000000000000000") == 0 && !ex, "to_fixed rounds -I64_MAX/(I64_MAX-1)");
    rat_to_fixed(rat_make(MAX, 1), 0, b, sizeof b, &ex);
    CK(strcmp(b, "9223372036854775807") == 0 && ex, "to_fixed I64_MAX at 0 places");
}

/* oracle renderings of to_fixed, built from __int128 like the wide path */
static void oracle_fixed(rat_t a, uint32_t places, char *out, size_t max, bool *exact)
{
    i128 scale = 1;
    for (uint32_t i = 0; i < places; i++) scale *= 10;
    bool neg = a.num < 0;
    i128 n = (i128) (neg ? -a.num : a.num) * scale;
    i128 q = n / a.den, rem = n % a.den;
    *exact = rem == 0;
    if (rem * 2 >= a.den) q += 1;
    unsigned long long ip = (unsigned long long) (q / scale), fp = (unsigned long long) (q % scale);
    if (places)
        snprintf(out, max, "%s%llu.%0*llu", (neg && q) ? "-" : "", ip, (int) places, fp);
    else
        snprintf(out, max, "%s%llu", (neg && q) ? "-" : "", ip);
}

static void differential(long rounds)
{
    for (long i = 0; i < rounds; i++) {
        rat_t a = rnd_rat(), b = rnd_rat(), want;
        /* add and sub: exact agreement, including refusal */
        rat_t s = rat_add(a, b);
        bool fits =
            oracle((i128) a.num * b.den + (i128) b.num * a.den, (i128) a.den * b.den, &want);
        CK(s.valid == fits && (!fits || same(s, want)), "add agrees with oracle");
        s = rat_sub(a, b);
        fits = oracle((i128) a.num * b.den - (i128) b.num * a.den, (i128) a.den * b.den, &want);
        CK(s.valid == fits && (!fits || same(s, want)), "sub agrees with oracle");
        /* mul and div: exact agreement, including refusal */
        rat_t m = rat_mul(a, b);
        fits = oracle((i128) a.num * b.num, (i128) a.den * b.den, &want);
        CK(m.valid == fits && (!fits || same(m, want)), "mul agrees with oracle");
        if (b.num != 0) {
            rat_t q = rat_div(a, b);
            fits = oracle((i128) a.num * b.den, (i128) a.den * b.num, &want);
            CK(q.valid == fits && (!fits || same(q, want)), "div agrees with oracle");
        }
        /* cmp: always exact */
        i128 l = (i128) a.num * b.den, r = (i128) b.num * a.den;
        CK(rat_cmp(a, b) == ((l < r) ? -1 : (l > r) ? 1 : 0), "cmp agrees with oracle");
        /* to_fixed: exact agreement at a random precision */
        char got[96], exp[96];
        bool ge, ee;
        uint32_t places = (uint32_t) (next() % 19);
        rat_to_fixed(a, places, got, sizeof got, &ge);
        oracle_fixed(a, places, exp, sizeof exp, &ee);
        CK(strcmp(got, exp) == 0 && ge == ee, "to_fixed agrees with oracle");
    }
}

int main(void)
{
    fixed_cases();
    differential(200000);
    printf("%ld checks\n", checks);
    printf("\n%s: %d failure(s)\n", F ? "FAILED" : "ALL PASS", F);
    return F ? 1 : 0;
}
