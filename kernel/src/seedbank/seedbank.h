/* seedbank.h — the Seed Bank: a contiguous, indexed archive of seed
 * records. A bank packs many tiny seeds into one immutable object with a
 * jump table mapping each seed's digest to its byte offset, so a lookup
 * is O(log n) and a slice is O(1) pointer arithmetic — no DHT storm of
 * provider records, no per-seed object overhead, one CID to pin.
 *
 * Format (all big-endian, fixed layout, zero allocation):
 *
 *   0   magic "ZXSB"
 *   4   version u8 = 1
 *   5   count     u32      number of jump-table entries
 *   9   table_off u32      byte offset of the jump table (from byte 0)
 *   13  payload_off u32    byte offset of the payload arena
 *   17  bank_len  u32      total length of the bank
 *   21  12 bytes reserved  (zero)
 *   32  jump table: count x 38 bytes, sorted by digest:
 *          digest[32]  the seed's content digest (sha256 of its bytes)
 *          off     u32  payload offset (from byte 0)
 *          len     u32  payload length
 *   ... payload arena: the seeds back-to-back, each aligned to 16.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SEEDBANK_H
#define SEEDBANK_H

#include <stdbool.h>
#include <stdint.h>

#define SB_MAGIC      0x5a585342u /* "ZXSB" big-endian */
#define SB_VERSION    1u
#define SB_ALIGN      16u
#define SB_ENTRY      40u /* digest 32 + off 4 + len 4 */
#define SB_HEADER     32u
#define SB_DIGEST_LEN 32u
#define SB_MAX_COUNT  65535u

typedef struct {
    const uint8_t *base;
    uint32_t len;
    uint32_t count;
    uint32_t table_off;
    uint32_t payload_off;
    bool ok;
} sb_bank_t;

/* Open a bank over `base` (bounded). 0, or a negative status. */
int sb_open(sb_bank_t *b, const uint8_t *base, uint32_t len);

/* Find a seed by its digest. Returns a pointer into the bank (the slice
 * is the caller's to read, bounded by *out_len), or NULL when absent. */
const uint8_t *sb_find(const sb_bank_t *b, const uint8_t digest[SB_DIGEST_LEN],
                       uint32_t *out_len);

/* The count of seeds in the bank. */
uint32_t sb_count(const sb_bank_t *b);

#endif /* SEEDBANK_H */