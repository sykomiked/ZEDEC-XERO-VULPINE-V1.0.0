/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx_z.c — the booklet codec (alx_z.h): round trips, the LZ4 block
 * rules, a hand-made LZ4 block, refusals, and determinism. argv[1]: a
 * directory of real files to round-trip (optional). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_z.h"
#include "../robin_debanks/sha256.h"

static int checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] " __VA_ARGS__);                                                        \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define KAT_LEN 94698u
#define KAT_HEX "d1a706907e727fc906b0c02cbf906740db08252ef73695ec6b8c433e69ef2c6d"

static int32_t work[ALX_Z_WORK];
static uint8_t A[ALX_Z_MAXPLAIN + 8192], B[ALX_Z_MAXPLAIN + 8192], C[ALX_Z_MAXPLAIN + 8192];

static uint32_t rnd(uint64_t *s)
{
    *s = *s * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t) (*s >> 33);
}

/* the LZ4 block rules a standard decoder relies on */
static int rules(const uint8_t *z, uint32_t zn, uint32_t n)
{
    uint32_t ip = 0, op = 0;
    for (;;) {
        uint32_t t = z[ip++], lit = t >> 4, b;
        if (lit == 15) do lit += b = z[ip++]; while (b == 255);
        ip += lit, op += lit;
        if (ip == zn) return op == n && (n <= 12 || lit >= 5); /* ends in >= 5 literals */
        uint32_t ml = t & 15;
        ip += 2;
        if (ml == 15) do ml += b = z[ip++]; while (b == 255);
        if (op + 12 > n) return 0; /* the last match starts 12 or more bytes before the end */
        op += ml + 4;
        if (op + 5 > n) return 0;
    }
}

static void trip(const uint8_t *in, uint32_t n, const char *what)
{
    uint32_t z = alx_z_lz4(in, n, B, sizeof B, work);
    CHECK(z > 0 && z <= alx_z_bound(n), "%s: encodes (%u bytes to %u)", what, n, z);
    CHECK(alx_z_unlz4(B, z, C, n) == (int32_t) n && !memcmp(in, C, n), "%s: round trip", what);
    CHECK(rules(B, z, n), "%s: keeps the LZ4 block rules", what);
    uint32_t z2 = alx_z_lz4(in, n, A + ALX_Z_MAXPLAIN, 4096, work); /* scratch reuse is fine */
    (void) z2;
    static uint8_t D[ALX_Z_MAXPLAIN + 8192];
    uint32_t z3 = alx_z_lz4(in, n, D, sizeof D, work);
    CHECK(z3 == z && !memcmp(B, D, z), "%s: the same bytes give the same block", what);
    if (z > 1) CHECK(alx_z_lz4(in, n, D, z - 1, work) == 0, "%s: too small an output is refused", what);
    if (n) CHECK(alx_z_unlz4(B, z, C, n - 1) < 0, "%s: too small a buffer is refused", what);
}

int main(int argc, char **argv)
{
    uint64_t s = 1;
    /* edges */
    trip((const uint8_t *) "", 0, "empty");
    trip((const uint8_t *) "a", 1, "one byte");
    trip((const uint8_t *) "abcabcabcabc", 12, "twelve bytes (no match allowed)");
    trip((const uint8_t *) "abcabcabcabcabcabcabcabc", 24, "a short repeat");
    memset(A, 0, ALX_Z_MAXPLAIN);
    trip(A, ALX_Z_MAXPLAIN, "1 MiB of zeros");
    CHECK(alx_z_lz4(A, ALX_Z_MAXPLAIN + 1u, B, sizeof B, work) == 0, "over 1 MiB is refused");
    for (uint32_t i = 0; i < ALX_Z_MAXPLAIN; i++) A[i] = (uint8_t) rnd(&s);
    trip(A, ALX_Z_MAXPLAIN, "1 MiB of noise");
    trip(A, 70000, "noise past the window");
    /* text with repeats farther apart than the window: matches never reach past 65535 */
    for (uint32_t i = 0; i < 300000; i++) A[i] = (uint8_t) ("int x = f(a, b);\n"[i % 17] + (i / 100000));
    trip(A, 300000, "text over three windows");
    for (uint32_t k = 0; k < 200; k++) { /* random sizes and mixtures */
        uint32_t n = rnd(&s) % 5000, alpha = 1 + rnd(&s) % 8;
        for (uint32_t i = 0; i < n; i++) A[i] = (uint8_t) ('a' + rnd(&s) % alpha);
        char w[32];
        snprintf(w, sizeof w, "mix %u", k);
        trip(A, n, w);
    }
    /* a block written by hand: 5 literals "abcde", then a match of 10 at offset 5,
     * then 5 literals: "abcdeabcdeabcdevwxyz" */
    static const uint8_t hand[] = {0x56, 'a', 'b', 'c', 'd', 'e', 5, 0, 0x50, 'v', 'w', 'x', 'y', 'z'};
    CHECK(alx_z_unlz4(hand, 14, C, 64) == 20, "a hand-made block decodes");
    CHECK(!memcmp(C, "abcdeabcdeabcde", 15) && !memcmp(C + 15, "vwxyz", 5), "to the right bytes");
    /* refusals */
    static const uint8_t off0[] = {0x14, 'a', 0, 0, 0x50, 'v', 'w', 'x', 'y', 'z'};
    CHECK(alx_z_unlz4(off0, sizeof off0, C, 64) < 0, "offset 0");
    static const uint8_t far[] = {0x14, 'a', 2, 0, 0x10, 'z'};
    CHECK(alx_z_unlz4(far, sizeof far, C, 64) < 0, "an offset before the start");
    static const uint8_t cut[] = {0xF0, 255};
    CHECK(alx_z_unlz4(cut, sizeof cut, C, 64) < 0, "a length that runs off the end");
    static const uint8_t lit[] = {0x50, 'a', 'b'};
    CHECK(alx_z_unlz4(lit, sizeof lit, C, 64) < 0, "literals past the end");
    CHECK(alx_z_unlz4(lit, 0, C, 64) < 0, "nothing");
    static const uint8_t half[] = {0x14, 'a', 1};
    CHECK(alx_z_unlz4(half, sizeof half, C, 64) < 0, "half an offset");
    /* ALXZ: a compressible booklet is wrapped, an incompressible one stored bare */
    for (uint32_t i = 0; i < 100000; i++) A[i] = (uint8_t) ("ALXB booklet text "[i % 18]);
    uint32_t m = alx_z_store(A, 100000, B, sizeof B, work);
    CHECK(m > ALX_Z_HEAD && m < 100000 && alx_z_is(B, m) && alx_z_plain_len(B, m) == 100000, "wrapped");
    CHECK(alx_z_open(B, m, C, 100000) == 100000 && !memcmp(A, C, 100000), "opened");
    uint8_t h[32];
    sha256(A, 100000, h);
    CHECK(B[16] == 1 && B[17] == 0x55 && B[18] == 0x12 && B[19] == 0x20 && !memcmp(B + 20, h, 32),
          "the plain CID is the booklet's");
    CHECK(alx_z_open(B, m, C, 99999) == ALX_E_LIMIT, "a small buffer");
    B[ALX_Z_HEAD + 3] ^= 1; /* a literal changed: decodes, but to other bytes */
    int32_t r = alx_z_open(B, m, C, 100000);
    CHECK(r == ALX_E_HASH || r == ALX_E_FORM, "a changed payload: %d", r);
    B[ALX_Z_HEAD + 3] ^= 1;
    for (uint32_t i = 0; i < ALX_Z_HEAD; i++) { /* every header byte matters */
        if (i >= 20 && i < 52) continue;      /* the digest: a hash failure, checked below */
        B[i] ^= 0x40;
        CHECK(alx_z_open(B, m, C, 200000) == ALX_E_FORM || !alx_z_is(B, m), "header byte %u", i);
        B[i] ^= 0x40;
    }
    B[30] ^= 1;
    CHECK(alx_z_open(B, m, C, 100000) == ALX_E_HASH, "another plain CID");
    B[30] ^= 1;
    CHECK(alx_z_open(B, m - 1, C, 100000) == ALX_E_FORM, "a short block");
    for (uint32_t i = 0; i < 4096; i++) A[i] = (uint8_t) rnd(&s);
    m = alx_z_store(A, 4096, B, sizeof B, work);
    CHECK(m == 4096 && !memcmp(A, B, 4096) && !alx_z_is(B, m), "noise is stored bare");
    CHECK(alx_z_store(A, 4096, B, 4096 + ALX_Z_HEAD - 1, work) == 0, "store needs room for a header");
    /* a fixed answer: this input must give exactly these bytes on every
     * platform, or CIDs over compressed booklets would differ between nodes */
    {
        uint64_t k = 42;
        for (uint32_t i = 0; i < 200000; i++)
            A[i] = (uint8_t) (i % 3000 < 1500 ? (uint32_t) (uint8_t) "static int f(void) { return 0; }\n"[i % 34]
                                               : 'a' + rnd(&k) % 23u);
        uint32_t z = alx_z_lz4(A, 200000, B, sizeof B, work);
        uint8_t d[32];
        sha256(B, z, d);
        char hex[65];
        for (uint32_t i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
        printf("alx_z fixed answer: %u bytes, sha256 %s\n", z, hex);
        CHECK(z == KAT_LEN && !strcmp(hex, KAT_HEX), "the fixed answer (%u %s)", z, hex);
    }
    /* real files */
    uint64_t raw = 0, packed = 0;
    if (argc > 1) {
        DIR *d = opendir(argv[1]);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char p[4096];
            snprintf(p, sizeof p, "%s/%s", argv[1], e->d_name);
            FILE *f = fopen(p, "rb");
            if (!f) continue;
            uint32_t n = (uint32_t) fread(A, 1, ALX_Z_MAXPLAIN, f);
            fclose(f);
            if (!n) continue;
            trip(A, n, e->d_name);
            raw += n;
            packed += alx_z_lz4(A, n, B, sizeof B, work);
        }
        if (d) closedir(d);
        if (raw) printf("alx_z: %llu bytes of %s to %llu (%.1f%%)\n", (unsigned long long) raw, argv[1],
                        (unsigned long long) packed, 100.0 * (double) packed / (double) raw);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
