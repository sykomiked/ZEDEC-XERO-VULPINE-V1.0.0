/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_store.h — a bounded local store of social records, and gossip
 * helpers so two peers can reconcile what they hold.
 *
 *   S1  CALLER MEMORY.  The store is a ring of `cap` sp_record_t slots and an
 *       open-addressing index of `index_cap` words (a power of two, at least
 *       2 * cap), both supplied by the caller. When the ring is full the
 *       oldest record is evicted from this device's cache. (That is local
 *       cache pressure, not deletion: the record lives on wherever else it
 *       is held.)
 *   S2  DEDUPE BY ID.  ss_put of a record already held is a no-op. The index
 *       is keyed on the first 8 bytes of the id mixed with a per-store
 *       salt, so lookups by full id and by short id share one table.
 *   S3  THREADS.  ss_children lists the held replies and reactions to a
 *       record in (Lamport, id) order; ss_root walks parent links up to the
 *       oldest held ancestor.
 *   S4  GOSSIP.  Three messages, each self-delimiting and strictly decoded:
 *         HAVE  'H' n:u16 { short_id[8] }*n     ids this node may share
 *         WANT  'W' n:u16 { short_id[8] }*n     ids the sender lacks
 *         REC   'R' record (social_post.h encoding)
 *       n is at most SS_HAVE_MAX. A node pages its HAVE list to a peer
 *       (ss_gossip_have), the peer answers WANT for the ids it lacks, and the
 *       node answers each wanted id with REC, after checking sp_may_share
 *       again, because a WANT is a request, not a right. Every REC is
 *       decoded and signature-checked (sp_decode) before it is stored. Both
 *       sides run the same exchange, so repeated rounds converge on the
 *       union of what each side may share with the other.
 *       ss_fingerprint gives a compact set summary (count and XOR of the
 *       ids a node may share with a peer) for a quick equality check.
 *   S5  TRANSPORT-AGNOSTIC.  The gossip context carries send() and recv()
 *       callbacks; nothing here knows about sockets, IPFS or the mesh.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Every buffer is the caller's.
 *
 * HONEST LIMITS
 * -------------
 * Short ids are 8 bytes: two different records with the same 8-byte prefix
 * (chance about n^2 / 2^65) would look identical to the HAVE/WANT diff and
 * the second would not be fetched. HAVE lists are linear in the number of
 * shareable records (8 bytes each), not a sublinear set reconciliation
 * (no IBLT or Bloom filter). The fingerprint's XOR is not collision
 * resistant against a peer choosing ids; it is an equality hint, never a
 * security check. Eviction is oldest-first only, with no pinning of the
 * user's own records. A HAVE page is built in ring order, so records that
 * arrive mid-paging can be missed until the next round. There is no rate
 * limit: a peer can WANT everything it may see, every round. The index salt
 * should be random per device so a peer cannot grind ids into one probe
 * chain; the store does not generate it.
 */
#ifndef ZXV_SOCIAL_STORE_H
#define ZXV_SOCIAL_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "social_post.h"

#define SS_SHORT_LEN 8u
#define SS_HAVE_MAX  128u
#define SS_MSG_MAX   (1u + SP_ENC_MAX) /* REC is the largest message */

#define SS_MSG_HAVE 'H'
#define SS_MSG_WANT 'W'
#define SS_MSG_REC  'R'

typedef enum {
    SS_DUP = 0,
    SS_ADDED = 1,
    SS_ERR_ARG = -20,
    SS_ERR_FORMAT = -21,
    SS_ERR_SEND = -22,
    SS_ERR_SPACE = -23
} ss_status_t;

typedef struct {
    sp_record_t *slot;
    uint32_t cap;
    uint32_t *index; /* 0 = empty, else slot + 1 */
    uint32_t index_cap;
    uint32_t head; /* next slot to write (the oldest once full) */
    uint32_t count;
    uint64_t salt;
    uint64_t evicted;
    void (*on_new)(void *ctx, const sp_record_t *r); /* optional */
    void *on_new_ctx;
} ss_store_t;

/* S1. Returns false if index_cap is not a power of two >= 2 * cap. */
bool ss_init(ss_store_t *s, sp_record_t *slots, uint32_t cap, uint32_t *index, uint32_t index_cap,
             uint64_t salt);

/* S2: SS_ADDED, SS_DUP, or a negative status (the record fails sp_validate).
 * The caller verifies signatures first (sp_decode does). */
int32_t ss_put(ss_store_t *s, const sp_record_t *r);

const sp_record_t *ss_get(const ss_store_t *s, const uint8_t id[SP_ID_LEN]);
const sp_record_t *ss_get_short(const ss_store_t *s, const uint8_t sid[SS_SHORT_LEN]);
uint32_t ss_count(const ss_store_t *s);
/* i-th held record, oldest first, or NULL. */
const sp_record_t *ss_at(const ss_store_t *s, uint32_t i);

/* S3 */
uint32_t ss_children(const ss_store_t *s, const uint8_t parent[SP_ID_LEN], const sp_record_t **out,
                     uint32_t max);
const sp_record_t *ss_root(const ss_store_t *s, const sp_record_t *r);

/* S4: compact summary of what `self` may share with `peer`. */
typedef struct {
    uint32_t count;
    uint8_t xor_ids[SP_ID_LEN];
} ss_fingerprint_t;

void ss_fingerprint(const ss_store_t *s, const uint8_t self[SP_NODEID_LEN],
                    const uint8_t peer[SP_NODEID_LEN], const sp_policy_t *p, ss_fingerprint_t *out);

/* S4, S5: one side of a gossip session with one peer. */
typedef struct {
    ss_store_t *store;
    const uint8_t *self; /* SP_NODEID_LEN */
    const uint8_t *peer; /* SP_NODEID_LEN */
    const sp_policy_t *policy;
    const sp_verifier_t *verifier;
    sp_clock_t *clock; /* optional: observes every received record */
    int32_t (*send)(void *ctx, const uint8_t *msg, uint32_t len); /* 0 = sent */
    int32_t (*recv)(void *ctx, uint8_t *buf, uint32_t cap);       /* len, 0 = none, <0 error */
    void *io_ctx;
    uint8_t *tx;      /* SS_MSG_MAX bytes */
    uint8_t *rx;      /* SS_MSG_MAX bytes */
    sp_record_t *tmp; /* decode scratch */
    /* counters */
    uint32_t recs_sent, recs_added, recs_dup, recs_rejected, msgs_rejected;
} ss_gossip_t;

/* Send one HAVE page starting at *cursor (0 to start). Returns the number of
 * ids sent (0 when the list is exhausted) or a negative status. */
int32_t ss_gossip_have(ss_gossip_t *g, uint32_t *cursor);

/* Handle one received message. Returns SS_ADDED/SS_DUP for REC, 0 for
 * HAVE/WANT, or a negative status. */
int32_t ss_gossip_handle(ss_gossip_t *g, const uint8_t *msg, uint32_t len);

/* Receive and handle up to `max` messages through recv(). Returns how many
 * were handled. */
uint32_t ss_gossip_pump(ss_gossip_t *g, uint32_t max);

#endif /* ZXV_SOCIAL_STORE_H */
