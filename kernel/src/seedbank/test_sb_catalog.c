/* test_sb_catalog.c — the master catalog: find across banks, misses,
 * bounds. Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
 * Genetics, LLC; SPDX-License-Identifier: Apache-2.0. */
#include <stdio.h>
#include <string.h>

#include "catalog.h"

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

/* build a bank over `buf`: (digest, bytes) pairs; sorted by digest.
 * Returns the bank length, or 0 on overflow. */
static uint32_t build(uint8_t *buf, uint32_t cap, const uint8_t *const *digests,
                      const uint8_t *const *seeds, const uint32_t *seeds_len, uint32_t n)
{
    uint32_t table_off = SB_HEADER, payload_off = table_off + n * SB_ENTRY, at = payload_off;
    if (payload_off > cap) return 0;
    memset(buf, 0, cap);
    buf[0] = 'Z';
    buf[1] = 'X';
    buf[2] = 'S';
    buf[3] = 'B';
    buf[4] = SB_VERSION;
    buf[9] = (uint8_t) (table_off >> 24);
    buf[10] = (uint8_t) (table_off >> 16);
    buf[11] = (uint8_t) (table_off >> 8);
    buf[12] = (uint8_t) table_off;
    buf[13] = (uint8_t) (payload_off >> 24);
    buf[14] = (uint8_t) (payload_off >> 16);
    buf[15] = (uint8_t) (payload_off >> 8);
    buf[16] = (uint8_t) payload_off;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = buf + table_off + i * SB_ENTRY;
        memcpy(e, digests[i], SB_DIGEST_LEN);
        uint32_t off = at - payload_off;
        e[32] = (uint8_t) (off >> 24);
        e[33] = (uint8_t) (off >> 16);
        e[34] = (uint8_t) (off >> 8);
        e[35] = (uint8_t) off;
        e[36] = (uint8_t) (seeds_len[i] >> 24);
        e[37] = (uint8_t) (seeds_len[i] >> 16);
        e[38] = (uint8_t) (seeds_len[i] >> 8);
        e[39] = (uint8_t) seeds_len[i];
        if (at + seeds_len[i] > cap) return 0;
        memcpy(buf + at, seeds[i], seeds_len[i]);
        at += (seeds_len[i] + SB_ALIGN - 1) & ~(SB_ALIGN - 1u);
    }
    buf[17] = (uint8_t) (at >> 24);
    buf[18] = (uint8_t) (at >> 16);
    buf[19] = (uint8_t) (at >> 8);
    buf[20] = (uint8_t) at;
    buf[5] = (uint8_t) (n >> 24);
    buf[6] = (uint8_t) (n >> 16);
    buf[7] = (uint8_t) (n >> 8);
    buf[8] = (uint8_t) n;
    return at;
}

int main(void)
{
    int passed = 0, fails = 0;
    static uint8_t b0[2048], b1[2048];
    static const uint8_t d0[32] = {0x02}, d1[32] = {0x10}, d2[32] = {0x88}, d3[32] = {0xA0};
    static const uint8_t s0[] = {1, 2, 3};
    static const uint8_t s1[] = {4};
    static const uint8_t s2[] = {5, 6, 7, 8, 9};
    static const uint8_t s3[] = {9, 8};
    static const uint8_t miss[32] = {0x55};
    /* bank 0: d0, d2; bank 1: d1, d3 */
    const uint8_t *const digs0[2] = {d0, d2};
    const uint8_t *const seeds0[2] = {s0, s2};
    const uint32_t lens0[2] = {sizeof s0, sizeof s2};
    const uint8_t *const digs1[2] = {d1, d3};
    const uint8_t *const seeds1[2] = {s1, s3};
    const uint32_t lens1[2] = {sizeof s1, sizeof s3};
    uint32_t n0 = build(b0, sizeof b0, digs0, seeds0, lens0, 2);
    uint32_t n1 = build(b1, sizeof b1, digs1, seeds1, lens1, 2);
    CHECK(n0 && n1, "banks built");
    sb_bank_t B[2];
    CHECK(sb_open(&B[0], b0, n0) == 0 && sb_open(&B[1], b1, n1) == 0, "banks open");
    sb_catalog_t c;
    sb_catalog_init(&c, B, 2);
    CHECK(c.nbanks == 2, "catalog holds both banks");
    uint32_t len = 0, bank = 99;
    const uint8_t *p = sb_catalog_find(&c, d0, &len, &bank);
    CHECK(p && bank == 0 && len == 3 && p[0] == 1, "bank 0 hit");
    p = sb_catalog_find(&c, d3, &len, &bank);
    CHECK(p && bank == 1 && len == 2 && p[1] == 8, "bank 1 hit");
    p = sb_catalog_find(&c, d2, &len, &bank);
    CHECK(p && bank == 0 && len == 5 && p[4] == 9, "second entry of bank 0");
    CHECK(sb_catalog_find(&c, miss, &len, &bank) == NULL &&
              sb_catalog_bank_of(&c, miss) == SB_CAT_MAX,
          "unknown digest misses");
    CHECK(sb_catalog_bank_of(&c, d1) == 1, "bank_of resolves bank 1");
    sb_catalog_init(&c, B, 0);
    CHECK(sb_catalog_find(&c, d0, &len, &bank) == NULL, "empty catalog misses");
    printf("sb_catalog: %d checks, %d failures\n", passed, fails);
    return fails ? 1 : 0;
}