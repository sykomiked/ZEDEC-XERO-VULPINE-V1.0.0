/* keccak.c — Freestanding Keccak-f[1600] / SHA-3 / SHAKE (FIPS 202)
 * See keccak.h for design notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "keccak.h"

static const uint64_t rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

/* Rotation offsets, indexed [x][y] per the standard Keccak specification */
static const int rho[5][5] = {
    { 0,  1, 62, 28, 27},
    {36, 44,  6, 55, 20},
    { 3, 10, 43, 25, 39},
    {41, 45, 15, 21,  8},
    {18,  2, 61, 56, 14}
};

static uint64_t rotl64(uint64_t x, int n) {
    return (uint64_t)((x << n) | (x >> (64 - n)));
}

/* state[x + 5*y] holds lane (x,y), the standard flattening convention. */
static void keccak_f1600(uint64_t state[25]) {
    for (int round = 0; round < 24; round++) {
        uint64_t c[5], d[5], b[25];

        /* Theta */
        for (int x = 0; x < 5; x++) {
            c[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (int x = 0; x < 5; x++) {
            d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        }
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                state[x + 5 * y] ^= d[x];
            }
        }

        /* Rho + Pi: B[y][(2x+3y) mod 5] = rotl(A[x][y], rho[x][y]) */
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                int nx = y;
                int ny = (2 * x + 3 * y) % 5;
                b[nx + 5 * ny] = rotl64(state[x + 5 * y], rho[y][x]);
            }
        }

        /* Chi */
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                state[x + 5 * y] = b[x + 5 * y] ^
                    ((~b[(x + 1) % 5 + 5 * y]) & b[(x + 2) % 5 + 5 * y]);
            }
        }

        /* Iota */
        state[0] ^= rc[round];
    }
}

/* Generic sponge: absorb `len` bytes with rate `rate` (bytes), pad with
 * FIPS 202 domain suffix `suffix` (0x06 for SHA3, 0x1F for SHAKE),
 * then squeeze `out_len` bytes. */
static void keccak_sponge(const uint8_t *data, size_t len, size_t rate,
                           uint8_t suffix, uint8_t *out, size_t out_len) {
    uint64_t state[25];
    for (int i = 0; i < 25; i++) state[i] = 0;

    uint8_t block[200];

    /* Absorb full-rate blocks */
    size_t off = 0;
    while (len - off >= rate) {
        for (size_t i = 0; i < rate; i++) {
            ((uint8_t *)state)[i] ^= data[off + i];
        }
        keccak_f1600(state);
        off += rate;
    }

    /* Final (possibly partial) block with padding */
    size_t rem = len - off;
    for (size_t i = 0; i < rate; i++) block[i] = 0;
    for (size_t i = 0; i < rem; i++) block[i] = data[off + i];
    block[rem] ^= suffix;
    block[rate - 1] ^= 0x80;
    for (size_t i = 0; i < rate; i++) {
        ((uint8_t *)state)[i] ^= block[i];
    }
    keccak_f1600(state);

    /* Squeeze */
    size_t produced = 0;
    while (produced < out_len) {
        size_t chunk = (out_len - produced < rate) ? (out_len - produced) : rate;
        for (size_t i = 0; i < chunk; i++) out[produced + i] = ((uint8_t *)state)[i];
        produced += chunk;
        if (produced < out_len) keccak_f1600(state);
    }
}

void sha3_256(const uint8_t *data, size_t len, uint8_t out[SHA3_256_DIGEST_LEN]) {
    keccak_sponge(data, len, 136, 0x06, out, SHA3_256_DIGEST_LEN);
}

void sha3_512(const uint8_t *data, size_t len, uint8_t out[SHA3_512_DIGEST_LEN]) {
    keccak_sponge(data, len, 72, 0x06, out, SHA3_512_DIGEST_LEN);
}

void shake128(const uint8_t *data, size_t len, uint8_t *out, size_t out_len) {
    keccak_sponge(data, len, 168, 0x1f, out, out_len);
}

void shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len) {
    keccak_sponge(data, len, 136, 0x1f, out, out_len);
}

/* ===== Incremental SHAKE128 (absorb-then-squeeze, single transition) =====
 * Sufficient for ML-KEM's usage pattern: absorb a short fixed prefix
 * once, then squeeze. Does not support interleaved absorb calls after
 * squeezing has begun (ML-KEM never needs that). */

void shake128_init(shake128_ctx_t *ctx) {
    for (int i = 0; i < 25; i++) ctx->state[i] = 0;
    ctx->buf_len = 0;
    ctx->squeezing = 0;
}

void shake128_absorb(shake128_ctx_t *ctx, const uint8_t *data, size_t len) {
    const size_t rate = 168;
    size_t off = 0;
    /* Fill any partial buffered block first */
    while (ctx->buf_len > 0 && off < len) {
        ctx->buf[ctx->buf_len++] = data[off++];
        if (ctx->buf_len == rate) {
            for (size_t i = 0; i < rate; i++) ((uint8_t *)ctx->state)[i] ^= ctx->buf[i];
            keccak_f1600(ctx->state);
            ctx->buf_len = 0;
        }
    }
    /* Absorb full blocks directly */
    while (len - off >= rate) {
        for (size_t i = 0; i < rate; i++) ((uint8_t *)ctx->state)[i] ^= data[off + i];
        keccak_f1600(ctx->state);
        off += rate;
    }
    /* Buffer the remainder */
    while (off < len) {
        ctx->buf[ctx->buf_len++] = data[off++];
    }
}

void shake128_squeeze(shake128_ctx_t *ctx, uint8_t *out, size_t out_len) {
    const size_t rate = 168;
    if (!ctx->squeezing) {
        /* Finalize absorption: pad the buffered partial block */
        uint8_t block[168];
        for (size_t i = 0; i < rate; i++) block[i] = 0;
        for (size_t i = 0; i < ctx->buf_len; i++) block[i] = ctx->buf[i];
        block[ctx->buf_len] ^= 0x1f;
        block[rate - 1] ^= 0x80;
        for (size_t i = 0; i < rate; i++) ((uint8_t *)ctx->state)[i] ^= block[i];
        keccak_f1600(ctx->state);
        ctx->buf_len = 0;
        ctx->squeezing = 1;
    }

    size_t produced = 0;
    while (produced < out_len) {
        size_t avail = rate - ctx->buf_len;
        size_t chunk = (out_len - produced < avail) ? (out_len - produced) : avail;
        for (size_t i = 0; i < chunk; i++) {
            out[produced + i] = ((uint8_t *)ctx->state)[ctx->buf_len + i];
        }
        ctx->buf_len += chunk;
        produced += chunk;
        if (ctx->buf_len == rate) {
            keccak_f1600(ctx->state);
            ctx->buf_len = 0;
        }
    }
}
