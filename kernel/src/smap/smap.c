/*
 * smap.c — Reassembly Key File (S-Map) implementation.
 *
 * All digests are SHA-256 (robin_debanks/sha256.c). This replaces an FNV-1a
 * "BLAKE3 substitute" that anyone could collide, and a reassembly routine that
 * wrote nothing to its output and returned the size "as proof". See smap.h for
 * what is and is not implemented.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "smap.h"
#include "../robin_debanks/sha256.h"

/* ===== Helpers ===== */

static void sm_memset(void *dst, int v, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t) v;
}

static void sm_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

/* Constant-time equality for digests. */
static bool sm_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static void sm_strncpy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int sm_strcmp(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int) (unsigned char) a[i] - (int) (unsigned char) b[i];
        i++;
    }
    return (int) (unsigned char) a[i] - (int) (unsigned char) b[i];
}

static void sm_upd32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
    sha256_update(c, b, 4);
}

/* ===== Hashes ===== */

void smap_compute_cid(const uint8_t *data, uint32_t len, uint8_t *out)
{
    sha256(data, len, out);
}

static void sm_leaf(const uint8_t digest[SMAP_HASH_SIZE], uint8_t out[SMAP_HASH_SIZE])
{
    sha256_ctx_t c;
    uint8_t tag = 0x00;
    sha256_init(&c);
    sha256_update(&c, &tag, 1);
    sha256_update(&c, digest, SMAP_HASH_SIZE);
    sha256_final(&c, out);
}

/* RFC 6962 MTH: split at the largest power of two below n. Depth <= 11.
 * Leaves are `stride` bytes apart so chunk descriptors can be used in place. */
static void sm_mth(const uint8_t *base, uint32_t stride, uint32_t n, uint8_t out[SMAP_HASH_SIZE])
{
    if (n == 1) {
        sm_leaf(base, out);
        return;
    }
    uint32_t k = 1;
    while (k * 2 < n) k *= 2;
    uint8_t l[SMAP_HASH_SIZE], r[SMAP_HASH_SIZE], tag = 0x01;
    sm_mth(base, stride, k, l);
    sm_mth(base + k * stride, stride, n - k, r);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &tag, 1);
    sha256_update(&c, l, SMAP_HASH_SIZE);
    sha256_update(&c, r, SMAP_HASH_SIZE);
    sha256_final(&c, out);
}

void smap_compute_merkle(const uint8_t (*leaves)[SMAP_HASH_SIZE], uint32_t n, uint8_t *out_root)
{
    if (n == 0 || !leaves) {
        sha256((const uint8_t *) "", 0, out_root);
        return;
    }
    sm_mth(leaves[0], SMAP_HASH_SIZE, n, out_root);
}

/* Merkle root over the chunk digests of a manifest (chunk_hash is the first
 * member of smap_chunk_t, so the descriptors are the leaves). */
static void sm_manifest_merkle(const smap_t *sm, uint8_t out[SMAP_HASH_SIZE])
{
    uint32_t n = sm->num_chunks > SMAP_MAX_CHUNKS ? SMAP_MAX_CHUNKS : sm->num_chunks;
    if (n == 0) {
        sha256((const uint8_t *) "", 0, out);
        return;
    }
    sm_mth(sm->chunks[0].chunk_hash, (uint32_t) sizeof(smap_chunk_t), n, out);
}

/* SHA-256 over the canonical little-endian encoding of the manifest. The
 * `verified`/`sealed` flags and smap_hash itself are not covered. */
static void sm_manifest_hash(const smap_t *sm, uint8_t out[SMAP_HASH_SIZE])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "ZXV-SMAP", 8);
    sm_upd32(&c, sm->magic);
    sm_upd32(&c, sm->version);
    sha256_update(&c, sm->root_cid, SMAP_HASH_SIZE);
    sha256_update(&c, sm->merkle_root, SMAP_HASH_SIZE);
    sm_upd32(&c, sm->total_size);
    sm_upd32(&c, sm->num_chunks);
    sm_upd32(&c, sm->chunk_size);
    uint32_t n = sm->num_chunks > SMAP_MAX_CHUNKS ? SMAP_MAX_CHUNKS : sm->num_chunks;
    for (uint32_t i = 0; i < n; i++) {
        const smap_chunk_t *k = &sm->chunks[i];
        sha256_update(&c, k->chunk_hash, SMAP_HASH_SIZE);
        sm_upd32(&c, k->original_offset);
        sm_upd32(&c, k->chunk_size);
        sm_upd32(&c, k->shard_index);
        sm_upd32(&c, k->key_index);
        sm_upd32(&c, sm->permutation[i]);
    }
    sha256_final(&c, out);
}

void smap_rehash(smap_t *sm)
{
    if (!sm) return;
    sm_manifest_merkle(sm, sm->merkle_root);
    sm_manifest_hash(sm, sm->smap_hash);
}

/* ===== Init ===== */

void smap_store_init(smap_store_t *st)
{
    sm_memset(st, 0, sizeof(smap_store_t));
    st->initialized = true;
}

/* ===== Chunk Operations ===== */

int smap_chunk_add(smap_t *sm, const uint8_t *chunk_data, uint32_t chunk_len, uint32_t offset,
                   uint8_t key_index)
{
    if (!sm || !chunk_data || sm->sealed || sm->num_chunks >= SMAP_MAX_CHUNKS) return -1;
    if (key_index < SMAP_KEY_K1 || key_index > SMAP_KEY_K5) return -1;

    uint32_t idx = sm->num_chunks;
    smap_chunk_t *c = &sm->chunks[idx];
    sm_memset(c, 0, sizeof(smap_chunk_t));
    sha256(chunk_data, chunk_len, c->chunk_hash);
    c->original_offset = offset;
    c->chunk_size = chunk_len;
    c->shard_index = idx;
    c->key_index = key_index;
    sm->permutation[idx] = (uint16_t) idx;
    sm->num_chunks++;
    return (int) idx;
}

uint32_t smap_chunk_count(const smap_t *sm)
{
    return sm->num_chunks;
}

/* Complete means the chunks tile [0, total_size) exactly, in order. */
bool smap_chunks_complete(const smap_t *sm)
{
    if (!sm || sm->num_chunks == 0 || sm->num_chunks > SMAP_MAX_CHUNKS) return false;
    uint32_t expect = 0;
    for (uint32_t i = 0; i < sm->num_chunks; i++) {
        if (sm->chunks[i].original_offset != expect) return false;
        if (sm->chunks[i].chunk_size == 0 || sm->chunks[i].chunk_size > SMAP_CHUNK_SIZE)
            return false;
        if (sm->chunks[i].shard_index != i || sm->permutation[i] != i) return false;
        expect += sm->chunks[i].chunk_size;
    }
    return expect == sm->total_size;
}

/* ===== Ingestion ===== */

int32_t smap_ingest(smap_store_t *st, const uint8_t *data, uint32_t len, const char *label,
                    uint8_t key_index)
{
    if (!st || st->num_smaps >= SMAP_MAX_FILES || !data || len == 0) return -1;
    if (len > SMAP_MAX_PAYLOAD) return -1;
    if (key_index < SMAP_KEY_K1 || key_index > SMAP_KEY_K5) return -1;

    int32_t idx = (int32_t) st->num_smaps;
    smap_t *sm = &st->smaps[idx];
    sm_memset(sm, 0, sizeof(smap_t));
    sm->magic = SMAP_MAGIC;
    sm->version = SMAP_VERSION;
    sm->total_size = len;
    sm->chunk_size = SMAP_CHUNK_SIZE;

    for (uint32_t off = 0; off < len;) {
        uint32_t clen = len - off;
        if (clen > SMAP_CHUNK_SIZE) clen = SMAP_CHUNK_SIZE;
        if (smap_chunk_add(sm, data + off, clen, off, key_index) < 0) return -1;
        off += clen;
    }
    smap_compute_cid(data, len, sm->root_cid);
    smap_rehash(sm);
    sm->verified = smap_verify(sm);
    if (!sm->verified) return -1;

    smap_file_t *f = &st->files[idx];
    sm_memset(f, 0, sizeof(smap_file_t));
    sm_memcpy(f->root_cid, sm->root_cid, SMAP_HASH_SIZE);
    sm_strncpy(f->label, label ? label : "[unnamed]", SMAP_MAX_LABEL);
    f->smap_index = (uint32_t) idx;
    f->total_size = len;
    f->active = true;
    f->shards_available = false;

    st->num_smaps++;
    st->num_files++;
    st->total_ingested += len;
    return idx;
}

/* ===== Reassembly ===== */

int smap_reassemble(smap_store_t *st, uint32_t smap_idx, const uint8_t *shards, uint32_t shards_len,
                    uint8_t *out, uint32_t max_len)
{
    if (!st || smap_idx >= st->num_smaps || !out || !shards) return SMAP_EINVAL;
    smap_t *sm = &st->smaps[smap_idx];
    if (sm->total_size > max_len || shards_len < sm->total_size) return SMAP_EINVAL;
    if (!smap_verify(sm)) {
        sm->verified = false;
        return SMAP_ECORRUPT;
    }

    /* Shards are laid out in shard-index order; with the identity
     * permutation shard i starts at chunk i's original offset. */
    for (uint32_t i = 0; i < sm->num_chunks; i++) {
        const smap_chunk_t *c = &sm->chunks[i];
        const uint8_t *src = shards + c->original_offset;
        uint8_t h[SMAP_HASH_SIZE];
        sha256(src, c->chunk_size, h);
        if (!sm_eq(h, c->chunk_hash, SMAP_HASH_SIZE)) {
            sm_memset(out, 0, sm->total_size);
            return SMAP_ESHARD;
        }
        sm_memcpy(out + c->original_offset, src, c->chunk_size);
    }

    uint8_t root[SMAP_HASH_SIZE];
    smap_compute_cid(out, sm->total_size, root);
    if (!sm_eq(root, sm->root_cid, SMAP_HASH_SIZE)) {
        sm_memset(out, 0, sm->total_size);
        return SMAP_EROOT;
    }
    st->total_reassembled += sm->total_size;
    return (int) sm->total_size;
}

/* ===== Verification ===== */

bool smap_verify(const smap_t *sm)
{
    if (!sm) return false;
    if (sm->magic != SMAP_MAGIC || sm->version != SMAP_VERSION) return false;
    if (!smap_chunks_complete(sm)) return false;

    uint8_t h[SMAP_HASH_SIZE];
    sm_manifest_merkle(sm, h);
    if (!sm_eq(h, sm->merkle_root, SMAP_HASH_SIZE)) return false;
    sm_manifest_hash(sm, h);
    return sm_eq(h, sm->smap_hash, SMAP_HASH_SIZE);
}

bool smap_verify_cid(const smap_t *sm, const uint8_t *expected_cid)
{
    if (!sm || !expected_cid) return false;
    return sm_eq(sm->root_cid, expected_cid, SMAP_HASH_SIZE);
}

int smap_seal(smap_t *sm)
{
    if (!sm || !smap_verify(sm)) return -1;
    sm->verified = true;
    sm->sealed = true;
    return 0;
}

/* ===== File Lookup ===== */

const smap_file_t *smap_file_find(smap_store_t *st, const uint8_t *cid)
{
    if (!st || !cid) return 0;
    for (uint32_t i = 0; i < st->num_files; i++) {
        if (st->files[i].active && sm_eq(st->files[i].root_cid, cid, SMAP_HASH_SIZE))
            return &st->files[i];
    }
    return 0;
}

const smap_file_t *smap_file_find_by_label(smap_store_t *st, const char *label)
{
    if (!st || !label) return 0;
    for (uint32_t i = 0; i < st->num_files; i++) {
        if (st->files[i].active && sm_strcmp(st->files[i].label, label) == 0) return &st->files[i];
    }
    return 0;
}

/* ===== Zero-Copy Access ===== */

const smap_chunk_t *smap_get_chunk(const smap_t *sm, uint32_t idx)
{
    if (!sm || idx >= sm->num_chunks) return 0;
    return &sm->chunks[idx];
}

/* ===== Phase Shift (relabel) ===== */

int smap_phase_shift(smap_store_t *st, uint32_t smap_idx, uint8_t new_key_index)
{
    if (!st || smap_idx >= st->num_smaps) return -1;
    if (new_key_index < SMAP_KEY_K1 || new_key_index > SMAP_KEY_K5) return -1;
    smap_t *sm = &st->smaps[smap_idx];
    if (!sm->sealed || !smap_verify(sm)) return -1;
    for (uint32_t i = 0; i < sm->num_chunks; i++) sm->chunks[i].key_index = new_key_index;
    smap_rehash(sm);
    return 0;
}

/* ===== Utility ===== */

const char *smap_key_name(uint8_t key_index)
{
    switch (key_index) {
    case SMAP_KEY_K1:
        return "K1 (Surface)";
    case SMAP_KEY_K2:
        return "K2 (Deep)";
    case SMAP_KEY_K3:
        return "K3 (Onion)";
    case SMAP_KEY_K4:
        return "K4 (Garlic)";
    case SMAP_KEY_K5:
        return "K5 (Shadow)";
    default:
        return "UNKNOWN";
    }
}
