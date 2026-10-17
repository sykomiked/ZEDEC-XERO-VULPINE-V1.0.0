/* test_seedbank.c — the Seed Bank archive: open, find, slice, bounds.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0. */
#include <stdio.h>
#include <string.h>

#include "seedbank.h"

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

/* build a bank over `buf`: the seeds given as (digest, bytes) pairs.
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
    /* entries sorted by digest */
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
    static uint8_t bank[4096];
    /* three seeds, digests ascending */
    static const uint8_t d0[32] = {0x01}, d1[32] = {0x11}, d2[32] = {0x99};
    static const uint8_t s0[] = {1, 2, 3, 4};
    static const uint8_t s1[] = {9, 9};
    static const uint8_t s2[] = {7};
    static const uint8_t miss[32] = {0x55};
    const uint8_t *const digests[3] = {d0, d1, d2};
    const uint8_t *const seeds[3] = {s0, s1, s2};
    const uint32_t seeds_len[3] = {sizeof s0, sizeof s1, sizeof s2};

    uint32_t blen = build(bank, sizeof bank, digests, seeds, seeds_len, 3);
    CHECK(blen > 0, "bank built");
    sb_bank_t b;
    CHECK(sb_open(&b, bank, blen) == 0, "open");
    CHECK(sb_count(&b) == 3, "count = 3");
    CHECK(!(sb_open(&b, bank, blen - 1) == 0), "truncated bank refused");
    CHECK(!(sb_open(&b, bank + 1, blen) == 0), "bad magic refused");
    uint32_t n = 0;
    const uint8_t *p = sb_find(&b, d0, &n);
    CHECK(p && n == sizeof s0 && !memcmp(p, s0, sizeof s0), "seed 0 found exact");
    p = sb_find(&b, d2, &n);
    CHECK(p && n == sizeof s2 && *p == 7, "seed 2 (last) found exact");
    p = sb_find(&b, d1, &n);
    CHECK(p && n == sizeof s1 && p[1] == 9, "seed 1 (middle) found exact");
    CHECK(sb_find(&b, miss, &n) == NULL, "unknown digest misses");
    /* the empty bank: valid, finds nothing */
    {
        static uint8_t eb[SB_HEADER + 0];
        memset(eb, 0, sizeof eb);
        memcpy(eb, "ZXSB", 4);
        eb[4] = SB_VERSION;
        eb[9] = (uint8_t) (SB_HEADER >> 24);
        eb[10] = (uint8_t) (SB_HEADER >> 16);
        eb[11] = (uint8_t) (SB_HEADER >> 8);
        eb[12] = (uint8_t) SB_HEADER;
        eb[13] = (uint8_t) (SB_HEADER >> 24);
        eb[14] = (uint8_t) (SB_HEADER >> 16);
        eb[15] = (uint8_t) (SB_HEADER >> 8);
        eb[16] = (uint8_t) SB_HEADER;
        eb[17] = (uint8_t) (SB_HEADER >> 24);
        eb[18] = (uint8_t) (SB_HEADER >> 16);
        eb[19] = (uint8_t) (SB_HEADER >> 8);
        eb[20] = (uint8_t) SB_HEADER;
        CHECK(sb_open(&b, eb, sizeof eb) == 0, "empty bank opens");
        CHECK(sb_count(&b) == 0, "empty bank count");
        CHECK(sb_find(&b, d0, &n) == NULL, "empty bank misses");
    }
    printf("seedbank: %d checks, %d failures\n", passed, fails);
    return fails ? 1 : 0;
}