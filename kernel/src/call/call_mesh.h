/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_mesh.h — decentralized group calls: every member is publisher,
 * subscriber and relay at once.
 *
 * The idea borrowed from IPFS/BitTorrent swarms, made two-way and real-time:
 * there is no conference server. Each member announces, in a signed MEMBER
 * message, its uplink capacity, how many peers it is willing to feed with
 * any one stream (fanout, a per-stream bound on children), the streams it publishes (each with 1..4
 * simulcast layers of strictly increasing bitrate) and the stream/layer it wants from everyone
 * else. Every member holds the same membership table and runs the same pure
 * function, call_mesh_build, which yields the same relay forest everywhere:
 *
 *  1. Demands are (subscriber, stream, wanted layer), in member-id order.
 *  2. Trees are built one (stream, layer) at a time: audio first (cheap and
 *     what a call cannot do without), then data, then video; within a class
 *     the highest bitrate first (ties: lower stream id, then lower layer), so
 *     big flows claim capacity before small ones, like first-fit-decreasing
 *     bin packing.
 *  3. Inside a tree the subscribers are attached in order of residual uplink
 *     (largest first, ties by id), so strong uplinks end up near the root
 *     and become relays. Each subscriber picks the tree node with the lowest
 *     depth, then the most residual uplink, then the lowest id, among nodes
 *     that still have uplink >= the layer bitrate, fanout left, and depth
 *     below max_depth. The publisher is the root.
 *  4. A subscriber that cannot be attached falls back to the next lower
 *     simulcast layer (processed later, as it is cheaper), and is reported
 *     unserved only if even layer 0 fails.
 * No member is ever charged more than its declared uplink. Join, leave and
 * failure all change the table; the caller then rebuilds. Membership is
 * kept sorted by id and messages carry a per-member version, so the result
 * does not depend on message arrival order.
 *
 * MEMBER message (big-endian):
 *   0 2 magic 0x434D ("CM")   2 1 version 1   3 1 type (1 member, 2 leave)
 *   4 4 group id   8 4 member id   12 4 member version (monotonic)
 *  16 32 public key
 *  type 1 only: 48 4 uplink kbit/s, 52 1 fanout, 53 1 stream count,
 *     per stream: 4 id, 1 kind, 1 layer count, 2 kbit/s per layer;
 *     2 subscription count, per subscription: 4 stream id, 1 layer
 *  then 64 octets of signature over everything before it.
 * Signatures are checked by a caller callback (ML-DSA, Ed25519, ...).
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Tables, plans and scratch come from the caller.
 *
 * HONEST LIMITS. The tree builder is a greedy heuristic, not an optimal
 * (NP-hard) degree-constrained multicast solver: with tight capacity it may
 * degrade a subscription that a cleverer packing could have served. Only
 * uplink is modelled; downlink, path latency and loss between pairs are not.
 * Members that are pure relays (forwarding a stream they do not watch) are
 * not used. Determinism holds only when all members hold the same table; in
 * the seconds after a join, leave or failure, views differ and two members
 * may briefly disagree on who feeds whom. A failure detected locally (no
 * heartbeat) is not signed by anyone, so members must use the same timeout.
 * There are no codecs and no camera or microphone capture here; streams are
 * opaque bitrates. Media comes from the host OS (AVFoundation on macOS) or
 * bundled libopus/libvpx later.
 */
#ifndef CALL_MESH_H
#define CALL_MESH_H

#include <stdbool.h>
#include <stdint.h>

#define CALL_MESH_MAGIC       0x434D
#define CALL_MESH_VERSION     1
#define CALL_MESH_MSG_MEMBER  1
#define CALL_MESH_MSG_LEAVE   2
#define CALL_MESH_MAX_STREAMS 4
#define CALL_MESH_MAX_LAYERS  4
#define CALL_MESH_MAX_SUBS    64
#define CALL_MESH_SIG_LEN     64
#define CALL_MESH_KEY_LEN     32
#define CALL_MESH_HDR_LEN     48
#define CALL_MESH_MAX_MSG                                                                          \
    (CALL_MESH_HDR_LEN + 6 + CALL_MESH_MAX_STREAMS * (6 + 2 * CALL_MESH_MAX_LAYERS) + 2 +          \
     CALL_MESH_MAX_SUBS * 5 + CALL_MESH_SIG_LEN)
#define CALL_MESH_NO_LAYER 0xff

typedef enum {
    CALL_STREAM_AUDIO = 1,
    CALL_STREAM_VIDEO = 2,
    CALL_STREAM_DATA = 3
} call_stream_kind_t;

typedef struct call_mesh_stream {
    uint32_t id;
    uint8_t kind;
    uint8_t nlayers;
    uint16_t kbps[CALL_MESH_MAX_LAYERS];
} call_mesh_stream_t;

typedef struct call_mesh_sub {
    uint32_t stream_id;
    uint8_t layer;
} call_mesh_sub_t;

typedef struct call_mesh_member {
    uint32_t id;
    uint32_t version;
    uint8_t pubkey[CALL_MESH_KEY_LEN];
    uint32_t uplink_kbps;
    uint8_t fanout; /* max children per (stream, layer) tree */
    uint8_t nstreams;
    uint16_t nsubs;
    uint8_t alive; /* 0 = left or failed (kept as a tombstone) */
    call_mesh_stream_t streams[CALL_MESH_MAX_STREAMS];
    call_mesh_sub_t subs[CALL_MESH_MAX_SUBS];
} call_mesh_member_t;

typedef bool (*call_mesh_verify_fn)(void *ctx, const uint8_t pubkey[CALL_MESH_KEY_LEN],
                                    const uint8_t *msg, uint32_t len,
                                    const uint8_t sig[CALL_MESH_SIG_LEN]);
typedef bool (*call_mesh_sign_fn)(void *ctx, const uint8_t *msg, uint32_t len,
                                  uint8_t sig[CALL_MESH_SIG_LEN]);

typedef struct call_mesh {
    uint32_t group_id;
    call_mesh_member_t *members; /* sorted by id */
    uint32_t cap;
    uint32_t n;
    uint8_t max_depth;
    call_mesh_verify_fn verify;
    void *vctx;
    uint32_t changes;
} call_mesh_t;

typedef struct call_mesh_edge {
    uint32_t stream_id;
    uint32_t from;
    uint32_t to;
    uint16_t kbps;
    uint8_t layer;
    uint8_t depth; /* depth of 'to' (publisher = 0) */
} call_mesh_edge_t;

typedef struct call_mesh_delivery {
    uint32_t member;
    uint32_t stream_id;
    uint32_t publisher;
    uint8_t want;
    uint8_t got; /* CALL_MESH_NO_LAYER if unserved */
} call_mesh_delivery_t;

typedef struct call_mesh_plan {
    call_mesh_edge_t *edges;
    uint32_t edge_cap, nedges;
    call_mesh_delivery_t *dl;
    uint32_t dl_cap, ndl;
    uint32_t *up_used_kbps; /* per member, same order as the table */
    uint32_t up_cap;
    uint32_t served, degraded, unserved;
    uint32_t max_depth_seen;
    uint32_t hash; /* FNV-1a over the edges: equal plans hash equal */
} call_mesh_plan_t;

void call_mesh_init(call_mesh_t *m, uint32_t group_id, call_mesh_member_t *table, uint32_t cap,
                    call_mesh_verify_fn verify, void *vctx);

/* Encode a signed MEMBER (or LEAVE) message for 'self'. Returns length. */
int call_mesh_encode_member(const call_mesh_member_t *self, uint32_t group_id, uint8_t *out,
                            uint32_t cap, call_mesh_sign_fn sign, void *sctx);
int call_mesh_encode_leave(const call_mesh_member_t *self, uint32_t group_id, uint8_t *out,
                           uint32_t cap, call_mesh_sign_fn sign, void *sctx);

/* Parse without verifying. type receives CALL_MESH_MSG_*. For LEAVE only the
 * id, version and pubkey of *out are set. Returns CALL_OK or CALL_ERR_*. */
int call_mesh_parse(const uint8_t *msg, uint32_t len, uint32_t *group_id, uint8_t *type,
                    call_mesh_member_t *out);

/* Verify and apply a message. Returns 1 if the table changed (rebuild),
 * 0 if it was stale or a duplicate, or a CALL_ERR_*. A known member's
 * public key may not change. */
int call_mesh_on_message(call_mesh_t *m, const uint8_t *msg, uint32_t len);

/* Local failure detection: mark a member dead until it sends a newer
 * signed version. Returns 1 if the table changed. */
int call_mesh_mark_failed(call_mesh_t *m, uint32_t member_id);

uint32_t call_mesh_alive(const call_mesh_t *m);

/* Scratch words needed by call_mesh_build for the current table. */
uint32_t call_mesh_scratch_words(const call_mesh_t *m);

/* Compute the relay forest. Returns CALL_OK or CALL_ERR_SPACE when the plan
 * or scratch is too small. */
int call_mesh_build(const call_mesh_t *m, call_mesh_plan_t *plan, uint32_t *scratch,
                    uint32_t scratch_words);

/* Who sends (stream, layer) to member, or 0 if nobody. */
uint32_t call_mesh_parent(const call_mesh_plan_t *p, uint32_t member, uint32_t stream_id,
                          uint8_t layer);
/* Up to max members that 'member' forwards (stream, layer) to. */
uint32_t call_mesh_children(const call_mesh_plan_t *p, uint32_t member, uint32_t stream_id,
                            uint8_t layer, uint32_t *out, uint32_t max);

#endif /* CALL_MESH_H */
