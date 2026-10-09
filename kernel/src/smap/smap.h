/*
 * smap.h — Reassembly Key File (S-Map) / structural manifest
 *
 * Every ingested payload produces two outputs:
 *   1. Root ID: SHA-256 of the whole payload (32 bytes). This is a plain
 *      digest, NOT an IPFS CID; kernel/src/ipfs_node makes real CIDs.
 *   2. The S-Map: per-chunk SHA-256 digests, offsets and shard indices, a
 *      Merkle root over the chunk digests (RFC 6962 tree shape, with 0x00
 *      leaf / 0x01 node domain separation), and a SHA-256 over the canonical
 *      encoding of the whole manifest (smap_hash).
 *
 * WHAT IS REAL
 *   - smap_verify() recomputes smap_hash and the Merkle root and checks the
 *     chunk layout; any edit to the manifest is detected.
 *   - smap_reassemble() takes the shard bytes, checks every shard against its
 *     chunk digest, copies it into place, then checks the result against the
 *     root ID. Tampered shards are rejected.
 *
 * WHAT IS NOT HERE
 *   - No encryption. The K1..K5 key indices are labels carried in the
 *     manifest; nothing is masked or encrypted, and nothing is secret. Anyone
 *     holding the shards can reassemble them without the S-Map (the
 *     permutation is the identity).
 *   - No storage or networking: the caller holds the shards.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_SMAP_H
#define ZEDEC_SMAP_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define SMAP_MAX_CHUNKS  1024
#define SMAP_HASH_SIZE   32
#define SMAP_KEY_SIZE    32
#define SMAP_MAX_PAYLOAD 65536
#define SMAP_CHUNK_SIZE  4096
#define SMAP_MAGIC       0x534D4150 /* "SMAP" */
#define SMAP_VERSION     2
#define SMAP_MAX_FILES   64
#define SMAP_MAX_LABEL   64

/* Key-index labels (no keys exist; see above) */
#define SMAP_KEY_K1 1
#define SMAP_KEY_K2 2
#define SMAP_KEY_K3 3
#define SMAP_KEY_K4 4
#define SMAP_KEY_K5 5

/* Error codes */
#define SMAP_EINVAL   (-1) /* bad argument or not verified */
#define SMAP_ECORRUPT (-2) /* manifest hash or Merkle root mismatch */
#define SMAP_ESHARD   (-3) /* a shard does not match its chunk digest */
#define SMAP_EROOT    (-4) /* reassembled payload does not match the root ID */

/* ===== Chunk Descriptor ===== */

typedef struct {
    uint8_t chunk_hash[SMAP_HASH_SIZE]; /* SHA-256 of chunk content */
    uint32_t original_offset;           /* Offset in original file */
    uint32_t chunk_size;                /* Size of this chunk */
    uint32_t shard_index;               /* Shard slot holding it */
    uint8_t key_index;                  /* K1..K5 label only */
} smap_chunk_t;

/* ===== S-Map File (Reassembly Key File) ===== */

typedef struct {
    uint32_t magic;                        /* SMAP_MAGIC */
    uint8_t version;                       /* SMAP_VERSION */
    uint8_t root_cid[SMAP_HASH_SIZE];      /* SHA-256 of the payload */
    uint8_t smap_hash[SMAP_HASH_SIZE];     /* SHA-256 of the canonical manifest */
    uint8_t merkle_root[SMAP_HASH_SIZE];   /* Merkle root of chunk digests */
    uint32_t total_size;                   /* Original file size */
    uint32_t num_chunks;                   /* Number of chunks */
    uint32_t chunk_size;                   /* Default chunk size */
    uint16_t permutation[SMAP_MAX_CHUNKS]; /* chunk -> shard (identity today) */
    smap_chunk_t chunks[SMAP_MAX_CHUNKS];  /* Chunk descriptors */
    bool verified;                         /* Last smap_verify() passed */
    bool sealed;                           /* Is S-Map sealed (immutable)? */
} smap_t;

/* ===== Stored File Entry ===== */

typedef struct {
    uint8_t root_cid[SMAP_HASH_SIZE]; /* File identity */
    char label[SMAP_MAX_LABEL];       /* Human-readable name */
    uint32_t smap_index;              /* Index into smap table */
    uint32_t total_size;              /* Original size */
    bool active;
    bool shards_available; /* always false: smap stores no shard bytes */
} smap_file_t;

/* ===== S-Map Store (Kernel Subsystem) ===== */

typedef struct {
    smap_t smaps[SMAP_MAX_FILES];
    smap_file_t files[SMAP_MAX_FILES];
    uint32_t num_smaps;
    uint32_t num_files;
    bool initialized;
    uint64_t total_ingested;
    uint64_t total_reassembled;
} smap_store_t;

/* ===== API ===== */

void smap_store_init(smap_store_t *st);

/* Ingestion: build the root ID and S-Map for data (len <= SMAP_MAX_PAYLOAD).
 * The caller keeps the shard bytes (chunk i is data[i*SMAP_CHUNK_SIZE ...]). */
int32_t smap_ingest(smap_store_t *st, const uint8_t *data, uint32_t len, const char *label,
                    uint8_t key_index);

/* Reassembly from shard bytes laid out in shard-index order (for an ingested
 * payload that is the original byte layout). Verifies the manifest, every
 * shard, and the root ID. Returns the payload size or a SMAP_E* code. */
int smap_reassemble(smap_store_t *st, uint32_t smap_idx, const uint8_t *shards, uint32_t shards_len,
                    uint8_t *out, uint32_t max_len);

/* Verification */
bool smap_verify(const smap_t *sm);
bool smap_verify_cid(const smap_t *sm, const uint8_t *expected_cid);
int smap_seal(smap_t *sm);
/* Recompute and store smap_hash and merkle_root after editing a manifest. */
void smap_rehash(smap_t *sm);

/* Chunk operations */
int smap_chunk_add(smap_t *sm, const uint8_t *chunk_data, uint32_t chunk_len, uint32_t offset,
                   uint8_t key_index);
uint32_t smap_chunk_count(const smap_t *sm);
bool smap_chunks_complete(const smap_t *sm);

/* File lookup */
const smap_file_t *smap_file_find(smap_store_t *st, const uint8_t *cid);
const smap_file_t *smap_file_find_by_label(smap_store_t *st, const char *label);

/* Utility */
void smap_compute_cid(const uint8_t *data, uint32_t len, uint8_t *out);
/* Merkle root over n leaf digests (RFC 6962 shape). n == 0 gives SHA-256(""). */
void smap_compute_merkle(const uint8_t (*leaves)[SMAP_HASH_SIZE], uint32_t n, uint8_t *out_root);
const char *smap_key_name(uint8_t key_index);

/* Zero-copy: get a chunk descriptor */
const smap_chunk_t *smap_get_chunk(const smap_t *sm, uint32_t idx);

/* Relabel every chunk with another key index and rehash the manifest. The
 * root ID does not change. This is a label change, not a re-keying. */
int smap_phase_shift(smap_store_t *st, uint32_t smap_idx, uint8_t new_key_index);

#endif /* ZEDEC_SMAP_H */
