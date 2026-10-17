/* seedbank.c — the Seed Bank archive (seedbank.h). Copyright (c) 2024-2026
 * Michael Laurence Curzi and 36N9 Genetics, LLC; SPDX-License-Identifier:
 * Apache-2.0. */
#include "seedbank.h"

#include <stddef.h>

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) |
           (uint32_t) p[3];
}


static int digest_cmp(const uint8_t a[SB_DIGEST_LEN], const uint8_t b[SB_DIGEST_LEN])
{
    for (uint32_t i = 0; i < SB_DIGEST_LEN; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

int sb_open(sb_bank_t *b, const uint8_t *base, uint32_t len)
{
    uint32_t count, table_off, payload_off, bank_len;
    if (!b || !base || len < SB_HEADER) return -1;
    if (rd32(base) != SB_MAGIC || base[4] != SB_VERSION) return -1;
    count = rd32(base + 5);
    table_off = rd32(base + 9);
    payload_off = rd32(base + 13);
    bank_len = rd32(base + 17);
    if (count > SB_MAX_COUNT) return -1;
    if (bank_len != len) return -1;
    /* the header, the table and the payload arena must all fit, in order */
    if (table_off < SB_HEADER || payload_off < table_off ||
        (uint64_t) table_off + (uint64_t) count * SB_ENTRY > payload_off ||
        (uint64_t) payload_off > bank_len) {
        return -1;
    }
    for (uint32_t i = 0; i < sizeof *b; i++) ((uint8_t *) b)[i] = 0;
    b->base = base;
    b->len = bank_len;
    b->count = count;
    b->table_off = table_off;
    b->payload_off = payload_off;
    b->ok = true;
    return 0;
}

const uint8_t *sb_find(const sb_bank_t *b, const uint8_t digest[SB_DIGEST_LEN],
                       uint32_t *out_len)
{
    const uint8_t *t;
    uint32_t lo, hi;
    if (!b || !b->ok || !digest) return NULL;
    lo = 0;
    hi = b->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c;
        t = b->base + b->table_off + mid * SB_ENTRY;
        c = digest_cmp(t, digest);
        if (c == 0) {
            uint32_t off = rd32(t + 32);
            uint32_t n = rd32(t + 36);
            if ((uint64_t) b->payload_off + off + n > b->len) return NULL;
            if (out_len) *out_len = n;
            return b->base + b->payload_off + off;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

uint32_t sb_count(const sb_bank_t *b)
{
    return b && b->ok ? b->count : 0;
}