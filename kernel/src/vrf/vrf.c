/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vrf.c — ECVRF-EDWARDS25519-SHA512-TAI (RFC 9381 suite 0x03); the rules
 * V1-V12 are in vrf.h. */
#include "vrf.h"

/* Namespace the vendored orlp/ed25519 code (public domain) with a vrf_
 * prefix so this file links next to robin_debanks/ed25519_verify.c, which
 * includes the same sources under their own names. */
#define fe_0                         vrf_fe_0
#define fe_1                         vrf_fe_1
#define fe_add                       vrf_fe_add
#define fe_cmov                      vrf_fe_cmov
#define fe_copy                      vrf_fe_copy
#define fe_cswap                     vrf_fe_cswap
#define fe_frombytes                 vrf_fe_frombytes
#define fe_invert                    vrf_fe_invert
#define fe_isnegative                vrf_fe_isnegative
#define fe_isnonzero                 vrf_fe_isnonzero
#define fe_mul                       vrf_fe_mul
#define fe_mul121666                 vrf_fe_mul121666
#define fe_neg                       vrf_fe_neg
#define fe_pow22523                  vrf_fe_pow22523
#define fe_sq                        vrf_fe_sq
#define fe_sq2                       vrf_fe_sq2
#define fe_sub                       vrf_fe_sub
#define fe_tobytes                   vrf_fe_tobytes
#define ge_add                       vrf_ge_add
#define ge_double_scalarmult_vartime vrf_ge_double_scalarmult_vartime
#define ge_frombytes_negate_vartime  vrf_ge_frombytes_negate_vartime
#define ge_madd                      vrf_ge_madd
#define ge_msub                      vrf_ge_msub
#define ge_p1p1_to_p2                vrf_ge_p1p1_to_p2
#define ge_p1p1_to_p3                vrf_ge_p1p1_to_p3
#define ge_p2_0                      vrf_ge_p2_0
#define ge_p2_dbl                    vrf_ge_p2_dbl
#define ge_p3_0                      vrf_ge_p3_0
#define ge_p3_dbl                    vrf_ge_p3_dbl
#define ge_p3_to_cached              vrf_ge_p3_to_cached
#define ge_p3_to_p2                  vrf_ge_p3_to_p2
#define ge_p3_tobytes                vrf_ge_p3_tobytes
#define ge_scalarmult_base           vrf_ge_scalarmult_base
#define ge_sub                       vrf_ge_sub
#define ge_tobytes                   vrf_ge_tobytes
#define sc_reduce                    vrf_sc_reduce
#define sc_muladd                    vrf_sc_muladd
#define sha512                       vrf_sha512
#define sha512_init                  vrf_sha512_init
#define sha512_update                vrf_sha512_update
#define sha512_final                 vrf_sha512_final

#include "../../third_party/ed25519/src/fe.c"
#include "../../third_party/ed25519/src/ge.c"
/* sc.c duplicates the static helpers load_3/load_4 of fe.c. */
#define load_3 vrf_sc_load_3
#define load_4 vrf_sc_load_4
#include "../../third_party/ed25519/src/sc.c"
#undef load_3
#undef load_4
#include "../../third_party/ed25519/src/sha512.c"

/* ---- small helpers ---- */

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

static void copy(uint8_t *d, const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

/* constant time: 1 when a == b over n bytes */
static int ct_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

/* q = 2^252 + 27742317777372353535851937790883648493, little-endian */
static const uint8_t Q[VRF_PT_LEN] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

/* s < q, little-endian, public data */
static bool scalar_canonical(const uint8_t s[VRF_PT_LEN])
{
    for (int i = 31; i >= 0; i--) {
        if (s[i] != Q[i]) return s[i] < Q[i];
    }
    return false;
}

/* V3: canonical y (< p) as a 255-bit integer */
static bool y_canonical(const uint8_t s[VRF_PT_LEN])
{
    if ((s[31] & 0x7f) != 0x7f) return true;
    for (int i = 30; i > 0; i--)
        if (s[i] != 0xff) return true;
    return s[0] < 0xed; /* p = 2^255 - 19: low byte 0xed */
}

/* V3: string_to_point. orlp's decoder returns -P; negate it back. */
static bool decode_point(ge_p3 *P, const uint8_t s[VRF_PT_LEN])
{
    if (!y_canonical(s)) return false;
    if (ge_frombytes_negate_vartime(P, s) != 0) return false;
    if (!fe_isnonzero(P->X) && (s[31] >> 7)) return false; /* "-0" */
    fe_neg(P->X, P->X);
    fe_neg(P->T, P->T);
    return true;
}

static void p3_dbl(ge_p3 *r, const ge_p3 *p)
{
    ge_p1p1 t;
    ge_p3_dbl(&t, p);
    ge_p1p1_to_p3(r, &t);
}

static void mul8(ge_p3 *r, const ge_p3 *p)
{
    p3_dbl(r, p);
    p3_dbl(r, r);
    p3_dbl(r, r);
}

static bool is_identity(const ge_p3 *p)
{
    static const uint8_t ID[VRF_PT_LEN] = {1};
    uint8_t b[VRF_PT_LEN];
    ge_p3_tobytes(b, p);
    return ct_eq(b, ID, VRF_PT_LEN);
}

/* r = p - q */
static void p3_sub(ge_p3 *r, const ge_p3 *p, const ge_p3 *q)
{
    ge_cached c;
    ge_p1p1 t;
    ge_p3_to_cached(&c, q);
    ge_sub(&t, p, &c);
    ge_p1p1_to_p3(r, &t);
}

/* V11: constant-time variable-base r = a*P, a[31] <= 127. */
static void cached_cmov(ge_cached *t, const ge_cached *u, unsigned char b)
{
    fe_cmov(t->YplusX, u->YplusX, b);
    fe_cmov(t->YminusX, u->YminusX, b);
    fe_cmov(t->Z, u->Z, b);
    fe_cmov(t->T2d, u->T2d, b);
}

static void ct_select(ge_cached *t, const ge_cached *tbl, signed char b)
{
    unsigned char neg = negative(b);
    signed char babs = (signed char) (b - (((-neg) & b) * 2));
    ge_cached m;
    fe_1(t->YplusX);
    fe_1(t->YminusX);
    fe_1(t->Z);
    fe_0(t->T2d);
    for (int j = 0; j < 8; j++) cached_cmov(t, &tbl[j], equal(babs, (signed char) (j + 1)));
    fe_copy(m.YplusX, t->YminusX);
    fe_copy(m.YminusX, t->YplusX);
    fe_copy(m.Z, t->Z);
    fe_neg(m.T2d, t->T2d);
    cached_cmov(t, &m, neg);
}

static void ct_scalarmult(ge_p3 *r, const uint8_t a[VRF_PT_LEN], const ge_p3 *P)
{
    ge_cached tbl[8], t;
    ge_p1p1 s;
    ge_p3 m;
    signed char e[2 * VRF_PT_LEN];
    int carry = 0;

    ge_p3_to_cached(&tbl[0], P); /* tbl[j] = (j+1) P */
    for (int j = 1; j < 8; j++) {
        ge_add(&s, P, &tbl[j - 1]);
        ge_p1p1_to_p3(&m, &s);
        ge_p3_to_cached(&tbl[j], &m);
    }
    /* signed radix 16: a = sum e[i] 16^i, e[i] in [-8, 8] */
    for (unsigned i = 0; i < VRF_PT_LEN; i++) {
        e[2 * i] = (signed char) (a[i] & 15);
        e[2 * i + 1] = (signed char) (a[i] >> 4);
    }
    for (int i = 0; i < 63; i++) {
        int v = e[i] + carry;
        carry = (v + 8) >> 4;
        e[i] = (signed char) (v - carry * 16);
    }
    e[63] = (signed char) (e[63] + carry);

    ge_p3_0(r);
    for (int i = 63; i >= 0; i--) {
        p3_dbl(r, r);
        p3_dbl(r, r);
        p3_dbl(r, r);
        p3_dbl(r, r);
        ct_select(&t, tbl, e[i]);
        ge_add(&s, r, &t);
        ge_p1p1_to_p3(r, &s);
    }
    wipe(e, sizeof e);
    wipe(tbl, sizeof tbl);
    wipe(&t, sizeof t);
}

/* ---- the RFC 9381 steps ---- */

/* V4 */
static bool encode_to_curve(ge_p3 *H, const uint8_t pk[VRF_PT_LEN], const uint8_t *alpha,
                            size_t len)
{
    static const uint8_t front[] = {VRF_SUITE, 0x01};
    for (unsigned ctr = 0; ctr < 256; ctr++) {
        sha512_context h;
        uint8_t tail[] = {(uint8_t) ctr, 0x00}, d[VRF_HASH_LEN];
        ge_p3 P;
        sha512_init(&h);
        sha512_update(&h, front, sizeof front);
        sha512_update(&h, pk, VRF_PT_LEN);
        sha512_update(&h, alpha, len);
        sha512_update(&h, tail, sizeof tail);
        sha512_final(&h, d);
        if (!decode_point(&P, d)) continue;
        mul8(H, &P);
        if (!is_identity(H)) return true;
    }
    return false;
}

/* V6: c over the five encoded points */
static void challenge(uint8_t c[VRF_C_LEN], const uint8_t pts[5][VRF_PT_LEN])
{
    static const uint8_t front[] = {VRF_SUITE, 0x02}, back[] = {0x00};
    sha512_context h;
    uint8_t d[VRF_HASH_LEN];
    sha512_init(&h);
    sha512_update(&h, front, sizeof front);
    for (int i = 0; i < 5; i++) sha512_update(&h, pts[i], VRF_PT_LEN);
    sha512_update(&h, back, sizeof back);
    sha512_final(&h, d);
    copy(c, d, VRF_C_LEN);
}

/* V8 */
static bool decode_proof(ge_p3 *G, const uint8_t pi[VRF_PROOF_LEN])
{
    return decode_point(G, pi) && scalar_canonical(pi + 48);
}

/* V9 */
static void gamma_to_hash(const ge_p3 *G, uint8_t beta[VRF_OUT_LEN])
{
    static const uint8_t front[] = {VRF_SUITE, 0x03}, back[] = {0x00};
    sha512_context h;
    uint8_t g8[VRF_PT_LEN];
    ge_p3 C;
    mul8(&C, G);
    ge_p3_tobytes(g8, &C);
    sha512_init(&h);
    sha512_update(&h, front, sizeof front);
    sha512_update(&h, g8, VRF_PT_LEN);
    sha512_update(&h, back, sizeof back);
    sha512_final(&h, beta);
}

/* V2: the expanded secret h = SHA-512(seed), clamped, and pk */
static void expand(const uint8_t seed[VRF_PT_LEN], uint8_t h[VRF_HASH_LEN], uint8_t pk[VRF_PT_LEN])
{
    ge_p3 A;
    sha512(seed, VRF_PT_LEN, h);
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;
    ge_scalarmult_base(&A, h);
    ge_p3_tobytes(pk, &A);
    wipe(&A, sizeof A);
}

void vrf_keypair(const uint8_t seed[VRF_SEED_LEN], uint8_t pk[VRF_PK_LEN], uint8_t sk[VRF_SK_LEN])
{
    uint8_t h[VRF_HASH_LEN];
    expand(seed, h, pk);
    copy(sk, seed, VRF_PT_LEN);
    copy(sk + VRF_PT_LEN, pk, VRF_PT_LEN);
    wipe(h, sizeof h);
}

/* V7; trace (test only) receives H, k, U, V */
static bool prove(const uint8_t sk[VRF_SK_LEN], const uint8_t *alpha, size_t alpha_len,
                  uint8_t pi[VRF_PROOF_LEN], uint8_t (*trace)[VRF_PT_LEN])
{
    uint8_t h[VRF_HASH_LEN], kh[VRF_HASH_LEN], pts[5][VRF_PT_LEN], c32[VRF_PT_LEN] = {0};
    sha512_context hc;
    ge_p3 H, P;
    bool ok = false;

    if (!pi) return false;
    wipe(pi, VRF_PROOF_LEN);
    if (!sk || (!alpha && alpha_len)) return false;
    expand(sk, h, pts[0]); /* pts = Y, H, Gamma, U, V */
    if (!ct_eq(pts[0], sk + VRF_PT_LEN, VRF_PT_LEN)) goto out;
    if (!encode_to_curve(&H, pts[0], alpha, alpha_len)) goto out;
    ge_p3_tobytes(pts[1], &H);
    ct_scalarmult(&P, h, &H); /* Gamma = x*H */
    ge_p3_tobytes(pts[2], &P);
    sha512_init(&hc); /* V5 */
    sha512_update(&hc, h + VRF_PT_LEN, VRF_PT_LEN);
    sha512_update(&hc, pts[1], VRF_PT_LEN);
    sha512_final(&hc, kh);
    sc_reduce(kh); /* k = kh[0..31] */
    ge_scalarmult_base(&P, kh);
    ge_p3_tobytes(pts[3], &P);
    ct_scalarmult(&P, kh, &H);
    ge_p3_tobytes(pts[4], &P);
    challenge(c32, (const uint8_t(*)[VRF_PT_LEN]) pts);
    copy(pi, pts[2], VRF_PT_LEN);
    copy(pi + VRF_PT_LEN, c32, VRF_C_LEN);
    sc_muladd(pi + 48, c32, h, kh); /* s = c*x + k mod q */
    if (trace) {
        copy(trace[0], pts[1], VRF_PT_LEN);
        copy(trace[1], kh, VRF_PT_LEN);
        copy(trace[2], pts[3], VRF_PT_LEN);
        copy(trace[3], pts[4], VRF_PT_LEN);
    }
    ok = true;
out:
    wipe(h, sizeof h);
    wipe(kh, sizeof kh);
    wipe(&P, sizeof P);
    wipe(&hc, sizeof hc);
    return ok;
}

bool vrf_prove(const uint8_t sk[VRF_SK_LEN], const uint8_t *alpha, size_t alpha_len,
               uint8_t pi[VRF_PROOF_LEN])
{
    return prove(sk, alpha, alpha_len, pi, NULL);
}

#ifdef TEST_HOST
bool vrf_prove_trace(const uint8_t sk[VRF_SK_LEN], const uint8_t *alpha, size_t alpha_len,
                     uint8_t pi[VRF_PROOF_LEN], vrf_trace_t *trace)
{
    uint8_t t[4][VRF_PT_LEN];
    if (!trace) return false;
    if (!prove(sk, alpha, alpha_len, pi, t)) return false;
    copy(trace->h, t[0], VRF_PT_LEN);
    copy(trace->k, t[1], VRF_PT_LEN);
    copy(trace->u, t[2], VRF_PT_LEN);
    copy(trace->v, t[3], VRF_PT_LEN);
    return true;
}
#endif

bool vrf_proof_to_hash(const uint8_t pi[VRF_PROOF_LEN], uint8_t beta[VRF_OUT_LEN])
{
    ge_p3 G;
    if (!beta) return false;
    wipe(beta, VRF_OUT_LEN);
    if (!pi || !decode_proof(&G, pi)) return false;
    gamma_to_hash(&G, beta);
    return true;
}

/* V10 */
bool vrf_verify(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                const uint8_t pi[VRF_PROOF_LEN], uint8_t beta[VRF_OUT_LEN])
{
    ge_p3 Y, G, H, T, S;
    ge_p2 R;
    uint8_t pts[5][VRF_PT_LEN], c32[VRF_PT_LEN] = {0}, c[VRF_C_LEN];

    if (!beta) return false;
    wipe(beta, VRF_OUT_LEN);
    if (!pk || !pi || (!alpha && alpha_len)) return false;
    if (!decode_point(&Y, pk)) return false;
    mul8(&T, &Y); /* validate_key */
    if (is_identity(&T)) return false;
    if (!decode_proof(&G, pi)) return false;
    if (!encode_to_curve(&H, pk, alpha, alpha_len)) return false;
    copy(c32, pi + VRF_PT_LEN, VRF_C_LEN);
    copy(pts[0], pk, VRF_PT_LEN);
    ge_p3_tobytes(pts[1], &H);
    copy(pts[2], pi, VRF_PT_LEN);
    /* U = s*B - c*Y = c*(-Y) + s*B */
    fe_neg(Y.X, Y.X);
    fe_neg(Y.T, Y.T);
    ge_double_scalarmult_vartime(&R, c32, &Y, pi + 48);
    ge_tobytes(pts[3], &R);
    /* V = s*H - c*Gamma */
    ct_scalarmult(&S, pi + 48, &H);
    ct_scalarmult(&T, c32, &G);
    p3_sub(&S, &S, &T);
    ge_p3_tobytes(pts[4], &S);
    challenge(c, (const uint8_t(*)[VRF_PT_LEN]) pts);
    if (!ct_eq(c, pi + VRF_PT_LEN, VRF_C_LEN)) return false;
    gamma_to_hash(&G, beta);
    return true;
}

bool vrf_encode_to_curve(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                         uint8_t h[VRF_PT_LEN])
{
    ge_p3 H;
    if (!h) return false;
    wipe(h, VRF_PT_LEN);
    if (!pk || (!alpha && alpha_len)) return false;
    if (!encode_to_curve(&H, pk, alpha, alpha_len)) return false;
    ge_p3_tobytes(h, &H);
    return true;
}

bool vrf_point_valid(const uint8_t s[VRF_PT_LEN])
{
    ge_p3 P;
    return s && decode_point(&P, s);
}

/* ---- V12 adapters ---- */

bool vrf_dsp_verify(void *ctx, const uint8_t seed[VRF_PT_LEN], const uint8_t *proof,
                    uint32_t proof_len, const uint8_t out[VRF_PT_LEN])
{
    const vrf_dsp_ctx_t *k = (const vrf_dsp_ctx_t *) ctx;
    uint8_t beta[VRF_OUT_LEN];
    if (!k || !seed || !out || proof_len != VRF_PROOF_LEN) return false;
    return vrf_verify(k->pk, seed, VRF_PT_LEN, proof, beta) && ct_eq(beta, out, VRF_PT_LEN);
}

bool vrf_dsp_out(const uint8_t sk[VRF_SK_LEN], const uint8_t seed[VRF_PT_LEN],
                 uint8_t pi[VRF_PROOF_LEN], uint8_t out[VRF_PT_LEN])
{
    uint8_t beta[VRF_OUT_LEN];
    if (!out) return false;
    wipe(out, VRF_PT_LEN);
    if (!seed || !vrf_prove(sk, seed, VRF_PT_LEN, pi)) return false;
    vrf_proof_to_hash(pi, beta);
    copy(out, beta, VRF_PT_LEN);
    return true;
}

void vrf_beacon_alpha(uint64_t epoch, uint8_t alpha[VRF_BEACON_ALPHA])
{
    static const char tag[] = "ZXV-ALLOT-BEACON";
    for (unsigned i = 0; i < VRF_BEACON_TAG; i++) alpha[i] = (uint8_t) tag[i];
    for (unsigned i = 0; i < 8; i++) alpha[VRF_BEACON_TAG + i] = (uint8_t) (epoch >> (8 * i));
}

bool vrf_beacon(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                const uint8_t pi[VRF_PROOF_LEN], uint8_t out[VRF_PT_LEN])
{
    uint8_t beta[VRF_OUT_LEN];
    if (!out) return false;
    bool ok = vrf_verify(pk, alpha, alpha_len, pi, beta);
    copy(out, beta, VRF_PT_LEN); /* zero when refused (V10) */
    return ok;
}
