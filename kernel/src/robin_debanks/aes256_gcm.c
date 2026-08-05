/* aes256_gcm.c — Freestanding AES-256-GCM implementation
 * See aes256_gcm.h for design notes and validation vector.
 *
 * AES-256 core follows FIPS 197 exactly (Nk=8, Nr=14). GCM mode follows
 * NIST SP 800-38D exactly (96-bit IV standard case, bit-by-bit GHASH for
 * simplicity/auditability over table-driven speed -- this is a low-
 * throughput vault subsystem, not a network data path).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "aes256_gcm.h"

/* ===== AES-256 core ===== */

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[15] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36,0x6c,0xd8,0xab,0x4d
};

#define AES256_NK 8
#define AES256_NR 14
#define AES256_ROUNDKEY_LEN (16 * (AES256_NR + 1)) /* 240 bytes */

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

static void key_expansion(const uint8_t key[AES256_KEY_LEN], uint8_t rk[AES256_ROUNDKEY_LEN]) {
    uint8_t temp[4];
    int i;

    for (i = 0; i < AES256_NK; i++) {
        rk[i * 4 + 0] = key[i * 4 + 0];
        rk[i * 4 + 1] = key[i * 4 + 1];
        rk[i * 4 + 2] = key[i * 4 + 2];
        rk[i * 4 + 3] = key[i * 4 + 3];
    }

    for (i = AES256_NK; i < 4 * (AES256_NR + 1); i++) {
        temp[0] = rk[(i - 1) * 4 + 0];
        temp[1] = rk[(i - 1) * 4 + 1];
        temp[2] = rk[(i - 1) * 4 + 2];
        temp[3] = rk[(i - 1) * 4 + 3];

        if (i % AES256_NK == 0) {
            uint8_t t0 = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[i / AES256_NK];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t0];
        } else if (i % AES256_NK == 4) {
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];
        }

        rk[i * 4 + 0] = rk[(i - AES256_NK) * 4 + 0] ^ temp[0];
        rk[i * 4 + 1] = rk[(i - AES256_NK) * 4 + 1] ^ temp[1];
        rk[i * 4 + 2] = rk[(i - AES256_NK) * 4 + 2] ^ temp[2];
        rk[i * 4 + 3] = rk[(i - AES256_NK) * 4 + 3] ^ temp[3];
    }
}

static void add_round_key(uint8_t state[16], const uint8_t *rk_round) {
    for (int i = 0; i < 16; i++) state[i] ^= rk_round[i];
}

static void sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

/* state[row + 4*col], standard AES column-major layout */
static void shift_rows(uint8_t state[16]) {
    uint8_t t;
    /* row 1: shift left 1 */
    t = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
    /* row 2: shift left 2 */
    t = state[2]; state[2] = state[10]; state[10] = t;
    t = state[6]; state[6] = state[14]; state[14] = t;
    /* row 3: shift left 3 (== shift right 1) */
    t = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;
}

static void mix_columns(uint8_t state[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *s = &state[c * 4];
        uint8_t a0 = s[0], a1 = s[1], a2 = s[2], a3 = s[3];
        s[0] = (uint8_t)(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
        s[1] = (uint8_t)(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
        s[2] = (uint8_t)(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
        s[3] = (uint8_t)(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
    }
}

/* Encrypt exactly one 16-byte block in place (ECB core, used as the
 * GCTR/GHASH building block -- never used directly for bulk data). */
static void aes256_encrypt_block(const uint8_t rk[AES256_ROUNDKEY_LEN], uint8_t block[16]) {
    add_round_key(block, &rk[0]);
    for (int round = 1; round < AES256_NR; round++) {
        sub_bytes(block);
        shift_rows(block);
        mix_columns(block);
        add_round_key(block, &rk[round * 16]);
    }
    sub_bytes(block);
    shift_rows(block);
    add_round_key(block, &rk[AES256_NR * 16]);
}

/* ===== GF(2^128) multiplication for GHASH (NIST SP 800-38D, bit-by-bit,
 * favoring auditability/correctness over speed for this low-throughput
 * vault subsystem). ===== */
static void gf128_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    /* NOTE: every call site uses gf128_mul(y, h, y) i.e. z aliases x.
     * Copy x locally and accumulate separately before ever writing to
     * z, so aliasing cannot corrupt the inputs mid-computation. */
    uint8_t v[16], xx[16], acc[16];
    for (int i = 0; i < 16; i++) { v[i] = y[i]; xx[i] = x[i]; acc[i] = 0; }

    for (int i = 0; i < 128; i++) {
        int bit = (xx[i / 8] >> (7 - (i % 8))) & 1;
        if (bit) {
            for (int j = 0; j < 16; j++) acc[j] ^= v[j];
        }
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) {
            v[j] = (uint8_t)((v[j] >> 1) | ((v[j - 1] & 1) << 7));
        }
        v[0] = (uint8_t)(v[0] >> 1);
        if (lsb) v[0] ^= 0xe1;
    }

    for (int i = 0; i < 16; i++) z[i] = acc[i];
}

static void ghash(const uint8_t h[16], const uint8_t *aad, size_t aad_len,
                   const uint8_t *c, size_t c_len, uint8_t out[16]) {
    uint8_t y[16] = {0};
    uint8_t block[16];
    size_t i;

    for (i = 0; i + 16 <= aad_len; i += 16) {
        for (int j = 0; j < 16; j++) y[j] ^= aad[i + j];
        gf128_mul(y, h, y);
    }
    if (i < aad_len) {
        for (int j = 0; j < 16; j++) block[j] = 0;
        for (size_t j = 0; j < aad_len - i; j++) block[j] = aad[i + j];
        for (int j = 0; j < 16; j++) y[j] ^= block[j];
        gf128_mul(y, h, y);
    }

    for (i = 0; i + 16 <= c_len; i += 16) {
        for (int j = 0; j < 16; j++) y[j] ^= c[i + j];
        gf128_mul(y, h, y);
    }
    if (i < c_len) {
        for (int j = 0; j < 16; j++) block[j] = 0;
        for (size_t j = 0; j < c_len - i; j++) block[j] = c[i + j];
        for (int j = 0; j < 16; j++) y[j] ^= block[j];
        gf128_mul(y, h, y);
    }

    /* Final block: len(AAD) and len(C) in bits, each as 64-bit big-endian */
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t c_bits = (uint64_t)c_len * 8;
    for (int j = 0; j < 16; j++) block[j] = 0;
    for (int j = 0; j < 8; j++) block[7 - j] = (uint8_t)(aad_bits >> (8 * j));
    for (int j = 0; j < 8; j++) block[15 - j] = (uint8_t)(c_bits >> (8 * j));
    for (int j = 0; j < 16; j++) y[j] ^= block[j];
    gf128_mul(y, h, y);

    for (int j = 0; j < 16; j++) out[j] = y[j];
}

static void inc32(uint8_t block[16]) {
    /* Increment only the last 32 bits, per NIST SP 800-38D */
    for (int i = 15; i >= 12; i--) {
        if (++block[i] != 0) break;
    }
}

static void gctr(const uint8_t rk[AES256_ROUNDKEY_LEN], const uint8_t icb[16],
                  const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t cb[16], keystream[16];
    for (int i = 0; i < 16; i++) cb[i] = icb[i];

    size_t i;
    for (i = 0; i + 16 <= len; i += 16) {
        for (int j = 0; j < 16; j++) keystream[j] = cb[j];
        aes256_encrypt_block(rk, keystream);
        for (int j = 0; j < 16; j++) out[i + j] = (uint8_t)(in[i + j] ^ keystream[j]);
        inc32(cb);
    }
    if (i < len) {
        for (int j = 0; j < 16; j++) keystream[j] = cb[j];
        aes256_encrypt_block(rk, keystream);
        for (size_t j = 0; j < len - i; j++) out[i + j] = (uint8_t)(in[i + j] ^ keystream[j]);
    }
}

static void compute_j0_and_h(const uint8_t rk[AES256_ROUNDKEY_LEN],
                              const uint8_t iv[GCM_IV_LEN],
                              uint8_t j0[16], uint8_t h[16]) {
    for (int i = 0; i < 16; i++) h[i] = 0;
    aes256_encrypt_block(rk, h); /* H = E(K, 0^128) */

    /* Standard 96-bit IV case: J0 = IV || 0x00000001 */
    for (int i = 0; i < GCM_IV_LEN; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
}

void aes256_gcm_encrypt(const uint8_t key[AES256_KEY_LEN],
                         const uint8_t iv[GCM_IV_LEN],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *plaintext, size_t len,
                         uint8_t *out,
                         uint8_t tag[GCM_TAG_LEN]) {
    uint8_t rk[AES256_ROUNDKEY_LEN];
    uint8_t j0[16], h[16], cb1[16], s[16], e_j0[16];

    key_expansion(key, rk);
    compute_j0_and_h(rk, iv, j0, h);

    for (int i = 0; i < 16; i++) cb1[i] = j0[i];
    inc32(cb1); /* CB_1 = inc32(J0) */
    gctr(rk, cb1, plaintext, len, out);

    ghash(h, aad, aad_len, out, len, s);

    for (int i = 0; i < 16; i++) e_j0[i] = j0[i];
    aes256_encrypt_block(rk, e_j0);
    for (int i = 0; i < GCM_TAG_LEN; i++) tag[i] = (uint8_t)(e_j0[i] ^ s[i]);
}

int aes256_gcm_decrypt(const uint8_t key[AES256_KEY_LEN],
                        const uint8_t iv[GCM_IV_LEN],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ciphertext, size_t len,
                        const uint8_t tag[GCM_TAG_LEN],
                        uint8_t *out) {
    uint8_t rk[AES256_ROUNDKEY_LEN];
    uint8_t j0[16], h[16], cb1[16], s[16], e_j0[16], expect_tag[GCM_TAG_LEN];

    key_expansion(key, rk);
    compute_j0_and_h(rk, iv, j0, h);

    ghash(h, aad, aad_len, ciphertext, len, s);
    for (int i = 0; i < 16; i++) e_j0[i] = j0[i];
    aes256_encrypt_block(rk, e_j0);
    for (int i = 0; i < GCM_TAG_LEN; i++) expect_tag[i] = (uint8_t)(e_j0[i] ^ s[i]);

    uint8_t diff = 0;
    for (int i = 0; i < GCM_TAG_LEN; i++) diff |= (uint8_t)(expect_tag[i] ^ tag[i]);
    if (diff != 0) return 0; /* authentication failure -- do not decrypt */

    for (int i = 0; i < 16; i++) cb1[i] = j0[i];
    inc32(cb1);
    gctr(rk, cb1, ciphertext, len, out);
    return 1;
}
