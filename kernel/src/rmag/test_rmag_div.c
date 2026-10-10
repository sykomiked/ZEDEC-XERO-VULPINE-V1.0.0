/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_rmag_div.c — the two rational bugs found while modelling rmag for
 * proofs/rational_bounds/RationalBounds.lean, and the fixed behaviour:
 *   1. rmag_div_quotas(x, 0) used to return sign(x)/1 (5 / 0 == 1), because
 *      rational_normalize maps any x/0 to sign(x)/1. It now returns 0/1 and
 *      rmag_div_quotas_checked reports the zero divisor and int64 overflow.
 *   2. rmag_rational_add(x, -x) with two unreduced representations whose
 *      common denominator overflows 64 bits returned +/-UINT64_MAX, with the
 *      sign depending on the argument order. It now returns exactly 0. */
#include <stdio.h>
#include <stdint.h>
#include "m5_types.h"
#include "rmag_core.h"
#include "rmag.h"

static int failures;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int req(rational_t r, int64_t n, int64_t d)
{
    return r.num == n && r.den == d;
}

int main(void)
{
    rational_t five = {5, 1}, zero = {0, 1}, zero7 = {0, 7}, q;

    /* 1. division by zero */
    CHECK(req(rmag_div_quotas(five, zero), 0, 1), "5 / 0 is 0/1, not the old fabricated 1/1");
    CHECK(req(rmag_div_quotas((rational_t){-5, 1}, zero7), 0, 1), "-5 / (0/7) is 0/1, not -1/1");
    CHECK(req(rmag_div_quotas(five, (rational_t){3, 0}), 0, 1), "a den == 0 divisor is refused");
    CHECK(req(rmag_div_quotas((rational_t){3, 0}, five), 0, 1), "a den == 0 dividend is refused");
    q.num = 42;
    q.den = 42;
    CHECK(!rmag_div_quotas_checked(five, zero, &q) && q.num == 42 && q.den == 42,
          "checked: zero divisor reported, out untouched");
    CHECK(!rmag_div_quotas_checked((rational_t){INT64_MAX, 1}, (rational_t){1, 2}, &q),
          "checked: a.num * b.den overflow reported");
    CHECK(!rmag_div_quotas_checked((rational_t){1, INT64_MAX}, (rational_t){3, 1}, &q),
          "checked: a.den * b.num overflow reported");
    CHECK(!rmag_div_quotas_checked((rational_t){INT64_MIN, 1}, (rational_t){1, 1}, &q),
          "checked: INT64_MIN operand reported");
    CHECK(rmag_div_quotas_checked((rational_t){1, 3}, (rational_t){2, 5}, &q) && req(q, 5, 6),
          "checked: 1/3 / 2/5 == 5/6");
    CHECK(rmag_div_quotas_checked((rational_t){1, 3}, (rational_t){-2, 5}, &q) && req(q, -5, 6),
          "checked: negative divisor, denominator stays positive");
    CHECK(req(rmag_div_quotas((rational_t){1, 3}, (rational_t){-2, 5}), -5, 6),
          "unchecked: 1/3 / -2/5 == -5/6");
    CHECK(req(rmag_div_quotas((rational_t){6, 4}, (rational_t){3, 2}), 1, 1), "6/4 / 3/2 == 1");

    /* normalization: den > 0 whenever the input den != 0 */
    int ok = 1;
    for (int64_t n = -6; n <= 6; n++)
        for (int64_t d = -6; d <= 6; d++) {
            if (!d) continue;
            rational_t r = rational_normalize((rational_t){n, d});
            if (r.den <= 0 || r.num * d != n * r.den) ok = 0;
        }
    CHECK(ok, "rational_normalize: den > 0 and value kept, n, d in -6..6");

    /* 2. x + (-x) when the common denominator overflows */
    uint64_t m1 = (1ull << 32) + 1, m2 = (1ull << 32) + 3;          /* coprime */
    rmag_rational_t x = rmag_rational_from_frac(3 * m1, m1, false); /* 3, unreduced */
    rmag_rational_t y = rmag_rational_from_frac(3 * m2, m2, true);  /* -3, unreduced */
    rmag_rational_t s1 = rmag_rational_add(x, y), s2 = rmag_rational_add(y, x);
    CHECK(rmag_rational_is_zero(s1) && rmag_rational_is_zero(s2),
          "3 + (-3) == 0 in either order, unreduced inputs with overflowing lcm");
    rmag_rational_t z = rmag_rational_add(rmag_rational_from_frac(4 * m1, m1, false), y);
    CHECK(z.numerator == UINT64_MAX && !z.negative, "4 + (-3) with that lcm saturates positive");
    rmag_rational_t w = rmag_rational_add(y, rmag_rational_from_frac(4 * m1, m1, false));
    CHECK(w.numerator == UINT64_MAX && !w.negative, "and in the other order too");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
