/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_spaces.h — spaces: who may see and change the files in each place
 * (zxvd's spaces.* and the "space" parameter of files.*; the native UI's
 * model of the same places is kernel/src/zui/zui_space.c).
 *
 * ZXV is laid out like the real world: a home, offices, team rooms, a
 * cloud top (your public front) and parks (commons). Each is a space with
 * its own folder (zxv_files.h F7) and a manifest naming its kind, its owner
 * and its members.
 *
 *   S1 RIGHTS. What a role may do follows from the space's kind alone:
 *        kind       owner        member       guest   public
 *        home       view, write  -            -       -
 *        office     view, write  view, write  view    -
 *        team       view, write  view, write  view    -
 *        cloudtop   view, write  view         view    view
 *        park       view, write  view, write  view, write  view, write
 *      zxvd checks them on every files.* call; the UI showing a button is
 *      never what allows it.
 *   S2 THIS NODE'S ROLE. Spaces this node made are owned ("me"); spaces it
 *      was invited into are recorded with the role it was given. Until the
 *      mesh syncs spaces, the check guards this node's copy; the owner's
 *      node checks again when changes reach it.
 *   S3 CROSSINGS ARE HELD. Copying a file into a more open kind of space
 *      (home < office < team < cloudtop < park), or into one whose audience
 *      is wider than the source's (anyone there who could not see the
 *      source), is held until the owner confirms it. Into home, or between spaces
 *      with the same audience or a narrower one, it goes at once.
 *   S4 MEMBERS. A space decides its member list by its governance, with
 *      the same rule as the UI model's zsp_decide (kernel/src/zui):
 *        vertical    the owner decides: set_members, or a member's proposal
 *                    that waits (NEITHER) until the owner votes on it;
 *        horizontal  a proposal passes (TRUE) when the yes votes of the
 *                    owner and members reach quorum_pct with no objection;
 *                    quorum with objections is a dispute (BOTH) for people
 *                    to settle, never applied; it fails (FALSE) once it can
 *                    no longer pass, and gathers (NEITHER) until then.
 *      zxvd counts the votes itself, never a tally a client sends. This
 *      node votes as `self`; other members' votes arrive through
 *      zxs_vote_from, which the mesh layer calls only after checking the
 *      member's signature (not wired yet). Guests do not propose or vote. */
#ifndef ZXV_SPACES_H
#define ZXV_SPACES_H

#include <stdbool.h>
#include <stdint.h>

#include "zxv_files.h"

#define ZXS_NAME_MAX      80u /* bytes of a display name */
#define ZXS_WHO_MAX       64u /* bytes of a member id (a peer id or a name) */
#define ZXS_MEMBERS_MAX   64u
#define ZXS_ME            "me" /* this node, as owner */
#define ZXS_PROPOSALS_MAX 4u   /* open proposals per space */
#define ZXS_VOTERS_MAX    (ZXS_MEMBERS_MAX + 1u)

/* zxv_spaces' own refusals (beside the ZXF_E_* codes) */
enum {
    ZXS_E_ROLE = -20,   /* this node's role may not do that */
    ZXS_E_SELF = -21,   /* this node has no member id in the space */
    ZXS_E_FULL = -22,   /* ZXS_PROPOSALS_MAX proposals are open */
    ZXS_E_NOPROP = -23, /* no open proposal with that id */
    ZXS_E_GOV = -24,    /* not how this space decides (S4) */
};

typedef enum { ZXS_GOV_VERTICAL, ZXS_GOV_HORIZONTAL } zxs_gov_t;
/* S4's outcomes, as lpres names them */
typedef enum { ZXS_D_FALSE, ZXS_D_TRUE, ZXS_D_NEITHER, ZXS_D_BOTH } zxs_decision_t;

typedef enum { ZXS_HOME, ZXS_OFFICE, ZXS_TEAM, ZXS_CLOUDTOP, ZXS_PARK, ZXS_KINDS } zxs_kind_t;
typedef enum { ZXS_OWNER, ZXS_MEMBER, ZXS_GUEST, ZXS_PUBLIC, ZXS_ROLES } zxs_role_t;
enum { ZXS_VIEW = 1, ZXS_WRITE = 2 };

typedef struct {
    char who[ZXS_WHO_MAX + 1];
    uint8_t role; /* ZXS_MEMBER or ZXS_GUEST */
} zxs_member_t;

typedef struct {
    char who[ZXS_WHO_MAX + 1];
    bool yes;
} zxs_vote_t;

typedef struct {
    uint32_t id;
    char by[ZXS_WHO_MAX + 1];        /* who proposed it */
    zxs_member_t m[ZXS_MEMBERS_MAX]; /* the member list it proposes */
    uint32_t n;
    zxs_vote_t v[ZXS_VOTERS_MAX];
    uint32_t nv;
} zxs_proposal_t;

typedef struct {
    char id[ZXF_SPACE_ID_MAX + 1]; /* "home" for home */
    uint8_t kind;
    char name[ZXS_NAME_MAX + 1];
    char owner[ZXS_WHO_MAX + 1]; /* ZXS_ME, or who invited this node */
    uint8_t role;                /* this node's role (S2) */
    zxs_member_t m[ZXS_MEMBERS_MAX];
    uint32_t n;
    uint8_t gov;                /* zxs_gov_t (S4) */
    uint8_t quorum_pct;         /* 1..100, horizontal */
    char self[ZXS_WHO_MAX + 1]; /* this node's id here: ZXS_ME as owner, "" = unknown */
    zxs_proposal_t p[ZXS_PROPOSALS_MAX];
    uint32_t np;
    uint32_t next_proposal;
} zxs_space_t;

/* S1 */
unsigned zxs_rights(zxs_kind_t kind, zxs_role_t role);
bool zxs_can(const zxs_space_t *s, unsigned want);
const char *zxs_kind_name(zxs_kind_t k);
const char *zxs_role_name(zxs_role_t r);
int zxs_kind_parse(const char *s); /* -1 when unknown */
int zxs_role_parse(const char *s);
bool zxs_who_ok(const char *who); /* 1..64 bytes, printable ASCII, no quote or backslash */

/* S3: true when moving content from `from` into `to` shows it to someone
 * who could not see it before. */
bool zxs_widens(const zxs_space_t *from, const zxs_space_t *to);

/* S4. The decision rule itself (zsp_decide's): voters = owner + members. */
zxs_decision_t zxs_decide(zxs_gov_t gov, uint8_t quorum_pct, zxs_role_t proposer, uint32_t voters,
                          uint32_t yes, uint32_t no);
const char *zxs_decision_name(zxs_decision_t d); /* "true", "false", "neither", "both" */
/* Proposal p's standing now: its counts and outcome. */
zxs_decision_t zxs_tally(const zxs_space_t *s, const zxs_proposal_t *p, uint32_t *yes, uint32_t *no,
                         uint32_t *voters);
/* This node proposes a member list (its own yes counted). TRUE applies it
 * at once (*id = 0); FALSE drops it; otherwise it stays open as *id. The
 * caller saves the space. */
int zxs_propose(zxs_space_t *s, const zxs_member_t *m, uint32_t n, uint32_t *id, zxs_decision_t *d);
/* This node votes on proposal id; another member's vote (zxs_vote_from) only
 * after its signature has been checked. A changed vote replaces the old
 * one. TRUE applies the list, FALSE drops the proposal. */
int zxs_vote(zxs_space_t *s, uint32_t id, bool yes, zxs_decision_t *d);
int zxs_vote_from(zxs_space_t *s, uint32_t id, const char *who, bool yes, zxs_decision_t *d);
const char *zxs_strerror(int code); /* ZXS_E_*, else zxf_strerror */

/* The store (manifests through zxv_files.h F7). Each returns ZXF_OK or a
 * ZXF_E_* code. */
void zxs_home(zxs_space_t *s);
int zxs_load(const char *id, zxs_space_t *s); /* "home" works too */
int zxs_save(const zxs_space_t *s, bool exclusive);
/* A new space this node owns, its id made from the name (unique). */
int zxs_create(zxs_kind_t kind, const char *name, zxs_gov_t gov, uint8_t quorum_pct,
               zxs_space_t *out);
/* Record a space this node was invited into; self: this node's member id
 * there ("" = not given: it cannot propose or vote). */
int zxs_join(const char *id, zxs_kind_t kind, const char *name, const char *owner, zxs_role_t role,
             const char *self, zxs_gov_t gov, uint8_t quorum_pct, zxs_space_t *out);
/* fn for home and then each space, in id order. */
typedef int (*zxs_each_fn)(void *ctx, const zxs_space_t *s);
int zxs_each(zxs_each_fn fn, void *ctx);

/* The manifest as JSON (for files and for zxvd's replies), and back. */
int32_t zxs_json(const zxs_space_t *s, char *out, uint32_t cap);
int zxs_parse(const char *js, uint32_t n, zxs_space_t *s);

#endif
