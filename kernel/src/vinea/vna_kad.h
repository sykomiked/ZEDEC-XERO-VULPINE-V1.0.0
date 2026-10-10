/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_kad.h — Kademlia routing table and the iterative lookup state machine.
 *
 * ROUTING TABLE
 *   K1  256-bit XOR metric; 256 k-buckets, k = 20. Bucket i holds contacts
 *       whose distance has its highest set bit at position i.
 *   K2  Contacts live in a caller-provided pool (only ~log2(N) buckets are
 *       ever populated, so a pool of a few hundred contacts serves 256
 *       logical buckets without 256 x 20 slots).
 *   K3  Least-recently-seen eviction with PING-BEFORE-EVICT: when a bucket is
 *       full, a newly seen contact does not displace anyone. The table names
 *       the least-recently-seen contact; the node pings it, and only if that
 *       ping times out is it evicted in favour of the newcomer. Long-lived
 *       nodes therefore keep their place, which is what makes Kademlia
 *       resistant to flooding by fresh identities.
 *   K4  Only contacts whose messages verified (ML-DSA signature, NodeID
 *       binding, PoW) are ever inserted (the node enforces this).
 *   K5  Each bucket records when it last saw traffic; only buckets idle longer
 *       than the refresh interval are refreshed (an active bucket is, by
 *       definition, already fresh: refreshing it would learn nothing new).
 *
 * LOOKUP (pure state machine; no sockets, no clock)
 *   L1  alpha = 3 queries in flight per path; the shortlist is ordered by XOR
 *       distance to the target.
 *   L2  A path is finished when its k closest live candidates have all
 *       answered and nothing is in flight (or nothing is left to ask).
 *   L3  Disjoint paths (S/Kademlia): with d > 1 paths, every candidate belongs
 *       to exactly one path and a node is queried by at most one path, so an
 *       adversary must sit on every path to steer the result.
 *   L4  Responses are accepted only from the exact NodeID a query went to and
 *       only with that query's rpc id; anything else is VNA_ERR_UNEXPECTED.
 *   L5  The caller drives it: vna_lookup_next() names the next candidate to
 *       query; vna_lookup_sent() records the rpc id and deadline;
 *       vna_lookup_on_nodes()/on_value()/on_fail() feed answers back;
 *       vna_lookup_expire(now) fails overdue queries.
 */
#ifndef VNA_KAD_H
#define VNA_KAD_H

#include "vna_common.h"

#define VNA_KAD_K       20u
#define VNA_KAD_ALPHA   3u
#define VNA_KAD_BUCKETS 256u
#define VNA_KAD_PATHS   4u  /* max disjoint paths */
#define VNA_LOOKUP_MAX  96u /* candidates tracked */

typedef struct {
    vna_id_t id;
    uint8_t addr[VNA_ADDR_MAX];
    uint8_t addr_len;
    uint8_t bucket;
    uint8_t in_use;
    uint8_t flags; /* VNA_CF_* */
    uint64_t last_seen_ms;
    uint64_t touch; /* strictly increasing: ties in last_seen are still ordered */
} vna_contact_t;

#define VNA_CF_UBH 0x01u /* peer advertised UBH-168 support */

typedef struct {
    vna_id_t self;
    vna_contact_t *pool;
    uint32_t cap;
    uint32_t used;
    uint8_t count[VNA_KAD_BUCKETS];
    uint64_t bucket_activity_ms[VNA_KAD_BUCKETS];
    uint64_t touch;
} vna_rt_t;

typedef enum {
    VNA_RT_UPDATED = 0, /* already known: moved to most-recently-seen */
    VNA_RT_ADDED = 1,
    VNA_RT_FULL = 2,   /* bucket full: ping *lrs, evict only if it fails */
    VNA_RT_IGNORED = 3 /* self, or the pool is exhausted */
} vna_rt_result_t;

void vna_rt_init(vna_rt_t *rt, const vna_id_t *self, vna_contact_t *pool, uint32_t cap);

/* A verified contact was seen at now. On VNA_RT_FULL, *lrs receives the
 * least-recently-seen contact of the bucket. */
vna_rt_result_t vna_rt_seen(vna_rt_t *rt, const vna_id_t *id, const uint8_t *addr,
                            uint32_t addr_len, uint8_t flags, uint64_t now, vna_contact_t *lrs);

/* Remove a contact (e.g. its eviction ping failed). True if it was present. */
bool vna_rt_remove(vna_rt_t *rt, const vna_id_t *id);

/* Pointer to a stored contact, or NULL. */
vna_contact_t *vna_rt_find(vna_rt_t *rt, const vna_id_t *id);

/* Up to k contacts closest to target, closest first, excluding `exclude`
 * (may be NULL). Returns how many. */
uint32_t vna_rt_closest(const vna_rt_t *rt, const vna_id_t *target, uint32_t k,
                        const vna_id_t *exclude, vna_contact_t *out);

/* Buckets that hold contacts but saw no traffic since now - idle_ms. Writes
 * their indices to out (up to max) and returns how many (K5). */
uint32_t vna_rt_idle_buckets(const vna_rt_t *rt, uint64_t now, uint64_t idle_ms, uint8_t *out,
                             uint32_t max);

/* A random id that falls in bucket b of self (for refresh). rnd: 32 bytes. */
void vna_rt_id_in_bucket(const vna_id_t *self, uint32_t b, const uint8_t rnd[32], vna_id_t *out);

/* ---- lookup ---- */
typedef enum { VNA_LK_NEW = 0, VNA_LK_INFLIGHT, VNA_LK_DONE, VNA_LK_FAILED } vna_lk_state_t;
typedef enum { VNA_LK_FIND_NODE = 0, VNA_LK_FIND_VALUE = 1 } vna_lk_mode_t;

typedef struct {
    vna_id_t id;
    uint8_t addr[VNA_ADDR_MAX];
    uint8_t addr_len;
    uint8_t state;
    uint8_t path;
    uint8_t flags;
    uint64_t rpc;
    uint64_t deadline_ms;
} vna_cand_t;

typedef struct {
    bool active;
    bool finished;
    bool found_value;
    uint8_t mode;
    uint8_t paths;
    uint8_t alpha;
    uint8_t k;
    vna_id_t target;
    vna_id_t self;
    vna_cand_t c[VNA_LOOKUP_MAX];
    uint32_t n;
    uint32_t inflight[VNA_KAD_PATHS];
    uint32_t queries, answers, failures;
    vna_id_t value_from; /* who answered with the value */
} vna_lookup_t;

/* Start a lookup seeded from the routing table. paths in 1..VNA_KAD_PATHS. */
void vna_lookup_init(vna_lookup_t *lk, const vna_rt_t *rt, const vna_id_t *target,
                     vna_lk_mode_t mode, uint32_t paths);

/* Index of the next candidate to query, or -1 if none may be queried now. */
int32_t vna_lookup_next(vna_lookup_t *lk);
void vna_lookup_sent(vna_lookup_t *lk, int32_t idx, uint64_t rpc, uint64_t deadline_ms);

/* Feed an answer back. Returns VNA_OK or VNA_ERR_UNEXPECTED (L4). */
vna_status_t vna_lookup_on_nodes(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc,
                                 const vna_contact_t *contacts, uint32_t n);
vna_status_t vna_lookup_on_value(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc);
vna_status_t vna_lookup_on_fail(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc);
/* Fail every in-flight query whose deadline has passed. Returns how many. */
uint32_t vna_lookup_expire(vna_lookup_t *lk, uint64_t now);

/* True once every path is finished (L2) or a value was found. */
bool vna_lookup_done(vna_lookup_t *lk);

/* The k closest candidates that answered, closest first. Returns how many. */
uint32_t vna_lookup_result(const vna_lookup_t *lk, uint32_t k, vna_cand_t *out);

#endif /* VNA_KAD_H */
