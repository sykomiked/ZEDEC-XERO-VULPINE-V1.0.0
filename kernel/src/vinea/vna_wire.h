/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_wire.h — signed Vinea messages and signed DHT records.
 *
 * MESSAGE (schema vna_msg_schema, all little-endian, version 1)
 *   magic "VNA2" | version | type | flags | features | src NodeID | dst NodeID |
 *   pow nonce | seq | timestamp_ms | rpc id | body<u16 len> | ML-DSA-65 pk |
 *   ML-DSA-65 signature
 * The signature (context "vinea/v2/msg") covers every byte before it, so
 * it binds the type, both NodeIDs (a message for node A cannot be replayed to
 * node B), the sequence, the timestamp, the rpc id and the body.
 *
 * RECEIVE PIPELINE (vna_msg_open) — cheapest checks first, nothing in the
 * node's state changes until the signature has verified:
 *   1. framing (UBH-168 or plain) and schema unpack: bounds, lengths, enums
 *   2. version, type range, dst == self (dst may be zero only for PING),
 *      src != self
 *   3. dedupe: SHA3-256 of the exact bytes already processed -> drop (an
 *      identical byte string was already verified and acted on; re-acting
 *      would be a replay, so dropping it is exact)
 *   4. timestamp window and replay window (read-only)
 *   5. NodeID binding SHA3-256(pk) == src and PoW (via the verified key cache)
 *   6. ML-DSA-65 signature
 *   7. commit: replay window advanced, message hash remembered
 */
#ifndef VNA_WIRE_H
#define VNA_WIRE_H

#include "vna_id.h"
#include "vna_schema.h"
#include "vna_frame.h"

#define VNA_MSG_MAGIC   0x32414E56u /* "VNA2" little-endian */
#define VNA_REC_MAGIC   0x32524E56u /* "VNR2" */
#define VNA_K           20u         /* bucket size and replication factor */
#define VNA_PAYLOAD_MAX 2600u
#define VNA_REC_MAX     8000u
#define VNA_BODY_MAX    8192u
#define VNA_HK_MAX      255u
#define VNA_RCPT_WIRE_MAX 7200u /* a trade receipt (vna_econ.h VNA_RCPT_MAX) */

typedef enum {
    VNA_MSG_PING = 1,
    VNA_MSG_PONG = 2,
    VNA_MSG_FIND_NODE = 3,
    VNA_MSG_NODES = 4,
    VNA_MSG_FIND_VALUE = 5,
    VNA_MSG_VALUE = 6,
    VNA_MSG_STORE = 7,
    VNA_MSG_STORE_ACK = 8,
    VNA_MSG_HK = 9,         /* one canonical Hackronomicon command line */
    VNA_MSG_SPOOL = 10,     /* one store-and-forward item (vna_spool_item_t) */
    VNA_MSG_SPOOL_ACK = 11, /* its acknowledgement (vna_b_spool_ack_t) */
    VNA_MSG_RCPT = 12,      /* one trade receipt (vna_econ.h), body vna_b_rec_t */
    VNA_MSG_TYPE_MAX = 12
} vna_msg_type_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t features;
    vna_id_t src;
    vna_id_t dst;
    uint64_t pow;
    uint64_t seq;
    uint64_t ts;
    uint64_t rpc;
    uint16_t body_len;
    uint8_t body[VNA_BODY_MAX];
    uint8_t pk[VNA_PK_LEN];
    uint8_t sig[VNA_SIG_LEN];
} vna_msg_t;

/* ---- bodies ---- */
typedef struct {
    vna_id_t key;
} vna_b_key_t; /* FIND_NODE, FIND_VALUE */

typedef struct {
    vna_id_t id;
    uint16_t addr_len;
    uint8_t addr[VNA_ADDR_MAX];
} vna_contact_wire_t;

typedef struct {
    uint16_t n;
    vna_contact_wire_t c[VNA_K];
} vna_b_nodes_t; /* NODES */

typedef struct {
    uint16_t len;
    uint8_t rec[VNA_REC_MAX];
} vna_b_rec_t; /* VALUE, STORE */

typedef struct {
    uint8_t status; /* 0 stored, else refusal reason */
} vna_b_ack_t;

typedef struct {
    uint8_t truth; /* swarm_hk_truth_t (K5) */
    uint32_t ordinal;
    uint16_t len;
    uint8_t text[VNA_HK_MAX];
} vna_b_hk_t;

extern const vna_schema_t vna_msg_schema;
extern const vna_schema_t vna_b_key_schema;
extern const vna_schema_t vna_b_nodes_schema;
extern const vna_schema_t vna_b_rec_schema;
extern const vna_schema_t vna_b_ack_schema;
extern const vna_schema_t vna_b_hk_schema;

/* ---- signed DHT records ---- */
typedef enum { VNA_REC_PROVIDER = 1, VNA_REC_AGREEMENT = 2 } vna_rec_type_t;
typedef enum { VNA_VIS_PUBLIC = 0, VNA_VIS_PRIVATE = 1 } vna_visibility_t;

typedef struct {
    uint32_t magic;
    uint8_t rtype;
    vna_id_t key;
    vna_id_t publisher;
    uint64_t pow;
    uint64_t created;
    uint64_t expires;
    uint16_t payload_len;
    uint8_t payload[VNA_PAYLOAD_MAX];
    uint8_t pk[VNA_PK_LEN];
    uint8_t sig[VNA_SIG_LEN];
} vna_rec_t;

/* Provider record payload: "publisher serves the file whose root is `key`". */
typedef struct {
    uint64_t file_size;
    uint32_t chunk_size;
    uint32_t n_chunks;
    uint8_t visibility; /* vna_visibility_t; PRIVATE is never announced or stored */
    uint16_t addr_len;
    uint8_t addr[VNA_ADDR_MAX];
} vna_provider_t;

extern const vna_schema_t vna_rec_schema;
extern const vna_schema_t vna_provider_schema;

/* Verification context shared by messages and records. */
typedef struct {
    uint8_t (*h)[32];
    uint32_t cap, head, n;
} vna_dedupe_t;
void vna_dedupe_init(vna_dedupe_t *d, uint8_t (*storage)[32], uint32_t cap);
bool vna_dedupe_seen(const vna_dedupe_t *d, const uint8_t h[32]);
void vna_dedupe_add(vna_dedupe_t *d, const uint8_t h[32]);

typedef struct {
    vna_id_t self;
    uint32_t pow_bits;
    vna_keycache_t *kc;
    vna_replay_t *rp;
    vna_dedupe_t *dd;
    /* counters of rejections by reason, for tests and monitoring */
    uint32_t rej_parse, rej_dst, rej_dup, rej_stale, rej_replay, rej_binding, rej_pow, rej_sig;
} vna_verifier_t;

/* Sign and encode a message. The caller fills type, flags, features, dst,
 * seq, ts, rpc and the body; magic, version, src, pow and pk are taken from
 * the identity. rnd: 32 fresh bytes (hedged ML-DSA). ubh selects UBH-168
 * framing (default between ZXV nodes) or plain bytes. Returns the encoded
 * length or -1. */
int32_t vna_msg_seal(vna_msg_t *m, const vna_identity_t *idn, const uint8_t rnd[32], bool ubh,
                     uint8_t *out, uint32_t cap);

/* Run the receive pipeline above. On VNA_OK, *m holds the verified message
 * and *was_ubh tells which syntax the peer used. */
vna_status_t vna_msg_open(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                          vna_msg_t *m, bool *was_ubh);

/* Sign a record (publisher fields come from idn). Returns the canonical
 * encoding length or -1. */
int32_t vna_rec_seal(vna_rec_t *r, const vna_identity_t *idn, const uint8_t rnd[32], uint8_t *out,
                     uint32_t cap);

/* Verify a record: schema, created <= now + skew, expires > now, NodeID
 * binding + PoW, signature, and the type-specific payload rules (a provider
 * record must be PUBLIC; an agreement record must sit under its owner's
 * agreement key and parse as an agreement owned by the publisher). */
vna_status_t vna_rec_verify(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                            uint64_t skew_ms, vna_rec_t *r);

/* ---- signed node records (identity + where to reach it) ----
 *
 * A node record is the node's vna_id identity (NodeID = SHA3-256(pk), PoW
 * nonce) bound by its own ML-DSA-65 signature (context
 * "vinea/v2/node-record") to the transport addresses it can be reached at,
 * a monotonically increasing seq, and a lifetime. It is what a LAN
 * announcement carries and what a host persists as a "cached peer": anyone
 * can check it offline, nobody but the key holder can make or change one.
 * A newer seq replaces an older one; an expired record is refused.
 *
 * VNA_NR_HIGHCAP marks a high-capacity peer (an always-on "server" someone
 * connected to enhance the network). It is advisory only: it grants no
 * authority anywhere in Vinea (routing, storage, economy treat it exactly
 * like any other peer). */
#define VNA_NODEREC_MAGIC 0x324E4E56u /* "VNN2" */
#define VNA_NR_ADDRS      4u
#define VNA_NR_HIGHCAP    0x01u
#define VNA_NR_GOODBYE    0x02u /* leaving: drop this node (signed, so not spoofable) */
#define VNA_NODEREC_MAX   5520u

typedef struct {
    uint16_t len;
    uint8_t a[VNA_ADDR_MAX];
} vna_addr_el_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    vna_id_t id;
    uint64_t pow;
    uint32_t features;
    uint8_t flags;
    uint64_t seq;
    uint64_t created;
    uint64_t expires;
    uint16_t naddr;
    vna_addr_el_t addr[VNA_NR_ADDRS];
    uint8_t pk[VNA_PK_LEN];
    uint8_t sig[VNA_SIG_LEN];
} vna_noderec_t;

extern const vna_schema_t vna_noderec_schema;

/* Sign a node record: magic, version, id, pow and pk come from idn; the caller
 * fills features, flags, seq, created, expires and the addresses. Returns the
 * canonical length or -1. */
int32_t vna_noderec_seal(vna_noderec_t *r, const vna_identity_t *idn, const uint8_t rnd[32],
                         uint8_t *out, uint32_t cap);

/* Verify: schema, version, created <= now + skew, now < expires (a GOODBYE
 * record may have expires == created), at least one address, NodeID binding +
 * PoW (via v->kc / v->pow_bits), signature. */
vna_status_t vna_noderec_verify(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                                uint64_t skew_ms, vna_noderec_t *r);

/* ---- store-and-forward (offline outbox) items ----
 * Signed by the origin at enqueue time (context "vinea/v2/spool-item"), so a
 * queue persisted by the host is tamper-evident and the receiver sees what
 * the owner authorised and when. Delivered inside a fresh VNA_MSG_SPOOL
 * message (new seq/ts, so the message replay window still holds). */
#define VNA_SPOOL_MAGIC    0x32534E56u /* "VNS2" */
#define VNA_SPOOL_ITEM_MAX 3700u

typedef struct {
    uint32_t magic;
    uint8_t version;
    vna_id_t src;
    vna_id_t dst;
    uint64_t sseq; /* origin's spool sequence: strictly increasing per origin */
    uint64_t created;
    uint64_t expires;
    uint8_t truth;
    uint32_t ordinal;
    uint16_t len;
    uint8_t text[VNA_HK_MAX];
    uint8_t sig[VNA_SIG_LEN];
} vna_spool_item_t;

typedef struct {
    uint64_t sseq;
    uint8_t status; /* vna_spool_ack_status_t */
} vna_b_spool_ack_t;

typedef enum {
    VNA_SPOOL_ACK_OK = 0,      /* accepted, surfaced once */
    VNA_SPOOL_ACK_DUP = 1,     /* already had it: the sender may drop it */
    VNA_SPOOL_ACK_REFUSED = 2, /* outside the agreement / unknown verb: drop it */
    VNA_SPOOL_ACK_BUSY = 3     /* receiver's event queue full: retry later */
} vna_spool_ack_status_t;

extern const vna_schema_t vna_spool_item_schema;
extern const vna_schema_t vna_b_spool_ack_schema;

#endif /* VNA_WIRE_H */
