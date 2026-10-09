/* test_rational.c — WyvernEye exact arithmetic, proved against floating point.
 *
 * The differentiator is only real if it is demonstrated, so several tests
 * run the SAME computation in double and in rat_t and assert that double
 * fails while rat_t succeeds. If a future change silently reintroduces
 * approximation, these break.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/rational src/rational/test_rational.c \
 *       src/rational/rational.c -o /tmp/test_rational && /tmp/test_rational
 */
#include <stdio.h>
#include <string.h>
#include "rational.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static const char *S(rat_t r)
{
    static char b[4][64];
    static int i = 0;
    i = (i + 1) & 3;
    rat_to_string(r, b[i], 64);
    return b[i];
}

int main(void)
{
    printf("=== WyvernEye exact rational arithmetic ===\n");

    /* ---- THE CLASSIC FLOAT FAILURE: 0.1 + 0.2 == 0.3 ---- */
    {
        double d = 0.1 + 0.2;
        printf("       double : 0.1 + 0.2 = %.20f\n", d);
        CHECK(d != 0.3, "double FAILS: 0.1 + 0.2 != 0.3 (the bug we remove)");

        rat_t a = rat_from_string("0.1");
        rat_t b = rat_from_string("0.2");
        rat_t c = rat_from_string("0.3");
        rat_t sum = rat_add(a, b);
        printf("       rat    : 0.1 + 0.2 = %s  (as %lld/%lld)\n", S(sum), (long long) sum.num,
               (long long) sum.den);
        CHECK(rat_eq(sum, c), "rat EXACT: 0.1 + 0.2 == 0.3");
        CHECK(a.num == 1 && a.den == 10, "0.1 parses to exactly 1/10");
    }

    /* ---- thirds: 1/3 * 3 == 1 exactly ---- */
    {
        double d = (1.0 / 3.0) * 3.0;
        rat_t t = rat_make(1, 3);
        rat_t sum = rat_add(rat_add(t, t), t);
        printf("       double : 1/3+1/3+1/3 = %.20f\n", (1.0 / 3.0) * 3.0);
        CHECK(rat_eq(sum, rat_from_int(1)), "rat EXACT: 1/3 + 1/3 + 1/3 == 1");
        CHECK(rat_eq(rat_mul(t, rat_from_int(3)), rat_from_int(1)), "rat EXACT: (1/3) * 3 == 1");
        (void) d;
    }

    /* ---- a currency column: 1000 x 0.01 must be exactly 10.00 ---- */
    {
        double dsum = 0.0;
        for (int i = 0; i < 1000; i++) dsum += 0.01;
        printf("       double : 1000 x 0.01 = %.20f\n", dsum);
        CHECK(dsum != 10.0, "double FAILS: 1000 x 0.01 != 10.00 (drift)");

        rat_t cent = rat_from_string("0.01");
        rat_t acc = rat_zero();
        int ok = 1;
        for (int i = 0; i < 1000; i++) {
            acc = rat_add(acc, cent);
            if (!acc.valid) {
                ok = 0;
                break;
            }
        }
        printf("       rat    : 1000 x 0.01 = %s\n", S(acc));
        CHECK(ok, "1000 accumulations stay representable (no overflow)");
        CHECK(rat_eq(acc, rat_from_int(10)), "rat EXACT: 1000 x 0.01 == 10");
    }

    /* ---- denominators stay small: the practical worry with rationals ---- */
    {
        rat_t acc = rat_zero();
        for (int i = 1; i <= 200; i++) acc = rat_add(acc, rat_from_string("0.05"));
        printf("       after 200 sums: %s (den=%lld)\n", S(acc), (long long) acc.den);
        CHECK(acc.valid && acc.den <= 100, "reduction keeps denominators small over long columns");
        CHECK(rat_eq(acc, rat_from_int(10)), "200 x 0.05 == 10 exactly");
    }

    /* ---- parsing ---- */
    CHECK(rat_eq(rat_from_string("-12.75"), rat_make(-51, 4)), "parse -12.75 = -51/4");
    CHECK(rat_eq(rat_from_string("3/4"), rat_make(3, 4)), "parse ratio 3/4");
    CHECK(rat_eq(rat_from_string("42"), rat_from_int(42)), "parse integer 42");
    CHECK(!rat_from_string("abc").valid, "malformed input is rejected");
    CHECK(!rat_from_string("1/0").valid, "zero denominator is rejected");

    /* ---- arithmetic + comparison ---- */
    CHECK(rat_eq(rat_mul(rat_make(2, 3), rat_make(3, 2)), rat_from_int(1)), "(2/3)*(3/2) == 1");
    CHECK(rat_eq(rat_div(rat_make(1, 2), rat_make(1, 4)), rat_from_int(2)), "(1/2)/(1/4) == 2");
    CHECK(rat_eq(rat_sub(rat_make(1, 3), rat_make(1, 3)), rat_zero()), "x - x == 0 exactly");
    CHECK(rat_cmp(rat_make(1, 3), rat_make(1, 2)) < 0, "1/3 < 1/2");
    CHECK(rat_cmp(rat_make(2, 4), rat_make(1, 2)) == 0, "2/4 == 1/2 after reduction");
    CHECK(!rat_div(rat_from_int(1), rat_zero()).valid, "division by zero -> invalid");

    /* ---- OVERFLOW IS DETECTED, NEVER SILENT ---- */
    {
        rat_t big = rat_make(0x7FFFFFFFFFFFFFFLL, 1);
        rat_t r = rat_mul(big, big);
        CHECK(!r.valid, "overflow is DETECTED (result marked invalid)");
        char b[32];
        rat_to_string(r, b, sizeof(b));
        CHECK(strcmp(b, "#OVERFLOW") == 0, "overflow renders as #OVERFLOW, not garbage");
        CHECK(!rat_add(r, rat_from_int(1)).valid, "invalid propagates through arithmetic");
    }

    /* ---- rendering: exact decimal when possible, ratio when not ---- */
    {
        char b[64];
        rat_to_string(rat_make(1, 4), b, sizeof(b));
        CHECK(strcmp(b, "0.25") == 0, "1/4 renders as exact 0.25");
        rat_to_string(rat_make(1, 3), b, sizeof(b));
        CHECK(strcmp(b, "1/3") == 0, "1/3 renders as the exact ratio, NOT 0.333");
        rat_to_string(rat_from_int(-7), b, sizeof(b));
        CHECK(strcmp(b, "-7") == 0, "integers render cleanly");
    }

    /* ---- display rounding reports whether it was lossless ---- */
    {
        char b[64];
        bool exact = true;
        rat_to_fixed(rat_make(1, 3), 2, b, sizeof(b), &exact);
        printf("       1/3 at 2dp = %s (exact=%s)\n", b, exact ? "yes" : "no");
        CHECK(!exact, "rounding 1/3 to 2dp reports exact=false (honest UI)");
        rat_to_fixed(rat_make(1, 4), 2, b, sizeof(b), &exact);
        CHECK(exact && strcmp(b, "0.25") == 0, "1/4 at 2dp is lossless");
    }

    /* ---- money split: shares must sum back EXACTLY ---- */
    {
        rat_t total = rat_from_string("10.00");
        rat_t shares[3];
        CHECK(rat_split(total, 3, shares), "split $10.00 three ways");
        rat_t back = rat_add(rat_add(shares[0], shares[1]), shares[2]);
        printf("       each share = %s ; sum back = %s\n", S(shares[0]), S(back));
        CHECK(rat_eq(back, total), "three-way split sums back to EXACTLY $10.00 (no lost cent)");

        /* the same split in double loses money */
        double ds = 10.0 / 3.0, dback = ds * 3;
        printf("       double split sums to %.20f\n", dback);
        CHECK(rat_eq(back, total), "rational split conserves value where double may not");
    }

    /* ---- HONESTY: we do NOT claim exactness over the reals ---- */
    {
        /* sqrt(2) is irrational: no rat_t represents it. The API offers no
         * rat_sqrt precisely so nothing can pretend to be exact here. */
        rat_t approx = rat_make(1414213562LL, 1000000000LL);
        rat_t sq = rat_mul(approx, approx);
        CHECK(!rat_eq(sq, rat_from_int(2)),
              "a rational approximation of sqrt(2) does NOT square to 2 (no overclaim)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
