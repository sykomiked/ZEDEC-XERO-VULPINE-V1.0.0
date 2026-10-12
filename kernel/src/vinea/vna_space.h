/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_space.h — public spaces on Vinea: places people make, anchored by a
 * CID and hosted by the devices of whoever is in them; and the watch hall,
 * where everyone sees the same moment of the same video.
 *
 * Like vna_node, a pure state machine: no sockets, no clock, no allocation.
 * The host hands it the time and sends the node's outbox.
 *
 *   P1 ANCHOR. A space is born from a GENESIS block: kind (room or hall),
 *      governance (owner, or quorum with a percentage), name, directory
 *      topic, the creator's NodeID, the creation time and a random nonce.
 *      The space's CID is the IPFS CIDv1 (raw, sha2-256) of those exact
 *      bytes (vna_cid.h), so the CID names the space and proves its genesis:
 *      anyone holding the block can check it against the CID. The space id
 *      used on the wire is the SHA-256 digest inside the CID.
 *   P2 HOSTED BY VISITORS. Every member hosts the space: it publishes a
 *      signed SPACE record (vna_rec_t, rtype VNA_REC_SPACE) holding the
 *      genesis block and its own address under one of VNA_SPACE_HOST_SLOTS
 *      DHT keys (its slot comes from its NodeID), and republishes it while it
 *      stays. A visitor who has only the CID looks the slots up until it
 *      finds a live host, then JOINs through it. The space exists for as long
 *      as one member is online; nobody's server is needed. A record's key
 *      must be one of the keys its own genesis gives (checked when it is
 *      verified, vna_space_rec_check), so records cannot be planted under
 *      another space's keys.
 *   P3 DIRECTORY. A space with a topic is also listed under one of
 *      VNA_SPACE_DIR_SLOTS directory keys for that topic; vna_space_find
 *      walks them. One record comes back per key, so a topic search shows at
 *      most VNA_SPACE_DIR_SLOTS spaces at a time (an honest limit of a
 *      single-value DHT).
 *   P4 MEMBERS. A host answers JOIN with WELCOME: the genesis block (the
 *      joiner checks it against the CID) and up to VNA_SPACE_WELCOME_MAX
 *      members with their addresses, then the latest playback state and the
 *      most recent posts. Members say PRESENCE to each other every
 *      presence_ms; one silent for member_ttl_ms is dropped. LEAVE says
 *      goodbye. Every message is a signed Vinea message, so a member is who
 *      it says it is.
 *   P5 EVENTS. A post or a playback change is an EVENT: a record signed by
 *      its author (rtype VNA_REC_SPACE_EV, key = space id), so it can be
 *      passed on unchanged. The author sends it to every member it knows;
 *      whoever receives a new one passes it to up to VNA_SPACE_FANOUT others.
 *      Each event is applied once (hash dedupe). There is no delete, ban or
 *      silence operation: nobody can remove anyone's speech (the free-forum
 *      mandate of src/social_spaces).
 *   P6 WATCH HALL. The playback state is one HALL event: media CID, state
 *      (stopped, paused, playing), position pos_ms at the author's wall time
 *      at_ms, and rate (Q16, 65536 = normal speed). Every viewer computes the
 *      same position from it (vna_space_hall_pos), so the group stays in step
 *      without a clock server. The latest state wins, by Lamport time, then
 *      author id. Who may change it follows the genesis: an OWNER space,
 *      only its creator; a QUORUM space, any member (horizontal).
 *   P7 BOUNDED. Fixed tables: VNA_SPACE_MAX spaces, VNA_SPACE_MEMBERS
 *      members and VNA_SPACE_LOG recent events each, a dedupe ring of
 *      VNA_SPACE_SEEN hashes.
 *
 * HONEST LIMITS. Delivery of the video itself (every viewer serving the
 * pieces it holds, src/stream/stream_swarm.h) is separate from this control
 * plane. Members with wall clocks that disagree see the playhead offset by
 * the difference. Gossip reaches members through the members each one knows;
 * a space larger than VNA_SPACE_MEMBERS per node relies on that forwarding.
 * Posts are kept in memory only (VNA_SPACE_LOG per space); durable history is
 * the host's job.
 */
#ifndef VNA_SPACE_H
#define VNA_SPACE_H

#include "vna_node.h"
#include "vna_cid.h"

#define VNA_SPACE_NAME_MAX    48u
#define VNA_SPACE_TOPIC_MAX   24u
#define VNA_SPACE_TEXT_MAX    1024u
#define VNA_SPACE_MEDIA_MAX   64u
#define VNA_SPACE_HOST_SLOTS  8u
#define VNA_SPACE_DIR_SLOTS   16u
#define VNA_SPACE_MAX         8u
#define VNA_SPACE_MEMBERS     32u
#define VNA_SPACE_LOG         32u
#define VNA_SPACE_SEEN        256u
#define VNA_SPACE_FANOUT      4u
#define VNA_SPACE_WELCOME_MAX 16u
#define VNA_SPACE_CATCHUP     8u /* recent posts a host sends a newcomer */
#define VNA_SPACE_FOUND_MAX   VNA_SPACE_DIR_SLOTS
#define VNA_SPACE_Q16         65536u
#define VNA_SPACE_MEMBER_STORE (VNA_SPACE_MAX * (VNA_SPACE_HOST_SLOTS + 2u) * 4u)

typedef enum { VNA_SPACE_ROOM = 1, VNA_SPACE_HALL = 2 } vna_space_kind_t;
typedef enum { VNA_GOV_OWNER = 1, VNA_GOV_QUORUM = 2 } vna_space_gov_t;
typedef enum { VNA_SPACE_EV_POST = 1, VNA_SPACE_EV_HALL = 2 } vna_space_ev_kind_t;
typedef enum { VNA_HALL_STOP = 0, VNA_HALL_PAUSE = 1, VNA_HALL_PLAY = 2 } vna_hall_state_t;

/* P1: the genesis block (its canonical bytes are what the CID names). */
typedef struct {
    uint8_t version; /* 1 */
    uint8_t kind;    /* vna_space_kind_t */
    uint8_t gov;     /* vna_space_gov_t */
    uint8_t quorum_pct;
    vna_id_t creator;
    uint64_t created;
    uint8_t nonce[16];
    uint16_t name_len;
    uint8_t name[VNA_SPACE_NAME_MAX];
    uint16_t topic_len;
    uint8_t topic[VNA_SPACE_TOPIC_MAX]; /* [a-z0-9-], may be empty (unlisted) */
} vna_space_genesis_t;

#define VNA_SPACE_GEN_MAX (4u + 32u + 8u + 16u + 2u + VNA_SPACE_NAME_MAX + 2u + VNA_SPACE_TOPIC_MAX)

/* P2/P3: the payload of a VNA_REC_SPACE record. */
typedef struct {
    uint16_t gen_len;
    uint8_t gen[VNA_SPACE_GEN_MAX];
    uint16_t addr_len;
    uint8_t addr[VNA_ADDR_MAX];
    uint16_t members; /* how many members the host knows (informational) */
} vna_space_host_t;

/* P6: the playback state a HALL event carries. */
typedef struct {
    uint8_t state; /* vna_hall_state_t */
    uint64_t pos_ms;
    uint64_t at_ms;
    uint32_t rate_q16; /* 65536 = 1x; at most 4x */
    uint16_t media_len;
    uint8_t media[VNA_SPACE_MEDIA_MAX]; /* the media's CID, binary; may be empty */
} vna_hall_t;

/* P5: the payload of a VNA_REC_SPACE_EV record. */
typedef struct {
    uint8_t kind; /* vna_space_ev_kind_t */
    uint64_t lamport;
    uint16_t len;
    uint8_t data[VNA_SPACE_TEXT_MAX]; /* POST: UTF-8 text; HALL: a packed vna_hall_t */
} vna_space_ev_t;

/* One event as kept in a space's log. */
typedef struct {
    uint64_t index; /* 1, 2, ... in the order this node applied them */
    uint8_t kind;
    vna_id_t author;
    uint64_t lamport;
    uint64_t created;
    uint16_t len;
    uint8_t data[VNA_SPACE_TEXT_MAX];
    uint16_t rec_len; /* the signed record, passed on to newcomers */
    uint8_t rec[VNA_REC_MAX];
} vna_space_logent_t;

typedef struct {
    vna_id_t id;
    uint8_t addr[VNA_ADDR_MAX];
    uint8_t addr_len;
    uint64_t last_seen;
} vna_space_member_t;

typedef enum {
    VNA_SPACE_SEARCHING = 1, /* looking for a host in the DHT */
    VNA_SPACE_JOINING = 2,   /* JOIN sent, waiting for WELCOME */
    VNA_SPACE_IN = 3,        /* a member, and so a host */
    VNA_SPACE_NOT_FOUND = 4  /* every host slot tried, nobody answered */
} vna_space_phase_t;

typedef struct {
    bool used;
    uint8_t phase; /* vna_space_phase_t */
    uint8_t cid[VNA_CID_RAW_LEN];
    vna_id_t sid; /* the digest inside the CID */
    bool have_gen;
    vna_space_genesis_t gen;
    uint16_t gen_len;
    uint8_t gen_bytes[VNA_SPACE_GEN_MAX];
    vna_space_member_t m[VNA_SPACE_MEMBERS];
    uint32_t nm;
    uint64_t lamport;
    /* joining */
    int32_t lookup;
    uint8_t slot_next, slots_tried;
    vna_id_t via;
    uint64_t wait_until;
    /* hosting */
    uint64_t next_publish, next_presence;
    /* watch hall */
    bool hall_set;
    vna_hall_t hall;
    vna_id_t hall_author;
    uint64_t hall_lamport;
    uint16_t hall_rec_len;
    uint8_t hall_rec[VNA_REC_MAX];
    /* recent events */
    vna_space_logent_t log[VNA_SPACE_LOG];
    uint32_t log_head, log_n;
    uint64_t log_index;
} vna_space_t;

typedef struct {
    uint8_t cid[VNA_CID_RAW_LEN];
    vna_space_genesis_t gen;
    vna_id_t host;
    uint16_t members;
} vna_space_found_t;

/* Hears every event applied (a post or a playback change), once. */
typedef void (*vna_space_event_fn)(void *ctx, const vna_space_t *s, const vna_space_logent_t *e);

typedef struct {
    vna_node_t *node;
    vna_outbox_t *ob;
    uint64_t now;
    vna_drbg_t rng;
    uint8_t addr[VNA_ADDR_MAX]; /* where peers reach this node (in host records) */
    uint8_t addr_len;
    uint64_t presence_ms, member_ttl_ms, join_wait_ms, publish_ms, record_ttl_ms;
    vna_space_t s[VNA_SPACE_MAX];
    uint8_t seen[VNA_SPACE_SEEN][32];
    uint32_t seen_head, seen_n;
    /* directory search (one at a time) */
    bool find_on;
    int32_t find_lookup;
    uint8_t find_topic[VNA_SPACE_TOPIC_MAX];
    uint8_t find_topic_len;
    uint8_t find_next;
    uint32_t nfound;
    vna_space_found_t found[VNA_SPACE_FOUND_MAX];
    vna_space_event_fn on_event;
    void *on_event_ctx;
    int32_t pubs[2u * VNA_SPACE_MAX]; /* publish lookups, released when done */
    uint32_t store_base; /* SPACE records held for others while in no space */
    /* statistics */
    uint32_t created, joins_in, welcomes_in, presence_in, leaves_in;
    uint32_t events_in, events_out, events_refused, forwards, published, publish_failed;
    uint32_t hosts_found, refused;
} vna_spaces_t;

/* Bind to a node (sets its space handler). seed: 32 bytes of entropy (the
 * genesis nonce and the record signatures' randomness). */
int vna_spaces_init(vna_spaces_t *sp, vna_node_t *node, vna_outbox_t *ob, const uint8_t seed[32]);

/* Where peers reach this node, as published in host records (may be empty:
 * peers then use the address the record's lookup came from). */
void vna_spaces_set_addr(vna_spaces_t *sp, const uint8_t *addr, uint32_t len);

/* How many SPACE records this node holds for others while it is in no
 * space (0, the default: none; a ROUTE-degree node then stores nothing).
 * While in a space it holds up to VNA_SPACE_MEMBER_STORE. */
void vna_spaces_store_base(vna_spaces_t *sp, uint32_t cap);

/* Hear every applied event (NULL clears). */
void vna_spaces_on_event(vna_spaces_t *sp, vna_space_event_fn fn, void *ctx);

/* P1: make a space and host it. topic may be empty (unlisted). Writes the CID.
 * 0, or a negative vna_status_t. */
int vna_space_create(vna_spaces_t *sp, uint8_t kind, uint8_t gov, uint8_t quorum_pct,
                     const char *name, const char *topic, uint64_t now,
                     uint8_t cid[VNA_CID_RAW_LEN]);

/* P2: join the space with this CID (look for a host, then JOIN). 0 when
 * started (or already in), else a negative vna_status_t. */
int vna_space_join(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now);

/* P4: say goodbye and forget the space. */
int vna_space_leave(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now);

/* P5: post text (1..VNA_SPACE_TEXT_MAX bytes) to a space this node is in. */
int vna_space_post(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], const uint8_t *text,
                   uint32_t len, uint64_t now);

/* P6: set the watch hall's playback state (load = STOP with a media CID,
 * play, pause, seek = the same state at another position). Refused
 * (VNA_ERR_DENIED) when this node may not control playback. */
int vna_space_hall_set(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint8_t state,
                       const uint8_t *media, uint32_t media_len, uint64_t pos_ms,
                       uint32_t rate_q16, uint64_t now);

/* P6: the position every viewer computes for `now`. */
uint64_t vna_space_hall_pos(const vna_hall_t *h, uint64_t now);

/* P6: may `who` change playback in this space? */
bool vna_space_may_control(const vna_space_t *s, const vna_id_t *who);

/* P3: list spaces under a topic ([a-z0-9-]). Results accumulate in
 * sp->found as the directory slots are looked up. */
int vna_space_find(vna_spaces_t *sp, const char *topic, uint64_t now);
bool vna_space_find_done(const vna_spaces_t *sp);

/* Time passes: host searches, joins, presence, member expiry, republishing. */
void vna_spaces_tick(vna_spaces_t *sp, uint64_t now);

/* The space with this CID, or NULL. */
const vna_space_t *vna_space_get(const vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN]);

/* Events applied after `since` (an index from an earlier result), oldest
 * first. Returns how many were written. */
uint32_t vna_space_events(const vna_space_t *s, uint64_t since, const vna_space_logent_t **out,
                          uint32_t cap);

/* Genesis encoding (canonical bytes) and its CID. */
int32_t vna_space_genesis_encode(const vna_space_genesis_t *g, uint8_t *out, uint32_t cap);
int vna_space_genesis_decode(const uint8_t *in, uint32_t len, vna_space_genesis_t *g);

/* P2/P3 keys. */
void vna_space_host_key(const uint8_t cid[VNA_CID_RAW_LEN], uint32_t slot, vna_id_t *key);
void vna_space_dir_key(const uint8_t *topic, uint32_t topic_len, uint32_t slot, vna_id_t *key);

/* Called by vna_rec_verify for rtypes SPACE and SPACE_EV (after the
 * signature): the payload parses and the key is one its content allows. */
vna_status_t vna_space_rec_check(const vna_rec_t *r);

extern const vna_schema_t vna_space_genesis_schema;
extern const vna_schema_t vna_space_host_schema;
extern const vna_schema_t vna_space_ev_schema;
extern const vna_schema_t vna_hall_schema;

#endif /* VNA_SPACE_H */
