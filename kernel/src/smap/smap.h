/*
 * smap.h — Reassembly Key File (S-Map) / Structural Manifest
 *
 * Dual-Artifact Pipeline: every ingested payload produces two outputs:
 *   1. Root CID (32-byte BLAKE3-style hash) — fixed routing pointer
 *   2. .smap Reassembly Key File — topological reconstruction blueprint
 *
 * The .smap contains: permutation indices, chunk offsets, XOR masks,
 * phase-derivation keys (K1..K5), and Merkle tree links.
 *
 * Benefits:
 *   - Fixed 32-byte pointers for O(1) network routing regardless of file size
 *   - Separation of identity (CID) and structure (S-Map)
 *   - Instant verification: corrupted .smap breaks hash before reassembly
 *   - Zero-copy storage: shards distributed across RAM/P2P mesh
 *   - Attacker with CID but no .smap has unreconstructable pointer
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifndef ZEDEC_SMAP_H
#define ZEDEC_SMAP_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define SMAP_MAX_CHUNKS       1024
#define SMAP_HASH_SIZE          32
#define SMAP_KEY_SIZE           32
#define SMAP_MAX_PAYLOAD     65536
#define SMAP_CHUNK_SIZE       4096
#define SMAP_MAGIC         0x534D4150  /* "SMAP" */
#define SMAP_VERSION            1
#define SMAP_MAX_FILES         64
#define SMAP_MAX_LABEL         64
#define SMAP_XOR_MASK_SIZE     16

/* Phase derivation keys */
#define SMAP_KEY_K1             1
#define SMAP_KEY_K2             2
#define SMAP_KEY_K3             3
#define SMAP_KEY_K4             4
#define SMAP_KEY_K5             5

/* ===== Chunk Descriptor ===== */

typedef struct {
    uint8_t  chunk_hash[SMAP_HASH_SIZE];   /* Hash of chunk content */
    uint32_t original_offset;               /* Offset in original file */
    uint32_t chunk_size;                    /* Size of this chunk */
    uint32_t shard_index;                   /* Where it's stored (RAM/P2P) */
    uint8_t  xor_mask[SMAP_XOR_MASK_SIZE];  /* XOR encryption mask */
    uint8_t  key_index;                     /* Which derivation key (K1..K5) */
    bool     encrypted;                     /* Is chunk encrypted? */
} smap_chunk_t;

/* ===== S-Map File (Reassembly Key File) ===== */

typedef struct {
    uint32_t magic;                         /* SMAP_MAGIC */
    uint8_t  version;                       /* SMAP_VERSION */
    uint8_t  root_cid[SMAP_HASH_SIZE];      /* Root CID (BLAKE3-style) */
    uint8_t  smap_hash[SMAP_HASH_SIZE];     /* Hash of the S-Map itself */
    uint8_t  merkle_root[SMAP_HASH_SIZE];   /* Merkle root of all chunks */
    uint32_t total_size;                    /* Original file size */
    uint32_t num_chunks;                    /* Number of chunks */
    uint32_t chunk_size;                    /* Default chunk size */
    uint8_t  permutation[SMAP_MAX_CHUNKS];  /* Permutation indices (simplified) */
    smap_chunk_t chunks[SMAP_MAX_CHUNKS];   /* Chunk descriptors */
    uint8_t  derivation_keys[5][SMAP_KEY_SIZE]; /* K1..K5 */
    bool     verified;                      /* Has S-Map been verified? */
    bool     sealed;                        /* Is S-Map sealed (immutable)? */
} smap_t;

/* ===== Stored File Entry ===== */

typedef struct {
    uint8_t  root_cid[SMAP_HASH_SIZE];  /* File identity */
    char     label[SMAP_MAX_LABEL];      /* Human-readable name */
    uint32_t smap_index;                 /* Index into smap table */
    uint32_t total_size;                 /* Original size */
    bool     active;
    bool     shards_available;           /* Are all chunks in local storage? */
} smap_file_t;

/* ===== S-Map Store (Kernel Subsystem) ===== */

typedef struct {
    smap_t      smaps[SMAP_MAX_FILES];
    smap_file_t files[SMAP_MAX_FILES];
    uint32_t    num_smaps;
    uint32_t    num_files;
    bool        initialized;
    uint64_t    total_ingested;
    uint64_t    total_reassembled;
} smap_store_t;

/* ===== API ===== */

void smap_store_init(smap_store_t *st);

/* Ingestion: split raw data into CID + S-Map */
int32_t smap_ingest(smap_store_t *st, const uint8_t *data, uint32_t len,
                     const char *label, uint8_t key_index);

/* Reassembly: reconstruct original data from CID + S-Map */
int smap_reassemble(smap_store_t *st, uint32_t smap_idx,
                     uint8_t *out, uint32_t max_len);

/* Verification */
bool smap_verify(const smap_t *sm);
bool smap_verify_cid(const smap_t *sm, const uint8_t *expected_cid);
int smap_seal(smap_t *sm);

/* Chunk operations */
int smap_chunk_add(smap_t *sm, const uint8_t *chunk_data, uint32_t chunk_len,
                   uint32_t offset, uint8_t key_index);
uint32_t smap_chunk_count(const smap_t *sm);
bool smap_chunks_complete(const smap_t *sm);

/* File lookup */
const smap_file_t *smap_file_find(smap_store_t *st, const uint8_t *cid);
const smap_file_t *smap_file_find_by_label(smap_store_t *st, const char *label);

/* Utility */
void smap_compute_cid(const uint8_t *data, uint32_t len, uint8_t *out);
void smap_compute_merkle(const uint8_t *chunks[], uint32_t num_chunks,
                          uint8_t *out_root);
void smap_derive_key(uint8_t key_index, const uint8_t *seed, uint32_t seed_len,
                     uint8_t *out_key);
const char *smap_key_name(uint8_t key_index);

/* Zero-copy: get pointer without reassembly */
const smap_chunk_t *smap_get_chunk(const smap_t *sm, uint32_t idx);

/* Phase shift: re-sign with different key */
int smap_phase_shift(smap_store_t *st, uint32_t smap_idx, uint8_t new_key_index);

#endif /* ZEDEC_SMAP_H */
