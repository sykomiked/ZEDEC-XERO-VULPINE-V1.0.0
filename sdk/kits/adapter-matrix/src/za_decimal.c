/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_decimal.c — decimal text to an exact int64 rational, never through a
 * float (R11).
 *
 *   number = [ "-" ] digits [ "." digits ] [ ( "e" | "E" ) [ "+" | "-" ] digits ]
 *
 * The value is D * 10^E, where D is the digit string without its point.
 * Leading and trailing zeros of D are dropped first (trailing ones move to
 * E), so "1.50000000000000000000" is 3/2, not an overflow. For E < 0 the
 * denominator 10^-E = 2^k * 5^k is cancelled against D's own factors of 2
 * and 5 before it must fit, so "5e-19" is 1/2000000000000000000. Anything
 * still outside int64 is ZXV_E_RANGE: never rounded. */
#include <string.h>

#include "za_internal.h"

#define EXP_CAP 100000 /* exponents are saturated here; any |E| > 40 overflows */

int32_t za_decimal_parse(const char *text, size_t len, int64_t *out_num, int64_t *out_den) {
    size_t i = 0;
    int neg = 0;
    size_t nd = 0;     /* digits in the integer and fraction parts */
    long long frac = 0; /* digits after the point */
    long long e = 0;
    if (!text || !out_num || !out_den) return ZXV_E_ARG;
    if (i < len && text[i] == '-') {
        neg = 1;
        i++;
    }
    if (i >= len || text[i] < '0' || text[i] > '9') {
        za_set_error("decimal: expected a digit");
        return ZXV_E_FORMAT;
    }
    /* the digits: [int_start, int_end) and [frac_start, frac_end) */
    size_t int_start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
    size_t int_end = i, frac_start = i, frac_end = i;
    if (i < len && text[i] == '.') {
        i++;
        frac_start = i;
        while (i < len && text[i] >= '0' && text[i] <= '9') i++;
        frac_end = i;
        if (frac_end == frac_start) {
            za_set_error("decimal: expected a digit after '.'");
            return ZXV_E_FORMAT;
        }
    }
    if (i < len && (text[i] == 'e' || text[i] == 'E')) {
        int eneg = 0;
        i++;
        if (i < len && (text[i] == '+' || text[i] == '-')) eneg = text[i++] == '-';
        if (i >= len || text[i] < '0' || text[i] > '9') {
            za_set_error("decimal: expected exponent digits");
            return ZXV_E_FORMAT;
        }
        while (i < len && text[i] >= '0' && text[i] <= '9') {
            if (e < EXP_CAP) e = e * 10 + (text[i] - '0');
            i++;
        }
        if (eneg) e = -e;
    }
    if (i != len) {
        za_set_error("decimal: unexpected character");
        return ZXV_E_FORMAT;
    }
    frac = (long long) (frac_end - frac_start);
    nd = (int_end - int_start) + (frac_end - frac_start);

    /* the digit string D, as an index walk over both parts */
#define DIG(k) ((k) < (int_end - int_start) ? text[int_start + (k)] : text[frac_start + (k) - (int_end - int_start)])
    size_t lo = 0, hi = nd; /* D = digits [lo, hi) */
    while (lo < hi && DIG(lo) == '0') lo++;
    if (lo == hi) {
        *out_num = 0;
        *out_den = 1;
        return ZXV_OK;
    }
    long long ex = e - frac; /* value = D[lo,hi) * 10^ex */
    while (hi > lo && DIG(hi - 1u) == '0') {
        hi--;
        ex++;
    }
    if (hi - lo > 19u) {
        za_set_error("decimal: more than 19 significant digits do not fit int64 exactly");
        return ZXV_E_RANGE;
    }
    uint64_t d = 0;
    for (size_t k = lo; k < hi; k++) d = d * 10u + (uint64_t) (DIG(k) - '0');
#undef DIG
    if (d > (uint64_t) INT64_MAX) {
        za_set_error("decimal: does not fit int64 exactly");
        return ZXV_E_RANGE;
    }
    uint64_t den = 1;
    if (ex >= 0) {
        for (long long k = 0; k < ex; k++) {
            if (d > (uint64_t) INT64_MAX / 10u) {
                za_set_error("decimal: does not fit int64 exactly");
                return ZXV_E_RANGE;
            }
            d *= 10u;
        }
    } else {
        long long k2 = -ex, k5 = -ex;
        if (k2 > 200) {
            za_set_error("decimal: the denominator does not fit int64 exactly");
            return ZXV_E_RANGE;
        }
        while (k2 > 0 && (d % 2u) == 0) {
            d /= 2u;
            k2--;
        }
        while (k5 > 0 && (d % 5u) == 0) {
            d /= 5u;
            k5--;
        }
        for (long long k = 0; k < k2; k++) {
            if (den > (uint64_t) INT64_MAX / 2u) {
                za_set_error("decimal: the denominator does not fit int64 exactly");
                return ZXV_E_RANGE;
            }
            den *= 2u;
        }
        for (long long k = 0; k < k5; k++) {
            if (den > (uint64_t) INT64_MAX / 5u) {
                za_set_error("decimal: the denominator does not fit int64 exactly");
                return ZXV_E_RANGE;
            }
            den *= 5u;
        }
    }
    *out_num = neg ? -(int64_t) d : (int64_t) d;
    *out_den = (int64_t) den;
    return ZXV_OK;
}
