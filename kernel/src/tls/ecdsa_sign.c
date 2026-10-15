/* ecdsa_sign.c — P-256 signing: RFC 6979 nonces, constant-time scalar
 * multiplication (ecdsa.h S1-S3). Kept apart from the verifier so the
 * verification builds need no HMAC.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ecdsa_int.h"
#include "hkdf.h" /* hmac_sha256, for RFC 6979 */

/* ---------------- P-256 signing (ecdsa.h S1-S3) ---------------- */

/* r = bit ? a : r, without a branch on bit */
static void pt_cmov(pt_t *r, const pt_t *a, uint32_t bit, uint32_t n)
{
    uint32_t m = (uint32_t) 0 - (bit & 1u);
    for (uint32_t i = 0; i < n; i++) {
        r->x[i] = (r->x[i] & ~m) | (a->x[i] & m);
        r->y[i] = (r->y[i] & ~m) | (a->y[i] & m);
        r->z[i] = (r->z[i] & ~m) | (a->z[i] & m);
    }
}

/* r = k*P by double-and-add-always: the same additions for every k, and
 * the complete formula never branches on the points (S2) */
static void pt_mul_ct(const bn_mont_t *f, const uint32_t *bm, pt_t *r, const pt_t *p,
                      const uint32_t *k)
{
    const uint32_t n = f->n;
    pt_t acc, t;
    for (uint32_t i = 0; i < n; i++) {
        acc.x[i] = 0;
        acc.y[i] = f->one[i];
        acc.z[i] = 0;
    }
    for (uint32_t bit = 32 * n; bit-- > 0;) {
        ec_pt_add(f, bm, &acc, &acc, &acc);
        ec_pt_add(f, bm, &t, &acc, p);
        pt_cmov(&acc, &t, k[bit / 32] >> (bit % 32), n);
    }
    for (uint32_t i = 0; i < n; i++) { /* a loop, not a struct copy (no memcpy) */
        r->x[i] = acc.x[i];
        r->y[i] = acc.y[i];
        r->z[i] = acc.z[i];
    }
}

static void wipe_u8(uint8_t *p, uint32_t n)
{
    volatile uint8_t *v = p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}
static void wipe_u32(uint32_t *p, uint32_t n)
{
    volatile uint32_t *v = p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}

/* d from 32 big-endian bytes, in [1, n-1] */
static bool load_priv(const bn_mont_t *g, uint32_t *d, const uint8_t priv[32])
{
    if (!bn_from_be(d, g->n, priv, 32)) return false;
    return !bn_is_zero(d, g->n) && bn_cmp(d, g->m, g->n) < 0;
}

/* affine x and y of a projective point, big-endian; false at infinity */
static bool pt_affine(const bn_mont_t *f, const pt_t *p, uint8_t *x_be, uint8_t *y_be, uint32_t len)
{
    uint32_t zi[EC_LIMBS], t[EC_LIMBS];
    if (bn_is_zero(p->z, f->n)) return false;
    bn_mont_inv_prime(f, zi, p->z);
    bn_mont_mul(f, t, p->x, zi);
    bn_from_mont(f, t, t);
    bn_to_be(x_be, len, t, f->n);
    if (y_be) {
        bn_mont_mul(f, t, p->y, zi);
        bn_from_mont(f, t, t);
        bn_to_be(y_be, len, t, f->n);
    }
    return true;
}

bool ecdsa_p256_public(const uint8_t priv[32], uint8_t pub[65])
{
    curve_t cv;
    bn_mont_t f, g;
    uint32_t bm[EC_LIMBS], d[EC_LIMBS];
    pt_t gp, q;
    if (!priv || !pub || !ec_setup(ECDSA_P256, &cv, &f, bm)) return false;
    if (!bn_mont_init(&g, cv.n, cv.len)) return false;
    if (!load_priv(&g, d, priv)) return false;
    if (!ec_pt_load(&f, bm, &cv, &gp, cv.gx, cv.gy)) return false;
    pt_mul_ct(&f, bm, &q, &gp, d);
    wipe_u32(d, EC_LIMBS);
    pub[0] = 0x04;
    return pt_affine(&f, &q, pub + 1, pub + 33, 32);
}

/* one DER INTEGER for a 32-byte big-endian magnitude, minimal */
static uint32_t der_int(uint8_t *o, const uint8_t v[32])
{
    uint32_t s = 0;
    while (s < 31 && v[s] == 0) s++;
    uint32_t pad = (v[s] & 0x80) ? 1u : 0u, len = 32 - s + pad, at = 0;
    o[at++] = 0x02;
    o[at++] = (uint8_t) len;
    if (pad) o[at++] = 0;
    for (uint32_t i = s; i < 32; i++) o[at++] = v[i];
    return at;
}

uint32_t ecdsa_p256_sign_digest(const uint8_t priv[32], const uint8_t *digest, uint32_t digest_len,
                                uint8_t *sig_der, uint32_t cap)
{
    curve_t cv;
    bn_mont_t f, g;
    uint32_t bm[EC_LIMBS], d[EC_LIMBS], e[EC_LIMBS], k[EC_LIMBS], t[EC_LIMBS];
    uint32_t r[EC_LIMBS], s[EC_LIMBS], u[EC_LIMBS];
    uint8_t x[32], h1[32], rb[32], sb[32];
    uint8_t K[32], V[32], buf[32 + 1 + 32 + 32];
    pt_t gp, kg;
    uint32_t out = 0;
    if (!priv || !digest || !sig_der || cap < ECDSA_P256_SIG_MAX) return 0;
    if (!ec_setup(ECDSA_P256, &cv, &f, bm) || !bn_mont_init(&g, cv.n, cv.len)) return 0;
    if (!load_priv(&g, d, priv)) return 0;
    if (!ec_pt_load(&f, bm, &cv, &gp, cv.gx, cv.gy)) return 0;
    const uint32_t n = g.n;

    /* e = the leftmost 256 bits of the digest, reduced mod n (S1) */
    uint32_t dl = digest_len > 32 ? 32 : digest_len;
    for (uint32_t i = 0; i < 32; i++) h1[i] = 0;
    for (uint32_t i = 0; i < dl; i++) h1[32 - dl + i] = digest[i];
    bn_from_be(t, n, h1, 32);
    bn_reduce_once(&g, e, t);
    bn_to_be(h1, 32, e, n); /* bits2octets(h1) */

    /* RFC 6979 3.2 with HMAC-SHA256: K and V from x = int2octets(d) */
    for (uint32_t i = 0; i < 32; i++) {
        V[i] = 0x01;
        K[i] = 0x00;
        x[i] = priv[i];
    }
    for (uint8_t round = 0; round < 2; round++) {
        for (uint32_t i = 0; i < 32; i++) {
            buf[i] = V[i];
            buf[33 + i] = x[i];
            buf[65 + i] = h1[i];
        }
        buf[32] = round;
        hmac_sha256(K, 32, buf, sizeof buf, K);
        hmac_sha256(K, 32, V, 32, V);
    }
    for (uint32_t tries = 0; tries < 64 && !out; tries++) {
        hmac_sha256(K, 32, V, 32, V);
        bn_from_be(k, n, V, 32);
        if (!bn_is_zero(k, n) && bn_cmp(k, g.m, n) < 0) {
            /* r = (k*G).x mod n */
            pt_mul_ct(&f, bm, &kg, &gp, k);
            if (pt_affine(&f, &kg, rb, 0, 32)) {
                bn_from_be(t, n, rb, 32);
                bn_reduce_once(&g, r, t); /* x < p < 2n */
                if (!bn_is_zero(r, n)) {
                    /* s = k^-1 (e + r*d) mod n; the inversion's exponent is
                     * the public n-2, so its time does not depend on k */
                    bn_to_mont(&g, t, r);
                    bn_to_mont(&g, u, d);
                    bn_mont_mul(&g, t, t, u); /* r*d, Montgomery form */
                    bn_to_mont(&g, u, e);
                    bn_mod_add(&g, t, t, u); /* e + r*d */
                    bn_to_mont(&g, u, k);
                    bn_mont_inv_prime(&g, u, u);
                    bn_mont_mul(&g, t, t, u);
                    bn_from_mont(&g, s, t);
                    if (!bn_is_zero(s, n)) {
                        bn_to_be(rb, 32, r, n);
                        bn_to_be(sb, 32, s, n);
                        uint32_t at = 2;
                        at += der_int(sig_der + at, rb);
                        at += der_int(sig_der + at, sb);
                        sig_der[0] = 0x30;
                        sig_der[1] = (uint8_t) (at - 2);
                        out = at;
                    }
                }
            }
        }
        if (!out) { /* RFC 6979 3.2 step h.3 */
            for (uint32_t i = 0; i < 32; i++) buf[i] = V[i];
            buf[32] = 0;
            hmac_sha256(K, 32, buf, 33, K);
            hmac_sha256(K, 32, V, 32, V);
        }
    }
    wipe_u32(d, EC_LIMBS);
    wipe_u32(k, EC_LIMBS);
    wipe_u32(u, EC_LIMBS);
    wipe_u32(t, EC_LIMBS);
    wipe_u8(x, sizeof x);
    wipe_u8(K, sizeof K);
    wipe_u8(V, sizeof V);
    wipe_u8(buf, sizeof buf);
    return out;
}
