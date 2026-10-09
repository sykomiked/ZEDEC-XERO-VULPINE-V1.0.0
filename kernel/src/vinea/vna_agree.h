/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_agree.h — the sharing agreement ("user agreement") each person writes
 * for their own space, and the fail-closed check every request goes through.
 *
 * Like a Vinea server of old: each person runs their own space with their
 * own rules. The agreement says
 *   - the PARTICIPATION DEGREE (a dial):
 *       0 OFF      share nothing, answer nothing (a pure client)
 *       1 ROUTE    answer PING / FIND_NODE / FIND_VALUE (help the mesh route)
 *       2 STORE    also hold DHT records and storage bytes for others
 *       3 SERVE    also serve the files listed below
 *       4 COMPUTE  also sell compute (token units per cycle)
 *       5 MEMORY   also lease working memory (bytes x cycles, released on expiry)
 *     A resource is offered only at or above its degree, whatever else the
 *     agreement says.
 *   - WHAT: files by root id (each PUBLIC or PRIVATE), compute tokens per
 *     cycle, storage bytes, memory bytes and the longest lease.
 *   - TO WHOM: everyone / an allowlist of NodeIDs / anyone at or above a trust
 *     threshold / allowlist-or-trust; a blocklist always wins. A PRIVATE file
 *     is served only to allowlisted peers, never announced, whatever the
 *     audience.
 *   - QUOTAS per peer per period, and reserve prices per unit (the floor the
 *     owner's agent will not negotiate below).
 *   - TERMS: free text, at most 512 bytes, printable.
 * The agreement is content-addressed: its CID is SHA3-256 of its canonical
 * bytes; it is published in the DHT as a record signed (ML-DSA-65) by its
 * owner under key SHA3("vinea/v2/agreement-of" ...owner).
 *
 * The check fails closed: no agreement, an expired agreement, an unknown
 * resource, a blocklisted peer, a degree too low, an audience miss, a file not
 * listed, or a quota/capacity overrun all refuse.
 */
#ifndef VNA_AGREE_H
#define VNA_AGREE_H

#include "vna_schema.h"

#define VNA_AGR_MAGIC     0x32474E56u /* "VNG2" */
#define VNA_AGR_MAX_FILES 16u
#define VNA_AGR_MAX_IDS   16u
#define VNA_AGR_TERMS_MAX 512u

typedef enum {
    VNA_DEG_OFF = 0,
    VNA_DEG_ROUTE = 1,
    VNA_DEG_STORE = 2,
    VNA_DEG_SERVE = 3,
    VNA_DEG_COMPUTE = 4,
    VNA_DEG_MEMORY = 5,
    VNA_DEG_MAX = 5
} vna_degree_t;

typedef enum {
    VNA_AUD_EVERYONE = 0,
    VNA_AUD_ALLOWLIST = 1,
    VNA_AUD_TRUST = 2,
    VNA_AUD_ALLOW_OR_TRUST = 3,
    VNA_AUD_MAX = 3
} vna_audience_t;

typedef enum {
    VNA_RES_ROUTE = 0,   /* DHT routing queries */
    VNA_RES_RECORD = 1,  /* holding a DHT record */
    VNA_RES_FILE = 2,    /* one file chunk */
    VNA_RES_COMPUTE = 3, /* compute tokens this cycle */
    VNA_RES_STORAGE = 4, /* storage bytes */
    VNA_RES_MEMORY = 5,  /* memory bytes leased for some cycles */
    VNA_RES_COUNT = 6
} vna_resource_t;

typedef struct {
    vna_id_t root;
    uint8_t visibility; /* vna_visibility_t */
} vna_agr_file_t;

typedef struct {
    vna_id_t id;
} vna_agr_id_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t degree;
    uint8_t audience;
    uint8_t flags;
    vna_id_t owner;
    uint64_t seqno; /* a newer agreement from the same owner has a larger seqno */
    uint64_t issued_ms;
    uint64_t expires_ms;
    uint64_t trust_min;
    uint64_t compute_per_cycle;
    uint64_t storage_bytes;
    uint64_t memory_bytes;
    uint32_t memory_max_cycles;
    uint32_t cycle_ms;
    uint64_t quota_period_ms;
    uint32_t q_chunks;
    uint64_t q_compute;
    uint64_t q_storage;
    uint64_t q_memory;
    uint32_t q_records;
    uint32_t price[VNA_RES_COUNT]; /* reserve price per unit, by vna_resource_t */
    uint16_t n_files;
    vna_agr_file_t files[VNA_AGR_MAX_FILES];
    uint16_t n_allow;
    vna_agr_id_t allow[VNA_AGR_MAX_IDS];
    uint16_t n_block;
    vna_agr_id_t block[VNA_AGR_MAX_IDS];
    uint16_t terms_len;
    uint8_t terms[VNA_AGR_TERMS_MAX];
} vna_agreement_t;

extern const vna_schema_t vna_agreement_schema;

/* Defaults: version 1, degree OFF, audience ALLOWLIST, everything zero — an
 * agreement that shares nothing until its owner says otherwise. */
void vna_agree_default(vna_agreement_t *a, const vna_id_t *owner);

int32_t vna_agree_encode(const vna_agreement_t *a, uint8_t *out, uint32_t cap);
/* Decode + validate (schema, printable terms). VNA_OK or VNA_ERR_PARSE. */
vna_status_t vna_agree_decode(const uint8_t *in, uint32_t len, vna_agreement_t *a);
/* Content address: SHA3-256 of the canonical bytes. */
vna_status_t vna_agree_cid(const vna_agreement_t *a, uint8_t cid[32]);
/* The DHT key an owner's agreement is published under. */
void vna_agree_key(const vna_id_t *owner, vna_id_t *key);

/* The least degree at which a resource is offered. */
uint8_t vna_res_min_degree(vna_resource_t r);

/* ---- per-peer usage, quotas and memory leases ---- */
typedef struct {
    vna_id_t peer;
    uint64_t period_start;
    uint32_t chunks;
    uint32_t records;
    uint64_t compute;
    uint64_t storage;
    uint64_t memory;
    bool used;
} vna_usage_ent_t;

typedef struct {
    vna_id_t peer;
    uint64_t bytes;
    uint64_t expires_ms;
    bool used;
} vna_lease_t;

typedef struct {
    vna_usage_ent_t *u;
    uint32_t ucap;
    vna_lease_t *l;
    uint32_t lcap;
    uint64_t compute_this_cycle; /* all peers, reset by vna_usage_new_cycle */
    uint64_t storage_used;
    uint64_t memory_leased;
} vna_usage_t;

void vna_usage_init(vna_usage_t *us, vna_usage_ent_t *u, uint32_t ucap, vna_lease_t *l,
                    uint32_t lcap);
void vna_usage_new_cycle(vna_usage_t *us);
/* Release every memory lease that expired by `now`; returns bytes released. */
uint64_t vna_usage_expire(vna_usage_t *us, uint64_t now);

/* The check. `trust` is the requester's trust score from the caller's own
 * ledger (vna_book_trust). `root` is the file root for VNA_RES_FILE, else
 * NULL. `amount` is units (chunks=1, tokens, bytes); `cycles` is the lease
 * length for MEMORY. With commit, a successful check is charged to the
 * peer's usage (and a memory lease is created). Returns VNA_OK or
 * VNA_ERR_DENIED / VNA_ERR_CAP / VNA_ERR_EXPIRED / VNA_ERR_ARG. */
vna_status_t vna_agree_check(const vna_agreement_t *a, vna_usage_t *us, const vna_id_t *peer,
                             uint64_t trust, vna_resource_t res, const vna_id_t *root,
                             uint64_t amount, uint32_t cycles, uint64_t now, bool commit);

/* Is the root listed, and is it PRIVATE? Returns false if not listed. */
bool vna_agree_lists_file(const vna_agreement_t *a, const vna_id_t *root, bool *is_private);

#endif /* VNA_AGREE_H */
