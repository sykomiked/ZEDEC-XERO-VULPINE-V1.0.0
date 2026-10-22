/* adiantum.c — Adiantum length-preserving encryption. See adiantum.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "adiantum.h"

static uint32_t ld32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void st32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static void wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    while (n--) *v++ = 0;
}

/* ======================================================================
 * AES-256, one block, constant time
 *
 * The S-box is the multiplicative inverse in GF(2^8) followed by an affine
 * map. Computing the inverse as x^254 with a branch-free multiply takes no
 * table and no data-dependent branch, so the timing does not depend on the
 * key or the data. It is slow per byte, which does not matter here: Adiantum
 * runs AES on one 16-byte block per sector.
 * ====================================================================== */

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r ^= (uint8_t) (a & (uint8_t) - (b & 1));
        uint8_t hi = (uint8_t) (a >> 7);
        a = (uint8_t) ((a << 1) ^ (0x1B & (uint8_t) -hi));
        b >>= 1;
    }
    return r;
}

static uint8_t gf_inv(uint8_t x) /* x^254; 0 maps to 0 */
{
    uint8_t x2 = gf_mul(x, x);
    uint8_t x3 = gf_mul(x2, x);
    uint8_t x12 = gf_mul(x3, x3);
    x12 = gf_mul(x12, x12);
    uint8_t x15 = gf_mul(x12, x3);
    uint8_t x240 = x15;
    for (int i = 0; i < 4; i++) x240 = gf_mul(x240, x240);
    return gf_mul(gf_mul(x240, x12), x2);
}

static uint8_t rotl8(uint8_t b, int n)
{
    return (uint8_t) ((b << n) | (b >> (8 - n)));
}

static uint8_t sbox(uint8_t x)
{
    uint8_t b = gf_inv(x);
    return (uint8_t) (b ^ rotl8(b, 1) ^ rotl8(b, 2) ^ rotl8(b, 3) ^ rotl8(b, 4) ^ 0x63);
}

static uint8_t inv_sbox(uint8_t s)
{
    return gf_inv((uint8_t) (rotl8(s, 1) ^ rotl8(s, 3) ^ rotl8(s, 6) ^ 0x05));
}

void aes256_ct_init(aes256_ct_t *a, const uint8_t key[32])
{
    uint8_t *w = a->rk;
    for (int i = 0; i < 32; i++) w[i] = key[i];
    uint8_t rcon = 1;
    for (int i = 8; i < 60; i++) {
        uint8_t t[4];
        for (int j = 0; j < 4; j++) t[j] = w[4 * (i - 1) + j];
        if (i % 8 == 0) {
            uint8_t t0 = t[0];
            t[0] = (uint8_t) (sbox(t[1]) ^ rcon);
            t[1] = sbox(t[2]);
            t[2] = sbox(t[3]);
            t[3] = sbox(t0);
            rcon = gf_mul(rcon, 2);
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox(t[j]);
        }
        for (int j = 0; j < 4; j++) w[4 * i + j] = (uint8_t) (w[4 * (i - 8) + j] ^ t[j]);
    }
}

static void add_round_key(uint8_t s[16], const uint8_t *rk)
{
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];
}

/* The state is column-major: s[row + 4 * column]. */
static void shift_rows(uint8_t s[16], bool inverse)
{
    uint8_t t[16];
    for (int i = 0; i < 16; i++) t[i] = s[i];
    for (int r = 1; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int from = inverse ? (c + 4 - r) & 3 : (c + r) & 3;
            s[r + 4 * c] = t[r + 4 * from];
        }
}

static void mix_columns(uint8_t s[16])
{
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t) (gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
        p[1] = (uint8_t) (a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
        p[2] = (uint8_t) (a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
        p[3] = (uint8_t) (gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
    }
}

static void inv_mix_columns(uint8_t s[16])
{
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t) (gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
        p[1] = (uint8_t) (gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
        p[2] = (uint8_t) (gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
        p[3] = (uint8_t) (gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
    }
}

void aes256_ct_encrypt(const aes256_ct_t *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i];
    add_round_key(s, a->rk);
    for (int round = 1; round <= 14; round++) {
        for (int i = 0; i < 16; i++) s[i] = sbox(s[i]);
        shift_rows(s, false);
        if (round != 14) mix_columns(s);
        add_round_key(s, a->rk + 16 * round);
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
    wipe(s, sizeof s);
}

void aes256_ct_decrypt(const aes256_ct_t *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i];
    add_round_key(s, a->rk + 16 * 14);
    for (int round = 13; round >= 0; round--) {
        shift_rows(s, true);
        for (int i = 0; i < 16; i++) s[i] = inv_sbox(s[i]);
        add_round_key(s, a->rk + 16 * round);
        if (round != 0) inv_mix_columns(s);
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
    wipe(s, sizeof s);
}

/* ======================================================================
 * XChaCha12
 * ====================================================================== */

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QR(a, b, c, d)                                                                             \
    do {                                                                                           \
        a += b;                                                                                    \
        d ^= a;                                                                                    \
        d = ROTL32(d, 16);                                                                         \
        c += d;                                                                                    \
        b ^= c;                                                                                    \
        b = ROTL32(b, 12);                                                                         \
        a += b;                                                                                    \
        d ^= a;                                                                                    \
        d = ROTL32(d, 8);                                                                          \
        c += d;                                                                                    \
        b ^= c;                                                                                    \
        b = ROTL32(b, 7);                                                                          \
    } while (0)

static void chacha_permute(uint32_t x[16], int rounds)
{
    for (int i = 0; i < rounds; i += 2) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
}

static void chacha_setup(uint32_t s[16], const uint8_t key[32])
{
    s[0] = 0x61707865u; /* "expand 32-byte k" */
    s[1] = 0x3320646Eu;
    s[2] = 0x79622D32u;
    s[3] = 0x6B206574u;
    for (int i = 0; i < 8; i++) s[4 + i] = ld32(key + 4 * i);
}

void xchacha12_xor(const uint8_t key[32], const uint8_t nonce[24], uint64_t counter,
                   const uint8_t *in, uint8_t *out, uint32_t len)
{
    uint32_t s[16], x[16];
    /* HChaCha12: the subkey for this nonce */
    chacha_setup(s, key);
    for (int i = 0; i < 4; i++) s[12 + i] = ld32(nonce + 4 * i);
    chacha_permute(s, 12);
    uint8_t subkey[32];
    for (int i = 0; i < 4; i++) {
        st32(subkey + 4 * i, s[i]);
        st32(subkey + 16 + 4 * i, s[12 + i]);
    }
    chacha_setup(s, subkey);
    s[14] = ld32(nonce + 16);
    s[15] = ld32(nonce + 20);

    uint8_t ks[64];
    while (len) {
        s[12] = (uint32_t) counter;
        s[13] = (uint32_t) (counter >> 32);
        for (int i = 0; i < 16; i++) x[i] = s[i];
        chacha_permute(x, 12);
        for (int i = 0; i < 16; i++) st32(ks + 4 * i, x[i] + s[i]);
        uint32_t n = len < 64 ? len : 64;
        for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (in[i] ^ ks[i]);
        in += n;
        out += n;
        len -= n;
        counter++;
    }
    wipe(subkey, sizeof subkey);
    wipe(ks, sizeof ks);
    wipe(s, sizeof s);
    wipe(x, sizeof x);
}

/* ======================================================================
 * Poly1305 as an e-almost-delta-universal hash: the polynomial in
 * GF(2^130 - 5), fully reduced, without the final "+ s" of the MAC.
 * 26-bit limbs, so every product fits in 64 bits.
 * ====================================================================== */

typedef struct {
    uint32_t h[5];
} poly_state_t;

static void poly_setkey(poly1305_key_t *k, const uint8_t key[16])
{
    k->r[0] = ld32(key + 0) & 0x3FFFFFFu;
    k->r[1] = (ld32(key + 3) >> 2) & 0x3FFFF03u;
    k->r[2] = (ld32(key + 6) >> 4) & 0x3FFC0FFu;
    k->r[3] = (ld32(key + 9) >> 6) & 0x3F03FFFu;
    k->r[4] = (ld32(key + 12) >> 8) & 0x00FFFFFu;
}

static void poly_init(poly_state_t *st)
{
    for (int i = 0; i < 5; i++) st->h[i] = 0;
}

/* Absorb one 16-byte block with the 2^128 bit set. */
static void poly_block(poly_state_t *st, const poly1305_key_t *k, const uint8_t m[16])
{
    const uint32_t r0 = k->r[0], r1 = k->r[1], r2 = k->r[2], r3 = k->r[3], r4 = k->r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0] + (ld32(m + 0) & 0x3FFFFFFu);
    uint32_t h1 = st->h[1] + ((ld32(m + 3) >> 2) & 0x3FFFFFFu);
    uint32_t h2 = st->h[2] + ((ld32(m + 6) >> 4) & 0x3FFFFFFu);
    uint32_t h3 = st->h[3] + ((ld32(m + 9) >> 6) & 0x3FFFFFFu);
    uint32_t h4 = st->h[4] + ((ld32(m + 12) >> 8) | (1u << 24));

    uint64_t d0 = (uint64_t) h0 * r0 + (uint64_t) h1 * s4 + (uint64_t) h2 * s3 +
                  (uint64_t) h3 * s2 + (uint64_t) h4 * s1;
    uint64_t d1 = (uint64_t) h0 * r1 + (uint64_t) h1 * r0 + (uint64_t) h2 * s4 +
                  (uint64_t) h3 * s3 + (uint64_t) h4 * s2;
    uint64_t d2 = (uint64_t) h0 * r2 + (uint64_t) h1 * r1 + (uint64_t) h2 * r0 +
                  (uint64_t) h3 * s4 + (uint64_t) h4 * s3;
    uint64_t d3 = (uint64_t) h0 * r3 + (uint64_t) h1 * r2 + (uint64_t) h2 * r1 +
                  (uint64_t) h3 * r0 + (uint64_t) h4 * s4;
    uint64_t d4 = (uint64_t) h0 * r4 + (uint64_t) h1 * r3 + (uint64_t) h2 * r2 +
                  (uint64_t) h3 * r1 + (uint64_t) h4 * r0;

    uint32_t c;
    c = (uint32_t) (d0 >> 26);
    h0 = (uint32_t) d0 & 0x3FFFFFFu;
    d1 += c;
    c = (uint32_t) (d1 >> 26);
    h1 = (uint32_t) d1 & 0x3FFFFFFu;
    d2 += c;
    c = (uint32_t) (d2 >> 26);
    h2 = (uint32_t) d2 & 0x3FFFFFFu;
    d3 += c;
    c = (uint32_t) (d3 >> 26);
    h3 = (uint32_t) d3 & 0x3FFFFFFu;
    d4 += c;
    c = (uint32_t) (d4 >> 26);
    h4 = (uint32_t) d4 & 0x3FFFFFFu;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3FFFFFFu;
    h1 += c;

    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
    st->h[3] = h3;
    st->h[4] = h4;
}

/* h mod (2^130 - 5), then its low 128 bits, little-endian. */
static void poly_emit(const poly_state_t *st, uint8_t out[16])
{
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4], c;
    c = h1 >> 26;
    h1 &= 0x3FFFFFFu;
    h2 += c;
    c = h2 >> 26;
    h2 &= 0x3FFFFFFu;
    h3 += c;
    c = h3 >> 26;
    h3 &= 0x3FFFFFFu;
    h4 += c;
    c = h4 >> 26;
    h4 &= 0x3FFFFFFu;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3FFFFFFu;
    h1 += c;

    /* g = h - p; keep g if it did not borrow */
    uint32_t g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= 0x3FFFFFFu;
    uint32_t g1 = h1 + c;
    c = g1 >> 26;
    g1 &= 0x3FFFFFFu;
    uint32_t g2 = h2 + c;
    c = g2 >> 26;
    g2 &= 0x3FFFFFFu;
    uint32_t g3 = h3 + c;
    c = g3 >> 26;
    g3 &= 0x3FFFFFFu;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1; /* all ones when g >= 0 */
    h0 = (h0 & ~mask) | (g0 & mask);
    h1 = (h1 & ~mask) | (g1 & mask);
    h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask);
    h4 = (h4 & ~mask) | (g4 & mask);

    st32(out + 0, h0 | (h1 << 26));
    st32(out + 4, (h1 >> 6) | (h2 << 20));
    st32(out + 8, (h2 >> 12) | (h3 << 14));
    st32(out + 12, (h3 >> 18) | (h4 << 8));
}

/* ======================================================================
 * NHPoly1305: NH over 1024-byte chunks, each 32-byte NH digest fed to
 * Poly1305 as two blocks.
 * ====================================================================== */

#define NH_CHUNK 1024u

void nhpoly1305_setkey(nhpoly1305_key_t *k, const uint8_t key[NHPOLY1305_KEY_LEN])
{
    poly_setkey(&k->poly, key);
    for (uint32_t i = 0; i < NH_KEY_WORDS; i++) k->nh[i] = ld32(key + 16 + 4 * i);
}

/* NH over `len` bytes (at most 1024) of `msg`, zero-padding the last
 * 16-byte unit. */
static void nh(const uint32_t *key, const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    uint64_t sums[4] = {0, 0, 0, 0};
    uint8_t unit[16];
    for (uint32_t off = 0; off < len; off += 16, key += 4) {
        const uint8_t *u = msg + off;
        if (len - off < 16) {
            uint32_t n = len - off, i;
            for (i = 0; i < n; i++) unit[i] = u[i];
            for (; i < 16; i++) unit[i] = 0;
            u = unit;
        }
        uint32_t m0 = ld32(u), m1 = ld32(u + 4), m2 = ld32(u + 8), m3 = ld32(u + 12);
        for (int p = 0; p < 4; p++) {
            const uint32_t *k = key + 4 * p;
            sums[p] += (uint64_t) (uint32_t) (m0 + k[0]) * (uint32_t) (m2 + k[2]);
            sums[p] += (uint64_t) (uint32_t) (m1 + k[1]) * (uint32_t) (m3 + k[3]);
        }
    }
    for (int p = 0; p < 4; p++) {
        st32(out + 8 * p, (uint32_t) sums[p]);
        st32(out + 8 * p + 4, (uint32_t) (sums[p] >> 32));
    }
}

void nhpoly1305(const nhpoly1305_key_t *k, const uint8_t *msg, uint32_t len, uint8_t out[16])
{
    poly_state_t st;
    uint8_t digest[32];
    poly_init(&st);
    for (uint32_t off = 0; off < len; off += NH_CHUNK) {
        uint32_t n = len - off < NH_CHUNK ? len - off : NH_CHUNK;
        nh(k->nh, msg + off, n, digest);
        poly_block(&st, &k->poly, digest);
        poly_block(&st, &k->poly, digest + 16);
    }
    poly_emit(&st, out);
}

/* ======================================================================
 * Adiantum
 * ====================================================================== */

#define DERIVED_LEN (32u + 16u + NHPOLY1305_KEY_LEN)

void adiantum_init(adiantum_t *c, const uint8_t key[ADIANTUM_KEY_LEN])
{
    /* K_E || K_T || K_M || K_N = XChaCha12(K_S, nonce = 1 || 0...) */
    static uint8_t derived[DERIVED_LEN]; /* too big for a small kernel stack */
    uint8_t nonce[24];
    for (uint32_t i = 0; i < 32; i++) c->ks[i] = key[i];
    for (uint32_t i = 0; i < 24; i++) nonce[i] = 0;
    nonce[0] = 1;
    for (uint32_t i = 0; i < DERIVED_LEN; i++) derived[i] = 0;
    xchacha12_xor(key, nonce, 0, derived, derived, DERIVED_LEN);
    aes256_ct_init(&c->aes, derived);
    poly_setkey(&c->ktweak, derived + 32);
    nhpoly1305_setkey(&c->kmsg, derived + 48);
    wipe(derived, DERIVED_LEN);
}

static void add128(uint8_t a[16], const uint8_t b[16])
{
    uint32_t carry = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t s = (uint32_t) a[i] + b[i] + carry;
        a[i] = (uint8_t) s;
        carry = s >> 8;
    }
}

static void sub128(uint8_t a[16], const uint8_t b[16])
{
    uint32_t borrow = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t d = (uint32_t) a[i] - b[i] - borrow;
        a[i] = (uint8_t) d;
        borrow = (d >> 8) & 1;
    }
}

/* H_T = Poly1305_{K_T}(bitlen(bulk) as 128 bits || tweak) */
static void hash_header(const adiantum_t *c, const uint8_t tweak[32], uint32_t bulk,
                        uint8_t out[16])
{
    uint8_t blk[16];
    poly_state_t st;
    uint64_t bits = (uint64_t) bulk * 8;
    for (int i = 0; i < 16; i++) blk[i] = 0;
    st32(blk, (uint32_t) bits);
    st32(blk + 4, (uint32_t) (bits >> 32));
    poly_init(&st);
    poly_block(&st, &c->ktweak, blk);
    poly_block(&st, &c->ktweak, tweak);
    poly_block(&st, &c->ktweak, tweak + 16);
    poly_emit(&st, out);
}

static bool adiantum_crypt(const adiantum_t *c, const uint8_t tweak[32], const uint8_t *in,
                           uint8_t *out, uint32_t len, bool enc)
{
    if (len < ADIANTUM_MIN_LEN) return false;
    uint32_t bulk = len - 16;
    uint8_t hT[16], d[16], r[16], nonce[24];

    /* First hash: r = in_R + H(T, in_L) */
    hash_header(c, tweak, bulk, hT);
    nhpoly1305(&c->kmsg, in, bulk, d);
    for (int i = 0; i < 16; i++) r[i] = in[bulk + i];
    add128(r, hT);
    add128(r, d);

    if (enc) aes256_ct_encrypt(&c->aes, r, r);

    /* The stream nonce is C_M || 1 || 0... */
    for (int i = 0; i < 16; i++) nonce[i] = r[i];
    for (int i = 16; i < 24; i++) nonce[i] = 0;
    nonce[16] = 1;
    xchacha12_xor(c->ks, nonce, 0, in, out, bulk);

    if (!enc) aes256_ct_decrypt(&c->aes, r, r);

    /* Second hash: out_R = r - H(T, out_L) */
    nhpoly1305(&c->kmsg, out, bulk, d);
    sub128(r, hT);
    sub128(r, d);
    for (int i = 0; i < 16; i++) out[bulk + i] = r[i];

    wipe(r, sizeof r);
    wipe(nonce, sizeof nonce);
    return true;
}

bool adiantum_encrypt(const adiantum_t *c, const uint8_t tweak[ADIANTUM_TWEAK_LEN],
                      const uint8_t *in, uint8_t *out, uint32_t len)
{
    return adiantum_crypt(c, tweak, in, out, len, true);
}

bool adiantum_decrypt(const adiantum_t *c, const uint8_t tweak[ADIANTUM_TWEAK_LEN],
                      const uint8_t *in, uint8_t *out, uint32_t len)
{
    return adiantum_crypt(c, tweak, in, out, len, false);
}

void adiantum_wipe(adiantum_t *c)
{
    wipe(c, sizeof *c);
}
