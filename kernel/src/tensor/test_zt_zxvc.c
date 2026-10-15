/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_zxvc.c - the .zxvc exact weight blocks and package (zt_zxvc.h).
 *
 * Checked here:
 *   every finite F16 and BF16 bit pattern, alone and packed 32 to a block
 *   beside other values, decodes back to the same bits; F32 the same on
 *   edge cases and 4 million pseudo-random patterns; the integer value
 *   m * 2^e equals the C float value (an independent oracle); Q16 decoding
 *   equals zt_f16_to_q16 / zt_bf16_to_q16 / zt_f32_to_q16 for the same
 *   values; negative zeros survive; NaN, infinity and a spread over 63 bits
 *   are refused, never rounded; corrupt blocks are refused; a package built
 *   in memory opens, finds its parts and tensors, re-hashes clean, decodes
 *   back to its source bytes, and refuses a flipped payload byte, a flipped
 *   table byte (digest changes), truncation and a wrong size.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_zxvc.h"
#include "zt_gguf.h"
#include "../robin_debanks/sha256.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            if (failures < 40) {                                                                   \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static bool finite16(uint16_t b)
{
    return ((b >> 10) & 0x1F) != 0x1F;
}
static bool finite_bf16(uint16_t b)
{
    return ((b >> 7) & 0xFF) != 0xFF;
}
static bool finite32(uint32_t b)
{
    return ((b >> 23) & 0xFF) != 0xFF;
}

static double f16_val(uint16_t b)
{
    int ex = (b >> 10) & 0x1F, m = b & 0x3FF;
    double v = ex ? ldexp(1024 + m, ex - 25) : ldexp(m, -24);
    return (b >> 15) ? -v : v;
}
static double f32_val(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return (double) f;
}

/* Value of block integer i, read independently of zt_zxvc.c. */
static double blk_val(const uint8_t *blk, uint32_t i)
{
    uint8_t mode = blk[0];
    int16_t e = (int16_t) (blk[1] | (blk[2] << 8));
    uint32_t hdr = (mode & 16) ? 7 : 3;
    if (mode & 8) return 0.0;
    static const uint32_t wbs[5] = {1, 2, 3, 4, 8};
    uint32_t wb = wbs[mode & 7];
    int64_t v = 0;
    const uint8_t *p = blk + hdr + i * wb;
    switch (wb) {
    case 1:
        v = (int8_t) p[0];
        break;
    case 2:
        v = (int16_t) (p[0] | (p[1] << 8));
        break;
    case 3:
        v = (int32_t) ((uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16)) << 8) >> 8;
        break;
    case 4: {
        uint32_t u = 0;
        memcpy(&u, p, 4);
        v = (int32_t) u;
        break;
    }
    default:
        memcpy(&v, p, 8);
    }
    return ldexp((double) v, e);
}

static void test_f16_all(void)
{
    uint8_t src[2 * ZXVC_BLK], back[2 * ZXVC_BLK], blk[ZXVC_BLK_MAX_BYTES];
    zt_fx q[ZXVC_BLK];
    uint32_t bad_bits = 0, bad_val = 0, bad_q = 0, n_fin = 0, refused = 0;
    for (uint32_t b = 0; b < 65536u; b++) {
        uint16_t h = (uint16_t) b;
        src[0] = (uint8_t) h;
        src[1] = (uint8_t) (h >> 8);
        int32_t n = zxvc_block_encode(ZT_GGML_F16, src, 1, blk);
        if (!finite16(h)) {
            refused += n == ZXVC_ENOTEXACT;
            continue;
        }
        n_fin++;
        int32_t d = zxvc_block_decode(ZT_GGML_F16, blk, (uint64_t) n, 1, back);
        if (n <= 0 || d != n || memcmp(src, back, 2)) bad_bits++;
        if (n > 0 && blk_val(blk, 0) != f16_val(h)) bad_val++;
        if (zxvc_block_q16(blk, (uint64_t) n, 1, q) != n || q[0] != zt_f16_to_q16(h)) bad_q++;
    }
    CHECK(n_fin == 63488u && bad_bits == 0, "f16 single: %u of %u finite patterns differ", bad_bits,
          n_fin);
    CHECK(bad_val == 0, "f16 single: %u values differ from the float oracle", bad_val);
    CHECK(bad_q == 0, "f16 single: %u Q16 values differ from zt_f16_to_q16", bad_q);
    CHECK(refused == 2048u, "f16: %u of 2048 NaN/inf patterns refused", refused);

    /* Packed: each block holds 32 consecutive finite patterns, plus a
     * shuffled pass so wide and narrow values share blocks. Every F16
     * spread fits in 63 bits, so nothing may be refused. */
    bad_bits = bad_q = 0;
    uint32_t blocks = 0, widths[5] = {0, 0, 0, 0, 0};
    for (int pass = 0; pass < 2; pass++) {
        uint16_t pat[ZXVC_BLK];
        uint32_t k = 0, b = 0;
        while (b < 65536u) {
            uint16_t h = pass ? (uint16_t) next() : (uint16_t) b;
            b++;
            if (!finite16(h)) continue;
            pat[k++] = h;
            if (k < ZXVC_BLK && b < 65536u) continue;
            for (uint32_t i = 0; i < k; i++) {
                src[2 * i] = (uint8_t) pat[i];
                src[2 * i + 1] = (uint8_t) (pat[i] >> 8);
            }
            int32_t n = zxvc_block_encode(ZT_GGML_F16, src, k, blk);
            int32_t d = n > 0 ? zxvc_block_decode(ZT_GGML_F16, blk, (uint64_t) n, k, back) : n;
            if (n <= 0 || d != n || memcmp(src, back, 2 * k)) bad_bits++;
            if (n > 0) {
                widths[blk[0] & 7]++;
                zxvc_block_q16(blk, (uint64_t) n, k, q);
                for (uint32_t i = 0; i < k; i++)
                    if (q[i] != zt_f16_to_q16(pat[i])) bad_q++;
            }
            blocks++;
            k = 0;
        }
    }
    CHECK(bad_bits == 0, "f16 packed: %u of %u blocks differ", bad_bits, blocks);
    CHECK(bad_q == 0, "f16 packed: %u Q16 values differ", bad_q);
    CHECK(widths[4] > 0 && widths[1] > 0, "f16 packed: widths 1B %u 2B %u 3B %u 4B %u 8B %u",
          widths[0], widths[1], widths[2], widths[3], widths[4]);
}

static void test_bf16_all(void)
{
    uint8_t src[2], back[2], blk[ZXVC_BLK_MAX_BYTES];
    zt_fx q;
    uint32_t bad_bits = 0, bad_val = 0, bad_q = 0, n_fin = 0;
    for (uint32_t b = 0; b < 65536u; b++) {
        uint16_t h = (uint16_t) b;
        if (!finite_bf16(h)) continue;
        n_fin++;
        src[0] = (uint8_t) h;
        src[1] = (uint8_t) (h >> 8);
        int32_t n = zxvc_block_encode(ZT_GGML_BF16, src, 1, blk);
        int32_t d = n > 0 ? zxvc_block_decode(ZT_GGML_BF16, blk, (uint64_t) n, 1, back) : n;
        if (n <= 0 || d != n || memcmp(src, back, 2)) bad_bits++;
        if (n > 0 && blk_val(blk, 0) != f32_val((uint32_t) h << 16)) bad_val++;
        if (n > 0 && (zxvc_block_q16(blk, (uint64_t) n, 1, &q) != n || q != zt_bf16_to_q16(h)))
            bad_q++;
    }
    CHECK(n_fin == 65280u && bad_bits == 0, "bf16 single: %u of %u differ", bad_bits, n_fin);
    CHECK(bad_val == 0, "bf16 single: %u values differ from the float oracle", bad_val);
    CHECK(bad_q == 0, "bf16 single: %u Q16 values differ from zt_bf16_to_q16", bad_q);
}

/* Gaussian-ish weights: a real model's spread, in BF16. */
static void test_bf16_weights(void)
{
    uint8_t src[2 * ZXVC_BLK], back[2 * ZXVC_BLK], blk[ZXVC_BLK_MAX_BYTES];
    uint32_t bad = 0, refused = 0, total = 0, w16 = 0;
    for (uint32_t r = 0; r < 20000; r++) {
        for (uint32_t i = 0; i < ZXVC_BLK; i++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += (double) (next() >> 11) / 9007199254740992.0 - 0.5;
            float f = (float) (s * 0.05);
            uint32_t u;
            memcpy(&u, &f, 4);
            uint16_t h = (uint16_t) (u >> 16);
            src[2 * i] = (uint8_t) h;
            src[2 * i + 1] = (uint8_t) (h >> 8);
        }
        int32_t n = zxvc_block_encode(ZT_GGML_BF16, src, ZXVC_BLK, blk);
        total++;
        if (n == ZXVC_ENOTEXACT) {
            refused++;
            continue;
        }
        w16 += (blk[0] & 7) <= 1;
        int32_t d = zxvc_block_decode(ZT_GGML_BF16, blk, (uint64_t) n, ZXVC_BLK, back);
        if (n <= 0 || d != n || memcmp(src, back, sizeof src)) bad++;
    }
    CHECK(bad == 0 && refused == 0, "bf16 weights: %u differ, %u refused of %u", bad, refused,
          total);
    printf("  bf16 weight-like blocks: %u of %u fit 8/16-bit integers\n", w16, total);
}

static void test_f32(void)
{
    uint8_t src[4 * ZXVC_BLK], back[4 * ZXVC_BLK], blk[ZXVC_BLK_MAX_BYTES];
    zt_fx q[ZXVC_BLK];
    static const uint32_t edge[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x00800000u, 0x3F800000u,
        0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F800001u, 0x47000000u, 0x46FFFE00u, 0x477FFF80u,
        0xC7000000u, 0x37800000u, 0x37000000u, 0x36FFFFFFu, 0x33800000u, 0x33000000u};
    uint32_t bad = 0, bad_val = 0, bad_q = 0, n = 0;
    for (uint32_t i = 0; i < sizeof edge / 4 + 4000000u; i++) {
        uint32_t u = i < sizeof edge / 4 ? edge[i] : (uint32_t) next();
        if (!finite32(u)) continue;
        n++;
        memcpy(src, &u, 4);
        int32_t k = zxvc_block_encode(ZT_GGML_F32, src, 1, blk);
        int32_t d = k > 0 ? zxvc_block_decode(ZT_GGML_F32, blk, (uint64_t) k, 1, back) : k;
        if (k <= 0 || d != k || memcmp(src, back, 4)) bad++;
        if (k > 0 && blk_val(blk, 0) != f32_val(u)) bad_val++;
        if (k > 0 && (zxvc_block_q16(blk, (uint64_t) k, 1, q) != k || q[0] != zt_f32_to_q16(u)))
            bad_q++;
    }
    CHECK(bad == 0, "f32 single: %u of %u differ", bad, n);
    CHECK(bad_val == 0, "f32 single: %u values differ from the float oracle", bad_val);
    CHECK(bad_q == 0, "f32 single: %u Q16 values differ from zt_f32_to_q16", bad_q);

    /* Blocks of nearby F32 values (spread small) all round-trip. */
    bad = 0;
    for (uint32_t r = 0; r < 50000; r++) {
        uint32_t base = (uint32_t) (next() % 0x7E000000u) & 0x7F800000u;
        for (uint32_t i = 0; i < ZXVC_BLK; i++) {
            uint32_t u = base | (uint32_t) (next() & 0x807FFFFFu);
            if (i % 7 == 3) u -= 0x01000000u * (uint32_t) (next() % 4) * (base > 0x05000000u);
            memcpy(src + 4 * i, &u, 4);
        }
        int32_t k = zxvc_block_encode(ZT_GGML_F32, src, ZXVC_BLK, blk);
        if (k == ZXVC_ENOTEXACT) continue; /* spread over 63 bits: refused is allowed */
        int32_t d = zxvc_block_decode(ZT_GGML_F32, blk, (uint64_t) k, ZXVC_BLK, back);
        if (k <= 0 || d != k || memcmp(src, back, sizeof src)) bad++;
    }
    CHECK(bad == 0, "f32 packed: %u blocks differ", bad);
}

static void test_refusals(void)
{
    uint8_t src[4 * ZXVC_BLK], back[4 * ZXVC_BLK], blk[ZXVC_BLK_MAX_BYTES];
    /* Spread over 63 bits: 1.0 beside the smallest F32 subnormal. */
    uint32_t a = 0x3F800000u, b = 0x00000001u;
    memcpy(src, &a, 4);
    memcpy(src + 4, &b, 4);
    CHECK(zxvc_block_encode(ZT_GGML_F32, src, 2, blk) == ZXVC_ENOTEXACT,
          "1.0 beside 2^-149 must be refused");
    /* Exactly 63 bits of spread still fits: 2^-149 * (2^62 + 1)? use 2^-149 and 2^-87 (62
     * apart, the 1.0 at 2^-87 needs 63 bits). */
    a = (uint32_t) (-87 + 127) << 23;
    memcpy(src, &a, 4);
    int32_t k = zxvc_block_encode(ZT_GGML_F32, src, 2, blk);
    CHECK(k > 0 && (blk[0] & 7) == 4, "62-bit spread fits a 64-bit block (rc %d)", k);
    CHECK(k > 0 && zxvc_block_decode(ZT_GGML_F32, blk, (uint64_t) k, 2, back) == k &&
              !memcmp(src, back, 8),
          "62-bit spread round-trips");
    a = (uint32_t) (-86 + 127) << 23;
    memcpy(src, &a, 4);
    CHECK(zxvc_block_encode(ZT_GGML_F32, src, 2, blk) == ZXVC_ENOTEXACT,
          "63-bit spread needs 64 magnitude bits: refused");
    /* Each integer width: 1 + 2^-22 needs 24 bits with the sign (3 bytes),
     * 1 + 2^-23 needs 25 (4 bytes), 1.0 one bit (1 byte). */
    static const uint32_t wv[3] = {0x3F800002u, 0x3F800001u, 0x3F800000u};
    static const uint8_t wcode[3] = {2, 3, 0};
    for (int i = 0; i < 3; i++) {
        memcpy(src, &wv[i], 4);
        k = zxvc_block_encode(ZT_GGML_F32, src, 1, blk);
        CHECK(k > 0 && (blk[0] & 7) == wcode[i] &&
                  zxvc_block_decode(ZT_GGML_F32, blk, (uint64_t) k, 1, back) == k &&
                  !memcmp(src, back, 4),
              "width code %u for 0x%08x", wcode[i], wv[i]);
    }
    /* NaN and infinity in a block. */
    uint16_t h[3] = {0x3C00, 0x7C00, 0x3C00};
    memcpy(src, h, 6);
    CHECK(zxvc_block_encode(ZT_GGML_F16, src, 3, blk) == ZXVC_ENOTEXACT, "inf refused");
    h[1] = 0x7E01;
    memcpy(src, h, 6);
    CHECK(zxvc_block_encode(ZT_GGML_F16, src, 3, blk) == ZXVC_ENOTEXACT, "NaN refused");
    CHECK(zxvc_block_encode(ZT_GGML_Q8_0, src, 3, blk) == ZXVC_ETYPE, "Q8_0 is not exact-encoded");
    CHECK(zxvc_block_encode(ZT_GGML_F16, src, 0, blk) == ZXVC_EARG, "n = 0 refused");
    CHECK(zxvc_block_encode(ZT_GGML_F16, src, 33, blk) == ZXVC_EARG, "n = 33 refused");

    /* Negative zeros and an all-zero block. */
    uint16_t z[4] = {0x8000, 0x0000, 0x8000, 0x0000};
    memcpy(src, z, 8);
    k = zxvc_block_encode(ZT_GGML_F16, src, 4, blk);
    CHECK(k == 7 && (blk[0] & 8) && (blk[0] & 16), "all-zero block with -0 mask is 7 bytes (%d)",
          k);
    CHECK(zxvc_block_decode(ZT_GGML_F16, blk, (uint64_t) k, 4, back) == k && !memcmp(src, back, 8),
          "-0 kept bit for bit");
    z[1] = 0x3C00;
    memcpy(src, z, 8);
    k = zxvc_block_encode(ZT_GGML_F16, src, 4, blk);
    CHECK(k > 0 && zxvc_block_decode(ZT_GGML_F16, blk, (uint64_t) k, 4, back) == k &&
              !memcmp(src, back, 8),
          "-0 beside values kept bit for bit");

    /* Corrupt blocks. */
    uint16_t v[2] = {0x3C00, 0x4000}; /* 1.0, 2.0 */
    memcpy(src, v, 4);
    k = zxvc_block_encode(ZT_GGML_F16, src, 2, blk);
    CHECK(zxvc_block_decode(ZT_GGML_F16, blk, (uint64_t) k - 1, 2, back) == ZXVC_ETRUNC,
          "short block refused");
    uint8_t c[ZXVC_BLK_MAX_BYTES];
    memcpy(c, blk, (size_t) k);
    c[0] |= 0x40;
    CHECK(zxvc_block_decode(ZT_GGML_F16, c, (uint64_t) k, 2, back) == ZXVC_ECORRUPT,
          "unknown mode bit refused");
    memcpy(c, blk, (size_t) k);
    c[0] = (uint8_t) ((c[0] & ~7u) | 5u);
    CHECK(zxvc_block_decode(ZT_GGML_F16, c, 300, 2, back) == ZXVC_ECORRUPT, "width code 5 refused");
    memcpy(c, blk, (size_t) k);
    c[1] = 0x40; /* exponent 64: 2^64 is past F16's range */
    c[2] = 0x00;
    CHECK(zxvc_block_decode(ZT_GGML_F16, c, (uint64_t) k, 2, back) == ZXVC_ECORRUPT,
          "value past the type's range refused");
    memcpy(c, blk, (size_t) k);
    c[1] = 0xC0; /* exponent -64: below F16's smallest subnormal */
    c[2] = 0xFF;
    CHECK(zxvc_block_decode(ZT_GGML_F16, c, (uint64_t) k, 2, back) == ZXVC_ECORRUPT,
          "value below the type's precision refused");
    /* Integer with more significant bits than F16 holds: 2049 * 2^0. */
    uint8_t wide[3 + 4] = {1, 0, 0, 0x01, 0x08, 0x01, 0x00};
    CHECK(zxvc_block_decode(ZT_GGML_F16, wide, sizeof wide, 2, back) == ZXVC_ECORRUPT,
          "12 significant bits do not fit F16: refused");
    uint8_t nz[7 + 1] = {16, 0, 0, 0x02, 0, 0, 0, 0}; /* mask bit 1 set but n = 1 */
    CHECK(zxvc_block_decode(ZT_GGML_F16, nz, sizeof nz, 1, back) == ZXVC_ECORRUPT,
          "negative-zero mask past n refused");
}

/* ---- an in-memory package ---- */

static uint8_t pkg[1 << 16];
static uint64_t pn;

static uint64_t align32(uint64_t v)
{
    return (v + 31u) & ~31ull;
}

static uint64_t build(void)
{
    static const char card[] = "# test model\n";
    static const char lic[] = "apache-2.0\n";
    uint16_t w16[100];
    for (int i = 0; i < 100; i++) w16[i] = (uint16_t) (0x3000 + i * 37);
    w16[5] = 0x8000;
    uint8_t q8[34 * 2];
    for (int i = 0; i < (int) sizeof q8; i++) q8[i] = (uint8_t) (i * 11);

    zxvc_t z = {0};
    z.n_parts = 2;
    z.n_tensors = 2;
    z.flags = ZXVC_FLAG_FROM_SAFETENSORS;
    z.parts_off = ZXVC_HEADER_BYTES;
    z.tensors_off = z.parts_off + 2 * ZXVC_PART_BYTES;
    z.data_off = align32(z.tensors_off + 2 * ZXVC_TENSOR_BYTES);
    memset(pkg, 0, sizeof pkg);
    uint64_t pos = z.data_off;

    zxvc_part_t p[2] = {{.name = "card"}, {.name = "licence"}};
    const char *pd[2] = {card, lic};
    for (int i = 0; i < 2; i++) {
        p[i].off = pos;
        p[i].len = strlen(pd[i]);
        memcpy(pkg + pos, pd[i], p[i].len);
        sha256(pkg + pos, p[i].len, p[i].sha);
        pos = align32(pos + p[i].len);
        zxvc_put_part(pkg + z.parts_off + (uint64_t) i * ZXVC_PART_BYTES, &p[i]);
    }

    zxvc_tensor_t t = {.name = "blk.0.w", .enc = ZXVC_ENC_EXACT, .src_type = ZT_GGML_F16};
    t.n_dims = 2;
    t.dims[0] = 25;
    t.dims[1] = 4;
    t.n_elems = 100;
    t.src_off = 1000;
    t.src_len = 200;
    sha256((const uint8_t *) w16, 200, t.src_sha);
    t.off = pos;
    for (uint32_t i = 0; i < 100; i += 32) {
        uint32_t n = 100 - i < 32 ? 100 - i : 32;
        int32_t k = zxvc_block_encode(ZT_GGML_F16, (const uint8_t *) (w16 + i), n, pkg + pos);
        pos += (uint64_t) k;
    }
    t.len = pos - t.off;
    sha256(pkg + t.off, t.len, t.sha);
    zxvc_put_tensor(pkg + z.tensors_off, &t);
    pos = align32(pos);

    zxvc_tensor_t u = {.name = "blk.0.q", .enc = ZXVC_ENC_NATIVE, .src_type = ZT_GGML_Q8_0};
    u.n_dims = 1;
    u.dims[0] = 64;
    u.n_elems = 64;
    u.off = pos;
    u.len = u.src_len = sizeof q8;
    u.src_off = 1200;
    memcpy(pkg + pos, q8, sizeof q8);
    sha256(q8, sizeof q8, u.sha);
    memcpy(u.src_sha, u.sha, 32);
    zxvc_put_tensor(pkg + z.tensors_off + ZXVC_TENSOR_BYTES, &u);
    pos += sizeof q8;

    z.file_size = pos;
    z.sig_off = pos;
    z.sig_len = 0;
    zxvc_put_header(pkg, &z);
    return pos;
}

static void test_package(void)
{
    pn = build();
    zxvc_t z;
    CHECK(zxvc_open(&z, pkg, pn) == ZXVC_OK, "open");
    CHECK(z.n_parts == 2 && z.n_tensors == 2 && z.flags == ZXVC_FLAG_FROM_SAFETENSORS, "counts");
    uint32_t bad = 99;
    CHECK(zxvc_verify_hashes(&z, &bad) == ZXVC_OK, "hashes clean");
    zxvc_part_t p;
    CHECK(zxvc_find_part(&z, "licence", &p) == ZXVC_OK && p.len == 11 &&
              !memcmp(p.data, "apache-2.0\n", 11),
          "licence part");
    CHECK(zxvc_find_part(&z, "tokenizer", &p) == ZXVC_ENOTFOUND, "missing part");
    zxvc_tensor_t t;
    CHECK(zxvc_find_tensor(&z, "blk.0.w", &t) == ZXVC_OK && t.n_elems == 100 && t.dims[1] == 4,
          "exact tensor");
    uint8_t src[200], h[32];
    CHECK(zxvc_tensor_source(&t, src, sizeof src) == ZXVC_OK, "decode exact tensor");
    sha256(src, 200, h);
    CHECK(!memcmp(h, t.src_sha, 32), "exact tensor decodes to its source bytes (SHA-256)");
    CHECK(zxvc_tensor_source(&t, src, 199) == ZXVC_ESPACE, "short output refused");
    zxvc_tensor_t extra = t;
    extra.len += 1; /* a stray byte after the last block */
    CHECK(zxvc_tensor_source(&extra, src, sizeof src) == ZXVC_ECORRUPT, "trailing byte refused");
    zt_fx qx[100];
    CHECK(zxvc_tensor_q16(&extra, qx, 100) == ZXVC_ECORRUPT, "trailing byte refused (Q16)");
    extra.len -= 2; /* the last block cut short */
    CHECK(zxvc_tensor_source(&extra, src, sizeof src) == ZXVC_ETRUNC, "short payload refused");
    zt_fx q[100];
    CHECK(zxvc_tensor_q16(&t, q, 100) == ZXVC_OK, "q16 decode");
    int qbad = 0;
    for (int i = 0; i < 100; i++)
        if (q[i] != zt_f16_to_q16((uint16_t) (src[2 * i] | (src[2 * i + 1] << 8)))) qbad++;
    CHECK(qbad == 0, "q16 decode matches zt_f16_to_q16 (%d differ)", qbad);
    CHECK(zxvc_find_tensor(&z, "blk.0.q", &t) == ZXVC_OK && t.enc == ZXVC_ENC_NATIVE, "native");
    uint8_t q8[68];
    CHECK(zxvc_tensor_source(&t, q8, sizeof q8) == ZXVC_OK, "native copy");
    sha256(q8, sizeof q8, h);
    CHECK(!memcmp(h, t.src_sha, 32), "native tensor bytes unchanged");
    CHECK(zxvc_tensor_q16(&t, q, 100) == ZXVC_ETYPE, "native has no exact Q16 path");

    uint8_t d0[32], d1[32];
    zxvc_digest(&z, d0);
    /* The signature fields are outside the digest. */
    pkg[56] ^= 1;
    zxvc_digest(&z, d1);
    CHECK(!memcmp(d0, d1, 32), "signature offset is not covered by the digest");
    pkg[56] ^= 1;
    /* A table byte is covered. */
    pkg[ZXVC_HEADER_BYTES + ZXVC_PART_BYTES + 50] ^= 1;
    zxvc_digest(&z, d1);
    CHECK(memcmp(d0, d1, 32) != 0, "a part hash byte changes the digest");
    pkg[ZXVC_HEADER_BYTES + ZXVC_PART_BYTES + 50] ^= 1;

    /* A payload byte is caught by the hashes. */
    zxvc_find_tensor(&z, "blk.0.w", &t);
    pkg[t.off + 5] ^= 0x10;
    CHECK(zxvc_verify_hashes(&z, &bad) == ZXVC_EHASH && bad == 2, "flipped payload caught (%u)",
          bad);
    pkg[t.off + 5] ^= 0x10;
    pkg[t.off + 0] = 0xFF; /* also a corrupt mode byte */
    CHECK(zxvc_tensor_source(&t, src, sizeof src) == ZXVC_ECORRUPT, "corrupt block caught");
    pn = build();

    CHECK(zxvc_open(&z, pkg, pn - 1) == ZXVC_ETRUNC, "wrong size refused");
    CHECK(zxvc_open(&z, pkg, 40) == ZXVC_ETRUNC, "truncated header refused");
    pkg[0] = 'X';
    CHECK(zxvc_open(&z, pkg, pn) == ZXVC_EMAGIC, "bad magic");
    pkg[0] = 'Z';
    pkg[4] = 2;
    CHECK(zxvc_open(&z, pkg, pn) == ZXVC_EVERSION, "bad version");
    pkg[4] = 1;
    /* A tensor payload pointing past the end. */
    zxvc_open(&z, pkg, pn);
    uint8_t *e = pkg + z.tensors_off;
    uint64_t far = pn;
    memcpy(e + 152, &far, 8);
    CHECK(zxvc_tensor(&z, 0, &t) == ZXVC_ETRUNC, "payload past the end refused");
    pn = build();
    zxvc_open(&z, pkg, pn);
    e[104] = 9;
    CHECK(zxvc_tensor(&z, 0, &t) == ZXVC_ECORRUPT, "bad dim count refused");
    pn = build();
    zxvc_open(&z, pkg, pn);
    e[144] = 99;
    CHECK(zxvc_tensor(&z, 0, &t) == ZXVC_ECORRUPT, "element count disagreeing with dims refused");
    pn = build();
}

int main(void)
{
    test_f16_all();
    test_bf16_all();
    test_bf16_weights();
    test_f32();
    test_refusals();
    test_package();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
