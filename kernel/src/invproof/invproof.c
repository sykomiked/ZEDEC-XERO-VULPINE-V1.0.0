/* invproof.c — ZXI inverse witness: build and verify. See invproof.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV inverse-witness slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "invproof.h"
#include "../robin_debanks/sha256.h"

static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8)); }

static int digest_eq(const uint8_t *a, const uint8_t *b) {
    uint8_t d = 0;
    for (uint32_t i = 0; i < ZXI_DIGEST; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

/* FNV-1a over everything except the seal field itself. */
static uint32_t seal_of(const uint8_t *buf, uint32_t len) {
    uint32_t s = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        if (i >= 84 && i < 88) continue;      /* the seal field */
        s ^= buf[i]; s *= 16777619u;
    }
    return s ? s : 1u;
}

int zxi_build(const uint8_t *before, const uint8_t *after, uint32_t len,
              uint8_t *out, uint32_t outmax) {
    if (!before || !after || !out) return ZXI_ERR_TRUNCATED;
    if (outmax < ZXI_HDR_BYTES) return ZXI_ERR_TRUNCATED;

    /* Walk the two states once, emitting a run per contiguous differing span. */
    uint32_t o = ZXI_HDR_BYTES, runs = 0, i = 0;
    while (i < len) {
        if (before[i] == after[i]) { i++; continue; }
        uint32_t start = i;
        while (i < len && before[i] != after[i] && (i - start) < 0xFFFFu) i++;
        uint32_t rl = i - start;
        if (runs >= ZXI_MAX_RUNS) return ZXI_ERR_TOO_MANY;
        if (o + ZXI_RUN_HDR + rl > outmax) return ZXI_ERR_TRUNCATED;
        wr32(out + o, start);            o += 4;
        wr16(out + o, (uint16_t)rl);     o += 2;
        for (uint32_t k = 0; k < rl; k++)
            out[o + k] = (uint8_t)(before[start + k] ^ after[start + k]);
        o += rl;
        runs++;
    }

    wr32(out + 0, ZXI_MAGIC);
    wr16(out + 4, (uint16_t)ZXI_VERSION);
    wr16(out + 6, 0);                    /* kind: reserved, exact = 0 */
    wr32(out + 8, len);
    wr32(out + 12, runs);
    sha256(before, len, out + 16);
    sha256(after,  len, out + 48);
    /* bytes 80..83 sit between d_after and the seal and were never written,
     * so the seal (and the artifact) carried whatever the caller's buffer
     * held (MemorySanitizer, test_zmedia). They are reserved and zero. */
    wr32(out + 80, 0);
    wr32(out + 84, 0);
    wr32(out + 84, seal_of(out, o));
    return (int)o;
}

bool zxi_is_witness(const uint8_t *buf, uint32_t len) {
    return buf && len >= 4 && rd32(buf) == ZXI_MAGIC;
}

zxi_result_t zxi_verify(const uint8_t *witness, uint32_t wlen,
                        const uint8_t *after, uint32_t after_len,
                        uint8_t *scratch, uint32_t scratch_len) {
    /* Identity before completeness: a caller asking "is this artifact a
     * witness?" must be able to tell "no, it is something else" apart from
     * "yes, but damaged". Checking the length first would collapse both into
     * TRUNCATED. */
    if (!witness || wlen < 4)             return ZXI_ERR_NOT_WITNESS;
    if (rd32(witness) != ZXI_MAGIC)       return ZXI_ERR_NOT_WITNESS;
    if (wlen < ZXI_HDR_BYTES)             return ZXI_ERR_TRUNCATED;
    if (rd16(witness + 4) != ZXI_VERSION) return ZXI_ERR_VERSION;

    uint32_t state_len = rd32(witness + 8);
    uint32_t runs      = rd32(witness + 12);
    const uint8_t *d_before = witness + 16;
    const uint8_t *d_after  = witness + 48;
    uint32_t seal = rd32(witness + 84);

    if (runs > ZXI_MAX_RUNS) return ZXI_ERR_TOO_MANY;
    if (seal_of(witness, wlen) != seal) return ZXI_ERR_SEAL;
    if (!after || after_len != state_len) return ZXI_ERR_STATE_LEN;
    if (!scratch || scratch_len < state_len) return ZXI_ERR_TRUNCATED;

    /* 1. This witness must be about THIS state — otherwise a witness could be
     *    lifted from an unrelated transition where the undo happened to work. */
    uint8_t got[ZXI_DIGEST];
    sha256(after, after_len, got);
    if (!digest_eq(got, d_after)) return ZXI_ERR_AFTER_DIGEST;

    /* 2. Apply the undo: recovered = after XOR delta. Every run is bounds- and
     *    order-checked BEFORE it is applied, so a hostile witness cannot write
     *    outside the state or cover the same byte twice to smuggle in a value. */
    for (uint32_t i = 0; i < state_len; i++) scratch[i] = after[i];

    uint32_t o = ZXI_HDR_BYTES;
    uint32_t prev_end = 0;
    for (uint32_t r = 0; r < runs; r++) {
        if (o + ZXI_RUN_HDR > wlen) return ZXI_ERR_TRUNCATED;
        uint32_t off = rd32(witness + o);
        uint32_t rl  = rd16(witness + o + 4);
        o += ZXI_RUN_HDR;
        if (o + rl > wlen) return ZXI_ERR_TRUNCATED;
        if (off > state_len || rl > state_len - off) return ZXI_ERR_RUN_BOUNDS;
        if (off < prev_end) return ZXI_ERR_RUN_ORDER;   /* overlap / unordered */
        for (uint32_t k = 0; k < rl; k++)
            scratch[off + k] = (uint8_t)(scratch[off + k] ^ witness[o + k]);
        prev_end = off + rl;
        o += rl;
    }

    /* 3. The undo must land exactly on the recorded prior state. */
    sha256(scratch, state_len, got);
    if (!digest_eq(got, d_before)) return ZXI_ERR_NOT_INVERSE;

    return ZXI_OK;
}

const char *zxi_result_name(zxi_result_t r) {
    switch (r) {
        case ZXI_OK:                return "ok";
        case ZXI_ERR_NOT_WITNESS:   return "not an inverse witness";
        case ZXI_ERR_TRUNCATED:     return "truncated";
        case ZXI_ERR_VERSION:       return "bad version";
        case ZXI_ERR_TOO_MANY:      return "too many runs";
        case ZXI_ERR_RUN_BOUNDS:    return "run outside the state";
        case ZXI_ERR_RUN_ORDER:     return "runs overlap or are unordered";
        case ZXI_ERR_SEAL:          return "seal mismatch";
        case ZXI_ERR_STATE_LEN:     return "state length mismatch";
        case ZXI_ERR_AFTER_DIGEST:  return "witness is about a different state";
        case ZXI_ERR_NOT_INVERSE:   return "undo did NOT restore the prior state";
        default:                    return "?";
    }
}
