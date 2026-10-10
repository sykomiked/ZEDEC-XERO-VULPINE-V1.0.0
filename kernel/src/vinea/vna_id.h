/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_id.h — node identity, Sybil proof-of-work, the verified key cache and
 * the replay window.
 *
 * IDENTITY
 *   I1  A node's long-term key is an ML-DSA-65 key pair (FIPS 204). There is
 *       no classical-only identity or signature path anywhere in Vinea.
 *   I2  NodeID = SHA3-256(ML-DSA-65 public key), 256 bits, never truncated.
 *       Every message carries the sender's public key, so any receiver can
 *       check the binding itself (self-certifying identifiers).
 *   I3  Proof of work (S/Kademlia "static puzzle", nonce form): a NodeID is
 *       admitted only if SHA3-256("vinea/v2/pow" || NodeID || nonce_le64)
 *       has at least `bits` leading zero bits. Each identity therefore costs
 *       ~2^bits hashes on top of a key generation, which prices Sybil floods.
 *       The difficulty is configuration (0 disables it).
 *
 * KEY CACHE (exact efficiency)
 *   A verified (NodeID -> public key, pow nonce) binding is remembered. A later
 *   message with byte-identical key and nonce skips re-hashing: comparing the
 *   1952 bytes against the bytes that were already hashed to that NodeID is an
 *   exact test, so no security is traded.
 *
 * REPLAY WINDOW
 *   R1  Each sender numbers its messages with a strictly increasing 64-bit
 *       sequence. A receiver keeps, per peer, the highest sequence seen and a
 *       64-bit bitmap of the 64 below it: any sequence at or below the window
 *       floor, or already marked, is a replay.
 *   R2  The window is only advanced AFTER the signature verified, so forged
 *       messages cannot poison it.
 *   R3  The table is bounded. Each entry also keeps the newest timestamp it
 *       accepted. When an entry is evicted, that timestamp raises a floor in
 *       one of VNA_REPLAY_FLOORS slots chosen by hashing the peer's NodeID; a
 *       peer without an entry is then accepted only with a timestamp ABOVE
 *       its slot's floor. Every message accepted from the evicted peer had a
 *       timestamp at or below that floor, so none of them can be replayed into
 *       a fresh entry (exact for replays). The only cost is a false refusal of
 *       a fresh message from a peer that shares the slot and whose timestamp
 *       is not newer than the floor (fail closed; an honest peer retries).
 *       Earlier drafts used one global floor = time of the last eviction; on
 *       a busy node that refused every in-flight message from untracked peers
 *       and let an attacker who forces evictions starve the node, so the
 *       floor is per slot and set from the evicted peer's own timestamps.
 */
#ifndef VNA_ID_H
#define VNA_ID_H

#include "vna_common.h"
#include "vna_pq.h"

typedef struct {
    uint8_t pk[VNA_PK_LEN];
    uint8_t sk[VNA_SK_LEN];
    vna_id_t id;
    uint64_t pow_nonce;
    uint32_t pow_bits;
} vna_identity_t;

/* Derive the identity from a 32-byte seed and search a proof-of-work nonce of
 * `pow_bits` (<= 32). Returns VNA_OK, or VNA_ERR_CAP if no nonce was found
 * within max_tries. */
vna_status_t vna_identity_create(vna_identity_t *idn, const uint8_t seed[32], uint32_t pow_bits,
                                 uint64_t max_tries);

void vna_node_id(const uint8_t pk[VNA_PK_LEN], vna_id_t *out);
bool vna_pow_ok(const vna_id_t *id, uint64_t nonce, uint32_t bits);

/* ---- verified key cache ---- */
typedef struct {
    vna_id_t id;
    uint8_t pk[VNA_PK_LEN];
    uint64_t pow_nonce;
    uint32_t pow_bits; /* difficulty this entry was verified at */
    uint64_t last_used;
    bool used;
} vna_keycache_ent_t;

typedef struct {
    vna_keycache_ent_t *e;
    uint32_t cap;
    uint64_t tick;
    uint64_t hits, misses;
} vna_keycache_t;

void vna_keycache_init(vna_keycache_t *kc, vna_keycache_ent_t *storage, uint32_t cap);

/* Check that pk binds to id (and the PoW at `pow_bits`), using the cache.
 * Inserts the binding on success. VNA_OK / VNA_ERR_BINDING / VNA_ERR_POW. */
vna_status_t vna_keycache_check(vna_keycache_t *kc, const vna_id_t *id,
                                const uint8_t pk[VNA_PK_LEN], uint64_t pow_nonce,
                                uint32_t pow_bits);

/* The cached public key for id, or NULL. */
const uint8_t *vna_keycache_get(vna_keycache_t *kc, const vna_id_t *id);

/* ---- replay window ---- */
#define VNA_REPLAY_FLOORS 64u
typedef struct {
    vna_id_t peer;
    uint64_t hi;     /* highest sequence accepted */
    uint64_t bitmap; /* bit i set: sequence hi - 1 - i accepted */
    uint64_t ts_hi;  /* newest timestamp accepted (R3) */
    uint64_t last_used;
    bool used;
} vna_replay_ent_t;

typedef struct {
    vna_replay_ent_t *e;
    uint32_t cap;
    uint64_t tick;
    uint64_t floor_ms[VNA_REPLAY_FLOORS]; /* R3, indexed by NodeID hash */
    uint64_t window_ms;                   /* timestamp acceptance window, both directions */
} vna_replay_t;

void vna_replay_init(vna_replay_t *r, vna_replay_ent_t *storage, uint32_t cap, uint64_t window_ms);

/* Read-only check (R1, timestamp window, R3). VNA_OK, VNA_ERR_STALE or
 * VNA_ERR_REPLAY. */
vna_status_t vna_replay_check(const vna_replay_t *r, const vna_id_t *peer, uint64_t seq,
                              uint64_t ts, uint64_t now);

/* Record an accepted message (call only after the signature verified, R2). */
void vna_replay_commit(vna_replay_t *r, const vna_id_t *peer, uint64_t seq, uint64_t ts);

#endif /* VNA_ID_H */
