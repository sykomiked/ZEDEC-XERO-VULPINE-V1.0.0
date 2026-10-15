/* ecdsa.c — ECDSA verification over P-256 and P-384. See ecdsa.h. The
 * curve arithmetic is shared with the P-256 signer (ecdsa_sign.c) through
 * ecdsa_int.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ecdsa.h"
#include "ecdsa_int.h"

static const uint8_t P256_P[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const uint8_t P256_B[32] = {
    0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
    0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6, 0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b,
};
static const uint8_t P256_N[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51,
};
static const uint8_t P256_GX[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
};
static const uint8_t P256_GY[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5,
};
static const uint8_t P384_P[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};
static const uint8_t P384_B[48] = {
    0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4, 0x98, 0x8e, 0x05, 0x6b, 0xe3, 0xf8, 0x2d, 0x19,
    0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12, 0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a,
    0xc6, 0x56, 0x39, 0x8d, 0x8a, 0x2e, 0xd1, 0x9d, 0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef,
};
static const uint8_t P384_N[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc7, 0x63, 0x4d, 0x81, 0xf4, 0x37, 0x2d, 0xdf,
    0x58, 0x1a, 0x0d, 0xb2, 0x48, 0xb0, 0xa7, 0x7a, 0xec, 0xec, 0x19, 0x6a, 0xcc, 0xc5, 0x29, 0x73,
};
static const uint8_t P384_GX[48] = {
    0xaa, 0x87, 0xca, 0x22, 0xbe, 0x8b, 0x05, 0x37, 0x8e, 0xb1, 0xc7, 0x1e, 0xf3, 0x20, 0xad, 0x74,
    0x6e, 0x1d, 0x3b, 0x62, 0x8b, 0xa7, 0x9b, 0x98, 0x59, 0xf7, 0x41, 0xe0, 0x82, 0x54, 0x2a, 0x38,
    0x55, 0x02, 0xf2, 0x5d, 0xbf, 0x55, 0x29, 0x6c, 0x3a, 0x54, 0x5e, 0x38, 0x72, 0x76, 0x0a, 0xb7,
};
static const uint8_t P384_GY[48] = {
    0x36, 0x17, 0xde, 0x4a, 0x96, 0x26, 0x2c, 0x6f, 0x5d, 0x9e, 0x98, 0xbf, 0x92, 0x92, 0xdc, 0x29,
    0xf8, 0xf4, 0x1d, 0xbd, 0x28, 0x9a, 0x14, 0x7c, 0xe9, 0xda, 0x31, 0x13, 0xb5, 0xf0, 0xb8, 0xc0,
    0x0a, 0x60, 0xb1, 0xce, 0x1d, 0x7e, 0x81, 0x9d, 0x7a, 0x43, 0x1d, 0x7c, 0x90, 0xea, 0x0e, 0x5f,
};

static bool curve_get(ecdsa_curve_t c, curve_t *out)
{
    if (c == ECDSA_P256) {
        *out = (curve_t){32, P256_P, P256_B, P256_N, P256_GX, P256_GY};
        return true;
    }
    if (c == ECDSA_P384) {
        *out = (curve_t){48, P384_P, P384_B, P384_N, P384_GX, P384_GY};
        return true;
    }
    return false;
}

uint32_t ecdsa_field_len(ecdsa_curve_t curve)
{
    curve_t cv;
    return curve_get(curve, &cv) ? cv.len : 0;
}

/* Renes-Costello-Batina 2016, Algorithm 4: complete addition for a = -3.
 * Everything in Montgomery form; bm is the curve constant b. */
void ec_pt_add(const bn_mont_t *f, const uint32_t *bm, pt_t *r, const pt_t *p, const pt_t *q)
{
    uint32_t t0[EC_LIMBS], t1[EC_LIMBS], t2[EC_LIMBS], t3[EC_LIMBS], t4[EC_LIMBS];
    uint32_t X3[EC_LIMBS], Y3[EC_LIMBS], Z3[EC_LIMBS];
    const uint32_t *X1 = p->x, *Y1 = p->y, *Z1 = p->z;
    const uint32_t *X2 = q->x, *Y2 = q->y, *Z2 = q->z;

    bn_mont_mul(f, t0, X1, X2);
    bn_mont_mul(f, t1, Y1, Y2);
    bn_mont_mul(f, t2, Z1, Z2);
    bn_mod_add(f, t3, X1, Y1);
    bn_mod_add(f, t4, X2, Y2);
    bn_mont_mul(f, t3, t3, t4);
    bn_mod_add(f, t4, t0, t1);
    bn_mod_sub(f, t3, t3, t4);
    bn_mod_add(f, t4, Y1, Z1);
    bn_mod_add(f, X3, Y2, Z2);
    bn_mont_mul(f, t4, t4, X3);
    bn_mod_add(f, X3, t1, t2);
    bn_mod_sub(f, t4, t4, X3);
    bn_mod_add(f, X3, X1, Z1);
    bn_mod_add(f, Y3, X2, Z2);
    bn_mont_mul(f, X3, X3, Y3);
    bn_mod_add(f, Y3, t0, t2);
    bn_mod_sub(f, Y3, X3, Y3);
    bn_mont_mul(f, Z3, bm, t2);
    bn_mod_sub(f, X3, Y3, Z3);
    bn_mod_add(f, Z3, X3, X3);
    bn_mod_add(f, X3, X3, Z3);
    bn_mod_sub(f, Z3, t1, X3);
    bn_mod_add(f, X3, t1, X3);
    bn_mont_mul(f, Y3, bm, Y3);
    bn_mod_add(f, t1, t2, t2);
    bn_mod_add(f, t2, t1, t2);
    bn_mod_sub(f, Y3, Y3, t2);
    bn_mod_sub(f, Y3, Y3, t0);
    bn_mod_add(f, t1, Y3, Y3);
    bn_mod_add(f, Y3, t1, Y3);
    bn_mod_add(f, t1, t0, t0);
    bn_mod_add(f, t0, t1, t0);
    bn_mod_sub(f, t0, t0, t2);
    bn_mont_mul(f, t1, t4, Y3);
    bn_mont_mul(f, t2, t0, Y3);
    bn_mont_mul(f, Y3, X3, Z3);
    bn_mod_add(f, Y3, Y3, t2);
    bn_mont_mul(f, X3, t3, X3);
    bn_mod_sub(f, X3, X3, t1);
    bn_mont_mul(f, Z3, t4, Z3);
    bn_mont_mul(f, t1, t3, t0);
    bn_mod_add(f, Z3, Z3, t1);

    for (uint32_t i = 0; i < f->n; i++) {
        r->x[i] = X3[i];
        r->y[i] = Y3[i];
        r->z[i] = Z3[i];
    }
}

/* Load an affine big-endian coordinate pair into Montgomery projective
 * form, rejecting coordinates >= p and points off the curve. */
bool ec_pt_load(const bn_mont_t *f, const uint32_t *bm, const curve_t *cv, pt_t *r,
                const uint8_t *x_be, const uint8_t *y_be)
{
    uint32_t x[EC_LIMBS], y[EC_LIMBS], lhs[EC_LIMBS], rhs[EC_LIMBS], t[EC_LIMBS];
    if (!bn_from_be(x, f->n, x_be, cv->len) || !bn_from_be(y, f->n, y_be, cv->len)) return false;
    if (bn_cmp(x, f->m, f->n) >= 0 || bn_cmp(y, f->m, f->n) >= 0) return false;
    bn_to_mont(f, r->x, x);
    bn_to_mont(f, r->y, y);
    for (uint32_t i = 0; i < f->n; i++) r->z[i] = f->one[i];

    /* y^2 == x^3 - 3x + b */
    bn_mont_mul(f, lhs, r->y, r->y);
    bn_mont_mul(f, rhs, r->x, r->x);
    bn_mont_mul(f, rhs, rhs, r->x);
    bn_mod_add(f, t, r->x, r->x);
    bn_mod_add(f, t, t, r->x);
    bn_mod_sub(f, rhs, rhs, t);
    bn_mod_add(f, rhs, rhs, bm);
    return bn_cmp(lhs, rhs, f->n) == 0;
}

static bool load_pub(const bn_mont_t *f, const uint32_t *bm, const curve_t *cv, pt_t *q,
                     const uint8_t *pub, uint32_t pub_len)
{
    if (pub_len != 1 + 2 * cv->len || pub[0] != 0x04) return false;
    return ec_pt_load(f, bm, cv, q, pub + 1, pub + 1 + cv->len);
}

bool ec_setup(ecdsa_curve_t curve, curve_t *cv, bn_mont_t *f, uint32_t *bm)
{
    uint32_t b[EC_LIMBS];
    if (!curve_get(curve, cv)) return false;
    if (!bn_mont_init(f, cv->p, cv->len)) return false;
    if (!bn_from_be(b, f->n, cv->b, cv->len)) return false;
    bn_to_mont(f, bm, b);
    return true;
}

bool ecdsa_pubkey_valid(ecdsa_curve_t curve, const uint8_t *pub, uint32_t pub_len)
{
    curve_t cv;
    bn_mont_t f;
    uint32_t bm[EC_LIMBS];
    pt_t q;
    if (!pub || !ec_setup(curve, &cv, &f, bm)) return false;
    return load_pub(&f, bm, &cv, &q, pub, pub_len);
}

/* DER length at p[*at]; definite, minimal form only. */
static bool der_len(const uint8_t *p, uint32_t end, uint32_t *at, uint32_t *len)
{
    if (*at >= end) return false;
    uint8_t b = p[(*at)++];
    if (b < 0x80) {
        *len = b;
        return true;
    }
    if (b != 0x81) return false; /* an ECDSA signature never needs more */
    if (*at >= end) return false;
    b = p[(*at)++];
    if (b < 0x80) return false; /* non-minimal */
    *len = b;
    return true;
}

/* One DER INTEGER, positive and minimally encoded, at most `max` bytes of
 * magnitude. Writes the magnitude (without the sign pad) to `out`. */
static bool der_uint(const uint8_t *p, uint32_t end, uint32_t *at, const uint8_t **out,
                     uint32_t *out_len, uint32_t max)
{
    uint32_t len;
    if (*at >= end || p[(*at)++] != 0x02) return false;
    if (!der_len(p, end, at, &len)) return false;
    if (len == 0 || len > end - *at) return false;
    const uint8_t *v = p + *at;
    if (v[0] & 0x80) return false;                            /* negative */
    if (len > 1 && v[0] == 0 && !(v[1] & 0x80)) return false; /* non-minimal */
    *at += len;
    if (v[0] == 0 && len > 1) {
        v++;
        len--;
    }
    if (len > max) return false;
    *out = v;
    *out_len = len;
    return true;
}

bool ecdsa_verify_digest(ecdsa_curve_t curve, const uint8_t *pub, uint32_t pub_len,
                         const uint8_t *digest, uint32_t digest_len, const uint8_t *sig_der,
                         uint32_t sig_len)
{
    curve_t cv;
    bn_mont_t f, g; /* field p, group order n */
    uint32_t bm[EC_LIMBS];
    pt_t q, gp, gq, acc;
    if (!pub || !digest || !sig_der) return false;
    if (!ec_setup(curve, &cv, &f, bm)) return false;
    if (!bn_mont_init(&g, cv.n, cv.len)) return false;
    if (!load_pub(&f, bm, &cv, &q, pub, pub_len)) return false;
    if (!ec_pt_load(&f, bm, &cv, &gp, cv.gx, cv.gy)) return false;
    const uint32_t n = f.n;

    /* signature: SEQUENCE { INTEGER r, INTEGER s } and nothing after it */
    uint32_t at = 0, seq_len;
    const uint8_t *rb, *sb;
    uint32_t rl, sl;
    if (sig_len < 2 || sig_der[at++] != 0x30) return false;
    if (!der_len(sig_der, sig_len, &at, &seq_len)) return false;
    if (seq_len != sig_len - at) return false;
    if (!der_uint(sig_der, sig_len, &at, &rb, &rl, cv.len)) return false;
    if (!der_uint(sig_der, sig_len, &at, &sb, &sl, cv.len)) return false;
    if (at != sig_len) return false;

    uint32_t r[EC_LIMBS], s[EC_LIMBS], e[EC_LIMBS], w[EC_LIMBS], t[EC_LIMBS];
    if (!bn_from_be(r, n, rb, rl) || !bn_from_be(s, n, sb, sl)) return false;
    if (bn_is_zero(r, n) || bn_is_zero(s, n)) return false;
    if (bn_cmp(r, g.m, n) >= 0 || bn_cmp(s, g.m, n) >= 0) return false;

    /* e = leftmost bits of the digest; both group orders are exactly 8*len
     * bits, so truncation is a byte cut and e < 2n needs one reduction */
    uint32_t dl = digest_len > cv.len ? cv.len : digest_len;
    if (!bn_from_be(t, n, digest, dl)) return false;
    bn_reduce_once(&g, e, t);

    /* w = s^-1, u1 = e*w, u2 = r*w (mod n) */
    uint32_t u1[EC_LIMBS], u2[EC_LIMBS];
    bn_to_mont(&g, t, s);
    bn_mont_inv_prime(&g, w, t);
    bn_to_mont(&g, t, e);
    bn_mont_mul(&g, t, t, w);
    bn_from_mont(&g, u1, t);
    bn_to_mont(&g, t, r);
    bn_mont_mul(&g, t, t, w);
    bn_from_mont(&g, u2, t);

    /* acc = u1*G + u2*Q */
    ec_pt_add(&f, bm, &gq, &gp, &q);
    for (uint32_t i = 0; i < n; i++) {
        acc.x[i] = 0;
        acc.y[i] = f.one[i];
        acc.z[i] = 0;
    }
    for (uint32_t bit = 32 * n; bit-- > 0;) {
        ec_pt_add(&f, bm, &acc, &acc, &acc);
        uint32_t b1 = (u1[bit / 32] >> (bit % 32)) & 1;
        uint32_t b2 = (u2[bit / 32] >> (bit % 32)) & 1;
        if (b1 && b2)
            ec_pt_add(&f, bm, &acc, &acc, &gq);
        else if (b1)
            ec_pt_add(&f, bm, &acc, &acc, &gp);
        else if (b2)
            ec_pt_add(&f, bm, &acc, &acc, &q);
    }
    if (bn_is_zero(acc.z, n)) return false;

    /* x = X / Z, reduced mod n, must equal r */
    uint32_t zi[EC_LIMBS], x[EC_LIMBS];
    bn_mont_inv_prime(&f, zi, acc.z);
    bn_mont_mul(&f, x, acc.x, zi);
    bn_from_mont(&f, x, x);
    bn_reduce_once(&g, x, x); /* x < p < 2n for both curves */
    return bn_cmp(x, r, n) == 0;
}
