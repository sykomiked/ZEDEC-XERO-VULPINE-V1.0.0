/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_shamir.c -- Shamir secret sharing over GF(2^8), byte by byte.
 *
 * Field: x^8 + x^4 + x^3 + x + 1 (0x11B, the AES field). Multiplication
 * is a fixed 8-step shift-and-add with masks (no tables, no branches on
 * secret data), inversion is a^254. Share i is (x = i, y = f(i)) for a
 * random polynomial f of degree k-1 with f(0) = secret, one polynomial
 * per byte. Any k shares give the secret by Lagrange interpolation at 0;
 * k-1 shares give no information about it.
 *
 * HONEST LIMITS: shares are not verifiable by themselves; the vault
 * stores a salted commitment to each share so a wrong or malicious share
 * is caught before combination. Coefficients come from host.random.
 */
#include "id_internal.h"

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r ^= (uint8_t) (a & (uint8_t) - (b & 1u));
        uint8_t hi = (uint8_t) - ((a >> 7) & 1u);
        a = (uint8_t) ((a << 1) ^ (0x1Bu & hi));
        b >>= 1;
    }
    return r;
}

static uint8_t gf_inv(uint8_t a)
{
    uint8_t r = 1, x = a;
    /* a^254 = a^(2+4+8+16+32+64+128) */
    for (int i = 0; i < 7; i++) {
        x = gf_mul(x, x);
        r = gf_mul(r, x);
    }
    return r;
}

id_status_t id_shamir_split(const uint8_t secret[ID_SHARE_LEN], uint8_t k, uint8_t n,
                            const id_host_t *h, uint8_t shares[][ID_SHARE_LEN])
{
    uint8_t coef[ID_MAX_GUARDIANS];
    if (!secret || !h || !h->random || !shares) return ID_ERR_ARG;
    if (k < 1 || n < k || n > ID_MAX_GUARDIANS) return ID_ERR_ARG;
    for (uint32_t b = 0; b < ID_SHARE_LEN; b++) {
        coef[0] = secret[b];
        if (k > 1) h->random(h->ctx, coef + 1, (uint32_t) k - 1u);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t x = (uint8_t) (i + 1u), y = 0;
            for (uint32_t j = k; j-- > 0;) y = (uint8_t) (gf_mul(y, x) ^ coef[j]);
            shares[i][b] = y;
        }
    }
    id_wipe(coef, sizeof coef);
    return ID_OK;
}

id_status_t id_shamir_combine(const uint8_t *xs, const uint8_t ys[][ID_SHARE_LEN], uint8_t k,
                              uint8_t secret[ID_SHARE_LEN])
{
    uint8_t lag[ID_MAX_GUARDIANS];
    if (!xs || !ys || !secret || k < 1 || k > ID_MAX_GUARDIANS) return ID_ERR_ARG;
    for (uint32_t i = 0; i < k; i++) {
        if (xs[i] == 0) return ID_ERR_ARG;
        for (uint32_t j = 0; j < i; j++)
            if (xs[j] == xs[i]) return ID_ERR_ARG;
    }
    /* lag_i = prod_{j != i} x_j / (x_j - x_i); subtraction is XOR. */
    for (uint32_t i = 0; i < k; i++) {
        uint8_t num = 1, den = 1;
        for (uint32_t j = 0; j < k; j++) {
            if (j == i) continue;
            num = gf_mul(num, xs[j]);
            den = gf_mul(den, (uint8_t) (xs[j] ^ xs[i]));
        }
        lag[i] = gf_mul(num, gf_inv(den));
    }
    for (uint32_t b = 0; b < ID_SHARE_LEN; b++) {
        uint8_t s = 0;
        for (uint32_t i = 0; i < k; i++) s ^= gf_mul(lag[i], ys[i][b]);
        secret[b] = s;
    }
    return ID_OK;
}
