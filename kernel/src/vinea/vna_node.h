/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_node.h — one Vinea DHT node as a pure state machine.
 *
 * The host gives it bytes that arrived (with the transport address they came
 * from) and the current time; it hands back the bytes to send, each tagged
 * with a destination address, in a caller-provided outbox. It never opens a
 * socket, reads a clock or draws entropy on its own (its DRBG is seeded by the
 * host). NAT traversal, hole punching and the UDP/stream glue are the host
 * layer's job (arch/hosted), not this module's.
 *
 * What it does:
 *   - verifies every message (vna_msg_open) before anything else;
 *   - keeps the Kademlia table (verified contacts only, ping-before-evict);
 *   - answers PING / FIND_NODE / FIND_VALUE at degree >= ROUTE and STORE at
 *     degree >= STORE (STORE also checked against the agreement's record
 *     quota), refusing everything at degree OFF;
 *   - runs iterative lookups (alpha = 3, optional disjoint paths), and for a
 *     publish, STOREs the signed record at the k closest nodes found;
 *   - expires records and republishes the ones it holds;
 *   - refreshes only idle buckets;
 *   - receives Hackronomicon requests (VNA_MSG_HK), refuses unknown verbs
 *     and anything the agreement does not allow, and surfaces the rest as
 *     events for the owner's agent;
 *   - can be seeded from signed node records (LAN discovery, vna_lan.h, or
 *     peers the host cached from an earlier run): no bootstrap server,
 *     relay or gateway is ever required, and well-known bootstrap addresses
 *     (vna_node_bootstrap) are optional;
 *   - keeps an optional offline outbox (the spool): signed items queued while
 *     no route to the destination exists, delivered in order, exactly once
 *     (receiver dedupe), when the destination becomes reachable;
 *   - passes every outgoing and incoming frame through an optional transform
 *     hook (vna_xform_t, vna_frame.h), the integration point for src/ehop.
 *
 * PURELY PEER TO PEER. Every node runs the same code with the same rules. A
 * high-capacity always-on node (a "server" someone connects to enhance the
 * network, for reward through the economy) is just a peer with more
 * capacity in its agreement; nothing in routing, storage or the economy gives
 * it authority over anyone else.
 */
#ifndef VNA_NODE_H
#define VNA_NODE_H

#include "vna_wire.h"
#include "vna_kad.h"
#include "vna_agree.h"
#include "vna_cmd.h"

#define VNA_NODE_PENDING 64u
#define VNA_NODE_LOOKUPS 3u
#define VNA_NODE_EVENTS  8u
#define VNA_OUTBOX_MAX   64u
#define VNA_MSG_WIRE_MAX 13700u /* largest framed message */

#define VNA_FL_RESPONSE 0x0001u /* HK message flag: this answers rpc */

typedef struct {
    uint32_t off, len;
    uint8_t addr[VNA_ADDR_MAX];
    uint8_t addr_len;
} vna_out_ent_t;

typedef struct {
    uint8_t *buf;
    uint32_t cap, used, n;
    uint32_t dropped;
    vna_out_ent_t e[VNA_OUTBOX_MAX];
} vna_outbox_t;

void vna_outbox_init(vna_outbox_t *ob, uint8_t *buf, uint32_t cap);
void vna_outbox_clear(vna_outbox_t *ob);

typedef struct {
    uint8_t rec[VNA_REC_MAX];
    uint16_t len;
    vna_id_t key;
    vna_id_t publisher;
    uint64_t created, expires, republish_at;
    bool used;
} vna_store_slot_t;

typedef struct {
    uint8_t degree;        /* vna_degree_t; the agreement may lower it further */
    uint32_t pow_bits;     /* PoW difficulty required of peers */
    uint64_t ts_window_ms; /* replay timestamp window */
    uint64_t rpc_timeout_ms;
    uint64_t rec_max_ttl_ms;       /* longest record lifetime accepted */
    uint64_t republish_ms;         /* republish interval for held records */
    uint32_t paths;                /* disjoint lookup paths, 1..4 */
    uint32_t max_records_per_peer; /* used when no agreement is attached */
    bool ubh;                      /* speak UBH-168 to peers that advertise it */
} vna_node_cfg_t;

void vna_node_cfg_default(vna_node_cfg_t *c);

typedef enum {
    VNA_P_FREE = 0,
    VNA_P_LOOKUP,
    VNA_P_EVICT,
    VNA_P_PING,
    VNA_P_BOOT,
    VNA_P_STORE,
    VNA_P_HK,
    VNA_P_SPOOL
} vna_pending_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t expect;
    uint8_t lookup;
    uint64_t rpc;
    vna_id_t peer; /* zero: any responder (bootstrap ping by address) */
    uint64_t deadline_ms;
    vna_contact_t newcomer; /* EVICT: who replaces the silent contact */
    uint64_t sseq;          /* SPOOL: the item in flight */
} vna_pending_t;

typedef struct {
    bool used;
    bool store_after; /* publish: STORE `value` at the k closest when done */
    bool reported;
    vna_lookup_t lk;
    uint16_t value_len;
    uint8_t value[VNA_REC_MAX]; /* found record (FIND_VALUE) or record to publish */
    uint32_t stores_sent, stores_acked;
} vna_node_lookup_t;

typedef struct {
    vna_id_t from;
    uint8_t addr[VNA_ADDR_MAX];
    uint8_t addr_len;
    uint64_t rpc;
    bool is_response;
    vna_cmd_t cmd;
    vna_b_hk_t hk;
    bool spooled;     /* delivered through the offline outbox */
    uint64_t sseq;    /* spooled: origin's spool sequence */
    uint64_t created; /* spooled: when the origin signed it */
} vna_event_t;

/* ---- offline outbox (store-and-forward spool) ----
 *
 * SENDER: vna_node_spool_hk signs an item (vna_spool_item_t) at once and
 * keeps it until it is acknowledged or expires. Items to the same
 * destination go strictly in sseq order, one in flight at a time
 * (stop-and-wait), so they arrive in order. With no route the item waits; if
 * the node knows anyone at all it looks the destination up in the DHT. A lost
 * acknowledgement makes the sender retry (backoff), never skip.
 * RECEIVER: keeps the highest sseq accepted per origin; an item at or below
 * it is a duplicate (acknowledged DUP, not surfaced again), so delivery is
 * in order and exactly once while the origin's entry stays in the table.
 * The receiver must have a spool attached (for that table) to accept items.
 * The host persists the spool entries and next_sseq across restarts. */
typedef struct {
    bool used;
    bool inflight;
    uint8_t tries;
    vna_id_t dst;
    uint64_t sseq, expires, next_try;
    uint16_t len;
    uint8_t item[VNA_SPOOL_ITEM_MAX]; /* canonical signed vna_spool_item_t */
} vna_spool_ent_t;

typedef struct {
    bool used;
    vna_id_t peer;
    uint64_t last; /* highest sseq accepted from peer */
    uint64_t seen_ms;
} vna_spool_rx_t;

typedef struct {
    vna_spool_ent_t *e;
    uint32_t cap;
    vna_spool_rx_t *rx;
    uint32_t rx_cap;
    uint64_t next_sseq;
    int32_t lookup_slot; /* DHT lookup started to find a destination, or -1 */
    uint32_t queued, delivered, expired, refused, retries;
    uint32_t rx_accepted, rx_dup, rx_refused, rx_busy;
} vna_spool_t;

void vna_spool_init(vna_spool_t *s, vna_spool_ent_t *e, uint32_t cap, vna_spool_rx_t *rx,
                    uint32_t rx_cap, uint64_t next_sseq);
/* Items still queued for dst (NULL: for anyone). */
uint32_t vna_spool_pending(const vna_spool_t *s, const vna_id_t *dst);

typedef uint64_t (*vna_trust_fn)(void *ctx, const vna_id_t *peer);

typedef struct {
    const vna_identity_t *idn;
    vna_node_cfg_t cfg;
    vna_rt_t rt;
    vna_keycache_t kc;
    vna_replay_t rp;
    vna_dedupe_t dd;
    vna_verifier_t v;
    vna_store_slot_t *store;
    uint32_t store_cap;
    const vna_agreement_t *agr;
    vna_usage_t *usage;
    vna_trust_fn trust;
    void *trust_ctx;
    vna_pending_t pend[VNA_NODE_PENDING];
    vna_node_lookup_t lks[VNA_NODE_LOOKUPS];
    vna_event_t ev[VNA_NODE_EVENTS];
    uint32_t ev_head, ev_n;
    vna_drbg_t rng;
    uint64_t seq;
    /* scratch (keeps big objects off the stack) */
    vna_msg_t in, out;
    vna_rec_t rec;
    vna_b_nodes_t nodes;
    vna_b_rec_t brec;
    vna_spool_item_t spi;
    uint8_t xbuf[VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD];
    vna_spool_t *spool;
    const vna_xform_t *xform;
    bool joined; /* a self-lookup has been started (Kademlia join) */
    /* statistics */
    uint32_t evictions, evict_kept, stores_ok, stores_refused, unexpected, denied, hk_refused;
    uint32_t sent, received, dropped_out, xform_refused, seeded;
} vna_node_t;

typedef struct {
    vna_contact_t *pool;
    uint32_t pool_cap;
    vna_keycache_ent_t *kc;
    uint32_t kc_cap;
    vna_replay_ent_t *rp;
    uint32_t rp_cap;
    uint8_t (*dd)[32];
    uint32_t dd_cap;
    vna_store_slot_t *store;
    uint32_t store_cap;
} vna_node_mem_t;

vna_status_t vna_node_init(vna_node_t *n, const vna_identity_t *idn, const vna_node_cfg_t *cfg,
                           const vna_node_mem_t *mem, const uint8_t seed[32]);

/* Attach the owner's agreement (may be NULL: then everything beyond routing
 * is refused), usage table and trust function. */
void vna_node_set_agreement(vna_node_t *n, const vna_agreement_t *a, vna_usage_t *us,
                            vna_trust_fn trust, void *ctx);

/* Process bytes received from addr. Returns the verification/handling status. */
vna_status_t vna_node_handle(vna_node_t *n, const uint8_t *buf, uint32_t len, const uint8_t *addr,
                             uint32_t addr_len, uint64_t now, vna_outbox_t *ob);

/* Time passes: expire RPCs (evictions, failed lookup queries), continue
 * lookups, expire and republish records. */
void vna_node_tick(vna_node_t *n, uint64_t now, vna_outbox_t *ob);

/* Ping a transport address whose NodeID is not yet known; when it answers,
 * it joins the table and a lookup of our own id starts (Kademlia join). */
vna_status_t vna_node_bootstrap(vna_node_t *n, const uint8_t *addr, uint32_t addr_len, uint64_t now,
                                vna_outbox_t *ob);

vna_status_t vna_node_ping(vna_node_t *n, const vna_id_t *id, uint64_t now, vna_outbox_t *ob);

/* Start a lookup. Returns its slot (>= 0) or a negative vna_status_t. */
int32_t vna_node_lookup(vna_node_t *n, const vna_id_t *target, vna_lk_mode_t mode, uint64_t now,
                        vna_outbox_t *ob);

/* Publish a signed record (canonical bytes from vna_rec_seal): keep a copy,
 * look up its key, STORE it at the k closest. Returns the lookup slot. */
int32_t vna_node_publish(vna_node_t *n, const uint8_t *rec, uint32_t len, uint64_t now,
                         vna_outbox_t *ob);

/* Announce that we provide a file. A PRIVATE provider record is refused
 * (VNA_ERR_DENIED): private items are never announced. */
int32_t vna_node_announce(vna_node_t *n, const vna_id_t *root, const vna_provider_t *p,
                          uint64_t now, uint64_t ttl_ms, vna_outbox_t *ob);

/* Publish the owner's agreement under its DHT key. */
int32_t vna_node_publish_agreement(vna_node_t *n, const vna_agreement_t *a, uint64_t now,
                                   uint64_t ttl_ms, vna_outbox_t *ob);

/* Refresh idle buckets (K5): one FIND_NODE lookup per idle bucket, up to max. */
uint32_t vna_node_refresh(vna_node_t *n, uint64_t now, uint64_t idle_ms, uint32_t max,
                          vna_outbox_t *ob);

const vna_node_lookup_t *vna_node_lookup_get(const vna_node_t *n, int32_t slot);
bool vna_node_lookup_done(vna_node_t *n, int32_t slot);
void vna_node_lookup_release(vna_node_t *n, int32_t slot);

/* Send one Hackronomicon line to a known contact (text is canonicalised
 * first). For a reply, set is_response and pass the request's rpc. */
vna_status_t vna_node_send_hk(vna_node_t *n, const vna_id_t *dst, const char *text, uint8_t truth,
                              uint32_t ordinal, bool is_response, uint64_t rpc, uint64_t now,
                              vna_outbox_t *ob);

/* Next Hackronomicon event (a request the agreement allows, or a response to
 * one of ours). */
bool vna_node_poll_event(vna_node_t *n, vna_event_t *ev);

/* Seed the routing table from a signed node record (LAN announcement or a
 * peer the host cached earlier). vna_node_seed_record verifies it first
 * (binding, PoW, signature, expiry; created within ts_window_ms);
 * vna_node_seed_verified takes one the caller already verified with this
 * node's verifier. The first seed starts the Kademlia join (a lookup of our
 * own id), exactly like a bootstrap PONG. A GOODBYE record removes the
 * contact instead. */
vna_status_t vna_node_seed_record(vna_node_t *n, const uint8_t *rec, uint32_t len, uint64_t now,
                                  vna_outbox_t *ob);
vna_status_t vna_node_seed_verified(vna_node_t *n, const vna_noderec_t *r, uint64_t now,
                                    vna_outbox_t *ob);

/* Attach the offline outbox (may be NULL to detach). */
void vna_node_set_spool(vna_node_t *n, vna_spool_t *s);

/* Queue one Hackronomicon line for dst through the spool (signed now, valid
 * for ttl_ms). Delivery starts at once if dst is reachable, else when it
 * becomes so. *sseq_out (may be NULL) receives its spool sequence. */
vna_status_t vna_node_spool_hk(vna_node_t *n, const vna_id_t *dst, const char *text, uint8_t truth,
                               uint32_t ordinal, uint64_t ttl_ms, uint64_t now, vna_outbox_t *ob,
                               uint64_t *sseq_out);

/* Install the per-frame transform hook (NULL: none). Both ends of a link must
 * agree on it; frames the hook refuses are dropped before parsing. */
void vna_node_set_xform(vna_node_t *n, const vna_xform_t *x);

/* Copy up to max contacts out of the routing table (for the host to persist
 * as cached peers together with the node records it received). */
uint32_t vna_node_contacts(const vna_node_t *n, vna_contact_t *out, uint32_t max);

/* Records currently held. */
uint32_t vna_node_store_count(const vna_node_t *n);
const vna_store_slot_t *vna_node_store_find(const vna_node_t *n, const vna_id_t *key);

#endif /* VNA_NODE_H */
