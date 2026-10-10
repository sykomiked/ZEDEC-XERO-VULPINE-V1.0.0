/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_freight.c — the freight packet decoder (freight/freight.c) and the op
 * stream expander (freight/freight_ops.c).
 *
 * Byte 0 picks the mode:
 *   0  header + packets: the next 64 bytes are a freight header, the rest
 *      are 21-byte smart packets handed to freight_decode
 *   1  op stream: the rest is an op stream for freight_ops_peek/_expand
 *   2  encoder round trip: the rest is data; freight_ops_encode then
 *      freight_ops_expand must give it back exactly
 *   3  erasure round trip: byte 1 is k, the next 21 bytes are an erasure
 *      bitmap over the 168 rows, the rest is the payload. Encode, drop the
 *      erased rows, decode: with >= k rows left the payload must come back
 *      exactly; with fewer the decoder must say TOO_FEW. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freight.h"
#include "fuzz_in.h"

#define OUT_MAX 65536u

static uint8_t g_work[FREIGHT_DECODE_WORK_BYTES];
static uint8_t g_out[OUT_MAX];
static uint8_t g_pk[FREIGHT_BYTES];
static uint32_t g_enc_work[FREIGHT_ENC_WORK_WORDS];

static bool resolve_zero(void *ctx, const uint8_t *cid, uint32_t cid_len, uint8_t *out,
                         uint64_t len)
{
    (void) ctx;
    (void) cid;
    (void) cid_len;
    memset(out, 0, (size_t) len);
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 3u * OUT_MAX) return 0;
    uint8_t mode = data[0] & 3u;
    const uint8_t *p = data + 1;
    size_t n = size - 1;

    if (mode == 0) {
        if (n < FREIGHT_HEADER_BYTES) return 0;
        uint8_t *hb = fz_dup(p, FREIGHT_HEADER_BYTES);
        freight_header_t h;
        if (freight_header_parse(hb, &h) == FREIGHT_OK) {
            uint8_t re[FREIGHT_HEADER_BYTES];
            if (freight_header_serialize(&h, re) != FREIGHT_OK) abort();
            freight_header_t h2;
            if (freight_header_parse(re, &h2) != FREIGHT_OK) abort();
            uint32_t np = (uint32_t) ((n - FREIGHT_HEADER_BYTES) / FREIGHT_PACKET_BYTES);
            uint8_t *pk = fz_dup(p + FREIGHT_HEADER_BYTES, (size_t) np * FREIGHT_PACKET_BYTES);
            uint8_t *out = (uint8_t *) malloc(h.payload_len ? h.payload_len : 1u);
            if (!out) abort();
            (void) freight_decode(&h, pk, np, out, h.payload_len, g_work, sizeof g_work);
            free(out);
            free(pk);
        }
        free(hb);
    } else if (mode == 1) {
        uint8_t *ops = fz_dup(p, n);
        uint64_t total = 0, got = 0;
        bool sha = false;
        if (freight_ops_peek(ops, n, &total, &sha) == FREIGHT_OK) {
            int rc = freight_ops_expand(ops, n, g_out, OUT_MAX, OUT_MAX, resolve_zero, NULL, &got);
            if (rc == FREIGHT_OK && got != total) abort();
        }
        free(ops);
    } else if (mode == 2) {
        if (n > OUT_MAX) return 0;
        bool sha = (data[0] & 4u) != 0;
        uint64_t cap = freight_ops_bound(n, sha);
        uint8_t *enc = (uint8_t *) malloc((size_t) cap);
        if (!enc) abort();
        uint64_t elen = 0, got = 0;
        if (freight_ops_encode(p, n, sha, enc, cap, &elen, g_enc_work, FREIGHT_ENC_WORK_WORDS) !=
                FREIGHT_OK ||
            elen > cap)
            abort();
        if (freight_ops_expand(enc, elen, g_out, OUT_MAX, OUT_MAX, NULL, NULL, &got) !=
                FREIGHT_OK ||
            got != n || memcmp(g_out, p, n) != 0)
            abort();
        free(enc);
    } else {
        if (n < 22) return 0;
        uint8_t k = p[0];
        const uint8_t *erase = p + 1;
        const uint8_t *pl = p + 22;
        size_t plen = n - 22;
        if (k == 0 || k > FREIGHT_ROWS || plen > FREIGHT_CAPACITY(k)) return 0;
        freight_header_t h;
        if (freight_header_init(&h, 7u, k, 0, 0, pl, (uint32_t) plen) != FREIGHT_OK) abort();
        if (freight_encode(&h, pl, g_pk) != FREIGHT_OK) abort();
        uint8_t *kept = (uint8_t *) malloc(FREIGHT_BYTES);
        if (!kept) abort();
        uint32_t nk = 0;
        for (uint32_t r = 0; r < FREIGHT_ROWS; r++) {
            if (erase[r / 8] & (1u << (r % 8))) continue;
            memcpy(kept + nk * FREIGHT_PACKET_BYTES, g_pk + r * FREIGHT_PACKET_BYTES,
                   FREIGHT_PACKET_BYTES);
            nk++;
        }
        uint8_t *out = (uint8_t *) malloc(plen ? plen : 1u);
        if (!out) abort();
        int rc = freight_decode(&h, kept, nk, out, (uint32_t) plen, g_work, sizeof g_work);
        if (nk >= k) {
            if (rc != FREIGHT_OK || memcmp(out, pl, plen) != 0) abort();
        } else if (rc != FREIGHT_ERR_TOO_FEW) {
            abort();
        }
        free(out);
        free(kept);
    }
    return 0;
}
