/* sha512.c — SHA-512 and SHA-384 (FIPS 180-4). See sha512.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "sha512.h"

static const uint64_t K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};

static uint64_t ror(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64 - n));
}

static void compress(uint64_t h[8], const uint8_t *p)
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | p[8 * i + j];
        w[i] = v;
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ror(w[i - 15], 1) ^ ror(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ror(w[i - 2], 19) ^ ror(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = ror(e, 14) ^ ror(e, 18) ^ ror(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = k + S1 + ch + K[i] + w[i];
        uint64_t S0 = ror(a, 28) ^ ror(a, 34) ^ ror(a, 39);
        uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + mj;
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

static void init_with(tls_sha512_ctx_t *c, const uint64_t iv[8], uint32_t out_len)
{
    for (int i = 0; i < 8; i++) c->h[i] = iv[i];
    c->buf_len = 0;
    c->total = 0;
    c->out_len = out_len;
}

void tls_sha512_init(tls_sha512_ctx_t *c)
{
    static const uint64_t iv[8] = {
        0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
        0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    init_with(c, iv, SHA512_DIGEST_LEN);
}

void tls_sha384_init(tls_sha512_ctx_t *c)
{
    static const uint64_t iv[8] = {
        0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
        0x67332667ffc00b31ull, 0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull};
    init_with(c, iv, SHA384_DIGEST_LEN);
}

void tls_sha512_update(tls_sha512_ctx_t *c, const uint8_t *data, uint32_t len)
{
    c->total += len;
    while (len) {
        uint32_t take = 128 - c->buf_len;
        if (take > len) take = len;
        for (uint32_t i = 0; i < take; i++) c->buf[c->buf_len + i] = data[i];
        c->buf_len += take;
        data += take;
        len -= take;
        if (c->buf_len == 128) {
            compress(c->h, c->buf);
            c->buf_len = 0;
        }
    }
}

void tls_sha512_final(tls_sha512_ctx_t *c, uint8_t *out)
{
    uint64_t bits = c->total << 3;
    uint64_t bits_hi = c->total >> 61;
    c->buf[c->buf_len++] = 0x80;
    if (c->buf_len > 112) {
        while (c->buf_len < 128) c->buf[c->buf_len++] = 0;
        compress(c->h, c->buf);
        c->buf_len = 0;
    }
    while (c->buf_len < 112) c->buf[c->buf_len++] = 0;
    for (int i = 0; i < 8; i++) c->buf[112 + i] = (uint8_t) (bits_hi >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) c->buf[120 + i] = (uint8_t) (bits >> (56 - 8 * i));
    compress(c->h, c->buf);
    for (uint32_t i = 0; i < c->out_len; i++)
        out[i] = (uint8_t) (c->h[i / 8] >> (56 - 8 * (i % 8)));
    for (int i = 0; i < 128; i++) c->buf[i] = 0;
}

void tls_sha512(const uint8_t *data, uint32_t len, uint8_t out[SHA512_DIGEST_LEN])
{
    tls_sha512_ctx_t c;
    tls_sha512_init(&c);
    tls_sha512_update(&c, data, len);
    tls_sha512_final(&c, out);
}

void tls_sha384(const uint8_t *data, uint32_t len, uint8_t out[SHA384_DIGEST_LEN])
{
    tls_sha512_ctx_t c;
    tls_sha384_init(&c);
    tls_sha512_update(&c, data, len);
    tls_sha512_final(&c, out);
}
