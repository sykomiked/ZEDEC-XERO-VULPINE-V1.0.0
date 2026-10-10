/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_post.h — signed social records for the decentralised ZXV commons.
 *
 * One record type carries every social act: a post, a reply, a reaction, a
 * direct message, a presence beacon and a follow. Records travel peer to
 * peer (social_store.h gossip); there is no server that owns them, so each
 * one proves its own author.
 *
 *   P1  AUTHOR.  A person's NodeID is SHA-256 of their ML-DSA-65 public key
 *       (sp_node_id). SHA-256 because it is the hash this tree already ships
 *       freestanding (robin_debanks/sha256.h); pqsec's SHAKE lives inside the
 *       vendored ML-DSA code and is not exported.
 *   P2  TIME.  A Lamport clock (sp_clock_t) orders records across peers with
 *       no shared wall clock. A wall time in milliseconds is optional and is
 *       only a hint: it is never used for ordering or security.
 *   P3  CANONICAL ENCODING.  Big-endian, fixed field order, presence of every
 *       optional field given by a flag bit, no padding and no alternatives,
 *       so a record has exactly one byte encoding. The "signed part" runs
 *       from the magic "ZXS1" to the last media CID:
 *
 *         "ZXS1" ver=1 kind vis flags author[32] lamport:u64
 *         [wall:u64] [parent[32]] [audience[32]]
 *         body_len:u16 body  n_media:u8 { len:u8 cid }*
 *
 *       followed by the envelope:  env_flags:u8 [pk[1952]] sig[3309].
 *   P4  ID.  The record id is SHA-256 of the signed part. The magic is the
 *       domain separator. The public key is outside the id, so a record keeps
 *       one id whether or not it carries its key.
 *   P5  SIGNATURE.  ML-DSA-65 (FIPS 204) over the 32-byte id with context
 *       string "zxv.social.v1". The signing calls sit behind sp_signer_t /
 *       sp_verifier_t so this file builds freestanding without the pqsec
 *       glue; social_sign_mldsa.h is the binding to pq_mldsa65_*.
 *   P6  STRICT DECODE.  sp_decode rejects a bad magic or version, unknown
 *       kinds, visibility classes or flag bits, out-of-range lengths, kind
 *       rules that do not hold (P7), trailing bytes, a carried key that does
 *       not hash to the author, and a signature that does not verify. Any
 *       byte string sp_decode accepts re-encodes to exactly the same bytes.
 *   P7  KIND RULES.
 *         POST      no parent; body or media present
 *         REPLY     parent required; body or media present
 *         REACTION  parent required; body 1..32 bytes (an emoji or a word)
 *         DM        visibility PRIVATE, audience = recipient NodeID,
 *                   body 1..BODY_MAX bytes of CIPHERTEXT, no media
 *         PRESENCE  no parent; body 0..128 bytes (a status line)
 *         FOLLOW    audience = followee NodeID; body exactly 1 byte,
 *                   1 = follow, 0 = unfollow
 *         BUCKET_LISTING  no parent, no media; body is the listing payload
 *                   (social_bucket.h B1), checked strictly
 *         RATING    parent = the listing id, no media; body is stars 1..5
 *                   then an optional comment (social_bucket.h B2)
 *         GROUP_*   the six group kinds; social_group.h G1 gives each one's
 *                   parent, audience and body rules, checked strictly
 *       Visibility LIST needs audience = the list id; PRIVATE needs
 *       audience = the recipient's NodeID; GROUP needs audience = the
 *       group id.
 *   P8  CONSENT AT THE POINT OF SHARING.  sp_may_share decides whether this
 *       node may hand a record to a given peer. Each user's own policy
 *       callbacks (followers, named lists, a final "agree" veto) decide who
 *       receives what. Rules fail closed: a missing callback means no.
 *       Only the author shares FOLLOWERS and LIST records; anyone may relay a
 *       PRIVATE record, but only to its audience; a GROUP record goes
 *       between members of that group only (both ends, by the in_group
 *       callback, normally sg_is_member over the derived roster); PUBLIC
 *       records relay freely. Nothing here bans, mutes or deletes a person: like
 *       social_spaces/social.h, the only levers are each person's own.
 *   P9  DIRECT MESSAGES ARE CIPHERTEXT.  The session layer encrypts a DM
 *       before it reaches this module. Here the body is opaque bytes that are
 *       copied, hashed and signed and NEVER printed, logged, embedded for
 *       ranking or put in a notification.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Every buffer is the caller's.
 *
 * HONEST LIMITS
 * -------------
 * A record carries 3309 bytes of signature and, optionally, 1952 bytes of
 * public key, so a short post is about 3.4 KB on the wire and sp_record_t is
 * about 6.5 KB in memory. Lamport time orders causally related records but
 * says nothing about real time, and a peer may lie about its clock (it can
 * only push its own records forward or back in someone's freshness term,
 * which is bounded in social_feed.h). Nothing here checks that a DM body is
 * really ciphertext, only that it is opaque to this layer. Visibility is a
 * sharing rule kept by honest peers: once a FOLLOWERS record reaches a
 * follower, a dishonest follower can forward it, so private content must
 * also be encrypted. There is no revocation or key rotation: a NodeID is
 * one key. Records are never deleted (the social_spaces mandate); an
 * unfollow is a newer FOLLOW record, not an erasure.
 */
#ifndef ZXV_SOCIAL_POST_H
#define ZXV_SOCIAL_POST_H

#include <stdint.h>
#include <stdbool.h>

#define SP_NODEID_LEN 32u
#define SP_ID_LEN     32u
#define SP_PK_LEN     1952u /* ML-DSA-65 public key */
#define SP_SIG_LEN    3309u /* ML-DSA-65 signature */
#define SP_BODY_MAX   1024u
#define SP_MEDIA_MAX  4u
#define SP_CID_MAX    64u /* a media CID, opaque bytes (CIDv1 binary fits) */
#define SP_REACT_MAX  32u
#define SP_STATUS_MAX 128u
#define SP_VERSION    1u

/* Largest signed part and largest whole encoding, in bytes. */
#define SP_SIGNED_MAX                                                                              \
    (4u + 4u + SP_NODEID_LEN + 8u + 8u + SP_ID_LEN + SP_NODEID_LEN + 2u + SP_BODY_MAX + 1u +       \
     SP_MEDIA_MAX * (1u + SP_CID_MAX))
#define SP_ENC_MAX (SP_SIGNED_MAX + 1u + SP_PK_LEN + SP_SIG_LEN)

typedef enum {
    SP_KIND_POST = 1,
    SP_KIND_REPLY = 2,
    SP_KIND_REACTION = 3,
    SP_KIND_DM = 4,
    SP_KIND_PRESENCE = 5,
    SP_KIND_FOLLOW = 6,
    SP_KIND_BUCKET_LISTING = 7, /* social_bucket.h: an update bucket */
    SP_KIND_RATING = 8,         /* social_bucket.h: 1-5 stars on a listing */
    SP_KIND_GROUP_CREATE = 9,   /* social_group.h: found a group or syndicate */
    SP_KIND_GROUP_INVITE = 10,
    SP_KIND_GROUP_JOIN = 11,
    SP_KIND_GROUP_LEAVE = 12, /* leave, or remove someone */
    SP_KIND_GROUP_ROLE = 13,
    SP_KIND_GROUP_CHARTER = 14 /* set or sign the group's agreement hash */
} sp_kind_t;

#define SP_KIND_LAST SP_KIND_GROUP_CHARTER

typedef enum {
    SP_VIS_PUBLIC = 0,    /* anyone */
    SP_VIS_FOLLOWERS = 1, /* the author's followers, as the author's policy says */
    SP_VIS_LIST = 2,      /* a named list of the author's (audience = list id) */
    SP_VIS_PRIVATE = 3,   /* one recipient (audience = their NodeID) */
    SP_VIS_GROUP = 4      /* members of one group (audience = the group id) */
} sp_vis_t;

typedef enum {
    SP_OK = 0,
    SP_ERR_ARG = -1,      /* NULL or impossible argument */
    SP_ERR_SPACE = -2,    /* output buffer too small */
    SP_ERR_FORMAT = -3,   /* bad magic, version, kind, flags or framing */
    SP_ERR_RANGE = -4,    /* a length out of range */
    SP_ERR_RULE = -5,     /* a P7 kind rule does not hold */
    SP_ERR_TRAILING = -6, /* bytes left over after the record */
    SP_ERR_KEY = -7,      /* no key for the author, or key does not hash to it */
    SP_ERR_SIG = -8       /* signature does not verify, or signing failed */
} sp_status_t;

/* A record. `id` is derived (sp_compute_id, sp_sign, sp_decode). Fields that
 * the has_* flags mark absent are ignored by the encoder and zeroed by the
 * decoder. */
typedef struct {
    uint8_t id[SP_ID_LEN];
    uint8_t kind; /* sp_kind_t */
    uint8_t vis;  /* sp_vis_t */
    bool has_wall, has_parent, has_audience, has_pk;
    uint8_t author[SP_NODEID_LEN];
    uint64_t lamport;
    uint64_t wall_ms; /* hint only (P2) */
    uint8_t parent[SP_ID_LEN];
    uint8_t audience[SP_NODEID_LEN]; /* list id or recipient NodeID */
    uint16_t body_len;
    uint8_t body[SP_BODY_MAX];
    uint8_t n_media;
    uint8_t media_len[SP_MEDIA_MAX];
    uint8_t media[SP_MEDIA_MAX][SP_CID_MAX];
    uint8_t pk[SP_PK_LEN];
    uint8_t sig[SP_SIG_LEN];
} sp_record_t;

/* P2: Lamport clock. next() before stamping a record you author; observe()
 * every record you receive. */
typedef struct {
    uint64_t t;
} sp_clock_t;

uint64_t sp_clock_next(sp_clock_t *c);
void sp_clock_observe(sp_clock_t *c, uint64_t remote);

/* P5: the signing interface. sign() writes SP_SIG_LEN bytes over the 32-byte
 * id and returns false on failure. */
typedef struct {
    bool (*sign)(void *ctx, const uint8_t id[SP_ID_LEN], uint8_t sig[SP_SIG_LEN]);
    void *ctx;
} sp_signer_t;

/* verify() checks sig over id under pk. lookup() returns the public key for
 * an author NodeID, or NULL if unknown; it is used only for records that do
 * not carry their key, and may itself be NULL. */
typedef struct {
    bool (*verify)(void *ctx, const uint8_t pk[SP_PK_LEN], const uint8_t id[SP_ID_LEN],
                   const uint8_t sig[SP_SIG_LEN]);
    void *verify_ctx;
    const uint8_t *(*lookup)(void *ctx, const uint8_t author[SP_NODEID_LEN]);
    void *lookup_ctx;
} sp_verifier_t;

/* P1 */
void sp_node_id(const uint8_t pk[SP_PK_LEN], uint8_t out[SP_NODEID_LEN]);

/* Zero a record and set its kind, visibility, author and Lamport time. */
void sp_init(sp_record_t *r, sp_kind_t kind, sp_vis_t vis, const uint8_t author[SP_NODEID_LEN],
             uint64_t lamport);

/* Field helpers. They copy and check bounds; false if out of range. */
bool sp_set_body(sp_record_t *r, const uint8_t *body, uint32_t len);
bool sp_set_parent(sp_record_t *r, const uint8_t parent[SP_ID_LEN]);
bool sp_set_audience(sp_record_t *r, const uint8_t audience[SP_NODEID_LEN]);
bool sp_add_media(sp_record_t *r, const uint8_t *cid, uint32_t len);
void sp_set_wall(sp_record_t *r, uint64_t wall_ms);
void sp_attach_pk(sp_record_t *r, const uint8_t pk[SP_PK_LEN]);

/* P7: structural and kind rules. SP_OK or a negative sp_status_t. */
int32_t sp_validate(const sp_record_t *r);

/* P4: compute r->id from the signed part. SP_OK or negative. */
int32_t sp_compute_id(sp_record_t *r);

/* P5: validate, compute the id and sign it. SP_OK or negative. */
int32_t sp_sign(sp_record_t *r, const sp_signer_t *s);

/* P5: check a record's key binding and signature (recomputes the id). */
int32_t sp_verify(const sp_record_t *r, const sp_verifier_t *v);

/* P3: encode the whole record (signed part + envelope) into buf. Returns the
 * length written, or a negative sp_status_t. Does not sign. */
int32_t sp_encode(const sp_record_t *r, uint8_t *buf, uint32_t cap);

/* P6: strict decode + verify. On any error *out is left zeroed. */
int32_t sp_decode(const uint8_t *buf, uint32_t len, sp_record_t *out, const sp_verifier_t *v);

/* P8: the user's own sharing agreement. Every callback may be NULL, which
 * denies whatever that callback would have allowed (agree NULL = accept). */
typedef struct {
    bool (*is_follower)(void *ctx, const uint8_t author[SP_NODEID_LEN],
                        const uint8_t peer[SP_NODEID_LEN]);
    bool (*in_list)(void *ctx, const uint8_t author[SP_NODEID_LEN],
                    const uint8_t list[SP_NODEID_LEN], const uint8_t peer[SP_NODEID_LEN]);
    bool (*agree)(void *ctx, const sp_record_t *r, const uint8_t peer[SP_NODEID_LEN]);
    void *ctx;
    /* Is `node` a member of `group` (social_group.h)? Last, so older
     * positional initialisers stay valid. */
    bool (*in_group)(void *ctx, const uint8_t group[SP_ID_LEN], const uint8_t node[SP_NODEID_LEN]);
} sp_policy_t;

/* May node `self` hand record r to `peer`? */
bool sp_may_share(const sp_record_t *r, const uint8_t self[SP_NODEID_LEN],
                  const uint8_t peer[SP_NODEID_LEN], const sp_policy_t *p);

/* Payload checks for the bucket kinds, implemented in social_bucket.c (so
 * linking social_post.o needs social_bucket.o). SP_OK or SP_ERR_RULE. */
int32_t sb_check_listing_body(const uint8_t *body, uint32_t len);
int32_t sb_check_rating_body(const uint8_t *body, uint32_t len);
/* Group kinds (social_group.c, likewise linked with social_post.o). */
int32_t sg_check_record(const sp_record_t *r);

/* Constant-shape byte compare / copy used across the social module. */
bool sp_eq(const uint8_t *a, const uint8_t *b, uint32_t n);
int32_t sp_cmp(const uint8_t *a, const uint8_t *b, uint32_t n);
void sp_copy(uint8_t *dst, const uint8_t *src, uint32_t n);

#endif /* ZXV_SOCIAL_POST_H */
