/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_qty.c — exact quantities over the libzxv rationals (zxv_rat_*), and
 * conversion to integer minor units under an explicit rounding rule. A
 * value never passes through a float (R11); nothing is rounded unless the
 * caller names the rule, and the residual is returned exactly. */
#include <string.h>

#include "zind_common.h"

static const int64_t k_pow10[19] = {1LL,
                                    10LL,
                                    100LL,
                                    1000LL,
                                    10000LL,
                                    100000LL,
                                    1000000LL,
                                    10000000LL,
                                    100000000LL,
                                    1000000000LL,
                                    10000000000LL,
                                    100000000000LL,
                                    1000000000000LL,
                                    10000000000000LL,
                                    100000000000000LL,
                                    1000000000000000LL,
                                    10000000000000000LL,
                                    100000000000000000LL,
                                    1000000000000000000LL};

static int64_t gcd64(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int zind_qty_valid(zind_qty q) {
    if (q.den <= 0 || q.num == INT64_MIN) return 0;
    if (q.num == 0) return q.den == 1;
    return gcd64(q.num, q.den) == 1;
}

int zind_qty_is_int(zind_qty q) { return zind_qty_valid(q) && q.den == 1; }

int zind_qty_sign(zind_qty q) { return q.num > 0 ? 1 : (q.num < 0 ? -1 : 0); }

int32_t zind_qty_make(int64_t num, int64_t den, zind_qty *out) {
    int64_t n = 0, d = 1;
    int32_t st;
    if (!out) return ZXV_E_ARG;
    st = zxv_rat_normalize(num, den, &n, &d);
    if (st != ZXV_OK) {
        zind_set_error("zind_qty_make: %lld/%lld is not a representable rational (%s)",
                       (long long) num, (long long) den, zxv_status_name(st));
        return st;
    }
    out->num = n;
    out->den = d;
    return ZXV_OK;
}

int32_t zind_qty_parse(const char *text, zind_qty *out) {
    int64_t n = 0, d = 1;
    int32_t st;
    if (!text || !out) return ZXV_E_ARG;
    st = zxv_rat_parse(text, &n, &d);
    if (st != ZXV_OK) {
        zind_set_error("zind_qty_parse: \"%.40s\" is not an exact quantity (%s)", text,
                       zxv_status_name(st));
        return st;
    }
    out->num = n;
    out->den = d;
    return ZXV_OK;
}

int32_t zind_qty_format(zind_qty q, char *buf, size_t cap, size_t *out_len) {
    if (!zind_qty_valid(q)) return ZXV_E_ARG;
    return zxv_rat_format(q.num, q.den, buf, cap, out_len);
}

typedef int32_t (*ratop)(int64_t, int64_t, int64_t, int64_t, int64_t *, int64_t *);

static int32_t binop(ratop op, const char *name, zind_qty a, zind_qty b, zind_qty *out) {
    int64_t n = 0, d = 1;
    int32_t st;
    if (!out || !zind_qty_valid(a) || !zind_qty_valid(b)) return ZXV_E_ARG;
    st = op(a.num, a.den, b.num, b.den, &n, &d);
    if (st != ZXV_OK) {
        zind_set_error("zind_qty_%s: %lld/%lld and %lld/%lld: %s", name, (long long) a.num,
                       (long long) a.den, (long long) b.num, (long long) b.den,
                       zxv_status_name(st));
        return st;
    }
    out->num = n;
    out->den = d;
    return ZXV_OK;
}

int32_t zind_qty_add(zind_qty a, zind_qty b, zind_qty *out) {
    return binop(zxv_rat_add, "add", a, b, out);
}
int32_t zind_qty_sub(zind_qty a, zind_qty b, zind_qty *out) {
    return binop(zxv_rat_sub, "sub", a, b, out);
}
int32_t zind_qty_mul(zind_qty a, zind_qty b, zind_qty *out) {
    return binop(zxv_rat_mul, "mul", a, b, out);
}

int32_t zind_qty_cmp(zind_qty a, zind_qty b, int32_t *out_cmp) {
    if (!out_cmp || !zind_qty_valid(a) || !zind_qty_valid(b)) return ZXV_E_ARG;
    return zxv_rat_cmp(a.num, a.den, b.num, b.den, out_cmp);
}

int32_t zind_qty_from_minor(int64_t minor, uint8_t digits, zind_qty *out) {
    if (!out || digits > 18) return ZXV_E_ARG;
    return zind_qty_make(minor, k_pow10[digits], out);
}

int32_t zind_qty_to_minor(zind_qty q, uint8_t digits, int32_t round_mode, int64_t *out_minor,
                          zind_qty *out_residual) {
    int64_t n = 0, d = 1, fq, r, m;
    int32_t st;
    int up;
    if (!out_minor || digits > 18 || !zind_qty_valid(q) || round_mode < ZIND_ROUND_EXACT ||
        round_mode > ZIND_ROUND_TRUNC)
        return ZXV_E_ARG;
    st = zxv_rat_mul(q.num, q.den, k_pow10[digits], 1, &n, &d);
    if (st != ZXV_OK) {
        zind_set_error("zind_qty_to_minor: %lld/%lld x 10^%u overflows", (long long) q.num,
                       (long long) q.den, (unsigned) digits);
        return st;
    }
    /* n/d = fq + r/d with 0 <= r < d (floor division). */
    fq = n / d;
    r = n % d;
    if (r < 0) {
        fq -= 1;
        r += d;
    }
    if (r == 0) {
        m = fq;
    } else {
        switch (round_mode) {
        case ZIND_ROUND_EXACT:
            zind_set_error("zind_qty_to_minor: %lld/%lld has no exact form with %u decimal places",
                           (long long) q.num, (long long) q.den, (unsigned) digits);
            return ZIND_E_INEXACT;
        case ZIND_ROUND_FLOOR: up = 0; break;
        case ZIND_ROUND_CEIL: up = 1; break;
        case ZIND_ROUND_TRUNC: up = n < 0; break;
        default: {
            /* compare r with d - r, i.e. 2r with d, without overflow */
            int64_t rest = d - r;
            if (r > rest) up = 1;
            else if (r < rest) up = 0;
            else if (round_mode == ZIND_ROUND_HALF_UP) up = n > 0;
            else up = (fq % 2) != 0; /* HALF_EVEN: to the even neighbour */
            break;
        }
        }
        m = fq + (up ? 1 : 0); /* r != 0 means d >= 2, so |fq| < INT64_MAX/2 */
    }
    if (m > ZXV_AMOUNT_MAX || m < -ZXV_AMOUNT_MAX) {
        zind_set_error("zind_qty_to_minor: %lld minor units is beyond +-ZXV_AMOUNT_MAX",
                       (long long) m);
        return ZXV_E_RANGE;
    }
    if (out_residual) {
        zind_qty mq, res;
        st = zind_qty_from_minor(m, digits, &mq);
        if (st == ZXV_OK) st = zind_qty_sub(q, mq, &res);
        if (st != ZXV_OK) return st;
        *out_residual = res;
    }
    *out_minor = m;
    return ZXV_OK;
}
