/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_file.h — content-addressed, chunked, Merkle-verified file sharing.
 *
 *   F1  A file is cut into chunks of a power-of-two size (1 KiB .. 16 KiB,
 *       default 16 KiB); the last chunk may be short.
 *   F2  Each chunk has a real IPFS raw-block CIDv1 (vna_cid_raw).
 *   F3  Leaves are SHA-256(0x00 || chunk CID); inner nodes are
 *       SHA-256(0x01 || left || right) over the RFC 6962 / RFC 9162 tree shape
 *       (split at the largest power of two below n), so every chunk has an
 *       audit path of at most 16 hashes.
 *   F4  The file ROOT (its DHT key) is SHA-256(0x02 || size_le64 ||
 *       chunk_size_le32 || Merkle root). It commits to the length, so a peer
 *       cannot truncate or extend a file.
 *   F5  STREAMING verification: a chunk travels with its audit path and is
 *       checked against the root the moment it arrives; only verified chunks
 *       are handed on. The receiver never buffers the whole file and keeps
 *       just a bitmap of chunks already accepted (duplicates refused).
 *   F6  Chunks travel only inside authenticated sessions (vna_session.h), and
 *       every request is checked against the server's agreement first.
 *
 * IPFS compatibility, honestly: each chunk's CID is exactly what IPFS gives
 * that block with raw leaves (and a file of one chunk has the same CID as
 * `ipfs add --cid-version=1 --raw-leaves` of it). The ROOT is NOT a UnixFS
 * dag-pb root: IPFS links chunks with protobuf DAG nodes, Vinea with a
 * binary Merkle tree that gives compact per-chunk proofs. An IPFS node can
 * fetch every chunk of a Vinea file by its raw CID; it cannot resolve the
 * Vinea root.
 */
#ifndef VNA_FILE_H
#define VNA_FILE_H

#include "vna_schema.h"
#include "vna_agree.h"
#include "vna_cid.h"

#define VNA_CHUNK_SIZE      16384u
#define VNA_CHUNK_MIN       1024u
#define VNA_FILE_MAX_CHUNKS 65536u
#define VNA_PROOF_MAX       17u
#define VNA_CHUNK_MAGIC     0x324B4E56u /* "VNK2" */
#define VNA_CHUNKREQ_MAGIC  0x32514E56u /* "VNQ2" */

typedef struct {
    uint8_t h[32];
} vna_hash_el_t;

typedef struct {
    uint32_t magic;
    vna_id_t root;
    uint32_t index;
} vna_chunk_req_t;

typedef struct {
    uint32_t magic;
    vna_id_t root;
    uint64_t file_size;
    uint32_t chunk_size;
    uint32_t index;
    uint16_t proof_n;
    vna_hash_el_t proof[VNA_PROOF_MAX];
    uint16_t data_len;
    uint8_t data[VNA_CHUNK_SIZE];
} vna_chunk_t;

extern const vna_schema_t vna_chunk_req_schema;
extern const vna_schema_t vna_chunk_schema;

/* Number of chunks, or 0 if chunk_size is not a power of two in range or the
 * file would need more than VNA_FILE_MAX_CHUNKS. A 0-byte file has 1 chunk. */
uint32_t vna_file_nchunks(uint64_t size, uint32_t chunk_size);

/* Leaf hash of one chunk (F3). */
void vna_file_leaf(const uint8_t *chunk, uint32_t len, uint8_t out[32]);

/* Merkle root over n leaves and the file root (F4). */
void vna_file_mth(const uint8_t (*leaves)[32], uint32_t n, uint8_t out[32]);
void vna_file_root(const uint8_t mth[32], uint64_t size, uint32_t chunk_size, vna_id_t *root);

/* Sender side: leaves for a whole in-memory file. Returns n or 0. */
uint32_t vna_file_build(const uint8_t *data, uint64_t size, uint32_t chunk_size,
                        uint8_t (*leaves)[32], uint32_t max, vna_id_t *root);

/* Audit path for chunk `index` (RFC 6962 PATH). Returns its length. */
uint32_t vna_file_proof(const uint8_t (*leaves)[32], uint32_t n, uint32_t index,
                        vna_hash_el_t *proof);

/* Verify one chunk against a root (F5): length right for its index, path
 * proves into the root. VNA_OK or VNA_ERR_MERKLE / VNA_ERR_PARSE. */
vna_status_t vna_file_verify_chunk(const vna_id_t *root, uint64_t size, uint32_t chunk_size,
                                   uint32_t index, const uint8_t *data, uint32_t len,
                                   const vna_hash_el_t *proof, uint32_t proof_n);

/* ---- streaming receiver ---- */
typedef struct {
    vna_id_t root;
    uint64_t size;
    uint32_t chunk_size;
    uint32_t n;
    uint8_t *bitmap; /* caller buffer, (n + 7) / 8 bytes */
    uint32_t received;
    uint32_t rejected;
} vna_fetch_t;

vna_status_t vna_fetch_init(vna_fetch_t *f, const vna_id_t *root, uint64_t size,
                            uint32_t chunk_size, uint8_t *bitmap, uint32_t bitmap_len);

/* Accept one CHUNK record (canonical bytes). On VNA_OK, *data / *len point at
 * the verified bytes inside `c` (decoded into the caller's vna_chunk_t). */
vna_status_t vna_fetch_accept(vna_fetch_t *f, const uint8_t *bytes, uint32_t len, vna_chunk_t *c);
bool vna_fetch_complete(const vna_fetch_t *f);
/* Next chunk index not yet received, or -1. */
int32_t vna_fetch_next(const vna_fetch_t *f);

/* ---- server side ---- */
typedef struct {
    vna_id_t root;
    uint64_t size;
    uint32_t chunk_size;
    uint32_t n;
    const uint8_t (*leaves)[32];
    const uint8_t *data; /* whole file (the host may stream from storage instead) */
} vna_shared_file_t;

/* Answer a CHUNK_REQ from `peer`: agreement check (VNA_RES_FILE, charged on
 * success), then the CHUNK encoding into out. Returns its length or a
 * negative vna_status_t (VNA_ERR_DENIED when the agreement refuses). */
int32_t vna_file_serve(const vna_shared_file_t *sf, const vna_agreement_t *agr, vna_usage_t *us,
                       const vna_id_t *peer, uint64_t trust, const uint8_t *req, uint32_t req_len,
                       uint64_t now, vna_chunk_t *scratch, uint8_t *out, uint32_t cap);

#endif /* VNA_FILE_H */
