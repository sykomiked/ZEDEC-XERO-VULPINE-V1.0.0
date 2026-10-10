/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_freight.c — host tests for freight packets: GF(256) axioms, the
 * Cauchy Reed-Solomon row code under erasures, corruption detection, header
 * serialisation, op-stream fuzzing, expansion limits, generators (known
 * answers from an independent Python reference), CIDREF, multi-freight
 * split/join, and honestly measured expansion ratios.
 *
 *   gcc -std=c11 -Wall -Werror -Wextra -O1 -g -fsanitize=address,undefined \
 *       -fno-sanitize-recover=all -Isrc/freight -Isrc/robin_debanks -Iinclude -Isrc/modbind \
 *       src/freight/test_freight.c src/freight/freight.c src/freight/freight_ops.c \
 *       src/robin_debanks/sha256.c -o /tmp/test_freight && /tmp/test_freight
 *
 * (run from kernel/; drop the sanitizer flags and use -O2 for a fast run)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freight.h"
#include "sha256.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* Silent check for inner loops: counts, prints only the first few failures. */
static int quiet_fail = 0;
#define QCHECK(c, m)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (quiet_fail++ < 10) printf("  [fail] %s (line %d)\n", m, __LINE__);                 \
        }                                                                                          \
    } while (0)

/* ---- deterministic test PRNG (splitmix64) ---- */
static uint64_t rng_s = 0x5A58564652454947ull;
static uint64_t rnd(void)
{
    uint64_t z = (rng_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}
static void rnd_fill(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) rnd();
}

static uint8_t work[FREIGHT_DECODE_WORK_BYTES];
static uint32_t enc_work[FREIGHT_ENC_WORK_WORDS];

/* ======================= GF(256) ======================= */

static uint8_t slow_mul(uint8_t a, uint8_t b)
{
    uint16_t r = 0, x = a;
    for (int i = 0; i < 8; i++) {
        if (b & (1 << i)) r ^= (uint16_t) (x << i);
    }
    for (int i = 15; i >= 8; i--)
        if (r & (1 << i)) r ^= (uint16_t) (0x11d << (i - 8));
    return (uint8_t) r;
}

static void test_gf(void)
{
    int bad_ref = 0, bad_comm = 0, bad_id = 0, bad_inv = 0, bad_div = 0;
    for (int a = 0; a < 256; a++) {
        if (freight_gf_mul((uint8_t) a, 1) != a || freight_gf_mul((uint8_t) a, 0) != 0) bad_id++;
        if (a) {
            uint8_t ia = freight_gf_inv((uint8_t) a);
            if (freight_gf_mul((uint8_t) a, ia) != 1) bad_inv++;
        }
        for (int b = 0; b < 256; b++) {
            uint8_t m = freight_gf_mul((uint8_t) a, (uint8_t) b);
            if (m != slow_mul((uint8_t) a, (uint8_t) b)) bad_ref++;
            if (m != freight_gf_mul((uint8_t) b, (uint8_t) a)) bad_comm++;
            if (b && freight_gf_div(m, (uint8_t) b) != a) bad_div++;
        }
    }
    CHECK(bad_ref == 0, "GF: table multiply equals carry-less reference mod 0x11d (all 65536)");
    CHECK(bad_comm == 0, "GF: multiplication commutes");
    CHECK(bad_id == 0, "GF: 1 is the identity, 0 annihilates");
    CHECK(bad_inv == 0, "GF: every nonzero element has a multiplicative inverse");
    CHECK(bad_div == 0, "GF: (a*b)/b == a");
    int bad_assoc = 0, bad_dist = 0;
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++)
            for (int c = 0; c < 256; c++) {
                uint8_t A = (uint8_t) a, B = (uint8_t) b, C = (uint8_t) c;
                if (freight_gf_mul(freight_gf_mul(A, B), C) !=
                    freight_gf_mul(A, freight_gf_mul(B, C)))
                    bad_assoc++;
                if (freight_gf_mul(A, (uint8_t) (B ^ C)) !=
                    (freight_gf_mul(A, B) ^ freight_gf_mul(A, C)))
                    bad_dist++;
            }
    CHECK(bad_assoc == 0, "GF: multiplication associates (all 2^24 triples)");
    CHECK(bad_dist == 0, "GF: multiplication distributes over XOR (all 2^24 triples)");
    /* 2 is a generator: its powers hit all 255 nonzero elements. */
    uint8_t seen[256] = {0}, x = 1;
    int distinct = 0;
    for (int i = 0; i < 255; i++) {
        if (!seen[x]) distinct++;
        seen[x] = 1;
        x = freight_gf_mul(x, 2);
    }
    CHECK(distinct == 255 && x == 1, "GF: 2 generates the multiplicative group (order 255)");
    CHECK(freight_gen_coef(10, 3, 3) == 1 && freight_gen_coef(10, 3, 4) == 0 &&
              freight_gen_coef(10, 12, 5) == freight_gf_inv(12 ^ 5),
          "generator: identity on data rows, Cauchy 1/(r^j) on parity rows");
}

/* ======================= erasure code ======================= */

static uint8_t pk_all[FREIGHT_BYTES];
static uint8_t pk_sub[FREIGHT_BYTES * 2 + 64];
static uint8_t payload[FREIGHT_MAX_PAYLOAD], back[FREIGHT_MAX_PAYLOAD];

static int mk_freight(freight_header_t *h, uint8_t k, uint32_t len)
{
    rnd_fill(payload, len);
    int rc = freight_header_init(h, rnd(), k, 0, 0, payload, len);
    if (rc) return rc;
    return freight_encode(h, payload, pk_all);
}

static void put_row(uint8_t *dst, uint32_t idx, uint32_t row)
{
    memcpy(dst + idx * FREIGHT_PACKET_BYTES, pk_all + row * FREIGHT_PACKET_BYTES,
           FREIGHT_PACKET_BYTES);
}

static bool decode_rows(const freight_header_t *h, const uint32_t *rows, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) put_row(pk_sub, i, rows[i]);
    memset(back, 0xEE, sizeof back);
    int rc = freight_decode(h, pk_sub, n, back, sizeof back, work, sizeof work);
    return rc == FREIGHT_OK && memcmp(back, payload, h->payload_len) == 0;
}

static void test_rs_exhaustive(void)
{
    freight_header_t h;
    uint32_t rows[FREIGHT_ROWS];
    /* Small k: every k-subset of the 168 rows decodes. Since the decoder uses
     * the first k distinct rows that arrive, this covers every arrival set. */
    for (uint8_t k = 1; k <= 3; k++) {
        mk_freight(&h, k, (uint32_t) k * 20 - (k == 2 ? 7 : 0));
        uint64_t tried = 0, ok = 0;
        if (k == 1)
            for (uint32_t a = 0; a < 168; a++) {
                rows[0] = a;
                tried++;
                ok += decode_rows(&h, rows, 1);
            }
        if (k == 2)
            for (uint32_t a = 0; a < 168; a++)
                for (uint32_t b = a + 1; b < 168; b++) {
                    rows[0] = b;
                    rows[1] = a;
                    tried++;
                    ok += decode_rows(&h, rows, 2);
                }
        if (k == 3)
            for (uint32_t a = 0; a < 168; a++)
                for (uint32_t b = a + 1; b < 168; b++)
                    for (uint32_t c = b + 1; c < 168; c++) {
                        rows[0] = c;
                        rows[1] = a;
                        rows[2] = b;
                        tried++;
                        ok += decode_rows(&h, rows, 3);
                    }
        char m[128];
        snprintf(m, sizeof m, "RS k=%u: every one of the %llu %u-row subsets decodes", k,
                 (unsigned long long) tried, k);
        CHECK(ok == tried, m);
    }
    /* Large k: every pattern of up to 168-k dropped rows. */
    for (uint32_t k = 166; k <= 168; k++) {
        mk_freight(&h, (uint8_t) k, (uint32_t) k * 20 - 3);
        uint32_t drop = 168 - k;
        uint64_t tried = 0, ok = 0;
        for (uint32_t a = 0; a < (drop >= 1 ? 168u : 1u); a++)
            for (uint32_t b = (drop >= 2 ? a + 1 : 0); b < (drop >= 2 ? 168u : 1u); b++) {
                uint32_t n = 0;
                for (uint32_t r = 0; r < 168; r++) {
                    if (drop >= 1 && r == a) continue;
                    if (drop >= 2 && r == b) continue;
                    rows[n++] = r;
                }
                tried++;
                ok += decode_rows(&h, rows, n);
            }
        char m[128];
        snprintf(m, sizeof m, "RS k=%u: all %llu patterns of %u dropped rows decode", k,
                 (unsigned long long) tried, drop);
        CHECK(ok == tried, m);
    }
}

static void shuffle(uint32_t *a, uint32_t n)
{
    for (uint32_t i = n; i > 1; i--) {
        uint32_t j = rndn(i), t = a[i - 1];
        a[i - 1] = a[j];
        a[j] = t;
    }
}

static void test_rs_random(void)
{
    static const uint8_t ks[] = {1, 21, 84, 167, 168};
    freight_header_t h;
    uint32_t rows[FREIGHT_ROWS];
    for (unsigned t = 0; t < sizeof ks; t++) {
        uint8_t k = ks[t];
        int trials = 300, ok = 0, few_ok = 0, dup_ok = 0;
        for (int i = 0; i < trials; i++) {
            mk_freight(&h, k, rndn(FREIGHT_CAPACITY(k) + 1));
            for (uint32_t r = 0; r < 168; r++) rows[r] = r;
            shuffle(rows, 168);
            uint32_t keep = k + rndn(169 - k);
            ok += decode_rows(&h, rows, keep);
            /* k-1 rows (even with duplicates and junk) must fail cleanly. */
            uint32_t n = 0;
            for (uint32_t r = 0; r + 1 < k; r++) put_row(pk_sub, n++, rows[r]);
            if (k > 1) put_row(pk_sub, n++, rows[0]);
            pk_sub[n * FREIGHT_PACKET_BYTES] = 200; /* out-of-range row index */
            n++;
            memset(back, 0xEE, sizeof back);
            int rc = freight_decode(&h, pk_sub, n, back, sizeof back, work, sizeof work);
            few_ok += rc == FREIGHT_ERR_TOO_FEW;
            /* Duplicates and junk interleaved with k good rows still decode. */
            n = 0;
            for (uint32_t r = 0; r < k; r++) {
                put_row(pk_sub, n++, rows[r]);
                if (r & 1) put_row(pk_sub, n++, rows[r]);
            }
            dup_ok += freight_decode(&h, pk_sub, n, back, sizeof back, work, sizeof work) == 0 &&
                      memcmp(back, payload, h.payload_len) == 0;
        }
        char m[160];
        snprintf(m, sizeof m,
                 "RS k=%u: %d random erasure patterns decode; k-1 rows fail TOO_FEW; dups ok", k,
                 trials);
        CHECK(ok == trials && few_ok == trials && dup_ok == trials, m);
    }
    /* Work buffer and output size guards. */
    mk_freight(&h, 84, 1000);
    CHECK(freight_decode(&h, pk_all, 168, back, 999, work, sizeof work) == FREIGHT_ERR_SPACE,
          "decode refuses a short output buffer");
    CHECK(freight_decode(&h, pk_all, 168, back, sizeof back, work, FREIGHT_DECODE_WORK(84) - 1) ==
              FREIGHT_ERR_SPACE,
          "decode refuses a short work buffer");
}

static void test_corruption(void)
{
    freight_header_t h;
    uint32_t rows[FREIGHT_ROWS];
    int caught = 0, trials = 500;
    for (int i = 0; i < trials; i++) {
        uint8_t k = (uint8_t) (1 + rndn(168));
        mk_freight(&h, k, 1 + rndn(FREIGHT_CAPACITY(k)));
        for (uint32_t r = 0; r < 168; r++) rows[r] = r;
        shuffle(rows, 168);
        for (uint32_t r = 0; r < k; r++) put_row(pk_sub, r, rows[r]);
        /* Flip bits in one payload byte of one row the decoder will use. */
        uint32_t victim = rndn(k);
        pk_sub[victim * FREIGHT_PACKET_BYTES + 1 + rndn(20)] ^= (uint8_t) (1 + rndn(255));
        int rc = freight_decode(&h, pk_sub, k, back, sizeof back, work, sizeof work);
        caught += rc == FREIGHT_ERR_HASH;
    }
    CHECK(caught == trials, "corruption: a corrupted row used in the rebuild is caught by SHA-256");
    /* A wrong header hash is caught at encode time too. */
    mk_freight(&h, 50, 900);
    h.payload_sha256[5] ^= 1;
    CHECK(freight_encode(&h, payload, pk_all) == FREIGHT_ERR_HASH,
          "encode refuses a payload that does not match the header hash");
}

static void test_header(void)
{
    freight_header_t h, g;
    uint8_t b[FREIGHT_HEADER_BYTES], c[FREIGHT_HEADER_BYTES];
    rnd_fill(payload, 777);
    int rc = freight_header_init(&h, 0x0102030405060708ull, 42, FREIGHT_F_OPSTREAM,
                                 0x1122334455667788ull, payload, 777);
    h.seq = 3;
    h.seq_count = 9;
    CHECK(rc == 0 && freight_header_serialize(&h, b) == 0, "header: init and serialise");
    CHECK(memcmp(b, "ZXFH", 4) == 0 && b[4] == 1 && b[5] == 42 && b[8] == (777 & 0xff) &&
              b[9] == (777 >> 8) && b[10] == 3 && b[12] == 9 && b[16] == 0x08 && b[23] == 0x01 &&
              b[24] == 0x88,
          "header: little-endian layout");
    CHECK(freight_header_parse(b, &g) == 0 && freight_header_serialize(&g, c) == 0 &&
              memcmp(b, c, sizeof b) == 0 && g.k == 42 && g.payload_len == 777 && g.seq == 3 &&
              g.seq_count == 9 && g.freight_id == h.freight_id &&
              g.expand_limit == h.expand_limit &&
              memcmp(g.payload_sha256, h.payload_sha256, 32) == 0,
          "header: parse(serialise(h)) == h");
    int rejects = 0, cases = 0;
    struct {
        int off;
        uint8_t val;
    } bad[] = {{0, 'Q'}, {4, 2}, {5, 0},  {5, 169}, {6, 0x80}, {7, 1},
               {14, 1},  {9, 4}, {10, 9}, {12, 0},  {13, 0x41}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(c, b, sizeof b);
        c[bad[i].off] = bad[i].val;
        if (bad[i].off == 12) c[13] = 0;
        cases++;
        rejects += freight_header_parse(c, &g) == FREIGHT_ERR_FORMAT;
    }
    CHECK(rejects == cases, "header: bad magic/version/k/flags/reserved/len/seq rejected");
    freight_header_t z = h;
    z.flags = 0;
    CHECK(freight_header_serialize(&z, c) == FREIGHT_ERR_FORMAT,
          "header: a non-opstream freight must not declare an expansion limit");
    /* Fuzz the parser: never accepts something it would not re-serialise. */
    int consistent = 1;
    for (int i = 0; i < 20000; i++) {
        memcpy(c, b, sizeof b);
        for (int m = 0; m < 1 + (int) rndn(4); m++) c[rndn(64)] = (uint8_t) rnd();
        if (freight_header_parse(c, &g) == 0) {
            uint8_t d[64];
            if (freight_header_serialize(&g, d) != 0 || memcmp(c, d, 64) != 0) consistent = 0;
        }
    }
    CHECK(consistent, "header: 20000 mutated headers: accepted ones are canonical");
}

/* ======================= op stream ======================= */

/* Test CID store. */
typedef struct {
    const uint8_t *cid;
    uint32_t cid_len;
    const uint8_t *data;
    uint64_t len;
} cid_entry_t;
typedef struct {
    cid_entry_t *e;
    int n;
    uint64_t served;
} cid_store_t;

static bool test_resolve(void *ctx, const uint8_t *cid, uint32_t cid_len, uint8_t *out,
                         uint64_t len)
{
    cid_store_t *s = ctx;
    for (int i = 0; i < s->n; i++) {
        if (s->e[i].cid_len == cid_len && memcmp(s->e[i].cid, cid, cid_len) == 0) {
            if (len > s->e[i].len) return false; /* never invent bytes */
            memcpy(out, s->e[i].data, len);
            s->served += len;
            return true;
        }
    }
    return false;
}

static bool fuzz_resolve(void *ctx, const uint8_t *cid, uint32_t cid_len, uint8_t *out,
                         uint64_t len)
{
    (void) ctx;
    if (cid[0] & 1) return false;
    for (uint64_t i = 0; i < len; i++) out[i] = (uint8_t) (cid[i % cid_len] + i);
    return true;
}

static void test_generators(void)
{
    const uint64_t N = 1u << 20;
    uint8_t *o = malloc(N), *o2 = malloc(N), d[32];
    char hex[65];
    uint8_t seed[8];
    uint64_t sv = 0x0123456789abcdefull;
    for (int i = 0; i < 8; i++) seed[i] = (uint8_t) (sv >> (8 * i));
    static const uint8_t xs16[16] = {0x8c, 0x70, 0xb6, 0x2c, 0x47, 0x82, 0x94, 0x7c,
                                     0xde, 0x28, 0x1f, 0xbf, 0x92, 0x56, 0x70, 0xd5};
#define HEXD(dg)                                                                                   \
    do {                                                                                           \
        for (int q = 0; q < 32; q++) sprintf(hex + 2 * q, "%02x", (dg)[q]);                        \
    } while (0)

    CHECK(freight_gen_run(FREIGHT_GEN_XORSHIFT, seed, 8, o, N) == 0 && memcmp(o, xs16, 16) == 0,
          "gen xorshift64*: first 16 bytes match the reference");
    sha256(o, N, d);
    HEXD(d);
    CHECK(strcmp(hex, "ed08de236f5dadb25ae5791557beeb6036fc1627220bc0c7d7ca0d4028fbb9cf") == 0,
          "gen xorshift64*: SHA-256 of 1 MiB matches the reference");
    freight_gen_run(FREIGHT_GEN_XORSHIFT, seed, 8, o2, N);
    CHECK(memcmp(o, o2, N) == 0, "gen xorshift64*: deterministic");
    memset(seed, 0, 8);
    CHECK(freight_gen_run(FREIGHT_GEN_XORSHIFT, seed, 8, o, 8) == FREIGHT_ERR_FORMAT,
          "gen xorshift64*: zero seed rejected");

    const uint8_t pat[] = "ZXV-168!";
    CHECK(freight_gen_run(FREIGHT_GEN_REPEAT, pat, 8, o, N) == 0, "gen repeat: runs");
    sha256(o, N, d);
    HEXD(d);
    CHECK(strcmp(hex, "3d7dee3c41a85e3384476e985035c1a0b78a8538e3904d0eea6f1d8dc446acb0") == 0 &&
              memcmp(o, "ZXV-168!ZXV-", 12) == 0,
          "gen repeat: known answer");

    const uint8_t ab[2] = {'a', 'b'};
    CHECK(freight_gen_run(FREIGHT_GEN_FIBWORD, ab, 2, o, N) == 0 &&
              memcmp(o, "abaababaabaababaababa", 21) == 0,
          "gen fibonacci word: abaababaabaababaababa...");
    sha256(o, N, d);
    HEXD(d);
    CHECK(strcmp(hex, "e01eba1affabafeeb4d4c64a5bf9eda10b82beb1b534f314ba05317808f7955e") == 0,
          "gen fibonacci word: SHA-256 of 1 MiB matches the reference");
    /* Fibonacci word lengths: prefix of length F(n) is S(n), no "bb", no "aaa". */
    int okw = 1;
    for (uint64_t i = 0; i + 2 < N; i++)
        if ((o[i] == 'b' && o[i + 1] == 'b') || (o[i] == 'a' && o[i + 1] == 'a' && o[i + 2] == 'a'))
            okw = 0;
    CHECK(okw, "gen fibonacci word: Sturmian (no bb, no aaa) across 1 MiB");
    CHECK(freight_gen_run(FREIGHT_GEN_FIBWORD, (const uint8_t *) "aa", 2, o, 4) ==
              FREIGHT_ERR_FORMAT,
          "gen fibonacci word: equal symbols rejected");

    const uint8_t sp[3] = {3, '#', '.'};
    CHECK(freight_gen_run(FREIGHT_GEN_SIERPINSKI, sp, 3, o, 64) == 0 &&
              memcmp(o, "#########.#.#.#.##..##..#...#...####....#.#.....##......#.......", 64) ==
                  0,
          "gen sierpinski: 8x8 known answer");
    const uint8_t sp10[3] = {10, '#', '.'};
    freight_gen_run(FREIGHT_GEN_SIERPINSKI, sp10, 3, o, N);
    sha256(o, N, d);
    HEXD(d);
    CHECK(strcmp(hex, "3ea6c31ceb54a7472545fd4c376c792f0627b82570fdcc75ce4880539cb5cde3") == 0,
          "gen sierpinski: SHA-256 of a 1024x1024 bitmap matches the reference");
    /* Self-similarity: the top-left quadrant equals the top-right and the
     * bottom-left quadrant; the bottom-right is empty. */
    int self = 1;
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            uint8_t a = o[y * 1024 + x];
            if (a != o[y * 1024 + x + 512] || a != o[(y + 512) * 1024 + x] ||
                o[(y + 512) * 1024 + x + 512] != '.')
                self = 0;
        }
    CHECK(self, "gen sierpinski: quadrants are copies of the whole at half scale");
    CHECK(freight_gen_run(9, sp, 3, o, 4) == FREIGHT_ERR_FORMAT, "gen: unknown generator rejected");
    free(o);
    free(o2);
}

/* Build a valid stream touching every op, for fuzz mutation. */
static uint64_t build_mixed(uint8_t *buf, uint64_t cap, uint64_t *total)
{
    freight_ops_writer_t w;
    uint64_t tot = 1 + rndn(4000);
    freight_ops_writer_init(&w, buf, cap, tot, NULL);
    uint64_t prod = 0;
    uint8_t lit[64], cid[8];
    while (prod < tot) {
        uint64_t left = tot - prod, n = 1 + rndn(left < 300 ? (uint32_t) left : 300);
        switch (prod == 0 ? 0 : rndn(4)) {
        case 0:
            if (n > 64) n = 64;
            rnd_fill(lit, n);
            freight_ops_put_literal(&w, lit, n);
            break;
        case 1:
            freight_ops_put_copy(&w, 1 + rndn((uint32_t) prod), n);
            break;
        case 2:
            rnd_fill(cid, 8);
            cid[0] &= 0xfe;
            freight_ops_put_cidref(&w, cid, 1 + rndn(8), n);
            break;
        default: {
            uint8_t g = (uint8_t) (1 + rndn(4)), p[8];
            uint32_t pl = g == 1 ? 8 : g == 2 ? 1 + rndn(8) : g == 3 ? 2 : 3;
            rnd_fill(p, 8);
            p[0] |= 1;
            if (g == 3) p[1] = (uint8_t) (p[0] ^ 0x55);
            if (g == 4) p[0] = (uint8_t) (1 + rndn(15));
            freight_ops_put_seed(&w, g, p, pl, n);
        }
        }
        prod += n;
    }
    uint64_t len = 0;
    if (freight_ops_writer_finish(&w, &len) != 0) return 0;
    *total = tot;
    return len;
}

static void test_ops_fuzz(void)
{
    const uint64_t CAP = 8192;
    uint8_t *tmpl = malloc(CAP * 4);
    long valid_ok = 0, valid_n = 0, rand_n = 0, mut_n = 0, accepted = 0, bad_accept = 0;
    /* Valid streams expand. */
    for (int i = 0; i < 2000; i++) {
        uint64_t tot = 0, len = build_mixed(tmpl, CAP * 4, &tot);
        uint8_t *ops = malloc(len), *out = malloc(tot);
        memcpy(ops, tmpl, len);
        uint64_t ol = 0;
        valid_n++;
        valid_ok +=
            freight_ops_expand(ops, len, out, tot, tot, fuzz_resolve, NULL, &ol) == 0 && ol == tot;
        free(ops);
        free(out);
    }
    CHECK(valid_ok == valid_n, "ops: 2000 hand-built mixed streams expand exactly");
    /* Random streams with a valid header, and mutated valid streams. Buffers
     * are malloc'd at exactly the advertised size so ASan sees any overrun. */
    for (int i = 0; i < 40000; i++) {
        uint64_t len, tot = 0;
        uint8_t *ops;
        if (i & 1) {
            len = FREIGHT_OPS_HDR + rndn(200);
            ops = malloc(len);
            rnd_fill(ops, len);
            memcpy(ops, "ZXOP\x01", 5);
            ops[5] &= 1;
            tot = rndn(5000);
            for (int b = 0; b < 8; b++) ops[6 + b] = (uint8_t) (tot >> (8 * b));
            rand_n++;
        } else {
            len = build_mixed(tmpl, CAP * 4, &tot);
            int muts = 1 + (int) rndn(6);
            for (int m = 0; m < muts; m++) {
                switch (rndn(4)) {
                case 0:
                    tmpl[rndn((uint32_t) len)] = (uint8_t) rnd();
                    break;
                case 1:
                    tmpl[rndn((uint32_t) len)] ^= (uint8_t) (1u << rndn(8));
                    break;
                case 2:
                    len = 1 + rndn((uint32_t) len); /* truncate */
                    break;
                default:
                    if (len > FREIGHT_OPS_HDR)
                        tmpl[FREIGHT_OPS_HDR + rndn((uint32_t) (len - FREIGHT_OPS_HDR))] = 0x80;
                }
                if (len < 2) len = 2;
            }
            ops = malloc(len);
            memcpy(ops, tmpl, len);
            mut_n++;
        }
        uint64_t cap = rndn(6000);
        uint8_t *out = malloc(cap ? cap : 1);
        uint64_t ol = 0;
        int rc = freight_ops_expand(ops, len, out, cap, 1u << 20, fuzz_resolve, NULL, &ol);
        if (rc == 0) {
            accepted++;
            uint64_t t2;
            if (freight_ops_peek(ops, len, &t2, NULL) != 0 || t2 != ol || ol > cap) bad_accept++;
        } else if (ol != 0)
            bad_accept++;
        free(ops);
        free(out);
    }
    printf("  ops fuzz: %ld random + %ld mutated streams, %ld accepted, %ld rejected\n", rand_n,
           mut_n, accepted, rand_n + mut_n - accepted);
    CHECK(rand_n + mut_n >= 20000 && bad_accept == 0,
          "ops: 40000 random/mutated streams never crash, overrun or misreport");
    free(tmpl);
}

static void test_limits(void)
{
    uint8_t buf[256], out[512];
    freight_ops_writer_t w;
    uint64_t len, ol;
    const uint8_t a[1] = {'A'};
    /* 1 byte + COPY: a tiny stream declaring 2^40 bytes. */
    freight_ops_writer_init(&w, buf, sizeof buf, 1ull << 40, NULL);
    freight_ops_put_literal(&w, a, 1);
    freight_ops_put_copy(&w, 1, (1ull << 40) - 1);
    CHECK(freight_ops_writer_finish(&w, &len) == 0 && len <= 25,
          "limits: a 1 TiB bomb is 25 bytes");
    memset(out, 0x77, sizeof out);
    CHECK(freight_ops_expand(buf, len, out, sizeof out, 1ull << 50, NULL, NULL, &ol) ==
                  FREIGHT_ERR_LIMIT &&
              out[0] == 0x77,
          "limits: declared total above out_cap refused before writing");
    CHECK(freight_ops_expand(buf, len, out, 1ull << 50, 4096, NULL, NULL, &ol) == FREIGHT_ERR_LIMIT,
          "limits: declared total above the caller limit refused");
    /* An op that runs past the declared total (declared small, op large). */
    uint8_t s[] = {'Z', 'X', 'O', 'P', 1,    0,    4,   0,    0,    0,
                   0,   0,   0,   0,   0x01, 0x01, 'A', 0x02, 0x01, 0x7f};
    CHECK(freight_ops_expand(s, sizeof s, out, sizeof out, sizeof out, NULL, NULL, &ol) ==
              FREIGHT_ERR_FORMAT,
          "limits: op longer than the declared remainder rejected");
    s[19] = 0x03; /* now produces exactly 4 */
    CHECK(freight_ops_expand(s, sizeof s, out, sizeof out, sizeof out, NULL, NULL, &ol) == 0 &&
              ol == 4 && memcmp(out, "AAAA", 4) == 0,
          "limits: the same stream with an honest length expands");
    s[6] = 5; /* declares 5, produces 4 */
    CHECK(freight_ops_expand(s, sizeof s, out, sizeof out, sizeof out, NULL, NULL, &ol) ==
              FREIGHT_ERR_FORMAT,
          "limits: short of the declared total rejected");
    s[6] = 4;
    s[18] = 0x02; /* distance 2 > produced 1 */
    CHECK(freight_ops_expand(s, sizeof s, out, sizeof out, sizeof out, NULL, NULL, &ol) ==
              FREIGHT_ERR_FORMAT,
          "limits: COPY reaching before the start rejected");
    /* Overlong / overflowing varints. */
    uint8_t v[] = {'Z', 'X', 'O', 'P', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x81, 0x00, 'A'};
    CHECK(freight_ops_expand(v, sizeof v, out, sizeof out, sizeof out, NULL, NULL, &ol) ==
              FREIGHT_ERR_FORMAT,
          "limits: non-minimal varint rejected");
    uint8_t v2[30] = {'Z', 'X', 'O', 'P', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0x01};
    for (int i = 15; i < 25; i++) v2[i] = 0xff;
    v2[25] = 0x01;
    CHECK(freight_ops_expand(v2, sizeof v2, out, sizeof out, sizeof out, NULL, NULL, &ol) ==
              FREIGHT_ERR_FORMAT,
          "limits: 11-byte varint rejected");
    /* SHA of the output. */
    uint8_t dg[32];
    sha256((const uint8_t *) "AAAA", 4, dg);
    freight_ops_writer_init(&w, buf, sizeof buf, 4, dg);
    freight_ops_put_literal(&w, a, 1);
    freight_ops_put_copy(&w, 1, 3);
    CHECK(freight_ops_writer_finish(&w, &len) == 0 &&
              freight_ops_expand(buf, len, out, 4, 4, NULL, NULL, &ol) == 0,
          "limits: carried SHA-256 of the output verifies");
    buf[FREIGHT_OPS_HDR] ^= 1;
    CHECK(freight_ops_expand(buf, len, out, 4, 4, NULL, NULL, &ol) == FREIGHT_ERR_HASH,
          "limits: wrong output SHA-256 rejected");
    /* Writer refuses inconsistent streams. */
    freight_ops_writer_init(&w, buf, sizeof buf, 4, NULL);
    freight_ops_put_copy(&w, 1, 4);
    CHECK(freight_ops_writer_finish(&w, &len) == FREIGHT_ERR_ARG,
          "limits: writer refuses COPY with nothing produced");
    freight_ops_writer_init(&w, buf, 10, 4, NULL);
    CHECK(freight_ops_writer_finish(&w, &len) == FREIGHT_ERR_SPACE,
          "limits: writer reports a short buffer");
}

static void test_cidref(void)
{
    static uint8_t doc[65536];
    for (int i = 0; i < 65536; i++) doc[i] = (uint8_t) (i * 7 + (i >> 9));
    uint8_t cid[34];
    cid[0] = 0x12;
    cid[1] = 0x20;
    sha256(doc, sizeof doc, cid + 2); /* a multihash-shaped id */
    cid_entry_t e = {cid, 34, doc, sizeof doc};
    cid_store_t st = {&e, 1, 0};
    uint8_t buf[128], *out = malloc(sizeof doc + 16), dg[32];
    sha256(doc, sizeof doc, dg);
    freight_ops_writer_t w;
    uint64_t len, ol;
    freight_ops_writer_init(&w, buf, sizeof buf, sizeof doc, dg);
    freight_ops_put_cidref(&w, cid, 34, sizeof doc);
    int rc = freight_ops_writer_finish(&w, &len);
    CHECK(rc == 0 &&
              freight_ops_expand(buf, len, out, sizeof doc, sizeof doc, test_resolve, &st, &ol) ==
                  0 &&
              ol == sizeof doc && memcmp(out, doc, sizeof doc) == 0 && st.served == sizeof doc,
          "cidref: resolved through the callback, output hash verified");
    CHECK(freight_ops_expand(buf, len, out, sizeof doc, sizeof doc, NULL, NULL, &ol) ==
              FREIGHT_ERR_RESOLVE,
          "cidref: no resolver -> FREIGHT_ERR_RESOLVE");
    buf[FREIGHT_OPS_HDR_SHA + 2 + 5] ^= 1; /* unknown cid in the stream */
    CHECK(freight_ops_expand(buf, len, out, sizeof doc, sizeof doc, test_resolve, &st, &ol) ==
              FREIGHT_ERR_RESOLVE,
          "cidref: unknown cid -> FREIGHT_ERR_RESOLVE");
    free(out);
}

static void test_encoder(void)
{
    static uint8_t in[200000], ops[300000], out[200000];
    int ok = 0, n = 0, det = 1;
    for (int t = 0; t < 60; t++) {
        uint32_t len = rndn(t < 30 ? 2000 : 200000);
        int kind = t % 3;
        for (uint32_t i = 0; i < len; i++) {
            if (kind == 0)
                in[i] = (uint8_t) rnd();
            else if (kind == 1)
                in[i] = (uint8_t) ("abcab"[rndn(5)]);
            else
                in[i] = i > 50 && rndn(10) ? in[i - 1 - rndn(50)] : (uint8_t) rnd();
        }
        uint64_t ol = 0, el = 0, ol2 = 0;
        uint32_t ww = t & 1 ? FREIGHT_ENC_WORK_WORDS : FREIGHT_ENC_WORK_MIN + rndn(20000);
        int rc = freight_ops_encode(in, len, t & 1, ops, sizeof ops, &ol, enc_work, ww);
        n++;
        if (rc == 0 && ol <= freight_ops_bound(len, t & 1) &&
            freight_ops_expand(ops, ol, out, sizeof out, len, NULL, NULL, &el) == 0 && el == len &&
            memcmp(in, out, len) == 0)
            ok++;
        static uint8_t ops2[300000];
        freight_ops_encode(in, len, t & 1, ops2, sizeof ops2, &ol2, enc_work, ww);
        if (ol2 != ol || memcmp(ops, ops2, ol) != 0) det = 0;
    }
    CHECK(ok == n, "encoder: 60 inputs (random/low-entropy/LZ-like, several window sizes) "
                   "round-trip within freight_ops_bound");
    CHECK(det, "encoder: deterministic");
    uint64_t ol;
    CHECK(freight_ops_encode(in, 1000, false, ops, 100, &ol, enc_work, FREIGHT_ENC_WORK_WORDS) ==
              FREIGHT_ERR_SPACE,
          "encoder: short output buffer reported");
    CHECK(freight_ops_encode(in, 1000, false, ops, sizeof ops, &ol, enc_work, 100) ==
              FREIGHT_ERR_SPACE,
          "encoder: short work buffer reported");
}

/* ======================= multi-freight ======================= */

/* Send `data` as a set at k, drop parity-many rows from each freight at
 * random, deliver members in reverse order with one duplicate, and join. */
static int ship(const uint8_t *data, uint64_t len, uint8_t k, uint8_t flags, uint64_t limit,
                uint8_t *joined, uint64_t cap, uint64_t *jlen, uint32_t *nfreights)
{
    uint32_t n = freight_set_count(len, k);
    if (!n) return FREIGHT_ERR_ARG;
    *nfreights = n;
    static freight_join_t j;
    freight_join_init(&j, joined, cap);
    uint64_t id = rnd();
    uint32_t rows[FREIGHT_ROWS];
    for (uint32_t s = n; s-- > 0;) {
        freight_header_t h, hr;
        uint8_t wire[FREIGHT_HEADER_BYTES];
        int rc = freight_split(id, k, flags, limit, data, len, s, &h, pk_all);
        if (rc) return rc;
        /* the header travels serialised in the outer message */
        if (freight_header_serialize(&h, wire) || freight_header_parse(wire, &hr))
            return FREIGHT_ERR_FORMAT;
        for (uint32_t r = 0; r < 168; r++) rows[r] = r;
        shuffle(rows, 168);
        for (uint32_t r = 0; r < k; r++) put_row(pk_sub, r, rows[r]);
        rc = freight_join_add(&j, &hr, pk_sub, k, work, sizeof work);
        if (rc) return rc;
        if (s == n / 2) {
            rc = freight_join_add(&j, &hr, pk_sub, k, work, sizeof work);
            if (rc) return rc;
        }
    }
    return freight_join_complete(&j, jlen) ? FREIGHT_OK : FREIGHT_ERR_SEQ;
}

static void test_multi(void)
{
    static uint8_t data[100000], joined[100000];
    rnd_fill(data, sizeof data);
    uint64_t jl = 0;
    uint32_t nf = 0;
    int ok = 1;
    static const uint64_t lens[] = {0, 1, 2519, 2520, 2521, 99999, 100000};
    for (unsigned i = 0; i < 7; i++) {
        int rc = ship(data, lens[i], 126, 0, 0, joined, sizeof joined, &jl, &nf);
        if (rc || jl != lens[i] || memcmp(data, joined, lens[i]) != 0) ok = 0;
    }
    CHECK(ok, "multi: split/join at k=126 for 0..100000 bytes with erasures, reordering, dups");
    CHECK(freight_set_count(2520, 126) == 1 && freight_set_count(2521, 126) == 2 &&
              freight_set_count(0, 126) == 1 && freight_set_count(3360ull * 16384, 168) == 16384 &&
              freight_set_count(3360ull * 16384 + 1, 168) == 0,
          "multi: set counts at the boundaries");
    /* A member of another set is refused. */
    freight_join_t j;
    freight_header_t h1, h2;
    freight_join_init(&j, joined, sizeof joined);
    freight_split(1, 100, 0, 0, data, 5000, 0, &h1, pk_all);
    int a = freight_join_add(&j, &h1, pk_all, 168, work, sizeof work);
    freight_split(2, 100, 0, 0, data, 5000, 1, &h2, pk_all);
    int b = freight_join_add(&j, &h2, pk_all, 168, work, sizeof work);
    CHECK(a == 0 && b == FREIGHT_ERR_SEQ && !freight_join_complete(&j, &jl),
          "multi: member with a different freight id refused");
    freight_join_init(&j, joined, 100);
    CHECK(freight_join_add(&j, &h1, pk_all, 168, work, sizeof work) == FREIGHT_ERR_SPACE,
          "multi: join refuses a set that cannot fit");
}

/* ======================= measured ratios ======================= */

static uint8_t *big_in, *big_ops, *big_join, *big_out;
#define BIG (4u << 20)

/* Encode-or-take op stream -> freights at k -> join -> expand. Returns the
 * ratio as expanded bytes per freight byte (3528 wire bytes per freight). */
static double measure(const char *name, const uint8_t *ops, uint64_t ops_len, const uint8_t *expect,
                      uint64_t expect_len, uint8_t k, cid_store_t *st, int *ok)
{
    uint64_t jl = 0, ol = 0;
    uint32_t nf = 0;
    *ok = 0;
    if (ship(ops, ops_len, k, FREIGHT_F_OPSTREAM, BIG, big_join, BIG, &jl, &nf) != 0) return -1;
    if (jl != ops_len || memcmp(big_join, ops, ops_len) != 0) return -1;
    int rc = freight_ops_expand(big_join, jl, big_out, BIG, BIG, test_resolve, st, &ol);
    if (rc || ol != expect_len || memcmp(big_out, expect, ol) != 0) return -1;
    double wire = (double) nf * FREIGHT_BYTES;
    double r = (double) ol / wire;
    printf("  ratio %-34s k=%3u: %8llu B -> %6llu B ops -> %5u freight(s) %8.0f B: %10.3f\n", name,
           k, (unsigned long long) ol, (unsigned long long) ops_len, nf, wire, r);
    *ok = 1;
    return r;
}

static uint64_t enc(const uint8_t *in, uint64_t n)
{
    uint64_t ol = 0;
    if (freight_ops_encode(in, n, true, big_ops, BIG + BIG / 2, &ol, enc_work,
                           FREIGHT_ENC_WORK_WORDS))
        return 0;
    return ol;
}

static void test_ratios(void)
{
    big_in = malloc(BIG);
    big_ops = malloc(BIG + BIG / 2);
    big_join = malloc(BIG);
    big_out = malloc(BIG);
    int ok;
    double r1, r2;
    uint64_t n, ol;

    /* 1. Random data. */
    n = 1u << 20;
    rnd_fill(big_in, n);
    ol = enc(big_in, n);
    r1 = measure("random 1 MiB (LZ)", big_ops, ol, big_in, n, 168, NULL, &ok);
    r2 = measure("random 1 MiB (LZ)", big_ops, ol, big_in, n, 126, NULL, &ok);
    CHECK(ok && ol > n && r1 < 1.0 && r2 < 1.0,
          "ratio: random data expands to < 1 byte per freight byte (op stream > input)");

    /* 2. Repetitive text-like data: words from a small vocabulary. */
    static const char *words[] = {
        "the",       "freight",     "packet", "carries", "a",       "matrix",  "of",
        "smart",     "packets",     "and",    "every",   "row",     "is",      "coded",
        "across",    "network",     "peer",   "node",    "content", "address", "with",
        "erasure",   "recovery",    "for",    "data",    "kernel",  "zxv",     "ledger",
        "sovereign", "holographic", "field",  "byte"};
    n = 0;
    while (n < (1u << 20) - 64) {
        const char *wd = words[rndn(32)];
        while (*wd) big_in[n++] = (uint8_t) *wd++;
        uint32_t p = rndn(16);
        big_in[n++] = p == 0 ? '.' : p == 1 ? ',' : p == 2 ? '\n' : ' ';
    }
    ol = enc(big_in, n);
    r2 = measure("text-like 1 MiB (LZ)", big_ops, ol, big_in, n, 168, NULL, &ok);
    r1 = measure("text-like 1 MiB (LZ)", big_ops, ol, big_in, n, 126, NULL, &ok);
    CHECK(ok && r1 > 1.5 && r2 > r1, "ratio: repetitive text-like data gives a clear win (> 1.5)");

    /* 3. Self-similar: Fibonacci word through the LZ encoder (no seed). */
    n = 1u << 20;
    freight_gen_run(FREIGHT_GEN_FIBWORD, (const uint8_t *) "ab", 2, big_in, n);
    ol = enc(big_in, n);
    r1 = measure("fibonacci word 1 MiB (LZ)", big_ops, ol, big_in, n, 126, NULL, &ok);
    CHECK(ok && r1 > 20.0, "ratio: self-similar Fibonacci word via COPY gives a large win (> 20)");

    /* 3b. Self-similar: Sierpinski bitmap through the LZ encoder. */
    const uint8_t sp[3] = {11, 0xff, 0x00};
    n = 4u << 20;
    freight_gen_run(FREIGHT_GEN_SIERPINSKI, sp, 3, big_in, n);
    ol = enc(big_in, n);
    r1 = measure("sierpinski 2048x2048 (LZ)", big_ops, ol, big_in, n, 126, NULL, &ok);
    CHECK(ok && r1 > 20.0, "ratio: self-similar Sierpinski bitmap via COPY gives a large win");

    /* 3c. The same bitmap declared as one SEED op. */
    freight_ops_writer_t w;
    uint8_t dg[32];
    sha256(big_in, n, dg);
    freight_ops_writer_init(&w, big_ops, 256, n, dg);
    freight_ops_put_seed(&w, FREIGHT_GEN_SIERPINSKI, sp, 3, n);
    freight_ops_writer_finish(&w, &ol);
    r1 = measure("sierpinski 2048x2048 (SEED)", big_ops, ol, big_in, n, 126, NULL, &ok);
    CHECK(ok && r1 > 1000.0,
          "ratio: a genuinely generated bitmap as one SEED op: > 1000 (bounded by the limit)");

    /* 4. CID reference: bounded by what the resolver holds. */
    static uint8_t doc[256 * 1024];
    rnd_fill(doc, sizeof doc);
    uint8_t cid[34] = {0x12, 0x20};
    sha256(doc, sizeof doc, cid + 2);
    cid_entry_t e = {cid, 34, doc, sizeof doc};
    cid_store_t st = {&e, 1, 0};
    sha256(doc, sizeof doc, dg);
    freight_ops_writer_init(&w, big_ops, 256, sizeof doc, dg);
    freight_ops_put_cidref(&w, cid, 34, sizeof doc);
    freight_ops_writer_finish(&w, &ol);
    r1 = measure("cidref 256 KiB random doc", big_ops, ol, doc, sizeof doc, 126, &st, &ok);
    CHECK(ok && st.served == sizeof doc && r1 <= (double) sizeof doc / FREIGHT_BYTES + 1e-9,
          "ratio: CIDREF expansion equals the bytes the resolver supplied, no more");
    /* Asking for more than the resolver holds fails. */
    freight_ops_writer_init(&w, big_ops, 256, sizeof doc + 1, NULL);
    freight_ops_put_cidref(&w, cid, 34, sizeof doc + 1);
    freight_ops_writer_finish(&w, &ol);
    CHECK(freight_ops_expand(big_ops, ol, big_out, BIG, BIG, test_resolve, &st, &n) ==
              FREIGHT_ERR_RESOLVE,
          "ratio: CIDREF longer than the resolver's content fails");
    free(big_in);
    free(big_ops);
    free(big_join);
    free(big_out);
}

int main(void)
{
    test_gf();
    test_rs_exhaustive();
    test_rs_random();
    test_corruption();
    test_header();
    test_generators();
    test_ops_fuzz();
    test_limits();
    test_cidref();
    test_encoder();
    test_multi();
    test_ratios();
    if (quiet_fail) failures++;
    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
