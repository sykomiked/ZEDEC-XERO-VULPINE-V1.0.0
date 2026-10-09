/* swarm.h — the Holodeck swarm: watching together makes it faster
 *
 * THE INVERSION
 * -------------
 * On a centralised service, popularity is a COST: every extra viewer is
 * another stream the origin must pay to send, so the thousandth viewer makes
 * the service worse. Here it is the opposite. Content is addressed by the
 * hash of its chunks, so any peer holding a chunk can serve it, and a peer
 * that has watched the first ten minutes is already a source for them.
 *
 *     capacity(n) = origin + (n - 1) x per_peer_upload
 *
 * Every arrival brings its own upload with it, so aggregate capacity grows
 * with the audience. A crowd is not congestion — it is the delivery network.
 * That is the IPFS/BitTorrent property, applied to a shared viewing.
 *
 * WHY CONTENT ADDRESSING MAKES THIS SAFE
 * --------------------------------------
 * A chunk is named by its own digest, so a peer cannot serve you something
 * else under the right name: you verify what you received against the name
 * you asked for. Peers therefore need no trust at all — a hostile peer can
 * waste your bandwidth but cannot substitute content. (swarm_verify_chunk.)
 *
 * WATCHING TOGETHER
 * -----------------
 * A viewing is not just a download. Everyone in a room shares one playback
 * position, agreed by event sequence rather than a wall clock, so the room
 * stays together without a central timekeeper. Around it sits the social
 * layer: the people watching the same thing can talk, and — only if their
 * owners enable it — their Chiglet companions may compare notes too.
 *
 * COMPANION SOCIALISING IS OFF BY DEFAULT AND BOUNDED
 * ---------------------------------------------------
 * A companion may only exchange with another when BOTH owners have opted in
 * (con_companion_share), and what it gains is bounded by the same ISF rule
 * that governs everything else: an exchange with a companion whose experience
 * duplicates yours adds nothing (R does not move). Companions grow from
 * genuinely different perspectives, never from crowd size.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Holodeck swarm slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_SWARM_H
#define ZXV_SWARM_H

#include <stdint.h>
#include <stdbool.h>
#include "../chiglet/chiglet.h"

#define SWARM_DIGEST_LEN   32u
#define SWARM_MAX_PEERS    32u
#define SWARM_MAX_CHUNKS   64u
#define SWARM_MAX_ROOM     16u

/* A content-addressed chunk: named by the digest of its own bytes. */
typedef struct {
    uint8_t  digest[SWARM_DIGEST_LEN];
    uint32_t length;
    bool     present;          /* do we hold it locally? */
} swarm_chunk_t;

/* A peer in the swarm and what it can contribute. */
typedef struct {
    uint32_t peer_id;
    bool     active;
    uint32_t upload_kbps;                  /* what it can serve */
    uint8_t  has_chunk[SWARM_MAX_CHUNKS];  /* 1 = holds that chunk */
    uint32_t chunks_served;                /* contribution, for fairness */
} swarm_peer_t;

/* One shared viewing. */
typedef struct {
    uint8_t  content_root[SWARM_DIGEST_LEN]; /* the work being watched */
    uint32_t n_chunks;
    swarm_chunk_t chunk[SWARM_MAX_CHUNKS];

    swarm_peer_t peer[SWARM_MAX_PEERS];
    uint32_t n_peers;
    uint32_t origin_kbps;                    /* the seed's own upload */

    /* shared playback position, advanced by EVENT SEQUENCE not a clock */
    uint64_t play_ordinal;
    uint32_t play_chunk;
} swarm_room_t;

void swarm_init(swarm_room_t *r, const uint8_t content_root[SWARM_DIGEST_LEN],
                uint32_t n_chunks, uint32_t origin_kbps);

/* Register a chunk's digest and length (the manifest of the work). */
bool swarm_set_chunk(swarm_room_t *r, uint32_t idx,
                     const uint8_t digest[SWARM_DIGEST_LEN], uint32_t length);

/* A viewer joins, bringing their own upload capacity with them. */
int32_t swarm_join(swarm_room_t *r, uint32_t peer_id, uint32_t upload_kbps);
bool    swarm_leave(swarm_room_t *r, uint32_t peer_id);

/* Note that a peer now holds a chunk (it watched that far, so it can serve). */
bool swarm_peer_has(swarm_room_t *r, uint32_t peer_id, uint32_t chunk_idx);

/* AGGREGATE CAPACITY — the whole point: it RISES with the audience. */
uint32_t swarm_capacity_kbps(const swarm_room_t *r);

/* How many peers can serve this chunk right now (its redundancy). */
uint32_t swarm_chunk_sources(const swarm_room_t *r, uint32_t chunk_idx);

/* Choose the best peer to fetch a chunk from: prefers a holder that has
 * served least, so load spreads instead of hammering one generous peer.
 * Returns a peer_id, or 0 if nobody holds it. */
uint32_t swarm_select_source(const swarm_room_t *r, uint32_t chunk_idx,
                             uint32_t requester_id);

/* Content addressing: does this data match the chunk's NAME? A peer cannot
 * substitute different bytes under the right digest. */
bool swarm_verify_chunk(const swarm_room_t *r, uint32_t chunk_idx,
                        const uint8_t *data, uint32_t len);

/* Advance the shared playback position by one event step. Everyone in the
 * room derives the same position from the same ordinal — no central clock. */
void     swarm_advance(swarm_room_t *r, uint64_t ordinal);
uint32_t swarm_position(const swarm_room_t *r);

/* ---- companion socialising (opt-in, bounded) ---- */
typedef struct {
    uint32_t peer_id;
    bool     sharing_enabled;                 /* the OWNER's choice */
    surplus_real_t view[CHG_DIM];             /* this companion's perspective */
} swarm_companion_t;

/* May these two companions exchange? Only if BOTH owners opted in. */
bool swarm_companions_may_share(const swarm_companion_t *a,
                                const swarm_companion_t *b);

/* What one companion GAINS from meeting others: the effective number of
 * independent perspectives (R) across every companion that consented.
 * Duplicated experience adds nothing — growth needs genuine difference. */
surplus_real_t swarm_companion_gain(const swarm_companion_t *self,
                                    const swarm_companion_t *others,
                                    uint32_t n_others,
                                    uint32_t *distinct_out);

#endif /* ZXV_SWARM_H */
