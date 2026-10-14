/* bn.c — Montgomery modular arithmetic for verification. See bn.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bn.h"

uint32_t bn_bits_be(const uint8_t *be, uint32_t len)
{
    uint32_t i = 0;
    while (i < len && be[i] == 0) i++;
    if (i == len) return 0;
    uint32_t bits = (len - i - 1) * 8;
    uint8_t top = be[i];
    while (top) {
        bits++;
        top >>= 1;
    }
    return bits;
}

bool bn_from_be(uint32_t *r, uint32_t n, const uint8_t *be, uint32_t len)
{
    for (uint32_t i = 0; i < n; i++) r[i] = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t byte = len - 1 - i; /* i-th least significant byte */
        uint32_t limb = i / 4;
        if (limb >= n) {
            if (be[byte]) return false;
            continue;
        }
        r[limb] |= (uint32_t) be[byte] << (8 * (i % 4));
    }
    return true;
}

void bn_to_be(uint8_t *be, uint32_t len, const uint32_t *a, uint32_t n)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t limb = i / 4;
        be[len - 1 - i] = limb < n ? (uint8_t) (a[limb] >> (8 * (i % 4))) : 0;
    }
}

int bn_cmp(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

bool bn_is_zero(const uint32_t *a, uint32_t n)
{
    uint32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= a[i];
    return acc == 0;
}

/* r = a - b over n limbs; returns the borrow. */
static uint32_t sub_n(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t n)
{
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t d = (uint64_t) a[i] - b[i] - borrow;
        r[i] = (uint32_t) d;
        borrow = (d >> 32) & 1;
    }
    return (uint32_t) borrow;
}

static uint32_t add_n(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t n)
{
    uint64_t carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t s = (uint64_t) a[i] + b[i] + carry;
        r[i] = (uint32_t) s;
        carry = s >> 32;
    }
    return (uint32_t) carry;
}

/* r = (hi:a) - m if that is non-negative, else a. `hi` is a carry limb. */
static void cond_sub(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, uint32_t hi)
{
    uint32_t t[BN_LIMBS_MAX];
    uint32_t borrow = sub_n(t, a, ctx->m, ctx->n);
    /* keep t when hi was set (the value exceeded 2^(32n)) or there was no
     * borrow; otherwise keep a */
    uint32_t keep_t = (uint32_t) 0 - (uint32_t) ((hi | (borrow ^ 1u)) & 1u);
    for (uint32_t i = 0; i < ctx->n; i++) r[i] = (t[i] & keep_t) | (a[i] & ~keep_t);
}

void bn_reduce_once(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a)
{
    cond_sub(ctx, r, a, 0);
}

void bn_mod_add(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t t[BN_LIMBS_MAX];
    uint32_t c = add_n(t, a, b, ctx->n);
    cond_sub(ctx, r, t, c);
}

void bn_mod_sub(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t t[BN_LIMBS_MAX], u[BN_LIMBS_MAX];
    uint32_t borrow = sub_n(t, a, b, ctx->n);
    add_n(u, t, ctx->m, ctx->n);
    uint32_t mask = (uint32_t) 0 - borrow;
    for (uint32_t i = 0; i < ctx->n; i++) r[i] = (u[i] & mask) | (t[i] & ~mask);
}

void bn_mont_mul(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    const uint32_t n = ctx->n;
    uint32_t t[BN_LIMBS_MAX + 2];
    for (uint32_t i = 0; i < n + 2; i++) t[i] = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t c = 0;
        for (uint32_t j = 0; j < n; j++) {
            uint64_t s = (uint64_t) t[j] + (uint64_t) a[j] * b[i] + c;
            t[j] = (uint32_t) s;
            c = s >> 32;
        }
        uint64_t s = (uint64_t) t[n] + c;
        t[n] = (uint32_t) s;
        t[n + 1] = (uint32_t) (s >> 32);

        uint32_t q = t[0] * ctx->n0;
        s = (uint64_t) t[0] + (uint64_t) q * ctx->m[0];
        c = s >> 32;
        for (uint32_t j = 1; j < n; j++) {
            s = (uint64_t) t[j] + (uint64_t) q * ctx->m[j] + c;
            t[j - 1] = (uint32_t) s;
            c = s >> 32;
        }
        s = (uint64_t) t[n] + c;
        t[n - 1] = (uint32_t) s;
        t[n] = t[n + 1] + (uint32_t) (s >> 32);
    }
    cond_sub(ctx, r, t, t[n]);
}

void bn_to_mont(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a)
{
    bn_mont_mul(ctx, r, a, ctx->rr);
}

void bn_from_mont(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a)
{
    uint32_t one[BN_LIMBS_MAX];
    for (uint32_t i = 0; i < ctx->n; i++) one[i] = 0;
    one[0] = 1;
    bn_mont_mul(ctx, r, a, one);
}

bool bn_mont_init(bn_mont_t *ctx, const uint8_t *mod_be, uint32_t len)
{
    uint32_t bits = bn_bits_be(mod_be, len);
    if (bits < 2 || bits > 32 * BN_LIMBS_MAX) return false;
    if ((mod_be[len - 1] & 1) == 0) return false;
    ctx->n = (bits + 31) / 32;
    if (!bn_from_be(ctx->m, ctx->n, mod_be, len)) return false;

    /* n0 = -m^-1 mod 2^32 by Newton iteration: each step doubles the
     * number of correct low bits, starting from 3 (m odd => m*m = 1 mod 8) */
    uint32_t inv = ctx->m[0];
    for (int i = 0; i < 5; i++) inv *= 2u - ctx->m[0] * inv;
    ctx->n0 = (uint32_t) 0 - inv;

    /* R mod m and R^2 mod m by repeated doubling of 1. 1 < m since m is odd
     * and has at least 2 bits. */
    uint32_t x[BN_LIMBS_MAX];
    for (uint32_t i = 0; i < ctx->n; i++) x[i] = 0;
    x[0] = 1;
    for (uint32_t i = 0; i < 32 * ctx->n; i++) bn_mod_add(ctx, x, x, x);
    for (uint32_t i = 0; i < ctx->n; i++) ctx->one[i] = x[i];
    for (uint32_t i = 0; i < 32 * ctx->n; i++) bn_mod_add(ctx, x, x, x);
    for (uint32_t i = 0; i < ctx->n; i++) ctx->rr[i] = x[i];
    return true;
}

void bn_mont_pow(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint8_t *e_be,
                 uint32_t e_len)
{
    uint32_t acc[BN_LIMBS_MAX], base[BN_LIMBS_MAX];
    for (uint32_t i = 0; i < ctx->n; i++) {
        acc[i] = ctx->one[i];
        base[i] = a[i];
    }
    for (uint32_t i = 0; i < e_len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            bn_mont_mul(ctx, acc, acc, acc);
            if ((e_be[i] >> bit) & 1) bn_mont_mul(ctx, acc, acc, base);
        }
    }
    for (uint32_t i = 0; i < ctx->n; i++) r[i] = acc[i];
}

void bn_mont_inv_prime(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a)
{
    uint32_t two[BN_LIMBS_MAX], e[BN_LIMBS_MAX];
    uint8_t e_be[4 * BN_LIMBS_MAX];
    for (uint32_t i = 0; i < ctx->n; i++) two[i] = 0;
    two[0] = 2;
    sub_n(e, ctx->m, two, ctx->n);
    bn_to_be(e_be, 4 * ctx->n, e, ctx->n);
    bn_mont_pow(ctx, r, a, e_be, 4 * ctx->n);
}
