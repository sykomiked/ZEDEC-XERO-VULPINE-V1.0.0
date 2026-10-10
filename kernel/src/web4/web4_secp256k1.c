/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_secp256k1.c — secp256k1 ECDSA for the Ethereum / Bitcoin edge.
 *
 * Construction
 *   Field and scalar arithmetic: one Montgomery multiplier (CIOS, eight 32-bit
 *   limbs, 32x32->64 products only) used for both moduli p and n. Results are
 *   reduced with a masked conditional subtraction, never a branch.
 *   Points: projective (X:Y:Z) with the COMPLETE addition formula of Renes,
 *   Costello and Batina, "Complete addition formulas for prime order elliptic
 *   curves" (EUROCRYPT 2016), Algorithm 7 (a = 0, b3 = 3b = 21). The same
 *   formula doubles, adds and handles the point at infinity, so there are no
 *   exceptional cases to branch on.
 *   Scalar multiplication: fixed 256-iteration double-and-add-always with a
 *   masked select on each secret bit.
 *   Inversion: Fermat (a^(m-2)) with a fixed public exponent.
 *   Nonces: RFC 6979 section 3.2 with HMAC-SHA256; signatures are normalised
 *   to low s (EIP-2 / BIP-62), flipping the recovery id's parity bit.
 *
 * Constant-time notes (honest): the code has no secret-dependent branches or
 * table indices in key generation and signing, apart from the RFC 6979 retry
 * loop (taken with probability ~2^-128) and the low-s flip (s is public once
 * the signature is out). Whether the compiler and CPU keep it constant-time
 * (e.g. 32x32 multiply latency on some cores) has not been measured.
 * Verification and recovery work on public data and are not constant-time.
 * Not audited.
 */
#include "web4_web3.h"
#include "../robin_debanks/sha256.h"

typedef struct {
    uint32_t v[8];
} fe;

typedef struct {
    uint32_t m[8];
    uint32_t r2[8];  /* R^2 mod m, R = 2^256 */
    uint32_t one[8]; /* R mod m: Montgomery 1 */
    uint32_t n0;     /* -m^-1 mod 2^32 */
} modulus;

static const modulus MP = {{0xfffffc2fu, 0xfffffffeu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                            0xffffffffu, 0xffffffffu, 0xffffffffu},
                           {0x000e90a1u, 0x000007a2u, 0x00000001u, 0, 0, 0, 0, 0},
                           {0x000003d1u, 0x00000001u, 0, 0, 0, 0, 0, 0},
                           0xd2253531u};

static const modulus MN = {
    {0xd0364141u, 0xbfd25e8cu, 0xaf48a03bu, 0xbaaedce6u, 0xfffffffeu, 0xffffffffu, 0xffffffffu,
     0xffffffffu},
    {0x67d7d140u, 0x896cf214u, 0x0e7cf878u, 0x741496c2u, 0x5bcd07c6u, 0xe697f5e4u, 0x81c69bc5u,
     0x9d671cd5u},
    {0x2fc9bebfu, 0x402da173u, 0x50b75fc4u, 0x45512319u, 0x00000001u, 0, 0, 0},
    0x5588b13fu};

static const uint32_t GX[8] = {0x16f81798u, 0x59f2815bu, 0x2dce28d9u, 0x029bfcdbu,
                               0xce870b07u, 0x55a06295u, 0xf9dcbbacu, 0x79be667eu};
static const uint32_t GY[8] = {0xfb10d4b8u, 0x9c47d08fu, 0xa6855419u, 0xfd17b448u,
                               0x0e1108a8u, 0x5da4fbfcu, 0x26a3c465u, 0x483ada77u};
/* (p + 1) / 4: square root exponent (p = 3 mod 4). */
static const uint32_t PSQRT[8] = {0xbfffff0cu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                  0xffffffffu, 0xffffffffu, 0xffffffffu, 0x3fffffffu};
/* floor(n / 2): the low-s bound. */
static const uint32_t NHALF[8] = {0x681b20a0u, 0xdfe92f46u, 0x57a4501du, 0x5d576e73u,
                                  0xffffffffu, 0xffffffffu, 0xffffffffu, 0x7fffffffu};

/* ---- limb helpers ---- */
static void fe_from_be(fe *a, const uint8_t b[32])
{
    for (int i = 0; i < 8; i++) a->v[i] = w4_be32_get(b + 4 * (7 - i));
}

static void fe_to_be(const fe *a, uint8_t b[32])
{
    for (int i = 0; i < 8; i++) w4_be32_put(b + 4 * (7 - i), a->v[i]);
}

static void fe_set(fe *a, const uint32_t v[8])
{
    for (int i = 0; i < 8; i++) a->v[i] = v[i];
}

/* r = a - b, returns the borrow (0/1). */
static uint32_t sub_raw(uint32_t r[8], const uint32_t a[8], const uint32_t b[8])
{
    uint64_t br = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t) a[i] - b[i] - br;
        r[i] = (uint32_t) d;
        br = (d >> 63) & 1u;
    }
    return (uint32_t) br;
}

static uint32_t add_raw(uint32_t r[8], const uint32_t a[8], const uint32_t b[8])
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++) {
        c += (uint64_t) a[i] + b[i];
        r[i] = (uint32_t) c;
        c >>= 32;
    }
    return (uint32_t) c;
}

/* r = mask ? a : b, mask all-ones or zero. */
static void sel(uint32_t r[8], uint32_t mask, const uint32_t a[8], const uint32_t b[8])
{
    for (int i = 0; i < 8; i++) r[i] = (a[i] & mask) | (b[i] & ~mask);
}

static uint32_t is_zero(const uint32_t a[8])
{
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) acc |= a[i];
    return (uint32_t) (((uint64_t) acc - 1u) >> 63) & 1u; /* 1 iff acc == 0 */
}

/* a < m (as integers): 1/0, constant time. */
static uint32_t lt(const uint32_t a[8], const uint32_t m[8])
{
    uint32_t t[8];
    return sub_raw(t, a, m);
}

/* ---- modular arithmetic (operands < m) ---- */
static void mod_add(fe *r, const fe *a, const fe *b, const modulus *M)
{
    uint32_t s[8], d[8];
    uint32_t c = add_raw(s, a->v, b->v);
    uint32_t br = sub_raw(d, s, M->m);
    uint32_t use_d = c | (br ^ 1u);
    sel(r->v, (uint32_t) 0 - use_d, d, s);
}

static void mod_sub(fe *r, const fe *a, const fe *b, const modulus *M)
{
    uint32_t d[8], s[8];
    uint32_t br = sub_raw(d, a->v, b->v);
    add_raw(s, d, M->m);
    sel(r->v, (uint32_t) 0 - br, s, d);
}

static void mont_mul(fe *r, const fe *a, const fe *b, const modulus *M)
{
    uint32_t t[10] = {0};
    for (int i = 0; i < 8; i++) {
        uint64_t c = 0;
        for (int j = 0; j < 8; j++) {
            c += (uint64_t) a->v[j] * b->v[i] + t[j];
            t[j] = (uint32_t) c;
            c >>= 32;
        }
        c += t[8];
        t[8] = (uint32_t) c;
        t[9] = (uint32_t) (c >> 32);
        uint32_t m = t[0] * M->n0;
        c = (uint64_t) m * M->m[0] + t[0];
        c >>= 32;
        for (int j = 1; j < 8; j++) {
            c += (uint64_t) m * M->m[j] + t[j];
            t[j - 1] = (uint32_t) c;
            c >>= 32;
        }
        c += t[8];
        t[7] = (uint32_t) c;
        t[8] = t[9] + (uint32_t) (c >> 32);
    }
    uint32_t d[8];
    uint32_t br = sub_raw(d, t, M->m);
    uint32_t use_d = t[8] | (br ^ 1u);
    sel(r->v, (uint32_t) 0 - (use_d & 1u), d, t);
}

static void to_mont(fe *r, const fe *a, const modulus *M)
{
    fe r2;
    fe_set(&r2, M->r2);
    mont_mul(r, a, &r2, M);
}

static void from_mont(fe *r, const fe *a, const modulus *M)
{
    fe one = {{1, 0, 0, 0, 0, 0, 0, 0}};
    mont_mul(r, a, &one, M);
}

/* r = a^e (Montgomery domain), e public. */
static void mont_pow(fe *r, const fe *a, const uint32_t e[8], const modulus *M)
{
    fe acc;
    fe_set(&acc, M->one);
    for (int i = 255; i >= 0; i--) {
        mont_mul(&acc, &acc, &acc, M);
        if ((e[i >> 5] >> (i & 31)) & 1u) mont_mul(&acc, &acc, a, M);
    }
    *r = acc;
}

static void mont_inv(fe *r, const fe *a, const modulus *M)
{
    uint32_t e[8];
    const uint32_t two[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    sub_raw(e, M->m, two);
    mont_pow(r, a, e, M);
}

/* ---- points (projective, Montgomery coordinates mod p) ---- */
typedef struct {
    fe X, Y, Z;
} pt;

/* b3 = 3 * 7 = 21 in Montgomery form (21 * 2^256 mod p); checked by the self-test. */
static const fe B3 = {{0x00005025u, 0x00000015u, 0, 0, 0, 0, 0, 0}};

/* RCB 2016, Algorithm 7 (a = 0). Complete: valid for P == Q and infinity. */
static void pt_add(pt *R, const pt *P, const pt *Q)
{
    const modulus *M = &MP;
    fe t0, t1, t2, t3, t4, X3, Y3, Z3;
    mont_mul(&t0, &P->X, &Q->X, M);
    mont_mul(&t1, &P->Y, &Q->Y, M);
    mont_mul(&t2, &P->Z, &Q->Z, M);
    mod_add(&t3, &P->X, &P->Y, M);
    mod_add(&t4, &Q->X, &Q->Y, M);
    mont_mul(&t3, &t3, &t4, M);
    mod_add(&t4, &t0, &t1, M);
    mod_sub(&t3, &t3, &t4, M);
    mod_add(&t4, &P->Y, &P->Z, M);
    mod_add(&X3, &Q->Y, &Q->Z, M);
    mont_mul(&t4, &t4, &X3, M);
    mod_add(&X3, &t1, &t2, M);
    mod_sub(&t4, &t4, &X3, M);
    mod_add(&X3, &P->X, &P->Z, M);
    mod_add(&Y3, &Q->X, &Q->Z, M);
    mont_mul(&X3, &X3, &Y3, M);
    mod_add(&Y3, &t0, &t2, M);
    mod_sub(&Y3, &X3, &Y3, M);
    mod_add(&X3, &t0, &t0, M);
    mod_add(&t0, &X3, &t0, M);
    mont_mul(&t2, &B3, &t2, M);
    mod_add(&Z3, &t1, &t2, M);
    mod_sub(&t1, &t1, &t2, M);
    mont_mul(&Y3, &B3, &Y3, M);
    mont_mul(&X3, &t4, &Y3, M);
    mont_mul(&t2, &t3, &t1, M);
    mod_sub(&X3, &t2, &X3, M);
    mont_mul(&Y3, &Y3, &t0, M);
    mont_mul(&t1, &t1, &Z3, M);
    mod_add(&Y3, &t1, &Y3, M);
    mont_mul(&t0, &t0, &t3, M);
    mont_mul(&Z3, &Z3, &t4, M);
    mod_add(&Z3, &Z3, &t0, M);
    R->X = X3;
    R->Y = Y3;
    R->Z = Z3;
}

static void pt_infinity(pt *R)
{
    w4_memset(R, 0, sizeof *R);
    fe_set(&R->Y, MP.one);
}

/* R = k * P, k a plain (non-Montgomery) 256-bit integer. Constant-time in k. */
static void pt_mul(pt *R, const uint32_t k[8], const pt *P)
{
    pt acc, t;
    pt_infinity(&acc);
    for (int i = 255; i >= 0; i--) {
        pt_add(&acc, &acc, &acc);
        pt_add(&t, &acc, P);
        uint32_t mask = (uint32_t) 0 - ((k[i >> 5] >> (i & 31)) & 1u);
        sel(acc.X.v, mask, t.X.v, acc.X.v);
        sel(acc.Y.v, mask, t.Y.v, acc.Y.v);
        sel(acc.Z.v, mask, t.Z.v, acc.Z.v);
    }
    *R = acc;
}

static void pt_from_affine(pt *R, const fe *x, const fe *y)
{
    to_mont(&R->X, x, &MP);
    to_mont(&R->Y, y, &MP);
    fe_set(&R->Z, MP.one);
}

static void pt_generator(pt *G)
{
    fe x, y;
    fe_set(&x, GX);
    fe_set(&y, GY);
    pt_from_affine(G, &x, &y);
}

/* Affine (plain integers). Returns false for the point at infinity. */
static bool pt_to_affine(const pt *P, fe *x, fe *y)
{
    if (is_zero(P->Z.v)) return false;
    fe zi, t;
    mont_inv(&zi, &P->Z, &MP);
    mont_mul(&t, &P->X, &zi, &MP);
    from_mont(x, &t, &MP);
    mont_mul(&t, &P->Y, &zi, &MP);
    from_mont(y, &t, &MP);
    return true;
}

/* y^2 == x^3 + 7 for plain x, y < p. */
static bool on_curve(const fe *x, const fe *y)
{
    fe xm, ym, l, r, seven = {{7, 0, 0, 0, 0, 0, 0, 0}}, s;
    to_mont(&xm, x, &MP);
    to_mont(&ym, y, &MP);
    to_mont(&s, &seven, &MP);
    mont_mul(&l, &ym, &ym, &MP);
    mont_mul(&r, &xm, &xm, &MP);
    mont_mul(&r, &r, &xm, &MP);
    mod_add(&r, &r, &s, &MP);
    return w4_ct_eq(l.v, r.v, sizeof l.v);
}

/* ---- scalars ---- */
static bool scalar_ok(const uint32_t k[8])
{
    return !is_zero(k) && lt(k, MN.m);
}

/* a mod n for a < 2^256 (< 2n), constant time. */
static void reduce_n(uint32_t r[8], const uint32_t a[8])
{
    uint32_t d[8];
    uint32_t br = sub_raw(d, a, MN.m);
    sel(r, (uint32_t) 0 - (br ^ 1u), d, a);
}

/* r = a * b mod n for plain a, b < n. */
static void n_mul(fe *r, const fe *a, const fe *b)
{
    fe am, bm, t;
    to_mont(&am, a, &MN);
    to_mont(&bm, b, &MN);
    mont_mul(&t, &am, &bm, &MN);
    from_mont(r, &t, &MN);
}

static void n_inv(fe *r, const fe *a)
{
    fe am, t;
    to_mont(&am, a, &MN);
    mont_inv(&t, &am, &MN);
    from_mont(r, &t, &MN);
}

int w4_secp_pubkey(const uint8_t sk[32], uint8_t pub64[64])
{
    if (!sk || !pub64) return W4_ERR_ARG;
    fe d, x, y;
    fe_from_be(&d, sk);
    if (!scalar_ok(d.v)) return W4_ERR_RANGE;
    pt G, Q;
    pt_generator(&G);
    pt_mul(&Q, d.v, &G);
    if (!pt_to_affine(&Q, &x, &y)) return W4_ERR_RANGE;
    fe_to_be(&x, pub64);
    fe_to_be(&y, pub64 + 32);
    w4_memset(&d, 0, sizeof d);
    return W4_OK;
}

void w4_secp_compress(const uint8_t pub64[64], uint8_t out[33])
{
    out[0] = (uint8_t) (0x02 | (pub64[63] & 1u));
    w4_memcpy(out + 1, pub64, 32);
}

/* HMAC_K(V || [sep] || x || h) for RFC 6979; sep < 0 means just V. */
static void hmac_k(const uint8_t K[32], const uint8_t V[32], int sep, const uint8_t *x,
                   const uint8_t *h, uint8_t out[32])
{
    uint8_t s = (uint8_t) sep;
    const uint8_t *parts[4] = {V, &s, x, h};
    uint32_t lens[4] = {32, sep >= 0 ? 1u : 0u, x ? 32u : 0u, h ? 32u : 0u};
    w4_hmac_sha256_parts(K, 32, parts, lens, 4, out);
}

int w4_secp_sign(const uint8_t sk[32], const uint8_t hash[32], w4_ecdsa_sig_t *sig)
{
    if (!sk || !hash || !sig) return W4_ERR_ARG;
    fe d, z, k, r, s, x, y, t;
    fe_from_be(&d, sk);
    if (!scalar_ok(d.v)) return W4_ERR_RANGE;
    fe_from_be(&z, hash);
    reduce_n(z.v, z.v);
    uint8_t h1[32], V[32], K[32];
    fe_to_be(&z, h1); /* bits2octets(h1) */
    w4_memset(V, 0x01, 32);
    w4_memset(K, 0x00, 32);
    hmac_k(K, V, 0x00, sk, h1, K);
    hmac_k(K, V, -1, NULL, NULL, V);
    hmac_k(K, V, 0x01, sk, h1, K);
    hmac_k(K, V, -1, NULL, NULL, V);
    pt G, R;
    pt_generator(&G);
    for (int attempt = 0; attempt < 64; attempt++) {
        hmac_k(K, V, -1, NULL, NULL, V);
        fe_from_be(&k, V);
        if (scalar_ok(k.v)) {
            pt_mul(&R, k.v, &G);
            if (pt_to_affine(&R, &x, &y)) {
                uint32_t over = lt(x.v, MN.m) ^ 1u; /* x >= n: r wrapped */
                reduce_n(r.v, x.v);
                if (!is_zero(r.v)) {
                    fe ki;
                    n_inv(&ki, &k);
                    n_mul(&t, &r, &d); /* r * d */
                    fe tm, zm;
                    to_mont(&tm, &t, &MN);
                    to_mont(&zm, &z, &MN);
                    mod_add(&tm, &tm, &zm, &MN);
                    from_mont(&t, &tm, &MN); /* z + r d */
                    n_mul(&s, &ki, &t);
                    if (!is_zero(s.v)) {
                        uint8_t recid = (uint8_t) ((y.v[0] & 1u) | (over << 1));
                        if (lt(NHALF, s.v)) { /* s > n/2: use n - s */
                            sub_raw(s.v, MN.m, s.v);
                            recid ^= 1u;
                        }
                        fe_to_be(&r, sig->r);
                        fe_to_be(&s, sig->s);
                        sig->recid = recid;
                        w4_memset(&k, 0, sizeof k);
                        w4_memset(&d, 0, sizeof d);
                        w4_memset(K, 0, sizeof K);
                        w4_memset(V, 0, sizeof V);
                        return W4_OK;
                    }
                }
            }
        }
        hmac_k(K, V, 0x00, NULL, NULL, K); /* K = HMAC_K(V || 0x00) */
        hmac_k(K, V, -1, NULL, NULL, V);
    }
    return W4_ERR_RANGE; /* unreachable in practice */
}

int w4_secp_signer(void *ctx, const uint8_t hash[32], w4_ecdsa_sig_t *sig)
{
    return w4_secp_sign((const uint8_t *) ctx, hash, sig);
}

static bool load_pub(const uint8_t pub64[64], pt *Q)
{
    fe x, y;
    fe_from_be(&x, pub64);
    fe_from_be(&y, pub64 + 32);
    if (!lt(x.v, MP.m) || !lt(y.v, MP.m) || !on_curve(&x, &y)) return false;
    pt_from_affine(Q, &x, &y);
    return true;
}

/* u1*G + u2*Q (public data). */
static void dual_mul(pt *R, const fe *u1, const fe *u2, const pt *Q)
{
    pt G, A, B;
    pt_generator(&G);
    pt_mul(&A, u1->v, &G);
    pt_mul(&B, u2->v, Q);
    pt_add(R, &A, &B);
}

int w4_secp_verify(const uint8_t pub64[64], const uint8_t hash[32], const w4_ecdsa_sig_t *sig,
                   bool require_low_s)
{
    if (!pub64 || !hash || !sig) return W4_ERR_ARG;
    fe r, s, z, w, u1, u2, x, y;
    fe_from_be(&r, sig->r);
    fe_from_be(&s, sig->s);
    if (!scalar_ok(r.v) || !scalar_ok(s.v)) return W4_ERR_SIG;
    if (require_low_s && lt(NHALF, s.v)) return W4_ERR_SIG;
    pt Q, R;
    if (!load_pub(pub64, &Q)) return W4_ERR_SIG;
    fe_from_be(&z, hash);
    reduce_n(z.v, z.v);
    n_inv(&w, &s);
    n_mul(&u1, &z, &w);
    n_mul(&u2, &r, &w);
    dual_mul(&R, &u1, &u2, &Q);
    if (!pt_to_affine(&R, &x, &y)) return W4_ERR_SIG;
    reduce_n(x.v, x.v);
    return w4_ct_eq(x.v, r.v, sizeof x.v) ? W4_OK : W4_ERR_SIG;
}

int w4_secp_recover(const uint8_t hash[32], const w4_ecdsa_sig_t *sig, uint8_t pub64[64])
{
    if (!hash || !sig || !pub64 || sig->recid > 3) return W4_ERR_ARG;
    fe r, s, z, x, y, xm, ym, t, seven = {{7, 0, 0, 0, 0, 0, 0, 0}};
    fe_from_be(&r, sig->r);
    fe_from_be(&s, sig->s);
    if (!scalar_ok(r.v) || !scalar_ok(s.v)) return W4_ERR_SIG;
    x = r;
    if (sig->recid & 2u) {
        if (add_raw(x.v, r.v, MN.m) || !lt(x.v, MP.m)) return W4_ERR_SIG;
    }
    /* y = sqrt(x^3 + 7) */
    to_mont(&xm, &x, &MP);
    mont_mul(&t, &xm, &xm, &MP);
    mont_mul(&t, &t, &xm, &MP);
    to_mont(&ym, &seven, &MP);
    mod_add(&t, &t, &ym, &MP);
    mont_pow(&ym, &t, PSQRT, &MP);
    fe chk;
    mont_mul(&chk, &ym, &ym, &MP);
    if (!w4_memeq(chk.v, t.v, sizeof t.v)) return W4_ERR_SIG; /* x not on the curve */
    from_mont(&y, &ym, &MP);
    if ((y.v[0] & 1u) != (sig->recid & 1u)) {
        fe zero = {{0}};
        mod_sub(&ym, &zero, &ym, &MP);
        from_mont(&y, &ym, &MP);
    }
    pt Rp, Q;
    pt_from_affine(&Rp, &x, &y);
    fe_from_be(&z, hash);
    reduce_n(z.v, z.v);
    fe ri, u1, u2, nz, zero = {{0}};
    n_inv(&ri, &r);
    /* u1 = -z / r, u2 = s / r */
    fe zm, zz;
    to_mont(&zm, &z, &MN);
    to_mont(&zz, &zero, &MN);
    mod_sub(&zm, &zz, &zm, &MN);
    from_mont(&nz, &zm, &MN);
    n_mul(&u1, &nz, &ri);
    n_mul(&u2, &s, &ri);
    dual_mul(&Q, &u1, &u2, &Rp);
    fe qx, qy;
    if (!pt_to_affine(&Q, &qx, &qy)) return W4_ERR_SIG;
    fe_to_be(&qx, pub64);
    fe_to_be(&qy, pub64 + 32);
    return W4_OK;
}

static bool check_mod(const modulus *M)
{
    /* R^2 mod m by 512 doublings of 1, compared with the stored constant. */
    fe r = {{1, 0, 0, 0, 0, 0, 0, 0}};
    for (int i = 0; i < 512; i++) mod_add(&r, &r, &r, M);
    if (!w4_memeq(r.v, M->r2, sizeof r.v)) return false;
    fe one = {{1, 0, 0, 0, 0, 0, 0, 0}}, o;
    to_mont(&o, &one, M);
    if (!w4_memeq(o.v, M->one, sizeof o.v)) return false;
    return (uint32_t) (M->m[0] * M->n0) == 0xffffffffu; /* m * n0 = -1 mod 2^32 */
}

bool w4_secp_selftest(void)
{
    if (!check_mod(&MP) || !check_mod(&MN)) return false;
    /* 4 * PSQRT - 1 == p and 2 * NHALF + 1 == n */
    uint32_t t[8], u[8];
    add_raw(t, PSQRT, PSQRT);
    add_raw(t, t, t);
    const uint32_t one[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    sub_raw(u, t, one);
    if (!w4_memeq(u, MP.m, sizeof u)) return false;
    add_raw(t, NHALF, NHALF);
    add_raw(t, t, one);
    if (!w4_memeq(t, MN.m, sizeof t)) return false;
    fe gx, gy;
    fe_set(&gx, GX);
    fe_set(&gy, GY);
    fe b = {{21, 0, 0, 0, 0, 0, 0, 0}}, bm;
    to_mont(&bm, &b, &MP);
    if (!w4_memeq(bm.v, B3.v, sizeof bm.v)) return false;
    return on_curve(&gx, &gy);
}
