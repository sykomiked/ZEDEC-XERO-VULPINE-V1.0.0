/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_group.h — groups (of people) and syndicates (of organisations),
 * formed peer to peer from signed records.
 *
 * There is no group server. A group IS its log: six signed record kinds,
 * carried by gossip like any other record, and a pure function
 * (sg_derive) that replays the log into a roster. Every peer that holds the
 * same records computes the same roster, whatever order they arrived in.
 *
 *   G1  RECORDS.  The group id is the record id of its GROUP_CREATE. Every
 *       other group record has parent = the group id. No group record has
 *       media. Bodies are exact (no trailing bytes):
 *         CREATE   no parent, no audience.
 *                  ver=1 kind:u8 (0 GROUP, 1 SYNDICATE) join:u8 (0 invite
 *                  only, 1 open) name_len:u8 (1..64) name
 *                  treasury_len:u8 (0..64) treasury  org_len:u8 (0..32) org
 *                  The treasury id is opaque bytes for the payments module.
 *                  A SYNDICATE's founder must give an org id.
 *         INVITE   audience = invitee; body role:u8 (ADMIN, MEMBER, OBSERVER)
 *         JOIN     no audience; body org_len:u8 (0..32) org. Required
 *                  (non-empty) in a SYNDICATE: the organisation identifier,
 *                  e.g. an LEI string. This module checks only that it is
 *                  present; whether it is a real LEI is validated elsewhere.
 *         LEAVE    audience = subject; empty body. subject == author is
 *                  leaving; otherwise it is a removal.
 *         ROLE     audience = subject; body role:u8 (OWNER..OBSERVER)
 *         CHARTER  no audience; body op:u8 (0 SET, 1 SIGN) hash[32]: the
 *                  hash of the group's own agreement text. The owner SETs
 *                  it; each member SIGNs the hash they agree to (the record
 *                  signature is the member's signature on it).
 *   G2  ORDER.  sg_derive sorts the group's records by (Lamport time, record
 *       id) and replays them in that one total order. Concurrent records are
 *       thereby ordered the same way on every peer. A record that is not
 *       authorised AT ITS PLACE in the order is ignored (and counted), never
 *       an error, so a forged or stale action cannot block the log.
 *   G3  ROLES.  OWNER > ADMIN > MEMBER > OBSERVER. Exactly one owner, the
 *       founder, until a ROLE record by the owner names a new OWNER, which
 *       demotes the old owner to ADMIN. The owner cannot LEAVE (transfer
 *       first) and is never removed.
 *         INVITE   by ADMIN or OWNER; only the OWNER invites an ADMIN.
 *                  The latest invite for a person replaces earlier ones.
 *         JOIN     open group: as MEMBER. Invite-only: needs an unused
 *                  invite issued to the joiner, and takes its role; the
 *                  invite is consumed. A member's repeat JOIN is ignored.
 *         LEAVE    a member may leave; a removal needs ADMIN or above and a
 *                  subject of strictly lower role (an admin removes members
 *                  and observers; the owner also removes admins).
 *         ROLE     the OWNER sets any role on any other member; an ADMIN
 *                  sets MEMBER or OBSERVER on a subject below ADMIN. So
 *                  REVOKING an admin is the owner's ROLE record making them
 *                  a MEMBER: their later actions are no longer authorised.
 *         CHARTER  SET by the OWNER replaces the hash and clears every
 *                  member's signature; SIGN by a member counts only for the
 *                  current hash.
 *   G4  GROUP VISIBILITY.  A post with visibility SP_VIS_GROUP and audience
 *       = the group id is shared only between members (social_post.h P8);
 *       sg_is_member over a derived roster is the in_group callback.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Every buffer is the caller's.
 *
 * HONEST LIMITS
 * -------------
 * Lamport time is chosen by each record's author, so a revoked admin can
 * BACKDATE an action to before their revocation and it will replay as
 * authorised. Peers can flag records whose Lamport time is far below what
 * they had already seen from that author, but this module does not; a
 * causal-parent (hash-linked) log would close the hole and is future work.
 * Until a peer has every record, its roster can differ from another's;
 * the guarantee is same records, same roster. The roster is only as large
 * as the caller's member array (overflow is reported, and the cut is the
 * same on every peer with the same capacity). Sorting is insertion sort,
 * O(n^2) in the group's records. A syndicate's org id is carried, not
 * verified. Removal is not deletion: a removed member keeps what they
 * already received.
 */
#ifndef ZXV_SOCIAL_GROUP_H
#define ZXV_SOCIAL_GROUP_H

#include <stdint.h>
#include <stdbool.h>
#include "social_post.h"
#include "social_store.h"

#define SG_NAME_MAX     64u
#define SG_TREASURY_MAX 64u
#define SG_ORG_MAX      32u
#define SG_HASH_LEN     32u

typedef enum { SG_KIND_GROUP = 0, SG_KIND_SYNDICATE = 1 } sg_kind_t;
typedef enum { SG_JOIN_INVITE = 0, SG_JOIN_OPEN = 1 } sg_join_t;
typedef enum {
    SG_ROLE_NONE = 0,
    SG_ROLE_OBSERVER = 1,
    SG_ROLE_MEMBER = 2,
    SG_ROLE_ADMIN = 3,
    SG_ROLE_OWNER = 4
} sg_role_t;
typedef enum { SG_CHARTER_SET = 0, SG_CHARTER_SIGN = 1 } sg_charter_op_t;

/* One person (or organisation) the log mentions. role NONE = not a member
 * (left, removed, or only invited). */
typedef struct {
    uint8_t node[SP_NODEID_LEN];
    uint8_t role;        /* sg_role_t */
    uint8_t invited;     /* pending invite role, or SG_ROLE_NONE */
    bool charter_signed; /* signed the current charter hash */
    uint8_t org_len;
    uint8_t org[SG_ORG_MAX];
    uint64_t since; /* Lamport time of the record that set the role */
} sg_member_t;

typedef struct {
    uint8_t id[SP_ID_LEN];
    bool exists; /* a GROUP_CREATE with this id was found */
    uint8_t kind, join;
    uint8_t name_len;
    uint8_t name[SG_NAME_MAX];
    uint8_t treasury_len;
    uint8_t treasury[SG_TREASURY_MAX];
    uint8_t owner[SP_NODEID_LEN];
    bool has_charter;
    uint8_t charter[SG_HASH_LEN];
    uint32_t n_entries; /* entries written to the member array */
    uint32_t n_members; /* entries with role >= OBSERVER */
    uint32_t applied, ignored, truncated;
} sg_group_t;

/* Builders. The record must already be sp_init'ed with the right kind and
 * author; these set parent, audience and body. SP_OK or negative. */
int32_t sg_make_create(sp_record_t *r, sg_kind_t kind, sg_join_t join, const uint8_t *name,
                       uint32_t name_len, const uint8_t *treasury, uint32_t treasury_len,
                       const uint8_t *org, uint32_t org_len);
int32_t sg_make_invite(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                       const uint8_t invitee[SP_NODEID_LEN], sg_role_t role);
int32_t sg_make_join(sp_record_t *r, const uint8_t group[SP_ID_LEN], const uint8_t *org,
                     uint32_t org_len);
int32_t sg_make_leave(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                      const uint8_t subject[SP_NODEID_LEN]);
int32_t sg_make_role(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                     const uint8_t subject[SP_NODEID_LEN], sg_role_t role);
int32_t sg_make_charter(sp_record_t *r, const uint8_t group[SP_ID_LEN], sg_charter_op_t op,
                        const uint8_t hash[SG_HASH_LEN]);

/* G2, G3: replay the n records (any order, any mix; records of other groups
 * and other kinds are skipped) into `members`. `sorted` is scratch for n
 * pointers. Returns the number of member entries written. */
uint32_t sg_derive(const sp_record_t *const *recs, uint32_t n, const uint8_t group[SP_ID_LEN],
                   const sp_record_t **sorted, sg_member_t *members, uint32_t cap, sg_group_t *out);

/* The same over every record held in a store; `sorted` holds sorted_cap
 * pointers (records past it are reported in out->truncated). */
uint32_t sg_derive_store(const ss_store_t *s, const uint8_t group[SP_ID_LEN],
                         const sp_record_t **sorted, uint32_t sorted_cap, sg_member_t *members,
                         uint32_t cap, sg_group_t *out);

const sg_member_t *sg_find(const sg_member_t *members, uint32_t n,
                           const uint8_t node[SP_NODEID_LEN]);
/* Member (role >= OBSERVER) of the derived roster? */
bool sg_is_member(const sg_member_t *members, uint32_t n, const uint8_t node[SP_NODEID_LEN]);
/* Members who signed the current charter. */
uint32_t sg_charter_signers(const sg_member_t *members, uint32_t n);

#endif /* ZXV_SOCIAL_GROUP_H */
