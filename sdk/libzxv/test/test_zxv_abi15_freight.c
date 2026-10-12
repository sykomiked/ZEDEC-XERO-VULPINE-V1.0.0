/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_freight.c — conformance test of the ABI 1.5 freight group,
 * linked against the shared library only (no kernel headers). The code is
 * restated here independently and every result is checked against it:
 *   - SHA-256 (FIPS 180-4) and GF(2^8) mod 0x11d (carry-less multiply) are
 *     written out here, and the header's 64-byte layout field by field;
 *   - data rows are the payload verbatim, every parity byte is recomputed
 *     from the Cauchy coefficients 1 / (r XOR j);
 *   - dropping exactly 168 - k rows (any pattern) still decodes byte-exactly,
 *     one more is ZXV_E_ERASURE and writes nothing; a corrupted row is
 *     ZXV_E_VERIFY;
 *   - split / join out of order with lost rows; the op-stream format byte
 *     by byte; the generators against their definitions and the kernel's
 *     xorshift64* reference bytes; every error path and status mapping. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)
#define OK(call)                                                       \
    do {                                                               \
        int32_t rc_ = (call);                                          \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_)); \
    } while (0)
#define IS(call, want)                                                                 \
    do {                                                                               \
        int32_t rc_ = (call);                                                          \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want); \
    } while (0)

/* ---- SHA-256, written out (FIPS 180-4) ---- */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 |
               (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}
static void sha256(const uint8_t *m, size_t n, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha256_block(h, m + i);
    uint8_t t[128] = {0};
    size_t r = n - i;
    if (r) memcpy(t, m + i, r);
    t[r] = 0x80;
    size_t tl = r < 56 ? 64 : 128;
    uint64_t bits = (uint64_t) n * 8;
    for (int b = 0; b < 8; b++) t[tl - 1 - b] = (uint8_t) (bits >> (8 * b));
    sha256_block(h, t);
    if (tl == 128) sha256_block(h, t + 64);
    for (int b = 0; b < 32; b++) out[b] = (uint8_t) (h[b / 4] >> (24 - 8 * (b % 4)));
}

/* ---- GF(2^8) mod x^8+x^4+x^3+x^2+1, carry-less ---- */
static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = (uint8_t) ((a << 1) ^ ((a & 0x80) ? 0x1d : 0));
        b >>= 1;
    }
    return r;
}
static uint8_t ginv(uint8_t a)
{
    for (int x = 1; x < 256; x++)
        if (gmul(a, (uint8_t) x) == 1) return (uint8_t) x;
    return 0;
}

static uint64_t rng = 0x5A5846524549474Bull;
static uint32_t rnd32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) (rng >> 16);
}
static void fill(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) rnd32();
}
static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

static int has_error_text(void)
{
    char e[256];
    size_t n = 0;
    return zxv_last_error(e, sizeof e, &n) == ZXV_OK && n > 0;
}

/* ===================================================================== */

static void test_header(void)
{
    static uint8_t p[3360];
    fill(p, sizeof p);
    uint8_t h[64], d[32];
    OK(zxv_freight_header_init(0x0102030405060708ull, 42, ZXV_FREIGHT_F_OPSTREAM,
                               0x1122334455667788ull, p, 777, h));
    sha256(p, 777, d);
    CHECK(memcmp(h, "ZXFH", 4) == 0 && h[4] == 1 && h[5] == 42 && h[6] == 1 && h[7] == 0 &&
              h[8] == (777 & 0xff) && h[9] == (777 >> 8) && h[10] == 0 && h[11] == 0 &&
              h[12] == 1 && h[13] == 0 && h[14] == 0 && h[15] == 0 &&
              le64(h + 16) == 0x0102030405060708ull && le64(h + 24) == 0x1122334455667788ull &&
              memcmp(h + 32, d, 32) == 0,
          "header_init: the documented little-endian layout with SHA-256 of the payload");

    uint8_t ver, k, fl, sha[32];
    uint16_t len, seq, cnt;
    uint64_t id, lim;
    OK(zxv_freight_header_parse(h, &ver, &k, &fl, &len, &seq, &cnt, &id, &lim, sha));
    CHECK(ver == 1 && k == 42 && fl == 1 && len == 777 && seq == 0 && cnt == 1 &&
              id == 0x0102030405060708ull && lim == 0x1122334455667788ull &&
              memcmp(sha, d, 32) == 0,
          "header_parse: every field");
    OK(zxv_freight_header_parse(h, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    OK(zxv_freight_header_check(h));

    uint8_t s[64];
    OK(zxv_freight_header_serialize(42, 1, 777, 0, 1, 0x0102030405060708ull,
                                    0x1122334455667788ull, d, s));
    CHECK(memcmp(s, h, 64) == 0, "header_serialize of the same fields equals header_init");
    OK(zxv_freight_header_serialize(42, 1, 777, 8, 9, 7, 0, d, s));
    CHECK(s[10] == 8 && s[12] == 9 && le64(s + 16) == 7, "header_serialize: seq / count / id");
    OK(zxv_freight_header_parse(s, NULL, NULL, NULL, NULL, &seq, &cnt, NULL, NULL, NULL));
    CHECK(seq == 8 && cnt == 9, "parse(serialize) round trip of a set member");

    /* init errors */
    IS(zxv_freight_header_init(1, 42, 0, 0, p, 10, NULL), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 42, 0, 0, NULL, 10, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 0, 0, 0, p, 0, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 169, 0, 0, p, 0, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 42, 0x80, 0, p, 0, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 42, 0x02, 0, p, 0, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 42, 0, 5, p, 0, h), ZXV_E_ARG);
    IS(zxv_freight_header_init(1, 42, 0, 0, p, 841, h), ZXV_E_TOO_LONG);
    CHECK(has_error_text(), "a failure leaves a zxv_last_error reason");
    OK(zxv_freight_header_init(1, 42, 0, 0, p, 840, h));
    OK(zxv_freight_header_init(1, 168, 0, 0, p, 3360, h));
    OK(zxv_freight_header_init(1, 1, 0, 0, NULL, 0, h));
    sha256(NULL, 0, d);
    CHECK(memcmp(h + 32, d, 32) == 0 && h[8] == 0 && h[9] == 0, "empty payload header");

    /* serialize errors */
    IS(zxv_freight_header_serialize(42, 0, 10, 1, 1, 1, 0, d, s), ZXV_E_ARG); /* seq >= count */
    IS(zxv_freight_header_serialize(42, 0, 10, 0, 0, 1, 0, d, s), ZXV_E_ARG);
    IS(zxv_freight_header_serialize(42, 0, 10, 0, 16385, 1, 0, d, s), ZXV_E_ARG);
    OK(zxv_freight_header_serialize(42, 0, 10, 16383, 16384, 1, 0, d, s));
    IS(zxv_freight_header_serialize(42, 0, 841, 0, 1, 1, 0, d, s), ZXV_E_ARG);
    IS(zxv_freight_header_serialize(42, 0, 10, 0, 1, 1, 9, d, s), ZXV_E_ARG);
    IS(zxv_freight_header_serialize(0, 0, 0, 0, 1, 1, 0, d, s), ZXV_E_ARG);
    IS(zxv_freight_header_serialize(42, 0, 10, 0, 1, 1, 0, NULL, s), ZXV_E_ARG);
    IS(zxv_freight_header_serialize(42, 0, 10, 0, 1, 1, 0, d, NULL), ZXV_E_ARG);

    /* parse / check errors: every byte that matters */
    OK(zxv_freight_header_init(0x0102030405060708ull, 42, 1, 99, p, 777, h));
    struct {
        int off;
        uint8_t val;
    } bad[] = {{0, 'Q'}, {3, 'X'}, {4, 2}, {5, 0}, {5, 169}, {6, 0x80}, {7, 1}, {14, 1},
               {15, 1},  {9, 4},  {10, 1}, {12, 0}, {13, 0x41}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(s, h, 64);
        s[bad[i].off] = bad[i].val;
        IS(zxv_freight_header_parse(s, &ver, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
           ZXV_E_FORMAT);
        IS(zxv_freight_header_check(s), ZXV_E_FORMAT);
    }
    memcpy(s, h, 64);
    s[6] = 0; /* no OPSTREAM but a limit */
    IS(zxv_freight_header_check(s), ZXV_E_FORMAT);
    IS(zxv_freight_header_check(NULL), ZXV_E_ARG);
    IS(zxv_freight_header_parse(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
       ZXV_E_ARG);
    ver = 77;
    memcpy(s, h, 64);
    s[0] = 0;
    zxv_freight_header_parse(s, &ver, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    CHECK(ver == 77, "parse writes no out parameter on failure");
}

/* The parity row r of a freight encoded at k, recomputed here. */
static void ref_row(const uint8_t *payload, size_t len, unsigned k, unsigned r, uint8_t out[20])
{
    for (unsigned b = 0; b < 20; b++) {
        uint8_t acc = 0;
        for (unsigned j = 0; j < k; j++) {
            size_t off = (size_t) j * 20 + b;
            uint8_t v = off < len ? payload[off] : 0;
            uint8_t c = r < k ? (r == j) : ginv((uint8_t) (r ^ j));
            acc ^= gmul(c, v);
        }
        out[b] = acc;
    }
}

/* Keep only the rows with keep[r] set, in a scrambled order with duplicates. */
static size_t subset(const uint8_t *pk, const uint8_t keep[168], uint8_t *out)
{
    size_t n = 0;
    for (int r = 167; r >= 0; r--)
        if (keep[r]) {
            memcpy(out + n * 21, pk + r * 21, 21);
            n++;
            if (r % 7 == 0) { /* a duplicate, ignored */
                memcpy(out + n * 21, pk + r * 21, 21);
                n++;
            }
        }
    return n * 21;
}

static void test_code(void)
{
    static uint8_t p[3360], pk[3528], sub[3528 * 2], back[3400];
    uint8_t h[64], row[20];
    const unsigned K[] = {1, 2, 50, 100, 167, 168};
    for (unsigned t = 0; t < sizeof K / sizeof K[0]; t++) {
        unsigned k = K[t];
        size_t len = k * 20 - (k > 2 ? 13 : 0);
        fill(p, len);
        OK(zxv_freight_header_init(t, (uint8_t) k, 0, 0, p, len, h));
        OK(zxv_freight_encode(h, p, len, pk));
        int rows_ok = 1;
        for (unsigned r = 0; r < 168; r++) {
            ref_row(p, len, k, r, row);
            rows_ok &= pk[r * 21] == r && memcmp(pk + r * 21 + 1, row, 20) == 0;
        }
        CHECK(rows_ok, "k = %u: data rows verbatim, every parity byte = sum 1/(r^j) * d_j", k);

        /* all rows, in order */
        size_t got = 0;
        memset(back, 0xEE, sizeof back);
        OK(zxv_freight_decode(h, pk, 3528, back, sizeof back, &got));
        CHECK(got == len && memcmp(back, p, len) == 0 && back[len] == 0xEE,
              "k = %u: decode of every row is byte-exact", k);

        /* drop exactly 168 - k rows, three patterns */
        unsigned drop = 168 - k;
        for (int pat = 0; pat < 3; pat++) {
            uint8_t keep[168];
            memset(keep, 1, sizeof keep);
            unsigned dropped = 0;
            for (unsigned i = 0; dropped < drop && i < 1000000; i++) {
                unsigned r = pat == 0 ? i : pat == 1 ? 167 - i : rnd32() % 168;
                if (keep[r]) {
                    keep[r] = 0;
                    dropped++;
                }
            }
            size_t sl = subset(pk, keep, sub);
            memset(back, 0xEE, sizeof back);
            got = 0;
            OK(zxv_freight_decode(h, sub, sl, back, sizeof back, &got));
            CHECK(got == len && memcmp(back, p, len) == 0,
                  "k = %u pattern %d: %u rows lost, still byte-exact", k, pat, drop);
            /* one more row lost: ZXV_E_ERASURE, buf untouched */
            for (unsigned r = 0; r < 168; r++)
                if (keep[r]) {
                    keep[r] = 0;
                    break;
                }
            sl = subset(pk, keep, sub);
            memset(back, 0xEE, sizeof back);
            got = 12345;
            IS(zxv_freight_decode(h, sub, sl, back, sizeof back, &got), ZXV_E_ERASURE);
            CHECK(back[0] == 0xEE && back[len - 1] == 0xEE && got == 12345,
                  "k = %u: one row past the limit fails cleanly, writes nothing", k);
        }
    }

    /* k = 100: rows with an out-of-range index are ignored, not counted */
    size_t len = 1987;
    fill(p, len);
    OK(zxv_freight_header_init(9, 100, 0, 0, p, len, h));
    OK(zxv_freight_encode(h, p, len, pk));
    memcpy(sub, pk, 99 * 21);
    memcpy(sub + 99 * 21, pk + 99 * 21, 21);
    sub[99 * 21] = 200; /* row 200 does not exist */
    size_t got;
    IS(zxv_freight_decode(h, sub, 100 * 21, back, sizeof back, &got), ZXV_E_ERASURE);
    sub[99 * 21] = 99;
    OK(zxv_freight_decode(h, sub, 100 * 21, back, sizeof back, &got));
    CHECK(got == len && memcmp(back, p, len) == 0, "exactly k rows decode");

    /* a corrupted row is detected, not repaired; without it decode succeeds */
    memcpy(sub, pk, 3528);
    sub[5 * 21 + 3] ^= 0x01;
    IS(zxv_freight_decode(h, sub, 3528, back, sizeof back, &got), ZXV_E_VERIFY);
    memcpy(sub, pk + 6 * 21, 3528 - 6 * 21); /* rows 6..167: row 5 gone */
    OK(zxv_freight_decode(h, sub, 3528 - 6 * 21, back, sizeof back, &got));
    CHECK(memcmp(back, p, len) == 0, "dropping the distrusted rows recovers the payload");
    /* corrupted parity row among the first k used */
    memcpy(sub, pk + 68 * 21, 100 * 21); /* rows 68..167 */
    sub[50 * 21 + 7] ^= 0x80;            /* row 118, parity */
    IS(zxv_freight_decode(h, sub, 100 * 21, back, sizeof back, &got), ZXV_E_VERIFY);

    /* R4 for bytes */
    got = 0;
    IS(zxv_freight_decode(h, pk, 3528, back, len - 1, &got), ZXV_E_BUFFER);
    CHECK(got == len, "BUFFER reports the payload length");
    got = 0;
    IS(zxv_freight_decode(h, pk, 3528, NULL, 0, &got), ZXV_E_BUFFER);
    CHECK(got == len, "NULL buffer reports the payload length");
    IS(zxv_freight_decode(h, pk, 3528, back, sizeof back, NULL), ZXV_E_ARG);
    IS(zxv_freight_decode(h, pk, 3527, back, sizeof back, &got), ZXV_E_ARG);
    IS(zxv_freight_decode(h, NULL, 21, back, sizeof back, &got), ZXV_E_ARG);
    IS(zxv_freight_decode(h, NULL, 0, back, sizeof back, &got), ZXV_E_ERASURE);
    IS(zxv_freight_decode(NULL, pk, 3528, back, sizeof back, &got), ZXV_E_ARG);
    uint8_t hb[64];
    memcpy(hb, h, 64);
    hb[0] = 'Y';
    IS(zxv_freight_decode(hb, pk, 3528, back, sizeof back, &got), ZXV_E_FORMAT);
    /* a header from another freight: hash mismatch */
    OK(zxv_freight_header_init(9, 100, 0, 0, p + 1, len, hb));
    IS(zxv_freight_decode(hb, pk, 3528, back, sizeof back, &got), ZXV_E_VERIFY);

    /* encode errors */
    IS(zxv_freight_encode(h, p, len - 1, pk), ZXV_E_ARG);
    IS(zxv_freight_encode(h, NULL, len, pk), ZXV_E_ARG);
    IS(zxv_freight_encode(h, p, len, NULL), ZXV_E_ARG);
    IS(zxv_freight_encode(NULL, p, len, pk), ZXV_E_ARG);
    p[3] ^= 1;
    IS(zxv_freight_encode(h, p, len, pk), ZXV_E_VERIFY);
    p[3] ^= 1;
    hb[5] = 0;
    IS(zxv_freight_encode(hb, p, len, pk), ZXV_E_FORMAT);

    /* empty payload: buf may be NULL */
    OK(zxv_freight_header_init(1, 10, 0, 0, NULL, 0, h));
    OK(zxv_freight_encode(h, NULL, 0, pk));
    got = 99;
    OK(zxv_freight_decode(h, pk + 158 * 21, 10 * 21, NULL, 0, &got));
    CHECK(got == 0, "empty payload decodes from any 10 rows");
}

static void test_sets(void)
{
    uint32_t n = 0;
    OK(zxv_freight_set_count(0, 50, &n));
    CHECK(n == 1, "set_count(0) = 1");
    OK(zxv_freight_set_count(1000, 50, &n));
    CHECK(n == 1, "set_count(k * 20) = 1");
    OK(zxv_freight_set_count(1001, 50, &n));
    CHECK(n == 2, "set_count(k * 20 + 1) = 2");
    OK(zxv_freight_set_count(ZXV_FREIGHT_SET_MAX_BYTES, 168, &n));
    CHECK(n == 16384, "set_count(max) = 16384");
    IS(zxv_freight_set_count(ZXV_FREIGHT_SET_MAX_BYTES + 1, 168, &n), ZXV_E_TOO_LONG);
    IS(zxv_freight_set_count(16384u * 20 + 1, 1, &n), ZXV_E_TOO_LONG);
    IS(zxv_freight_set_count(10, 0, &n), ZXV_E_ARG);
    IS(zxv_freight_set_count(10, 169, &n), ZXV_E_ARG);
    IS(zxv_freight_set_count(10, 1, NULL), ZXV_E_ARG);

    enum { LEN = 9500, KK = 50, M = 10 };
    static uint8_t data[LEN], hdr[M][64], pk[M][3528], sub[3528], out[LEN + 8];
    fill(data, LEN);
    uint8_t d[32];
    for (unsigned s = 0; s < M; s++) {
        OK(zxv_freight_split(0xABCDEF, KK, 0, 0, data, LEN, s, hdr[s], pk[s]));
        uint8_t k;
        uint16_t pl, seq, cnt;
        uint64_t id;
        uint8_t sha[32];
        OK(zxv_freight_header_parse(hdr[s], NULL, &k, NULL, &pl, &seq, &cnt, &id, NULL, sha));
        size_t want = s + 1 < M ? 1000 : LEN - 9000;
        sha256(data + s * 1000, want, d);
        CHECK(k == KK && pl == want && seq == s && cnt == M && id == 0xABCDEF &&
                  memcmp(sha, d, 32) == 0 && memcmp(pk[s] + 1, data + s * 1000, 20) == 0,
              "split member %u: header fields and data row 0", s);
    }
    uint8_t h1[64], p1[3528];
    IS(zxv_freight_split(1, KK, 0, 0, data, LEN, M, h1, p1), ZXV_E_RANGE);
    IS(zxv_freight_split(1, 0, 0, 0, data, LEN, 0, h1, p1), ZXV_E_ARG);
    IS(zxv_freight_split(1, KK, 4, 0, data, LEN, 0, h1, p1), ZXV_E_ARG);
    IS(zxv_freight_split(1, KK, 0, 3, data, LEN, 0, h1, p1), ZXV_E_ARG);
    IS(zxv_freight_split(1, KK, 0, 0, NULL, LEN, 0, h1, p1), ZXV_E_ARG);
    IS(zxv_freight_split(1, KK, 0, 0, data, LEN, 0, NULL, p1), ZXV_E_ARG);
    IS(zxv_freight_split(1, KK, 0, 0, data, LEN, 0, h1, NULL), ZXV_E_ARG);
    IS(zxv_freight_split(1, 1, 0, 0, data, 16384u * 20 + 1, 0, h1, p1), ZXV_E_TOO_LONG);
    OK(zxv_freight_split(1, 7, 1, 55, NULL, 0, 0, h1, p1));

    /* join out of order, each member missing 118 rows (the most it may) */
    zxv_freight_join *j = NULL;
    IS(zxv_freight_join_create(ZXV_FREIGHT_SET_MAX_BYTES + 1, &j), ZXV_E_RANGE);
    IS(zxv_freight_join_create(10, NULL), ZXV_E_ARG);
    OK(zxv_freight_join_create(LEN, &j));
    uint32_t rec = 9, cnt = 9;
    int32_t done = 9;
    uint64_t total = 9;
    OK(zxv_freight_join_status(j, &rec, &cnt, &done, &total));
    CHECK(rec == 0 && cnt == 0 && done == 0 && total == 0, "fresh join: nothing yet");
    size_t got = 0;
    IS(zxv_freight_join_result(j, out, sizeof out, &got), ZXV_E_STATE);
    const unsigned order[M] = {7, 2, 9, 0, 5, 1, 8, 3, 6, 4};
    for (unsigned i = 0; i < M; i++) {
        unsigned s = order[i];
        unsigned first = (s * 13) % 119; /* keep rows first .. first + 49 */
        memcpy(sub, pk[s] + first * 21, 50 * 21);
        if (i == 3) {
            /* one row short: refused, not recorded; then retried in full */
            IS(zxv_freight_join_add(j, hdr[s], sub, 49 * 21), ZXV_E_ERASURE);
            OK(zxv_freight_join_status(j, &rec, NULL, NULL, NULL));
            CHECK(rec == 3, "a failed member is not counted");
        }
        OK(zxv_freight_join_add(j, hdr[s], sub, 50 * 21));
        if (i == 4) OK(zxv_freight_join_add(j, hdr[s], NULL, 0)); /* already have it */
    }
    OK(zxv_freight_join_status(j, &rec, &cnt, &done, &total));
    CHECK(rec == M && cnt == M && done == 1 && total == LEN, "join complete: %u of %u", rec, cnt);
    got = 0;
    IS(zxv_freight_join_result(j, out, LEN - 1, &got), ZXV_E_BUFFER);
    CHECK(got == LEN, "join_result BUFFER reports the total");
    memset(out, 0xEE, sizeof out);
    OK(zxv_freight_join_result(j, out, sizeof out, &got));
    CHECK(got == LEN && memcmp(out, data, LEN) == 0 && out[LEN] == 0xEE,
          "joined set equals the original bytes");
    /* a member of another set */
    OK(zxv_freight_split(0xABCDEE, KK, 0, 0, data, LEN, 1, h1, p1));
    IS(zxv_freight_join_add(j, h1, p1, 3528), ZXV_E_STATE);
    IS(zxv_freight_join_add(j, NULL, p1, 3528), ZXV_E_ARG);
    IS(zxv_freight_join_add(j, hdr[0], p1, 3527), ZXV_E_ARG);
    IS(zxv_freight_join_add(NULL, hdr[0], pk[0], 3528), ZXV_E_ARG);
    IS(zxv_freight_join_status(NULL, &rec, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_freight_join_result(NULL, out, sizeof out, &got), ZXV_E_ARG);
    IS(zxv_freight_join_result(j, out, sizeof out, NULL), ZXV_E_ARG);
    zxv_freight_join_destroy(j);
    zxv_freight_join_destroy(NULL);

    /* capacity: the whole set must fit, checked on the first member */
    OK(zxv_freight_join_create(LEN - 501, &j)); /* last member would not fit */
    IS(zxv_freight_join_add(j, hdr[9], pk[9], 3528), ZXV_E_CAPACITY);
    zxv_freight_join_destroy(j);
    OK(zxv_freight_join_create(9000, &j)); /* first 9 fit, the last does not */
    OK(zxv_freight_join_add(j, hdr[0], pk[0], 3528));
    IS(zxv_freight_join_add(j, hdr[9], pk[9], 3528), ZXV_E_CAPACITY);
    /* a corrupted row in a member */
    memcpy(sub, pk[1], 3528);
    sub[1] ^= 0xFF;
    IS(zxv_freight_join_add(j, hdr[1], sub, 3528), ZXV_E_VERIFY);
    zxv_freight_join_destroy(j);
    /* a non-last member that is not full belongs to no valid set */
    OK(zxv_freight_join_create(LEN, &j));
    OK(zxv_freight_join_add(j, hdr[0], pk[0], 3528));
    OK(zxv_freight_header_init(0xABCDEF, KK, 0, 0, data, 999, h1));
    uint8_t sha[32];
    OK(zxv_freight_header_parse(h1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, sha));
    OK(zxv_freight_header_serialize(KK, 0, 999, 2, M, 0xABCDEF, 0, sha, h1));
    IS(zxv_freight_join_add(j, h1, pk[2], 3528), ZXV_E_STATE);
    zxv_freight_join_destroy(j);

    /* the empty set: one freight of 0 bytes, joined into a 0-byte join */
    OK(zxv_freight_join_create(0, &j));
    OK(zxv_freight_split(5, 3, 0, 0, NULL, 0, 0, h1, p1));
    OK(zxv_freight_join_add(j, h1, p1 + 100 * 21, 3 * 21));
    OK(zxv_freight_join_status(j, NULL, NULL, &done, &total));
    got = 9;
    OK(zxv_freight_join_result(j, NULL, 0, &got));
    CHECK(done == 1 && total == 0 && got == 0, "empty set joins to 0 bytes");
    zxv_freight_join_destroy(j);
}

/* ---- op streams ---- */
static int32_t resolver(void *user, const uint8_t *cid, size_t cid_len, uint8_t *out, uint64_t len)
{
    int *calls = user;
    (*calls)++;
    if (cid_len != 4 || memcmp(cid, "cid1", 4) != 0) return ZXV_E_NOT_FOUND;
    for (uint64_t i = 0; i < len; i++) out[i] = (uint8_t) ('Z' - (i % 3));
    zxv_freight_header_check(NULL); /* a nested failure must not leak out */
    return ZXV_OK;
}
static int32_t refuser(void *user, const uint8_t *cid, size_t cid_len, uint8_t *out, uint64_t len)
{
    (void) user, (void) cid, (void) cid_len, (void) out, (void) len;
    return -1;
}

/* xorshift64* as the header defines it */
static void ref_xorshift(uint64_t x, uint8_t *o, size_t n)
{
    size_t i = 0;
    while (i < n) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t v = x * 0x2545F4914F6CDD1DULL;
        for (int b = 0; b < 8 && i < n; b++, i++) o[i] = (uint8_t) (v >> (8 * b));
    }
}

static void test_generators(void)
{
    uint8_t o[4096], r[4096];
    uint8_t seed[8];
    for (int i = 0; i < 8; i++) seed[i] = (uint8_t) (0x0123456789abcdefull >> (8 * i));
    static const uint8_t xs16[16] = {0x8c, 0x70, 0xb6, 0x2c, 0x47, 0x82, 0x94, 0x7c,
                                     0xde, 0x28, 0x1f, 0xbf, 0x92, 0x56, 0x70, 0xd5};
    OK(zxv_freight_gen_run(ZXV_FREIGHT_GEN_XORSHIFT, seed, 8, o, sizeof o));
    ref_xorshift(0x0123456789abcdefull, r, sizeof r);
    CHECK(memcmp(o, xs16, 16) == 0 && memcmp(o, r, sizeof o) == 0,
          "xorshift64*: kernel reference bytes and the definition");
    OK(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, (const uint8_t *) "abc", 3, o, 10));
    CHECK(memcmp(o, "abcabcabca", 10) == 0, "repeat");
    OK(zxv_freight_gen_run(ZXV_FREIGHT_GEN_FIBWORD, (const uint8_t *) "ab", 2, o, 21));
    CHECK(memcmp(o, "abaababaabaababaababa", 21) == 0, "Fibonacci word");
    const uint8_t sp[3] = {3, '#', '.'};
    OK(zxv_freight_gen_run(ZXV_FREIGHT_GEN_SIERPINSKI, sp, 3, o, 200));
    int sier = 1;
    for (unsigned i = 0; i < 200; i++) {
        unsigned x = i % 8, y = (i >> 3) % 8;
        sier &= o[i] == ((x & y) == 0 ? '#' : '.');
    }
    CHECK(sier, "Sierpinski bitmap, (x & y) == 0, width 8, wraps every 64");
    OK(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, (const uint8_t *) "a", 1, NULL, 0));
    uint8_t z8[8] = {0};
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_XORSHIFT, z8, 8, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_XORSHIFT, seed, 7, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_FIBWORD, (const uint8_t *) "aa", 2, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, NULL, 0, o, 8), ZXV_E_ARG);
    const uint8_t sp16[3] = {16, 1, 0};
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_SIERPINSKI, sp16, 3, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(9, seed, 8, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, o, 33, r, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, NULL, 3, o, 8), ZXV_E_ARG);
    IS(zxv_freight_gen_run(ZXV_FREIGHT_GEN_REPEAT, o, 3, NULL, 8), ZXV_E_ARG);
}

static void test_ops(void)
{
    /* exact bytes of a tiny stream */
    zxv_freight_ops_writer *w = NULL;
    OK(zxv_freight_ops_writer_create(5, NULL, 64, &w));
    OK(zxv_freight_ops_put_literal(w, (const uint8_t *) "AB", 2));
    OK(zxv_freight_ops_put_copy(w, 2, 3));
    uint8_t s[256];
    size_t sl = 0;
    OK(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl));
    const uint8_t want[] = {'Z', 'X', 'O', 'P', 1, 0, 5, 0, 0, 0, 0, 0, 0, 0,
                            0x01, 0x02, 'A', 'B', 0x02, 0x02, 0x03};
    CHECK(sl == sizeof want && memcmp(s, want, sl) == 0, "op stream bytes as documented");
    uint8_t o[256];
    size_t ol = 0;
    OK(zxv_freight_ops_expand(s, sl, 5, NULL, NULL, o, sizeof o, &ol));
    CHECK(ol == 5 && memcmp(o, "ABABA", 5) == 0, "COPY with overlap repeats the period");
    sl = 0;
    IS(zxv_freight_ops_writer_finish(w, s, 20, &sl), ZXV_E_BUFFER);
    CHECK(sl == sizeof want, "finish BUFFER reports the length");
    IS(zxv_freight_ops_put_literal(w, (const uint8_t *) "C", 1), ZXV_E_ARG); /* past total */
    IS(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl), ZXV_E_ARG);       /* sticky */
    zxv_freight_ops_writer_destroy(w);

    /* every op kind, with the output's SHA-256 */
    uint8_t ex[100];
    memcpy(ex, "abcd", 4);
    for (int i = 0; i < 20; i++) ex[4 + i] = ex[i];
    for (int i = 0; i < 30; i++) ex[24 + i] = "xy"[i % 2];
    memcpy(ex + 54, "PQPPQPQPPQ", 10); /* Fibonacci word over P, Q */
    for (int i = 0; i < 36; i++) ex[64 + i] = (uint8_t) ('Z' - (i % 3));
    uint8_t d[32];
    sha256(ex, 100, d);
    OK(zxv_freight_ops_writer_create(100, d, 4096, &w));
    IS(zxv_freight_ops_put_copy(w, 1, 1), ZXV_E_ARG); /* nothing produced yet */
    zxv_freight_ops_writer_destroy(w);
    OK(zxv_freight_ops_writer_create(100, d, 4096, &w));
    OK(zxv_freight_ops_put_literal(w, (const uint8_t *) "abcd", 4));
    OK(zxv_freight_ops_put_copy(w, 4, 20));
    OK(zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_REPEAT, (const uint8_t *) "xy", 2, 30));
    OK(zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_FIBWORD, (const uint8_t *) "PQ", 2, 10));
    IS(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl), ZXV_E_STATE); /* 64 of 100 */
    OK(zxv_freight_ops_put_cidref(w, (const uint8_t *) "cid1", 4, 36));
    OK(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl));
    zxv_freight_ops_writer_destroy(w);
    uint64_t tot = 0;
    int32_t hs = 0;
    OK(zxv_freight_ops_peek(s, sl, &tot, &hs));
    CHECK(tot == 100 && hs == 1 && memcmp(s + 14, d, 32) == 0, "peek: total and SHA flag");
    OK(zxv_freight_ops_peek(s, sl, NULL, NULL));
    int calls = 0;
    memset(o, 0, sizeof o);
    OK(zxv_freight_ops_expand(s, sl, 100, resolver, &calls, o, sizeof o, &ol));
    CHECK(ol == 100 && memcmp(o, ex, 100) == 0 && calls == 1,
          "expand: literal, copy, seeds and a resolved CIDREF give the expected bytes");
    char e[64];
    size_t en = 99;
    zxv_last_error(e, sizeof e, &en);
    CHECK(en == 0, "a resolver's own failure does not leak into zxv_last_error");
    IS(zxv_freight_ops_expand(s, sl, 99, resolver, &calls, o, sizeof o, &ol), ZXV_E_TOO_LONG);
    ol = 0;
    IS(zxv_freight_ops_expand(s, sl, 100, resolver, &calls, o, 99, &ol), ZXV_E_BUFFER);
    CHECK(ol == 100, "expand BUFFER reports the declared total");
    IS(zxv_freight_ops_expand(s, sl, 100, resolver, &calls, NULL, 0, &ol), ZXV_E_BUFFER);
    IS(zxv_freight_ops_expand(s, sl, 100, refuser, NULL, o, sizeof o, &ol), ZXV_E_NOT_FOUND);
    IS(zxv_freight_ops_expand(s, sl, 100, NULL, NULL, o, sizeof o, &ol), ZXV_E_NOT_FOUND);
    IS(zxv_freight_ops_expand(s, sl, ZXV_FREIGHT_OPS_MAX + 1, NULL, NULL, o, sizeof o, &ol),
       ZXV_E_RANGE);
    IS(zxv_freight_ops_expand(NULL, sl, 100, NULL, NULL, o, sizeof o, &ol), ZXV_E_ARG);
    IS(zxv_freight_ops_expand(s, sl, 100, NULL, NULL, o, sizeof o, NULL), ZXV_E_ARG);
    s[20] ^= 1; /* the SHA-256 */
    IS(zxv_freight_ops_expand(s, sl, 100, resolver, &calls, o, sizeof o, &ol), ZXV_E_VERIFY);
    s[20] ^= 1;
    IS(zxv_freight_ops_expand(s, sl - 1, 100, resolver, &calls, o, sizeof o, &ol), ZXV_E_FORMAT);
    s[4] = 2;
    IS(zxv_freight_ops_expand(s, sl, 100, resolver, &calls, o, sizeof o, &ol), ZXV_E_FORMAT);
    IS(zxv_freight_ops_peek(s, sl, &tot, &hs), ZXV_E_FORMAT);
    IS(zxv_freight_ops_peek(s, 10, &tot, &hs), ZXV_E_FORMAT);
    IS(zxv_freight_ops_peek(NULL, 10, &tot, &hs), ZXV_E_ARG);
    /* a stream that declares more than it produces */
    const uint8_t shortst[] = {'Z', 'X', 'O', 'P', 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 'A'};
    IS(zxv_freight_ops_expand(shortst, sizeof shortst, 3, NULL, NULL, o, sizeof o, &ol),
       ZXV_E_FORMAT);
    /* the empty stream */
    OK(zxv_freight_ops_writer_create(0, NULL, 14, &w));
    OK(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl));
    CHECK(sl == 14, "empty stream is the 14-byte header");
    ol = 9;
    OK(zxv_freight_ops_expand(s, sl, 0, NULL, NULL, NULL, 0, &ol));
    CHECK(ol == 0, "empty stream expands to nothing");
    zxv_freight_ops_writer_destroy(w);

    /* writer errors */
    IS(zxv_freight_ops_writer_create(5, NULL, 13, &w), ZXV_E_ARG);
    IS(zxv_freight_ops_writer_create(5, d, 45, &w), ZXV_E_ARG);
    IS(zxv_freight_ops_writer_create(5, NULL, ZXV_FREIGHT_OPS_MAX + 1, &w), ZXV_E_RANGE);
    IS(zxv_freight_ops_writer_create(5, NULL, 64, NULL), ZXV_E_ARG);
    const struct {
        int op;
    } kinds[] = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}};
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        OK(zxv_freight_ops_writer_create(50, NULL, 64, &w));
        OK(zxv_freight_ops_put_literal(w, (const uint8_t *) "q", 1));
        int32_t rc = 0;
        switch (kinds[i].op) {
        case 0: rc = zxv_freight_ops_put_literal(w, (const uint8_t *) "q", 0); break;
        case 1: rc = zxv_freight_ops_put_literal(w, NULL, 3); break;
        case 2: rc = zxv_freight_ops_put_copy(w, 2, 3); break;
        case 3: rc = zxv_freight_ops_put_copy(w, 0, 3); break;
        case 4: rc = zxv_freight_ops_put_cidref(w, (const uint8_t *) "c", 0, 3); break;
        case 5: rc = zxv_freight_ops_put_cidref(w, o, 129, 3); break;
        case 6: rc = zxv_freight_ops_put_seed(w, 9, (const uint8_t *) "ab", 2, 3); break;
        case 7: rc = zxv_freight_ops_put_seed(w, ZXV_FREIGHT_GEN_FIBWORD, (const uint8_t *) "aa", 2, 3); break;
        case 8: rc = zxv_freight_ops_put_copy(w, 1, 50); break; /* passes the total */
        }
        CHECK(rc == ZXV_E_ARG, "writer refusal %u -> %s", i, zxv_status_name(rc));
        IS(zxv_freight_ops_put_literal(w, (const uint8_t *) "q", 1), ZXV_E_ARG); /* sticky */
        zxv_freight_ops_writer_destroy(w);
    }
    OK(zxv_freight_ops_writer_create(50, NULL, 160, &w));
    OK(zxv_freight_ops_put_cidref(w, o, 128, 3)); /* 128 is allowed... */
    IS(zxv_freight_ops_put_literal(w, o, 40), ZXV_E_CAPACITY); /* ...but the buffer is full */
    IS(zxv_freight_ops_put_copy(w, 1, 1), ZXV_E_CAPACITY);      /* sticky */
    IS(zxv_freight_ops_writer_finish(w, s, sizeof s, &sl), ZXV_E_CAPACITY);
    zxv_freight_ops_writer_destroy(w);
    zxv_freight_ops_writer_destroy(NULL);
    IS(zxv_freight_ops_put_literal(NULL, o, 1), ZXV_E_ARG);
    IS(zxv_freight_ops_writer_finish(NULL, s, sizeof s, &sl), ZXV_E_ARG);

    /* the LZ77 encoder: round trips */
    static uint8_t in[20000], enc[40000], dec[20000];
    for (int i = 0; i < 20000; i++) in[i] = (uint8_t) ("the freight, "[i % 13] ^ (i / 5000));
    size_t el = 0;
    OK(zxv_freight_ops_encode(in, sizeof in, 1, enc, sizeof enc, &el));
    OK(zxv_freight_ops_peek(enc, el, &tot, &hs));
    CHECK(tot == sizeof in && hs == 1 && el < sizeof in, "encode: repeating text, SHA flag");
    OK(zxv_freight_ops_expand(enc, el, sizeof in, NULL, NULL, dec, sizeof dec, &ol));
    CHECK(ol == sizeof in && memcmp(dec, in, sizeof in) == 0, "encode then expand: exact");
    size_t el2 = 0;
    uint8_t *enc2 = malloc(sizeof enc);
    OK(zxv_freight_ops_encode(in, sizeof in, 1, enc2, sizeof enc, &el2));
    CHECK(el2 == el && memcmp(enc, enc2, el) == 0, "encode is deterministic");
    free(enc2);
    fill(in, sizeof in);
    uint64_t bound = 0;
    OK(zxv_freight_ops_bound(sizeof in, 0, &bound));
    CHECK(bound == 14 + 20000 + 11 * (20000 / 4 + 1), "bound = header + n + 11 * (n/4 + 1)");
    OK(zxv_freight_ops_bound(0, 1, &bound));
    CHECK(bound == 46 + 11, "bound(0, sha)");
    OK(zxv_freight_ops_encode(in, sizeof in, 0, enc, sizeof enc, &el));
    OK(zxv_freight_ops_peek(enc, el, &tot, &hs));
    CHECK(hs == 0 && el > sizeof in && el <= 14 + 20000 + 11 * (20000 / 4 + 1),
          "random data encodes LARGER than itself (honest limit), within the bound");
    OK(zxv_freight_ops_expand(enc, el, sizeof in, NULL, NULL, dec, sizeof dec, &ol));
    CHECK(memcmp(dec, in, sizeof in) == 0, "random round trip exact");
    el = 0;
    IS(zxv_freight_ops_encode(in, sizeof in, 0, enc, 100, &el), ZXV_E_BUFFER);
    CHECK(el > sizeof in, "encode BUFFER reports the exact length");
    OK(zxv_freight_ops_encode(NULL, 0, 0, enc, sizeof enc, &el));
    CHECK(el == 14, "encode of nothing is a bare header");
    IS(zxv_freight_ops_encode(in, ZXV_FREIGHT_OPS_MAX + 1, 0, enc, sizeof enc, &el),
       ZXV_E_TOO_LONG);
    IS(zxv_freight_ops_encode(NULL, 5, 0, enc, sizeof enc, &el), ZXV_E_ARG);
    IS(zxv_freight_ops_encode(in, 5, 0, enc, sizeof enc, NULL), ZXV_E_ARG);
    IS(zxv_freight_ops_bound(5, 0, NULL), ZXV_E_ARG);
    IS(zxv_freight_ops_bound(ZXV_FREIGHT_OPS_MAX + 1, 0, &bound), ZXV_E_TOO_LONG);
}

/* An op stream carried as an OPSTREAM freight set, rows lost, joined and
 * expanded under the header's limit. */
static void test_end_to_end(void)
{
    static uint8_t text[6000], ops[8000], back[8000], out[6000], h[64], pk[3528], sub[3528];
    for (int i = 0; i < 6000; i++) text[i] = (uint8_t) ("smart packets "[i % 14]);
    size_t ol = 0;
    OK(zxv_freight_ops_encode(text, sizeof text, 1, ops, sizeof ops, &ol));
    uint32_t n = 0;
    OK(zxv_freight_set_count(ol, 20, &n));
    zxv_freight_join *j;
    OK(zxv_freight_join_create(ol, &j));
    for (uint32_t s = 0; s < n; s++) {
        OK(zxv_freight_split(77, 20, ZXV_FREIGHT_F_OPSTREAM, sizeof text, ops, ol, s, h, pk));
        memcpy(sub, pk + 148 * 21, 20 * 21); /* only the last 20 rows: all parity */
        OK(zxv_freight_join_add(j, h, sub, 20 * 21));
    }
    uint64_t lim = 0;
    OK(zxv_freight_header_parse(h, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &lim, NULL));
    size_t bl = 0, xl = 0;
    OK(zxv_freight_join_result(j, back, sizeof back, &bl));
    zxv_freight_join_destroy(j);
    OK(zxv_freight_ops_expand(back, bl, lim, NULL, NULL, out, sizeof out, &xl));
    CHECK(bl == ol && xl == sizeof text && memcmp(out, text, sizeof text) == 0,
          "op stream -> %u freights -> parity rows only -> join -> expand: exact", n);
}

int main(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_FREIGHT, "ZXV_FEAT2_FREIGHT is live");
    test_header();
    test_code();
    test_sets();
    test_generators();
    test_ops();
    test_end_to_end();
    printf("test_zxv_abi15_freight: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
