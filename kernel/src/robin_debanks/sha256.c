/* sha256.c — Freestanding SHA-256 implementation (FIPS 180-4)
 * Validated against the standard test vectors:
 *   SHA256("")    = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85
 *   SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "sha256.h"

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotr(uint32_t x, int n) {
    return (uint32_t)((x >> n) | (x << (32 - n)));
}

/* Process a single 64-byte message block.  `block` must be 64 bytes. */
static void sha256_block(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = (uint32_t)(w[i - 16] + s0 + w[i - 7] + s1);
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = (uint32_t)(hh + S1 + ch + k[i] + w[i]);
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = (uint32_t)(S0 + maj);

        hh = g; g = f; f = e; e = (uint32_t)(d + temp1);
        d = c; c = b; b = a; a = (uint32_t)(temp1 + temp2);
    }

    h[0] = (uint32_t)(h[0] + a); h[1] = (uint32_t)(h[1] + b);
    h[2] = (uint32_t)(h[2] + c); h[3] = (uint32_t)(h[3] + d);
    h[4] = (uint32_t)(h[4] + e); h[5] = (uint32_t)(h[5] + f);
    h[6] = (uint32_t)(h[6] + g); h[7] = (uint32_t)(h[7] + hh);
}

void sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    /* Process all full 64-byte blocks directly from the input. */
    size_t full_blocks = len / 64;
    for (size_t b = 0; b < full_blocks; b++)
        sha256_block(h, data + b * 64);

    /* Final block(s): append 0x80, pad with zeros, and append the
     * 64-bit big-endian bit length.  This is bounded to two 64-byte
     * blocks regardless of input size, so no stack blow-up. */
    uint8_t final[128];
    size_t rem = len % 64;
    for (size_t i = 0; i < rem; i++) final[i] = data[full_blocks * 64 + i];
    final[rem] = 0x80;
    for (size_t i = rem + 1; i < 128; i++) final[i] = 0;

    uint64_t bit_len = (uint64_t)len * 8;
    if (rem < 56) {
        for (int i = 0; i < 8; i++)
            final[56 + i] = (uint8_t)(bit_len >> (56 - 8 * i));
        sha256_block(h, final);
    } else {
        for (int i = 0; i < 8; i++)
            final[120 + i] = (uint8_t)(bit_len >> (56 - 8 * i));
        sha256_block(h, final);
        sha256_block(h, final + 64);
    }

    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

/* ===================== streaming API ===================== */

void sha256_init(sha256_ctx_t *c) {
    if (!c) return;
    c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85;
    c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
    c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c;
    c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
    c->buf_len = 0;
    c->total = 0;
}

void sha256_update(sha256_ctx_t *c, const uint8_t *data, size_t len) {
    if (!c || !data) return;
    c->total += len;

    /* top up a partial block first */
    if (c->buf_len) {
        size_t take = 64 - c->buf_len;
        if (take > len) take = len;
        for (size_t i = 0; i < take; i++) c->buf[c->buf_len + i] = data[i];
        c->buf_len += take;
        data += take; len -= take;
        if (c->buf_len == 64) { sha256_block(c->h, c->buf); c->buf_len = 0; }
    }
    /* then whole blocks straight from the caller's buffer */
    while (len >= 64) { sha256_block(c->h, data); data += 64; len -= 64; }
    /* and keep the remainder */
    for (size_t i = 0; i < len; i++) c->buf[c->buf_len++] = data[i];
}

void sha256_final(sha256_ctx_t *c, uint8_t out[SHA256_DIGEST_LEN]) {
    if (!c || !out) return;
    uint64_t bits = c->total * 8u;

    uint8_t pad[128];
    size_t rem = c->buf_len;
    for (size_t i = 0; i < rem; i++) pad[i] = c->buf[i];
    pad[rem] = 0x80;
    size_t total_len = (rem < 56) ? 64 : 128;
    for (size_t i = rem + 1; i < total_len - 8; i++) pad[i] = 0;
    for (int i = 0; i < 8; i++)
        pad[total_len - 1 - (size_t)i] = (uint8_t)(bits >> (8 * i));

    for (size_t off = 0; off < total_len; off += 64) sha256_block(c->h, pad + off);

    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

/* ---- DECLARATION -----------------------------------------------------------
 * L0 substrate: a pure function over bytes, with nothing beneath it. It cannot
 * require anything, and several modules (alloc's content addressing, the CID
 * layer) require it, so it is the cleanest possible root of the requires-graph.
 *
 * The bring-up is the FIPS 180-4 "abc" known-answer test. A hash that compiles
 * and returns the wrong digest is worse than one that is absent, because every
 * content address derived from it is then confidently wrong. */
#include "zxv_decl.h"

static int sha256_bringup(void) {
    static const uint8_t msg[3]  = { 'a', 'b', 'c' };
    static const uint8_t want[8] = { 0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea };
    uint8_t d[SHA256_DIGEST_LEN];
    sha256(msg, sizeof msg, d);
    for (uint32_t i = 0; i < sizeof want; i++) if (d[i] != want[i]) return -1;
    return 0;
}

ZXV_DECLARE(sha256,
    ZXV_PROVIDES(sha256_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(sha256_bringup));
