/*
 * lattice.h — LATTICE-P2P File Sharing
 *
 * Integrated P2P file distribution built on the content-addressed
 * file substrate. Files are split into chunks, indexed by cryptographic
 * hashes, tracked via DHT, and seeded according to phase-aware policies.
 *
 * 8-Tier Network Mapping:
 *   Tier 5: Static dark storage (Freenet-like, .ula/GLUT0)
 *   Tier 6: Dynamic P2P block mesh (DHT/ZeroNet, .36n9/TRUE)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_LATTICE_H
#define ZEDEC_LATTICE_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define LATTICE_MAX_PEERS        64
#define LATTICE_MAX_SWARMS       32
#define LATTICE_MAX_CHUNKS       256
#define LATTICE_CHUNK_SIZE       4096
#define LATTICE_HASH_SIZE         32
#define LATTICE_MAX_DHT_ENTRIES  128
#define LATTICE_MAX_PEER_ADDR     64
#define LATTICE_MAX_LABEL         64

/* ===== Phase-Aware Seeding Policies ===== */

typedef enum {
    LATTICE_PHASE_TRUE    = 0,  /* .36n9: High-priority active swarms */
    LATTICE_PHASE_GLUT_PLUS = 1, /* .zedec: Speculative stream sharing */
    LATTICE_PHASE_GLUT_MINUS = 2, /* .vino: Immutable ledger blocks */
    LATTICE_PHASE_GLUT_ZERO = 3, /* .ula: Cold storage, low activity */
    LATTICE_PHASE_SHADOW  = 4,  /* .9n63: ZK vault, no network exposure */
} lattice_phase_t;

/* ===== Network Tier ===== */

typedef enum {
    LATTICE_TIER_SURFACE     = 1,
    LATTICE_TIER_DEEP        = 2,
    LATTICE_TIER_ONION       = 3,
    LATTICE_TIER_GARLIC      = 4,
    LATTICE_TIER_DARK_STORAGE = 5,
    LATTICE_TIER_P2P_MESH    = 6,
    LATTICE_TIER_RADIO       = 7,
    LATTICE_TIER_SHADOW      = 8,
} lattice_tier_t;

/* ===== DHT Entry ===== */

typedef struct {
    uint8_t  key[LATTICE_HASH_SIZE];    /* Content hash key */
    uint8_t  value[LATTICE_HASH_SIZE];  /* Peer address hash */
    uint32_t tier;                       /* Network tier */
    bool     active;
} lattice_dht_entry_t;

/* ===== Peer Node ===== */

typedef struct {
    char     addr[LATTICE_MAX_PEER_ADDR];  /* M5 or onion address */
    uint8_t  pubkey[LATTICE_HASH_SIZE];     /* Peer public key hash */
    uint32_t tier;                           /* Network tier */
    bool     connected;
    uint32_t chunks_served;
    uint32_t chunks_downloaded;
    uint64_t last_seen;                      /* Tick count */
} lattice_peer_t;

/* ===== Chunk ===== */

typedef struct {
    uint8_t  hash[LATTICE_HASH_SIZE];  /* Content hash of chunk data */
    uint32_t size;
    uint32_t replica_count;            /* How many peers have this chunk */
    bool     available;                /* Do we have this chunk locally? */
} lattice_chunk_t;

/* ===== Swarm (File Distribution Group) ===== */

typedef struct {
    uint8_t  root_hash[LATTICE_HASH_SIZE];  /* Merkle root of all chunks */
    char     label[LATTICE_MAX_LABEL];
    lattice_phase_t phase;
    lattice_tier_t  tier;
    uint32_t num_chunks;
    lattice_chunk_t chunks[LATTICE_MAX_CHUNKS];
    uint32_t num_peers;
    uint32_t peer_indices[LATTICE_MAX_PEERS];
    bool     active;
    bool     seeding;
    bool     leeching;
    uint64_t total_uploaded;
    uint64_t total_downloaded;
} lattice_swarm_t;

/* ===== LATTICE-P2P State ===== */

typedef struct {
    lattice_peer_t      peers[LATTICE_MAX_PEERS];
    uint32_t            num_peers;
    lattice_swarm_t     swarms[LATTICE_MAX_SWARMS];
    uint32_t            num_swarms;
    lattice_dht_entry_t dht[LATTICE_MAX_DHT_ENTRIES];
    uint32_t            num_dht;
    bool                initialized;
    uint64_t            total_uploaded;
    uint64_t            total_downloaded;
} lattice_t;

/* ===== API ===== */

void lattice_init(lattice_t *l);

/* Peer management */
int32_t lattice_peer_add(lattice_t *l, const char *addr, uint32_t tier);
int lattice_peer_remove(lattice_t *l, uint32_t idx);
lattice_peer_t *lattice_peer_find(lattice_t *l, const char *addr);
int lattice_peer_connect(lattice_t *l, uint32_t idx);
uint32_t lattice_peers_by_tier(lattice_t *l, uint32_t tier);

/* Swarm management */
int32_t lattice_swarm_create(lattice_t *l, const char *label,
                              lattice_phase_t phase, lattice_tier_t tier);
int lattice_swarm_add_chunk(lattice_t *l, uint32_t swarm_idx,
                             const uint8_t *hash, uint32_t size);
int lattice_swarm_add_peer(lattice_t *l, uint32_t swarm_idx, uint32_t peer_idx);
int lattice_swarm_start_seeding(lattice_t *l, uint32_t swarm_idx);
int lattice_swarm_stop_seeding(lattice_t *l, uint32_t swarm_idx);

/* DHT operations */
int32_t lattice_dht_put(lattice_t *l, const uint8_t *key, const uint8_t *value,
                         uint32_t tier);
const lattice_dht_entry_t *lattice_dht_get(lattice_t *l, const uint8_t *key);
int lattice_dht_remove(lattice_t *l, const uint8_t *key);

/* Chunk operations */
int lattice_chunk_announce(lattice_t *l, uint32_t swarm_idx, uint32_t chunk_idx);
int lattice_chunk_request(lattice_t *l, uint32_t swarm_idx, uint32_t chunk_idx,
                           uint32_t peer_idx);
bool lattice_swarm_complete(lattice_t *l, uint32_t swarm_idx);
uint32_t lattice_swarm_progress(lattice_t *l, uint32_t swarm_idx);

/* Utility */
const char *lattice_phase_name(lattice_phase_t phase);
const char *lattice_tier_name(lattice_tier_t tier);

#endif /* ZEDEC_LATTICE_H */
