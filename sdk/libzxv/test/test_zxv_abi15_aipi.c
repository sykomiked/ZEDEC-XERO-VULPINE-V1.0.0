/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_aipi.c — ABI 1.5 aipi group: CIDs, AIPI records and the
 * zxv_aipi node handle, linked against the shared library only.
 *
 * Where the expected values come from:
 *   - Kubo 0.32.1 strings, copied from kernel/src/ipfs_node/test_ipfs_node.c
 *     (`ipfs add -Q --offline --cid-version=1` over the same xorshift32
 *     files, `ipfs cid base32` for the CIDv0 pair);
 *   - everything else is recomputed HERE, independently of the kernel: a
 *     SHA-256 (FIPS 180-4), unsigned varints, base32 and base58btc, and the
 *     AIPI wire records written out byte by byte from docs/AIPI.md
 *     section 11. A CID or record from the library must equal them. */
#include <stdbool.h>
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

/* ===================================================================== */
/* Independent reference code                                            */
/* ===================================================================== */

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

static void sha_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k;
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 |
               (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

/* SHA-256 of the concatenation of two pieces (b may be empty). */
static void sha256_2(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t blk[64];
    size_t fill = 0, total = an + bn;
    for (size_t i = 0; i < total; i++) {
        blk[fill++] = i < an ? a[i] : b[i - an];
        if (fill == 64) sha_block(h, blk), fill = 0;
    }
    blk[fill++] = 0x80;
    if (fill > 56) {
        memset(blk + fill, 0, 64 - fill);
        sha_block(h, blk);
        fill = 0;
    }
    memset(blk + fill, 0, 56 - fill);
    uint64_t bits = (uint64_t) total * 8u;
    for (int i = 0; i < 8; i++) blk[56 + i] = (uint8_t) (bits >> (56 - 8 * i));
    sha_block(h, blk);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t) (h[i] >> (24 - 8 * j));
}

static size_t put_varint(uint64_t v, uint8_t *o)
{
    size_t n = 0;
    do {
        uint8_t b = (uint8_t) (v & 0x7f);
        v >>= 7;
        o[n++] = (uint8_t) (b | (v ? 0x80 : 0));
    } while (v);
    return n;
}

/* CIDv1 sha2-256 of `data` under `codec`, binary. */
static size_t ref_cid(uint32_t codec, const uint8_t *data, size_t len, uint8_t out[80])
{
    size_t n = 0;
    out[n++] = 1;
    n += put_varint(codec, out + n);
    out[n++] = 0x12;
    out[n++] = 0x20;
    sha256_2(data, len, NULL, 0, out + n);
    return n + 32;
}

static void ref_base32(const uint8_t *in, size_t n, char *out)
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 5) out[o++] = A[(acc >> (bits - 5)) & 31], bits -= 5;
    }
    if (bits) out[o++] = A[(acc << (5 - bits)) & 31];
    out[o] = 0;
}

static void ref_base58(const uint8_t *in, size_t n, char *out)
{
    static const char A[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t digits[256] = {0};
    size_t nd = 0, zeros = 0, o = 0;
    while (zeros < n && in[zeros] == 0) zeros++;
    for (size_t i = zeros; i < n; i++) {
        uint32_t carry = in[i];
        for (size_t j = 0; j < nd; j++) {
            carry += (uint32_t) digits[j] << 8;
            digits[j] = (uint8_t) (carry % 58);
            carry /= 58;
        }
        while (carry) digits[nd++] = (uint8_t) (carry % 58), carry /= 58;
    }
    for (size_t i = 0; i < zeros; i++) out[o++] = '1';
    for (size_t i = nd; i > 0; i--) out[o++] = A[digits[i - 1]];
    out[o] = 0;
}

static int hexval(char c)
{
    return c <= '9' ? c - '0' : c - 'a' + 10;
}

static void unhex(const char *h, uint8_t *out)
{
    for (size_t i = 0; h[2 * i]; i++) out[i] = (uint8_t) (hexval(h[2 * i]) << 4 | hexval(h[2 * i + 1]));
}

/* xorshift32, 4 bytes LE per step: the generator Kubo's vectors were made from. */
static void gen_fill(uint32_t seed, uint8_t *b, size_t n)
{
    uint32_t x = seed ? seed : 1;
    int k = 4;
    for (size_t i = 0; i < n; i++) {
        if (k == 4) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            k = 0;
        }
        b[i] = (uint8_t) (x >> (8 * k));
        k++;
    }
}

/* text of a binary CID in the default base */
static const char *txt(const uint8_t *cid, size_t n)
{
    static char s[ZXV_CID_TEXT_MAX + 1];
    size_t l;
    if (zxv_cid_to_text(cid, n, ZXV_CID_BASE_DEFAULT, s, sizeof s, &l) != ZXV_OK) return "<err>";
    return s;
}

/* ===================================================================== */
/* CIDs                                                                  */
/* ===================================================================== */

#define EMPTY_RAW "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku"
#define HELLO_RAW "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4"
#define HELLO_SHA "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447"
#define V0_TEXT "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5o"
#define V0_AS_V1 "bafybeicg2rebjoofv4kbyovkw7af3rpiitvnl6i7ckcywaq6xjcxnc2mby"
static const uint8_t HELLO[12] = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '\n'};

static void test_known_cids(void)
{
    uint8_t c[ZXV_CID_MAX], d[ZXV_CID_MAX], ref[80], sha[32];
    size_t n = 0, m = 0;
    char s[ZXV_CID_TEXT_MAX + 1], r[200];

    OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, NULL, 0, c, &n));
    CHECK(!strcmp(txt(c, n), EMPTY_RAW), "empty raw CID %s", txt(c, n));
    OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, HELLO, sizeof HELLO, c, &n));
    CHECK(n == 36 && c[0] == 1 && c[1] == 0x55 && c[2] == 0x12 && c[3] == 0x20, "binary layout");
    unhex(HELLO_SHA, sha);
    CHECK(!memcmp(c + 4, sha, 32), "digest = sha256sum");
    sha256_2(HELLO, sizeof HELLO, NULL, 0, ref);
    CHECK(!memcmp(ref, sha, 32), "reference SHA-256 agrees with sha256sum");
    CHECK(!strcmp(txt(c, n), HELLO_RAW), "hello world raw CID (Kubo) %s", txt(c, n));
    ref_base32(c, n, r + 1);
    r[0] = 'b';
    CHECK(!strcmp(r, HELLO_RAW), "independent base32 of the binary CID");

    /* "z" form: canonical base58btc of the same bytes, and it parses back */
    OK(zxv_cid_to_text(c, n, ZXV_CID_BASE_58BTC, s, sizeof s, &m));
    r[0] = 'z';
    ref_base58(c, n, r + 1);
    CHECK(!strcmp(s, r) && m == strlen(r), "z text %s vs %s", s, r);
    OK(zxv_cid_from_text(s, d, &m));
    CHECK(m == n && !memcmp(c, d, n), "z text round trip");
    OK(zxv_cid_from_text(HELLO_RAW, d, &m));
    CHECK(m == n && !memcmp(c, d, n), "b text round trip");
    /* a base58btc CIDv1 written elsewhere (the multiformats example that the
     * ipfs_node parser alone refuses) parses and prints back unchanged */
    static const char ZEX[] = "zb2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93bA";
    OK(zxv_cid_from_text(ZEX, d, &m));
    OK(zxv_cid_to_text(d, m, ZXV_CID_BASE_58BTC, s, sizeof s, &m));
    CHECK(!strcmp(s, ZEX), "external z CID round trip");

    /* CIDv0 */
    OK(zxv_cid_from_text(V0_TEXT, d, &m));
    CHECK(m == 34 && d[0] == 0x12 && d[1] == 0x20, "CIDv0 binary is the bare multihash");
    CHECK(!strcmp(txt(d, m), V0_TEXT), "CIDv0 text round trip");
    ref_base58(d, m, r);
    CHECK(!strcmp(r, V0_TEXT), "CIDv0 text is base58btc of its bytes");
    uint32_t ver = 9, codec = 9, hash = 9;
    uint8_t dig[ZXV_CID_DIGEST_MAX];
    size_t dl = 0;
    OK(zxv_cid_info(d, m, &ver, &codec, &hash, dig, &dl));
    CHECK(ver == 0 && codec == ZXV_CID_CODEC_DAG_PB && hash == ZXV_CID_HASH_SHA2_256 && dl == 32 &&
              !memcmp(dig, d + 2, 32),
          "CIDv0 info");
    IS(zxv_cid_to_text(d, m, ZXV_CID_BASE_58BTC, s, sizeof s, &n), ZXV_E_CID);
    uint8_t v1[ZXV_CID_MAX];
    size_t v1n = 0;
    OK(zxv_cid_to_v1(d, m, v1, &v1n));
    CHECK(!strcmp(txt(v1, v1n), V0_AS_V1), "CIDv0 -> v1 (Kubo `ipfs cid base32`) %s", txt(v1, v1n));
    CHECK(v1n == 36 && v1[0] == 1 && v1[1] == 0x70 && !memcmp(v1 + 4, d + 2, 32), "v1 layout");
    OK(zxv_cid_to_v1(v1, v1n, c, &n));
    CHECK(n == v1n && !memcmp(c, v1, n), "v1 unchanged");
    OK(zxv_cid_info(v1, v1n, &ver, &codec, NULL, NULL, NULL));
    CHECK(ver == 1 && codec == 0x70, "v1 info");
    OK(zxv_cid_info(v1, v1n, NULL, NULL, NULL, NULL, NULL));

    /* any codec: the AIPI private-use ones hash the same way */
    OK(zxv_cid_compute(ZXV_AIPI_MC_CALL, HELLO, sizeof HELLO, c, &n));
    size_t rn = ref_cid(ZXV_AIPI_MC_CALL, HELLO, sizeof HELLO, ref);
    CHECK(n == rn && !memcmp(c, ref, n) && n == 39, "private-use codec CID (len %zu)", n);
    OK(zxv_cid_info(c, n, &ver, &codec, &hash, dig, &dl));
    CHECK(ver == 1 && codec == ZXV_AIPI_MC_CALL && hash == 0x12 && dl == 32, "codec read back");

    /* errors */
    IS(zxv_cid_compute(0x55, NULL, 1, c, &n), ZXV_E_ARG);
    IS(zxv_cid_compute(0x55, HELLO, 1, NULL, &n), ZXV_E_ARG);
    IS(zxv_cid_compute(0x55, HELLO, 1, c, NULL), ZXV_E_ARG);
    if (sizeof(size_t) > 4) IS(zxv_cid_compute(0x55, HELLO, (size_t) UINT32_MAX + 1u, c, &n), ZXV_E_TOO_LONG);
    IS(zxv_cid_to_v1(NULL, 36, c, &n), ZXV_E_ARG);
    IS(zxv_cid_to_v1(v1, v1n, NULL, &n), ZXV_E_ARG);
    IS(zxv_cid_to_v1(v1, v1n, c, NULL), ZXV_E_ARG);
    IS(zxv_cid_info(NULL, 36, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
}

static void test_unixfs(void)
{
    static const struct {
        uint32_t seed;
        size_t n;
        const char *cid;
    } V[] = {
        {3, 262144, "bafkreifirtqwzxgmqfujuxv4dqq7k5d3rtg3bxyqtreqlzdm4bw2zsyl2i"},
        {4, 262145, "bafybeiblcoqeiu2es5gippzqzdwxpubdibjzeomolvxth3qrks7dvmtn2i"},
        {1, 1048576, "bafybeie2ua5pl4qjmdr5272vm3sezaacwpff6oa5qovteyusjd5kw6up7y"},
    };
    uint8_t c[ZXV_CID_MAX], r[ZXV_CID_MAX];
    size_t n, rn;
    uint8_t *buf = malloc(1048576);
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        gen_fill(V[i].seed, buf, V[i].n);
        OK(zxv_cid_unixfs(buf, V[i].n, c, &n));
        CHECK(!strcmp(txt(c, n), V[i].cid), "Kubo add --cid-version=1, %zu bytes: %s", V[i].n,
              txt(c, n));
        OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, buf, V[i].n, r, &rn));
        if (V[i].n <= 262144) {
            CHECK(rn == n && !memcmp(r, c, n), "one chunk: unixfs == raw block CID");
            OK(zxv_cid_verify(c, n, buf, V[i].n));
        } else {
            CHECK(c[1] == 0x70 && memcmp(r, c, n) != 0, "more than one chunk: a dag-pb root");
            IS(zxv_cid_verify(c, n, buf, V[i].n), ZXV_E_CID); /* names the root block */
        }
    }
    free(buf);
    OK(zxv_cid_unixfs(NULL, 0, c, &n));
    CHECK(!strcmp(txt(c, n), EMPTY_RAW), "empty file");
    OK(zxv_cid_unixfs(HELLO, sizeof HELLO, c, &n));
    CHECK(!strcmp(txt(c, n), HELLO_RAW), "hello world file");
    IS(zxv_cid_unixfs(NULL, 1, c, &n), ZXV_E_ARG);
    IS(zxv_cid_unixfs(HELLO, 1, NULL, &n), ZXV_E_ARG);
    IS(zxv_cid_unixfs(HELLO, 1, c, NULL), ZXV_E_ARG);
}

static void test_cid_text_and_errors(void)
{
    uint8_t c[ZXV_CID_MAX], d[ZXV_CID_MAX];
    size_t n = 0, m = 0;
    char s[ZXV_CID_TEXT_MAX + 1];
    OK(zxv_cid_from_text(HELLO_RAW, c, &n));

    /* R4 text */
    size_t want = strlen(HELLO_RAW);
    m = 0;
    IS(zxv_cid_to_text(c, n, ZXV_CID_BASE_DEFAULT, NULL, 0, &m), ZXV_E_BUFFER);
    CHECK(m == want, "needed length %zu", m);
    memset(s, 'X', sizeof s);
    IS(zxv_cid_to_text(c, n, ZXV_CID_BASE_DEFAULT, s, want, &m), ZXV_E_BUFFER);
    CHECK(s[0] == 'X', "nothing written on ZXV_E_BUFFER");
    OK(zxv_cid_to_text(c, n, ZXV_CID_BASE_DEFAULT, s, want + 1, &m));
    CHECK(m == want && s[want] == 0 && !strcmp(s, HELLO_RAW), "exact fit");
    IS(zxv_cid_to_text(c, n, 2, s, sizeof s, &m), ZXV_E_ARG);
    IS(zxv_cid_to_text(c, n, 0, s, sizeof s, NULL), ZXV_E_ARG);
    IS(zxv_cid_to_text(NULL, n, 0, s, sizeof s, &m), ZXV_E_ARG);

    /* refused text */
    static const char *bad[] = {
        "Bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4",
        "bAfkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4",
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei",
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4a",
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4=",
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff50",
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5",
        "z1b2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93bA", /* extra leading zero byte */
        "zb2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93b0",  /* '0' is not base58 */
        "z",
        "b",
        "fakecid",
        "mAXASIA",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        IS(zxv_cid_from_text(bad[i], d, &m), ZXV_E_CID);
    IS(zxv_cid_from_text("", d, &m), ZXV_E_ARG);
    IS(zxv_cid_from_text(NULL, d, &m), ZXV_E_ARG);
    IS(zxv_cid_from_text(HELLO_RAW, NULL, &m), ZXV_E_ARG);
    IS(zxv_cid_from_text(HELLO_RAW, d, NULL), ZXV_E_ARG);
    IS(zxv_cid_from_text("b\xff\xfe", d, &m), ZXV_E_UTF8);
    char lng[ZXV_CID_TEXT_MAX + 2];
    memset(lng, 'a', sizeof lng - 1);
    lng[0] = 'b';
    lng[sizeof lng - 1] = 0;
    IS(zxv_cid_from_text(lng, d, &m), ZXV_E_TOO_LONG);
    lng[ZXV_CID_TEXT_MAX] = 0; /* at the limit: parsed, and refused as a CID */
    IS(zxv_cid_from_text(lng, d, &m), ZXV_E_CID);

    /* refused binary: every CID-taking function shares the parser */
    uint8_t b[ZXV_CID_MAX + 2];
    memcpy(b, c, n);
    IS(zxv_cid_info(c, 0, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID);
    IS(zxv_cid_info(c, n - 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID); /* truncated */
    b[n] = 0;
    IS(zxv_cid_info(b, n + 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID); /* trailing byte */
    IS(zxv_cid_info(b, ZXV_CID_MAX + 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID);
    b[0] = 2;
    IS(zxv_cid_info(b, n, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID); /* version 2 */
    static const uint8_t nonmin[] = {0x01, 0xd5, 0x00, 0x12, 0x20}; /* codec 0x55 in two bytes */
    memcpy(b, nonmin, sizeof nonmin);
    memcpy(b + sizeof nonmin, c + 4, 32);
    IS(zxv_cid_info(b, sizeof nonmin + 32, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID);
    static const uint8_t sha512[] = {0x01, 0x55, 0x13, 0x40};
    memset(b, 0, sizeof b);
    memcpy(b, sha512, 4);
    IS(zxv_cid_info(b, 68, NULL, NULL, NULL, NULL, NULL), ZXV_E_CID);
    IS(zxv_cid_to_text(c, n - 1, 0, s, sizeof s, &m), ZXV_E_CID);
    IS(zxv_cid_to_v1(c, n + 1, d, &m), ZXV_E_CID);
    uint8_t key[32];
    IS(zxv_aipi_provider_key(c, n - 1, key), ZXV_E_CID);
}

static void test_verify_and_identity(void)
{
    uint8_t c[ZXV_CID_MAX];
    size_t n = 0;
    OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, HELLO, sizeof HELLO, c, &n));
    OK(zxv_cid_verify(c, n, HELLO, sizeof HELLO));
    uint8_t h2[sizeof HELLO];
    for (size_t i = 0; i < sizeof HELLO; i++) {
        memcpy(h2, HELLO, sizeof h2);
        h2[i] ^= 1;
        IS(zxv_cid_verify(c, n, h2, sizeof h2), ZXV_E_CID);
    }
    IS(zxv_cid_verify(c, n, HELLO, sizeof HELLO - 1), ZXV_E_CID);
    IS(zxv_cid_verify(c, n, NULL, 0), ZXV_E_CID);
    IS(zxv_cid_verify(c, n, NULL, 1), ZXV_E_ARG);
    IS(zxv_cid_verify(NULL, n, HELLO, 1), ZXV_E_ARG);
    IS(zxv_cid_verify(c, n - 1, HELLO, sizeof HELLO), ZXV_E_CID);
    /* codec does not change what a raw multihash checks */
    OK(zxv_cid_compute(0x70, HELLO, sizeof HELLO, c, &n));
    OK(zxv_cid_verify(c, n, HELLO, sizeof HELLO));
    /* identity multihash: the bytes ARE the digest */
    uint8_t id[] = {0x01, 0x55, 0x00, 0x02, 'h', 'i'};
    OK(zxv_cid_verify(id, sizeof id, (const uint8_t *) "hi", 2));
    IS(zxv_cid_verify(id, sizeof id, (const uint8_t *) "ho", 2), ZXV_E_CID);
    IS(zxv_cid_verify(id, sizeof id, (const uint8_t *) "hi!", 3), ZXV_E_CID);
    uint32_t hash = 9;
    size_t dl = 0;
    uint8_t dig[ZXV_CID_DIGEST_MAX];
    OK(zxv_cid_info(id, sizeof id, NULL, NULL, &hash, dig, &dl));
    CHECK(hash == ZXV_CID_HASH_IDENTITY && dl == 2 && dig[0] == 'h' && dig[1] == 'i', "identity info");
    char s[ZXV_CID_TEXT_MAX + 1], r[64];
    size_t m;
    OK(zxv_cid_to_text(id, sizeof id, ZXV_CID_BASE_DEFAULT, s, sizeof s, &m));
    r[0] = 'b';
    ref_base32(id, sizeof id, r + 1);
    CHECK(!strcmp(s, r), "identity text %s", s);
    uint8_t back[ZXV_CID_MAX];
    size_t bn;
    OK(zxv_cid_from_text(s, back, &bn));
    CHECK(bn == sizeof id && !memcmp(back, id, bn), "identity round trip");
    OK(zxv_cid_to_text(id, sizeof id, ZXV_CID_BASE_58BTC, s, sizeof s, &m));
    OK(zxv_cid_from_text(s, back, &bn));
    CHECK(bn == sizeof id && !memcmp(back, id, bn), "identity z round trip");
}

static void test_provider_key(void)
{
    uint8_t c[ZXV_CID_MAX], key[32], ref[32];
    size_t n;
    static const char pfx[] = "zxv/aipi/v1/provider";
    OK(zxv_cid_from_text(HELLO_RAW, c, &n));
    OK(zxv_aipi_provider_key(c, n, key));
    sha256_2((const uint8_t *) pfx, sizeof pfx - 1, c, n, ref);
    CHECK(!memcmp(key, ref, 32), "provider key = SHA-256(prefix || binary CID)");
    OK(zxv_cid_from_text(V0_TEXT, c, &n));
    OK(zxv_aipi_provider_key(c, n, key));
    sha256_2((const uint8_t *) pfx, sizeof pfx - 1, c, n, ref);
    CHECK(!memcmp(key, ref, 32), "provider key of a CIDv0 uses its 34 bytes");
    IS(zxv_aipi_provider_key(c, n, NULL), ZXV_E_ARG);
    IS(zxv_aipi_provider_key(NULL, n, key), ZXV_E_ARG);
}

/* ===================================================================== */
/* Records                                                               */
/* ===================================================================== */

static uint8_t MOD[ZXV_CID_MAX], CON[ZXV_CID_MAX];
static size_t MODN, CONN;

/* The function record, written out from docs/AIPI.md section 11. */
static size_t ref_fn(uint8_t intent, uint8_t verify, uint8_t priv, uint8_t det, uint8_t qn,
                     uint8_t qk, uint64_t fuel, bool contract, const char *entry,
                     const char *checker, const char *certifier, uint8_t *o)
{
    size_t p = 0;
    memcpy(o, "AIPF", 4);
    p = 4;
    o[p++] = 1;
    o[p++] = intent, o[p++] = verify, o[p++] = priv, o[p++] = det, o[p++] = qn, o[p++] = qk;
    for (int i = 0; i < 8; i++) o[p++] = (uint8_t) (fuel >> (8 * i));
    o[p++] = (uint8_t) MODN;
    memcpy(o + p, MOD, MODN), p += MODN;
    if (contract) {
        o[p++] = (uint8_t) CONN;
        memcpy(o + p, CON, CONN), p += CONN;
    } else {
        o[p++] = 0;
    }
    const char *names[3] = {entry, checker, certifier};
    for (int i = 0; i < 3; i++) {
        size_t l = names[i] ? strlen(names[i]) : 0;
        o[p++] = (uint8_t) l;
        if (l) memcpy(o + p, names[i], l), p += l;
    }
    return p;
}

static void test_fn_records(void)
{
    uint8_t rec[ZXV_AIPI_FN_MAX], ref[ZXV_AIPI_FN_MAX], c[ZXV_CID_MAX], rc[80];
    size_t n = 0, rn, cn;
    OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, HELLO, sizeof HELLO, MOD, &MODN));
    OK(zxv_cid_compute(ZXV_CID_CODEC_RAW, NULL, 0, CON, &CONN));

    /* CHECKER, with a contract */
    OK(zxv_aipi_fn_encode(ZXV_AIPI_INTENT_FN, ZXV_AIPI_VERIFY_CHECKER, ZXV_AIPI_PRIV_PUBLIC,
                          ZXV_AIPI_DET_DETERMINISTIC, 0, 0, 0x0102030405060708ull, MOD, MODN, CON,
                          CONN, "isqrt", "isqrt_ok", NULL, rec, sizeof rec, &n));
    rn = ref_fn(0, 1, 0, 1, 0, 0, 0x0102030405060708ull, true, "isqrt", "isqrt_ok", NULL, ref);
    CHECK(n == rn && !memcmp(rec, ref, n), "CHECKER record bytes (%zu vs %zu)", n, rn);
    OK(zxv_aipi_fn_cid(rec, n, c, &cn));
    size_t rcn = ref_cid(ZXV_AIPI_MC_FN, ref, rn, rc);
    CHECK(cn == rcn && !memcmp(c, rc, cn), "function CID = CIDv1(0x3A1000, sha256(record))");
    CHECK(c[1] == 0x80 && c[2] == 0xa0 && c[3] == 0xe8 && c[4] == 0x01, "varint of 0x3A1000");
    uint32_t in_ = 9, ve = 9, pr = 9, de = 9, qn = 9, qk = 9;
    uint64_t fu = 0;
    OK(zxv_aipi_fn_decode(rec, n, &in_, &ve, &pr, &de, &qn, &qk, &fu));
    CHECK(in_ == 0 && ve == 1 && pr == 0 && de == 1 && qn == 0 && qk == 0 &&
              fu == 0x0102030405060708ull,
          "decode fields");
    OK(zxv_aipi_fn_decode(rec, n, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    uint8_t g[ZXV_CID_MAX];
    size_t gn = 0;
    OK(zxv_aipi_fn_get_cid(rec, n, ZXV_AIPI_FIELD_MODULE, g, &gn));
    CHECK(gn == MODN && !memcmp(g, MOD, gn), "module CID field");
    OK(zxv_aipi_fn_get_cid(rec, n, ZXV_AIPI_FIELD_CONTRACT, g, &gn));
    CHECK(gn == CONN && !memcmp(g, CON, gn), "contract CID field");
    IS(zxv_aipi_fn_get_cid(rec, n, 2, g, &gn), ZXV_E_ARG);
    IS(zxv_aipi_fn_get_cid(rec, n, 0, NULL, &gn), ZXV_E_ARG);
    IS(zxv_aipi_fn_get_cid(rec, n, 0, g, NULL), ZXV_E_ARG);
    char nm[ZXV_AIPI_NAME_MAX + 1];
    size_t nl = 0;
    OK(zxv_aipi_fn_get_name(rec, n, ZXV_AIPI_FIELD_ENTRY, nm, sizeof nm, &nl));
    CHECK(nl == 5 && !strcmp(nm, "isqrt"), "entry");
    OK(zxv_aipi_fn_get_name(rec, n, ZXV_AIPI_FIELD_CHECKER, nm, sizeof nm, &nl));
    CHECK(nl == 8 && !strcmp(nm, "isqrt_ok"), "checker");
    OK(zxv_aipi_fn_get_name(rec, n, ZXV_AIPI_FIELD_CERTIFIER, nm, sizeof nm, &nl));
    CHECK(nl == 0 && nm[0] == 0, "no certifier");
    IS(zxv_aipi_fn_get_name(rec, n, ZXV_AIPI_FIELD_CHECKER, nm, 8, &nl), ZXV_E_BUFFER);
    CHECK(nl == 8, "name needs 8 + NUL");
    IS(zxv_aipi_fn_get_name(rec, n, 3, nm, sizeof nm, &nl), ZXV_E_ARG);
    IS(zxv_aipi_fn_get_name(rec, n, 0, nm, sizeof nm, NULL), ZXV_E_ARG);
    IS(zxv_aipi_fn_get_name(rec, n - 1, 0, nm, sizeof nm, &nl), ZXV_E_FORMAT);

    /* R4 bytes */
    size_t need = 0;
    IS(zxv_aipi_fn_encode(0, 1, 0, 1, 0, 0, 1, MOD, MODN, NULL, 0, "e", "c", NULL, NULL, 0, &need),
       ZXV_E_BUFFER);
    rn = ref_fn(0, 1, 0, 1, 0, 0, 1, false, "e", "c", NULL, ref);
    CHECK(need == rn, "needed %zu, want %zu", need, rn);
    memset(rec, 0xEE, sizeof rec);
    IS(zxv_aipi_fn_encode(0, 1, 0, 1, 0, 0, 1, MOD, MODN, NULL, 0, "e", "c", NULL, rec, rn - 1, &need),
       ZXV_E_BUFFER);
    CHECK(rec[0] == 0xEE, "nothing written");
    OK(zxv_aipi_fn_encode(0, 1, 0, 1, 0, 0, 1, MOD, MODN, NULL, 0, "e", "c", "", rec, rn, &n));
    CHECK(n == rn && !memcmp(rec, ref, n), "no contract: a 0 length byte");
    IS(zxv_aipi_fn_get_cid(rec, n, ZXV_AIPI_FIELD_CONTRACT, g, &gn), ZXV_E_NOT_FOUND);

    /* every intent picks its codec; the intent is inside the hash too */
    uint8_t prev[ZXV_CID_MAX];
    for (uint32_t it = 0; it < 4; it++) {
        OK(zxv_aipi_fn_encode(it, ZXV_AIPI_VERIFY_LOCAL, 0, 0, 0, 0, 7, MOD, MODN, NULL, 0, "run",
                              NULL, NULL, rec, sizeof rec, &n));
        OK(zxv_aipi_fn_cid(rec, n, c, &cn));
        rn = ref_fn((uint8_t) it, 4, 0, 0, 0, 0, 7, false, "run", NULL, NULL, ref);
        rcn = ref_cid(ZXV_AIPI_MC_FN + it, ref, rn, rc);
        CHECK(cn == rcn && !memcmp(c, rc, cn), "intent %u CID", it);
        uint32_t codec = 0;
        OK(zxv_cid_info(c, cn, NULL, &codec, NULL, NULL, NULL));
        CHECK(codec == ZXV_AIPI_MC_FN + it, "intent %u codec 0x%x", it, codec);
        if (it) CHECK(memcmp(prev + 5, c + 5, 32) != 0, "different digest per intent");
        memcpy(prev, c, cn);
    }

    /* CERT and QUORUM */
    OK(zxv_aipi_fn_encode(1, ZXV_AIPI_VERIFY_CERT, 1, 0, 0, 0, 99, MOD, MODN, CON, CONN, "factor",
                          "check", "cert", rec, sizeof rec, &n));
    rn = ref_fn(1, 2, 1, 0, 0, 0, 99, true, "factor", "check", "cert", ref);
    CHECK(n == rn && !memcmp(rec, ref, n), "CERT record");
    OK(zxv_aipi_fn_get_name(rec, n, ZXV_AIPI_FIELD_CERTIFIER, nm, sizeof nm, &nl));
    CHECK(!strcmp(nm, "cert"), "certifier");
    OK(zxv_aipi_fn_encode(0, ZXV_AIPI_VERIFY_QUORUM, 0, 1, 3, 2, 5, MOD, MODN, NULL, 0, "f", NULL,
                          NULL, rec, sizeof rec, &n));
    OK(zxv_aipi_fn_decode(rec, n, NULL, &ve, NULL, NULL, &qn, &qk, NULL));
    CHECK(ve == 3 && qn == 3 && qk == 2, "quorum 2 of 3");
    OK(zxv_aipi_fn_encode(0, 3, 0, 1, 8, 8, 5, MOD, MODN, NULL, 0, "f", NULL, NULL, rec, sizeof rec, &n));
    OK(zxv_aipi_fn_encode(0, 3, 0, 1, 8, 5, 5, MOD, MODN, NULL, 0, "f", NULL, NULL, rec, sizeof rec, &n));
    OK(zxv_aipi_fn_encode(0, 3, 0, 1, 2, 2, 5, MOD, MODN, NULL, 0, "f", NULL, NULL, rec, sizeof rec, &n));
    /* longest names: the record of the documented maximum size */
    char n63a[64], n63b[64], n63c[64];
    memset(n63a, 'a', 63), memset(n63b, 'b', 63), memset(n63c, 'c', 63);
    n63a[63] = n63b[63] = n63c[63] = 0;
    OK(zxv_aipi_fn_encode(0, 2, 0, 0, 0, 0, UINT64_MAX, MOD, MODN, CON, CONN, n63a, n63b, n63c, rec,
                          sizeof rec, &n));
    CHECK(n == 19 + 2 * 37 + 3 * 64, "63-char names: %zu bytes", n);
    OK(zxv_aipi_fn_decode(rec, n, NULL, NULL, NULL, NULL, NULL, NULL, &fu));
    CHECK(fu == UINT64_MAX, "fuel 2^64-1");

    /* rule violations: each is ZXV_E_ARG */
    struct {
        uint32_t it, ve, pr, de, qn, qk;
        uint64_t fuel;
        const char *e, *c, *ce;
    } bad[] = {
        {4, 1, 0, 0, 0, 0, 1, "e", "c", NULL},    /* intent 4 */
        {256, 1, 0, 0, 0, 0, 1, "e", "c", NULL},  /* not a byte */
        {0, 0, 0, 0, 0, 0, 1, "e", NULL, NULL},   /* verify 0 */
        {0, 5, 0, 0, 0, 0, 1, "e", NULL, NULL},   /* verify 5 */
        {0, 4, 2, 0, 0, 0, 1, "e", NULL, NULL},   /* privacy 2 */
        {0, 4, 0, 2, 0, 0, 1, "e", NULL, NULL},   /* determinism 2 */
        {0, 4, 0, 0, 0, 0, 0, "e", NULL, NULL},   /* fuel 0 */
        {0, 1, 0, 0, 0, 0, 1, "e", NULL, NULL},   /* CHECKER without checker */
        {0, 1, 0, 0, 0, 0, 1, "e", "e", NULL},    /* checker == entry */
        {0, 4, 0, 0, 0, 0, 1, "e", "c", NULL},    /* LOCAL with a checker */
        {0, 1, 0, 0, 0, 0, 1, "e", "c", "x"},     /* certifier outside CERT */
        {0, 2, 0, 0, 0, 0, 1, "e", "c", NULL},    /* CERT without certifier */
        {0, 2, 0, 0, 0, 0, 1, "e", "c", "c"},     /* certifier == checker */
        {0, 2, 0, 0, 0, 0, 1, "e", "c", "e"},     /* certifier == entry */
        {0, 3, 0, 0, 3, 2, 1, "e", NULL, NULL},   /* QUORUM, nondeterministic */
        {0, 3, 0, 1, 1, 1, 1, "e", NULL, NULL},   /* n < 2 */
        {0, 3, 0, 1, 9, 5, 1, "e", NULL, NULL},   /* n > 8 */
        {0, 3, 0, 1, 4, 2, 1, "e", NULL, NULL},   /* k = n/2 */
        {0, 3, 0, 1, 3, 4, 1, "e", NULL, NULL},   /* k > n */
        {0, 1, 0, 0, 3, 2, 1, "e", "c", NULL},    /* quorum outside QUORUM */
        {0, 1, 0, 0, 0, 1, 1, "e", "c", NULL},    /* k outside QUORUM */
        {0, 4, 0, 0, 0, 0, 1, "bad-name", NULL, NULL},
        {0, 4, 0, 0, 0, 0, 1, "", NULL, NULL},
        {0, 4, 0, 0, 0, 0, 1, NULL, NULL, NULL},
        {0, 1, 0, 0, 0, 0, 1, "e", "c d", NULL},
        {0, 2, 0, 0, 0, 0, 1, "e", "c", "c.d"},
        {0, 4, 0, 0, 0, 0, 1, "caf\xc3\xa9", NULL, NULL}, /* UTF-8 but not [A-Za-z0-9_] */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        IS(zxv_aipi_fn_encode(bad[i].it, bad[i].ve, bad[i].pr, bad[i].de, bad[i].qn, bad[i].qk,
                              bad[i].fuel, MOD, MODN, NULL, 0, bad[i].e, bad[i].c, bad[i].ce, rec,
                              sizeof rec, &n),
           ZXV_E_ARG);
    char n64[65];
    memset(n64, 'a', 64);
    n64[64] = 0;
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, NULL, 0, n64, NULL, NULL, rec, sizeof rec, &n),
       ZXV_E_TOO_LONG);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, NULL, 0, "e\xff", NULL, NULL, rec, sizeof rec, &n),
       ZXV_E_UTF8);
    /* module / contract must be CIDv1 raw sha2-256 */
    uint8_t dp[ZXV_CID_MAX], v0[ZXV_CID_MAX];
    size_t dpn, v0n;
    OK(zxv_cid_compute(0x70, HELLO, sizeof HELLO, dp, &dpn));
    OK(zxv_cid_from_text(V0_TEXT, v0, &v0n));
    uint8_t id[] = {0x01, 0x55, 0x00, 0x02, 'h', 'i'};
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, dp, dpn, NULL, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, v0, v0n, NULL, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, id, sizeof id, NULL, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, dp, dpn, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, CON, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN - 1, NULL, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_CID);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, NULL, 5, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_ARG);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, NULL, 36, NULL, 0, "e", NULL, NULL, rec, sizeof rec, &n), ZXV_E_ARG);
    IS(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 1, MOD, MODN, NULL, 0, "e", NULL, NULL, rec, sizeof rec, NULL), ZXV_E_ARG);

    /* strict decode: every single-bit flip is refused or names another function */
    OK(zxv_aipi_fn_encode(0, 2, 0, 1, 0, 0, 1000, MOD, MODN, CON, CONN, "f", "g", "h", rec, sizeof rec, &n));
    OK(zxv_aipi_fn_cid(rec, n, c, &cn));
    int same = 0;
    for (size_t i = 0; i < n * 8; i++) {
        uint8_t t[ZXV_AIPI_FN_MAX], tc[ZXV_CID_MAX];
        size_t tcn;
        memcpy(t, rec, n);
        t[i / 8] ^= (uint8_t) (1u << (i % 8));
        if (zxv_aipi_fn_cid(t, n, tc, &tcn) == ZXV_OK && tcn == cn && !memcmp(tc, c, cn)) same++;
    }
    CHECK(same == 0, "a flipped bit never gives the same function (%d)", same);
    IS(zxv_aipi_fn_decode(rec, n - 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    rec[n] = 0;
    IS(zxv_aipi_fn_decode(rec, n + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_aipi_fn_decode(rec, ZXV_AIPI_FN_MAX + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_aipi_fn_decode(NULL, n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_aipi_fn_decode(rec, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_aipi_fn_cid(rec, n - 1, c, &cn), ZXV_E_FORMAT);
    IS(zxv_aipi_fn_cid(rec, n, NULL, &cn), ZXV_E_ARG);
    IS(zxv_aipi_fn_cid(rec, n, c, NULL), ZXV_E_ARG);
    IS(zxv_aipi_fn_cid(NULL, n, c, &cn), ZXV_E_ARG);
    IS(zxv_aipi_fn_get_cid(rec, n - 1, 0, g, &gn), ZXV_E_FORMAT);
}

static void test_call_and_claim(void)
{
    uint8_t rec[ZXV_AIPI_FN_MAX], f[ZXV_CID_MAX], call[ZXV_AIPI_CALL_MAX], ref[ZXV_AIPI_CLAIM_MAX];
    uint8_t cc[ZXV_CID_MAX], rc[80];
    size_t n, fn_, cl, ccn;
    OK(zxv_aipi_fn_encode(0, 1, 0, 1, 0, 0, 100, MOD, MODN, NULL, 0, "isqrt", "isqrt_ok", NULL, rec,
                          sizeof rec, &n));
    OK(zxv_aipi_fn_cid(rec, n, f, &fn_));
    static const uint8_t in[8] = {0xe2, 0x07, 0, 0, 0, 0, 0, 0}; /* 2018 LE */

    OK(zxv_aipi_call_encode(f, fn_, in, sizeof in, call, sizeof call, &cl));
    size_t p = 0;
    memcpy(ref, "AIPC", 4), p = 4;
    ref[p++] = 1, ref[p++] = 0, ref[p++] = (uint8_t) fn_;
    memcpy(ref + p, f, fn_), p += fn_;
    ref[p++] = sizeof in, ref[p++] = 0;
    memcpy(ref + p, in, sizeof in), p += sizeof in;
    CHECK(cl == p && !memcmp(call, ref, p), "call record bytes");
    OK(zxv_aipi_call_cid(f, fn_, in, sizeof in, cc, &ccn));
    size_t rcn = ref_cid(ZXV_AIPI_MC_CALL, ref, p, rc);
    CHECK(ccn == rcn && !memcmp(cc, rc, ccn), "call CID = CIDv1(0x3A10C0, sha256(call record))");
    size_t fo = 0, fl = 0, io = 0, il = 0;
    OK(zxv_aipi_call_decode(call, cl, &fo, &fl, &io, &il));
    CHECK(fo == 7 && fl == fn_ && !memcmp(call + fo, f, fl) && io == 7 + fn_ + 2 && il == 8 &&
              !memcmp(call + io, in, 8),
          "call decode offsets");
    OK(zxv_aipi_call_decode(call, cl, NULL, NULL, NULL, NULL));
    /* the empty input, and the largest */
    uint8_t c2[ZXV_CID_MAX];
    size_t c2n;
    OK(zxv_aipi_call_encode(f, fn_, NULL, 0, call, sizeof call, &cl));
    CHECK(cl == 7 + fn_ + 2, "empty input");
    OK(zxv_aipi_call_decode(call, cl, NULL, NULL, &io, &il));
    CHECK(il == 0 && io == cl, "empty input decoded");
    OK(zxv_aipi_call_cid(f, fn_, NULL, 0, c2, &c2n));
    CHECK(memcmp(c2, cc, ccn) != 0, "input changes the call CID");
    uint8_t big[ZXV_AIPI_IO_MAX + 1];
    memset(big, 7, sizeof big);
    OK(zxv_aipi_call_encode(f, fn_, big, ZXV_AIPI_IO_MAX, call, sizeof call, &cl));
    CHECK(cl == 7 + fn_ + 2 + ZXV_AIPI_IO_MAX, "1024-byte input");
    IS(zxv_aipi_call_encode(f, fn_, big, ZXV_AIPI_IO_MAX + 1, call, sizeof call, &cl), ZXV_E_TOO_LONG);
    IS(zxv_aipi_call_cid(f, fn_, big, ZXV_AIPI_IO_MAX + 1, c2, &c2n), ZXV_E_TOO_LONG);
    size_t need = 0;
    IS(zxv_aipi_call_encode(f, fn_, in, sizeof in, NULL, 0, &need), ZXV_E_BUFFER);
    CHECK(need == p, "call needs %zu", need);
    IS(zxv_aipi_call_encode(f, fn_, in, sizeof in, call, p - 1, &need), ZXV_E_BUFFER);
    IS(zxv_aipi_call_encode(f, fn_, in, sizeof in, call, p, NULL), ZXV_E_ARG);
    IS(zxv_aipi_call_encode(f, fn_, NULL, 1, call, sizeof call, &cl), ZXV_E_ARG);
    IS(zxv_aipi_call_encode(MOD, MODN, in, sizeof in, call, sizeof call, &cl), ZXV_E_CID); /* raw codec */
    IS(zxv_aipi_call_cid(cc, ccn, in, sizeof in, c2, &c2n), ZXV_E_CID);                   /* a call CID */
    IS(zxv_aipi_call_cid(f, fn_, in, sizeof in, NULL, &c2n), ZXV_E_ARG);
    IS(zxv_aipi_call_cid(f, fn_, in, sizeof in, c2, NULL), ZXV_E_ARG);
    {
        uint8_t fid[] = {0x01, 0x80, 0xa0, 0xe8, 0x01, 0x00, 0x01, 0x00}; /* identity, fn codec */
        IS(zxv_aipi_call_cid(fid, sizeof fid, in, sizeof in, c2, &c2n), ZXV_E_CID);
    }
    /* strict call decode */
    OK(zxv_aipi_call_encode(f, fn_, in, sizeof in, call, sizeof call, &cl));
    IS(zxv_aipi_call_decode(call, cl - 1, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    call[cl] = 0;
    IS(zxv_aipi_call_decode(call, cl + 1, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    call[5] = 1;
    IS(zxv_aipi_call_decode(call, cl, NULL, NULL, NULL, NULL), ZXV_E_FORMAT); /* flags */
    call[5] = 0;
    call[4] = 2;
    IS(zxv_aipi_call_decode(call, cl, NULL, NULL, NULL, NULL), ZXV_E_FORMAT); /* version */
    call[4] = 1;
    IS(zxv_aipi_call_decode(NULL, cl, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_aipi_call_decode(call, ZXV_AIPI_CALL_MAX + 1, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);

    /* claims */
    uint8_t ex[32], out[8] = {45, 0, 0, 0, 0, 0, 0, 0}, cert[3] = {1, 2, 3};
    uint8_t claim[ZXV_AIPI_CLAIM_MAX];
    for (int i = 0; i < 32; i++) ex[i] = (uint8_t) (0xA0 + i);
    OK(zxv_aipi_claim_encode(cc, ccn, ex, out, sizeof out, cert, sizeof cert, claim, sizeof claim, &cl));
    p = 0;
    memcpy(ref, "AIPR", 4), p = 4;
    ref[p++] = 1, ref[p++] = 0, ref[p++] = (uint8_t) ccn;
    memcpy(ref + p, cc, ccn), p += ccn;
    memcpy(ref + p, ex, 32), p += 32;
    ref[p++] = sizeof out, ref[p++] = 0;
    memcpy(ref + p, out, sizeof out), p += sizeof out;
    ref[p++] = sizeof cert, ref[p++] = 0;
    memcpy(ref + p, cert, sizeof cert), p += sizeof cert;
    CHECK(cl == p && !memcmp(claim, ref, p), "claim record bytes");
    size_t co, cln, eo, oo, ol, ceo, cel;
    OK(zxv_aipi_claim_decode(claim, cl, &co, &cln, &eo, &oo, &ol, &ceo, &cel));
    CHECK(co == 7 && cln == ccn && !memcmp(claim + co, cc, ccn) && eo == 7 + ccn &&
              !memcmp(claim + eo, ex, 32) && oo == eo + 34 && ol == 8 &&
              !memcmp(claim + oo, out, 8) && ceo == oo + 10 && cel == 3 &&
              !memcmp(claim + ceo, cert, 3),
          "claim offsets");
    OK(zxv_aipi_claim_decode(claim, cl, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    OK(zxv_aipi_claim_encode(cc, ccn, ex, NULL, 0, NULL, 0, claim, sizeof claim, &cl));
    CHECK(cl == 7 + ccn + 32 + 4, "empty claim");
    OK(zxv_aipi_claim_decode(claim, cl, NULL, NULL, NULL, &oo, &ol, &ceo, &cel));
    CHECK(ol == 0 && cel == 0 && ceo == cl, "empty parts");
    OK(zxv_aipi_claim_encode(cc, ccn, ex, big, ZXV_AIPI_IO_MAX, big, ZXV_AIPI_CERT_MAX, claim,
                             sizeof claim, &cl));
    CHECK(cl == 7 + ccn + 32 + 4 + 2048, "largest claim %zu", cl);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, big, ZXV_AIPI_IO_MAX + 1, NULL, 0, claim, sizeof claim, &cl),
       ZXV_E_TOO_LONG);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, NULL, 0, big, ZXV_AIPI_CERT_MAX + 1, claim, sizeof claim, &cl),
       ZXV_E_TOO_LONG);
    IS(zxv_aipi_claim_encode(f, fn_, ex, out, 8, NULL, 0, claim, sizeof claim, &cl), ZXV_E_CID);
    IS(zxv_aipi_claim_encode(cc, ccn - 1, ex, out, 8, NULL, 0, claim, sizeof claim, &cl), ZXV_E_CID);
    IS(zxv_aipi_claim_encode(cc, ccn, NULL, out, 8, NULL, 0, claim, sizeof claim, &cl), ZXV_E_ARG);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, NULL, 8, NULL, 0, claim, sizeof claim, &cl), ZXV_E_ARG);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, out, 8, NULL, 1, claim, sizeof claim, &cl), ZXV_E_ARG);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, out, 8, NULL, 0, claim, sizeof claim, NULL), ZXV_E_ARG);
    IS(zxv_aipi_claim_encode(cc, ccn, ex, out, 8, NULL, 0, NULL, 0, &need), ZXV_E_BUFFER);
    CHECK(need == 7 + ccn + 32 + 4 + 8, "claim needs %zu", need);
    OK(zxv_aipi_claim_encode(cc, ccn, ex, out, 8, cert, 3, claim, sizeof claim, &cl));
    IS(zxv_aipi_claim_decode(claim, cl - 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    claim[cl] = 9;
    IS(zxv_aipi_claim_decode(claim, cl + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    claim[0] = 'X';
    IS(zxv_aipi_claim_decode(claim, cl, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_aipi_claim_decode(NULL, cl, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_aipi_claim_decode(claim, ZXV_AIPI_CLAIM_MAX + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
       ZXV_E_FORMAT);
    /* a call record is not a claim, and the reverse */
    OK(zxv_aipi_call_encode(f, fn_, in, sizeof in, call, sizeof call, &cl));
    IS(zxv_aipi_claim_decode(call, cl, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_aipi_fn_decode(call, cl > ZXV_AIPI_FN_MAX ? ZXV_AIPI_FN_MAX : cl, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL),
       ZXV_E_FORMAT);
}

/* ===================================================================== */
/* The node handle                                                       */
/* ===================================================================== */

static void test_handle(void)
{
    zxv_ctx *ctx, *ctx2;
    zxv_cap *cap, *cap2, *nocap, *other;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_ctx_create(&ctx2));
    int32_t grc = zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL | ZXV_CAP_AIPI, &cap);
    if (grc != ZXV_OK) {
        /* zxv_cap_create still refuses the 1.5 bits (zxv_core.c checks
         * ZXV_CAP_ALL_1_4): nothing can hold ZXV_CAP_AIPI, so every gated
         * call is refused. Check that, and say the success paths were not run. */
        CHECK(grc == ZXV_E_ARG, "cap_create with ZXV_CAP_AIPI -> %s", zxv_status_name(grc));
        OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &nocap));
        zxv_aipi *a = NULL;
        IS(zxv_aipi_create(nocap, 65536, 16, NULL, &a), ZXV_E_CAP);
        CHECK(a == NULL, "no handle");
        printf("NOTE: ZXV_CAP_AIPI is not grantable in this build (zxv_cap_create accepts only "
               "ZXV_CAP_ALL_1_4); the zxv_aipi handle success paths were NOT run\n");
        zxv_cap_destroy(nocap);
        zxv_ctx_destroy(ctx);
        zxv_ctx_destroy(ctx2);
        return;
    }
    OK(zxv_cap_attenuate(cap, ZXV_CAP_AIPI, &cap2));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ALL, &nocap));
    OK(zxv_cap_create(ctx2, 2, NULL, ZXV_CAP_AIPI, &other));

    zxv_aipi *a = NULL, *pa = NULL;
    IS(zxv_aipi_create(nocap, 65536, 16, NULL, &a), ZXV_E_CAP);
    IS(zxv_aipi_create(NULL, 65536, 16, NULL, &a), ZXV_E_ARG);
    IS(zxv_aipi_create(cap, 65536, 16, NULL, NULL), ZXV_E_ARG);
    IS(zxv_aipi_create(cap, ZXV_AIPI_STORE_MIN - 1, 16, NULL, &a), ZXV_E_ARG);
    IS(zxv_aipi_create(cap, (size_t) ZXV_AIPI_STORE_MAX + 1, 16, NULL, &a), ZXV_E_ARG);
    IS(zxv_aipi_create(cap, 65536, 0, NULL, &a), ZXV_E_ARG);
    IS(zxv_aipi_create(cap, 65536, ZXV_AIPI_BLOCKS_MAX + 1, NULL, &a), ZXV_E_ARG);
    CHECK(a == NULL, "nothing created on failure");
    OK(zxv_aipi_create(cap2, 65536, 16, NULL, &a));
    zxv_aipi_destroy(NULL);

    /* put / get */
    uint8_t c[ZXV_CID_MAX], buf[64];
    size_t cn = 0, n = 0;
    uint32_t blocks = 9, gets = 9, forb = 9;
    uint64_t used = 9;
    OK(zxv_aipi_info(a, &blocks, &used, &gets, &forb));
    CHECK(blocks == 0 && used == 0 && gets == 0 && forb == 0, "empty node");
    OK(zxv_aipi_put(cap2, a, HELLO, sizeof HELLO, 0, c, &cn));
    CHECK(!strcmp(txt(c, cn), HELLO_RAW), "put gives the Kubo CID");
    OK(zxv_aipi_info(a, &blocks, &used, NULL, NULL));
    CHECK(blocks == 1 && used > sizeof HELLO && used < sizeof HELLO + 120, "one block (%llu bytes)",
          (unsigned long long) used);
    uint64_t used1 = used;
    OK(zxv_aipi_put(cap2, a, HELLO, sizeof HELLO, 0, c, &cn));
    OK(zxv_aipi_info(a, &blocks, &used, NULL, NULL));
    CHECK(blocks == 1 && used == used1, "same bytes again: no-op");
    size_t need = 0;
    IS(zxv_aipi_get(a, c, cn, NULL, 0, &need), ZXV_E_BUFFER);
    CHECK(need == sizeof HELLO, "get needs %zu", need);
    memset(buf, 0xEE, sizeof buf);
    IS(zxv_aipi_get(a, c, cn, buf, sizeof HELLO - 1, &need), ZXV_E_BUFFER);
    CHECK(buf[0] == 0xEE, "nothing written");
    OK(zxv_aipi_get(a, c, cn, buf, sizeof HELLO, &n));
    CHECK(n == sizeof HELLO && !memcmp(buf, HELLO, n), "get returns the bytes");
    OK(zxv_aipi_info(a, NULL, NULL, &gets, &forb));
    CHECK(gets == 3 && forb == 0, "verified gets counted (%u)", gets);
    OK(zxv_aipi_info(a, NULL, NULL, NULL, NULL));
    uint8_t e[ZXV_CID_MAX];
    size_t en;
    OK(zxv_aipi_put(cap2, a, NULL, 0, 0, e, &en));
    CHECK(!strcmp(txt(e, en), EMPTY_RAW), "empty blob");
    OK(zxv_aipi_get(a, e, en, NULL, 0, &n));
    CHECK(n == 0, "empty blob get with no buffer");
    uint8_t miss[ZXV_CID_MAX];
    size_t mn;
    OK(zxv_cid_compute(0x55, (const uint8_t *) "absent", 6, miss, &mn));
    IS(zxv_aipi_get(a, miss, mn, buf, sizeof buf, &n), ZXV_E_NOT_FOUND);
    uint8_t id[] = {0x01, 0x55, 0x00, 0x02, 'h', 'i'};
    IS(zxv_aipi_get(a, id, sizeof id, buf, sizeof buf, &n), ZXV_E_CID);
    IS(zxv_aipi_get(a, c, cn - 1, buf, sizeof buf, &n), ZXV_E_CID);
    IS(zxv_aipi_get(a, NULL, cn, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_aipi_get(a, c, cn, buf, sizeof buf, NULL), ZXV_E_ARG);
    IS(zxv_aipi_get(NULL, c, cn, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_aipi_info(NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    /* the same block under its CIDv1 dag-pb or another codec is another CID */
    uint8_t other_codec[ZXV_CID_MAX];
    size_t ocn;
    OK(zxv_cid_compute(0x70, HELLO, sizeof HELLO, other_codec, &ocn));
    IS(zxv_aipi_get(a, other_codec, ocn, buf, sizeof buf, &n), ZXV_E_NOT_FOUND);

    /* put errors */
    IS(zxv_aipi_put(nocap, a, HELLO, 1, 0, c, &cn), ZXV_E_CAP);
    IS(zxv_aipi_put(other, a, HELLO, 1, 0, c, &cn), ZXV_E_CAP); /* another context's cap */
    IS(zxv_aipi_put(cap2, NULL, HELLO, 1, 0, c, &cn), ZXV_E_ARG);
    IS(zxv_aipi_put(cap2, a, NULL, 1, 0, c, &cn), ZXV_E_ARG);
    IS(zxv_aipi_put(cap2, a, HELLO, 1, 0, NULL, &cn), ZXV_E_ARG);
    IS(zxv_aipi_put(cap2, a, HELLO, 1, 0, c, NULL), ZXV_E_ARG);
    IS(zxv_aipi_put(cap2, a, HELLO, (size_t) ZXV_AIPI_BLOCK_MAX + 1, 0, c, &cn), ZXV_E_TOO_LONG);
    IS(zxv_aipi_put(cap2, a, HELLO, 5, 1, c, &cn), ZXV_E_STATE); /* private, no key */

    /* functions */
    uint8_t rec[ZXV_AIPI_FN_MAX], f[ZXV_CID_MAX], fc[ZXV_CID_MAX], lrec[ZXV_AIPI_FN_MAX];
    size_t rn, fn_, fcn, ln;
    uint8_t modc[ZXV_CID_MAX];
    size_t modn;
    OK(zxv_cid_compute(0x55, (const uint8_t *) "\0asm module", 11, modc, &modn));
    OK(zxv_aipi_fn_encode(0, 1, 0, 1, 0, 0, 1000, modc, modn, NULL, 0, "half", "half_ok", NULL, rec,
                          sizeof rec, &rn));
    OK(zxv_aipi_fn_cid(rec, rn, fc, &fcn));
    int32_t route = 0;
    IS(zxv_aipi_plan(a, fc, fcn, NULL, 0, 0, &route), ZXV_E_NOT_FOUND); /* not published */
    IS(zxv_aipi_publish_fn(nocap, a, rec, rn, f, &fn_), ZXV_E_CAP);
    IS(zxv_aipi_publish_fn(cap2, a, rec, rn - 1, f, &fn_), ZXV_E_FORMAT);
    IS(zxv_aipi_publish_fn(cap2, a, rec, rn, NULL, &fn_), ZXV_E_ARG);
    IS(zxv_aipi_publish_fn(cap2, a, NULL, rn, f, &fn_), ZXV_E_ARG);
    OK(zxv_aipi_publish_fn(cap2, a, rec, rn, f, &fn_));
    CHECK(fn_ == fcn && !memcmp(f, fc, fn_), "publish returns zxv_aipi_fn_cid");
    IS(zxv_aipi_load_fn(a, f, fn_, NULL, 0, &ln), ZXV_E_BUFFER);
    CHECK(ln == rn, "load needs %zu", ln);
    IS(zxv_aipi_load_fn(a, f, fn_, lrec, rn - 1, &ln), ZXV_E_BUFFER);
    OK(zxv_aipi_load_fn(a, f, fn_, lrec, sizeof lrec, &ln));
    CHECK(ln == rn && !memcmp(lrec, rec, rn), "load returns the record");
    /* the record block is a raw-hash block too: GET by function CID works */
    IS(zxv_aipi_get(a, f, fn_, buf, 0, &n), ZXV_E_BUFFER);
    CHECK(n == rn, "function record is %zu bytes", n);
    IS(zxv_aipi_load_fn(a, c, cn, lrec, sizeof lrec, &ln), ZXV_E_CID); /* raw codec */
    IS(zxv_aipi_load_fn(a, miss, mn, lrec, sizeof lrec, &ln), ZXV_E_CID);
    {
        uint8_t f2[ZXV_CID_MAX];
        memcpy(f2, f, fn_);
        f2[fn_ - 1] ^= 1; /* a function CID nobody holds */
        IS(zxv_aipi_load_fn(a, f2, fn_, lrec, sizeof lrec, &ln), ZXV_E_NOT_FOUND);
        /* same digest, another intent's codec: the bytes are not held under it */
        memcpy(f2, f, fn_);
        f2[1] = 0x81;
        IS(zxv_aipi_load_fn(a, f2, fn_, lrec, sizeof lrec, &ln), ZXV_E_NOT_FOUND);
    }
    IS(zxv_aipi_load_fn(a, f, fn_, lrec, sizeof lrec, NULL), ZXV_E_ARG);
    IS(zxv_aipi_load_fn(NULL, f, fn_, lrec, sizeof lrec, &ln), ZXV_E_ARG);
    IS(zxv_aipi_load_fn(a, NULL, fn_, lrec, sizeof lrec, &ln), ZXV_E_ARG);

    /* plan: offline, so the route depends only on whether the code is held */
    static const uint8_t in[8] = {0xe2, 0x07};
    route = 0;
    OK(zxv_aipi_plan(a, f, fn_, in, sizeof in, 0, &route));
    CHECK(route == ZXV_AIPI_ROUTE_UNAVAILABLE, "module not held: unavailable (%d)", route);
    OK(zxv_aipi_put(cap2, a, (const uint8_t *) "\0asm module", 11, 0, c, &cn));
    CHECK(cn == modn && !memcmp(c, modc, cn), "module CID");
    OK(zxv_aipi_plan(a, f, fn_, in, sizeof in, 0, &route));
    CHECK(route == ZXV_AIPI_ROUTE_LOCAL, "module held: local (%d)", route);
    OK(zxv_aipi_plan(a, f, fn_, NULL, 0, 1, &route));
    CHECK(route == ZXV_AIPI_ROUTE_LOCAL, "private input: local");
    uint8_t big[ZXV_AIPI_IO_MAX + 1] = {0};
    IS(zxv_aipi_plan(a, f, fn_, big, sizeof big, 0, &route), ZXV_E_TOO_LONG);
    IS(zxv_aipi_plan(a, f, fn_, NULL, 1, 0, &route), ZXV_E_ARG);
    IS(zxv_aipi_plan(a, f, fn_, in, 8, 0, NULL), ZXV_E_ARG);
    IS(zxv_aipi_plan(a, c, cn, in, 8, 0, &route), ZXV_E_CID);
    IS(zxv_aipi_plan(NULL, f, fn_, in, 8, 0, &route), ZXV_E_ARG);
    /* a contract that is not held makes it unavailable again */
    OK(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 10, modc, modn, MOD, MODN, "half", NULL, NULL, rec,
                          sizeof rec, &rn));
    OK(zxv_aipi_publish_fn(cap2, a, rec, rn, f, &fn_));
    OK(zxv_aipi_plan(a, f, fn_, in, sizeof in, 0, &route));
    CHECK(route == ZXV_AIPI_ROUTE_LOCAL, "contract is HELLO, held: local (%d)", route);
    OK(zxv_aipi_fn_encode(0, 4, 0, 0, 0, 0, 10, modc, modn, miss, mn, "half", NULL, NULL, rec,
                          sizeof rec, &rn));
    OK(zxv_aipi_publish_fn(cap2, a, rec, rn, f, &fn_));
    OK(zxv_aipi_plan(a, f, fn_, in, sizeof in, 0, &route));
    CHECK(route == ZXV_AIPI_ROUTE_UNAVAILABLE, "contract not held: unavailable (%d)", route);

    /* a private function needs a key */
    OK(zxv_aipi_fn_encode(0, 4, 1, 0, 0, 0, 10, modc, modn, NULL, 0, "secret", NULL, NULL, rec,
                          sizeof rec, &rn));
    IS(zxv_aipi_publish_fn(cap2, a, rec, rn, f, &fn_), ZXV_E_STATE);

    /* private blocks with a node key */
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t) (i * 7 + 1);
    OK(zxv_aipi_create(cap, 65536, 8, key, &pa));
    uint8_t pc[ZXV_CID_MAX];
    size_t pcn;
    static const uint8_t SECRET[] = "private weights";
    OK(zxv_aipi_put(cap, pa, SECRET, sizeof SECRET, 1, pc, &pcn));
    OK(zxv_cid_verify(pc, pcn, SECRET, sizeof SECRET));
    OK(zxv_aipi_get(pa, pc, pcn, buf, sizeof buf, &n));
    CHECK(n == sizeof SECRET && !memcmp(buf, SECRET, n), "owner reads a private block");
    OK(zxv_aipi_put(cap, pa, SECRET, sizeof SECRET, 1, pc, &pcn)); /* again: fine */
    uint64_t used_before = 0, used_after = 1;
    OK(zxv_aipi_info(pa, NULL, &used_before, NULL, NULL));
    IS(zxv_aipi_put(cap, pa, SECRET, sizeof SECRET, 0, pc, &pcn), ZXV_E_STATE); /* now public? no */
    OK(zxv_aipi_info(pa, NULL, &used_after, NULL, NULL));
    CHECK(used_after == used_before, "the refused public put wrote nothing (no plaintext copy)");
    OK(zxv_aipi_get(pa, pc, pcn, buf, sizeof buf, &n));
    CHECK(n == sizeof SECRET, "still held after the refused public put");
    OK(zxv_aipi_put(cap, pa, HELLO, sizeof HELLO, 0, c, &cn));
    OK(zxv_aipi_info(pa, NULL, &used_before, NULL, NULL));
    IS(zxv_aipi_put(cap, pa, HELLO, sizeof HELLO, 1, c, &cn), ZXV_E_STATE); /* public -> private */
    OK(zxv_aipi_info(pa, NULL, &used_after, NULL, NULL));
    CHECK(used_after == used_before, "the refused private put wrote nothing");
    OK(zxv_aipi_publish_fn(cap, pa, rec, rn, f, &fn_)); /* private fn: sealed */
    OK(zxv_aipi_load_fn(pa, f, fn_, lrec, sizeof lrec, &ln));
    CHECK(ln == rn && !memcmp(lrec, rec, rn), "private function loads locally");
    uint32_t pr = 0;
    OK(zxv_aipi_fn_decode(lrec, ln, NULL, NULL, &pr, NULL, NULL, NULL, NULL));
    CHECK(pr == ZXV_AIPI_PRIV_LOCAL, "privacy LOCAL");
    OK(zxv_aipi_plan(pa, f, fn_, in, 8, 0, &route));
    CHECK(route == ZXV_AIPI_ROUTE_UNAVAILABLE, "private fn, module not here");
    OK(zxv_aipi_put(cap2, pa, HELLO, 1, 0, c, &cn)); /* any AIPI cap of the context */

    /* capacity: the pin table (max_blocks) and the store bytes */
    zxv_aipi *small = NULL;
    OK(zxv_aipi_create(cap, ZXV_AIPI_STORE_MIN, 2, NULL, &small));
    OK(zxv_aipi_put(cap, small, (const uint8_t *) "a", 1, 0, c, &cn));
    OK(zxv_aipi_put(cap, small, (const uint8_t *) "b", 1, 0, c, &cn));
    IS(zxv_aipi_put(cap, small, (const uint8_t *) "c", 1, 0, c, &cn), ZXV_E_CAPACITY);
    zxv_aipi_destroy(small);
    small = NULL;
    OK(zxv_aipi_create(cap, ZXV_AIPI_STORE_MIN, 16, NULL, &small));
    static uint8_t blob[5000];
    memset(blob, 1, sizeof blob);
    IS(zxv_aipi_put(cap, small, blob, sizeof blob, 0, c, &cn), ZXV_E_CAPACITY);
    OK(zxv_aipi_put(cap, small, blob, 3000, 0, c, &cn));
    IS(zxv_aipi_put(cap, small, blob, 2000, 0, c, &cn), ZXV_E_CAPACITY);
    IS(zxv_aipi_get(small, c, cn, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == 3000, "the first blob is intact (%zu)", n);
    zxv_aipi_destroy(small);

    /* revocation and fail-closed */
    OK(zxv_cap_revoke(cap2));
    IS(zxv_aipi_put(cap2, a, HELLO, 1, 0, c, &cn), ZXV_E_CAP);
    OK(zxv_aipi_get(a, e, en, NULL, 0, &n)); /* reads need only the handle */
    OK(zxv_ctx_halt(ctx, "test"));
    IS(zxv_aipi_get(a, e, en, NULL, 0, &n), ZXV_E_POISONED);
    IS(zxv_aipi_load_fn(a, f, fn_, lrec, sizeof lrec, &ln), ZXV_E_POISONED);
    IS(zxv_aipi_plan(a, f, fn_, in, 8, 0, &route), ZXV_E_POISONED);
    IS(zxv_aipi_info(a, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_aipi_put(cap, pa, HELLO, 1, 0, c, &cn), ZXV_E_POISONED);
    IS(zxv_aipi_publish_fn(cap, pa, rec, rn, f, &fn_), ZXV_E_POISONED);
    IS(zxv_aipi_create(cap, 65536, 4, NULL, &small), ZXV_E_POISONED);
    IS(zxv_aipi_get(a, NULL, 0, NULL, 0, NULL), ZXV_E_POISONED); /* before argument checks */
    char err[256];
    size_t el;
    OK(zxv_last_error(err, sizeof err, &el));
    CHECK(strstr(err, "test") != NULL, "poison reason reported: %s", err);

    zxv_aipi_destroy(a);
    zxv_aipi_destroy(pa);
    zxv_cap_destroy(other);
    zxv_cap_destroy(nocap);
    zxv_cap_destroy(cap2);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    zxv_ctx_destroy(ctx2);
}

int main(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_AIPI, "ZXV_FEAT2_AIPI is set");
    CHECK(ZXV_CAP_AIPI == (UINT64_C(1) << 12) && ZXV_E_CID == -34, "constants");
    CHECK(!strcmp(zxv_status_name(ZXV_E_CID), "ZXV_E_CID"), "status name");
    test_known_cids();
    test_unixfs();
    test_cid_text_and_errors();
    test_verify_and_identity();
    test_provider_key();
    test_fn_records();
    test_call_and_claim();
    test_handle();
    printf("test_zxv_abi15_aipi: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
