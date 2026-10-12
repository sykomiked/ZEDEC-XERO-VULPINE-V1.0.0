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
 *   S4 MEMBERS. Only the owner changes the member list. Group decisions
 *      (flat or hierarchical governance, quorum) are the UI model's
 *      zsp_decide; this layer records the outcome. */
#ifndef ZXV_SPACES_H
#define ZXV_SPACES_H

#include <stdbool.h>
#include <stdint.h>

#include "zxv_files.h"

#define ZXS_NAME_MAX    80u /* bytes of a display name */
#define ZXS_WHO_MAX     64u /* bytes of a member id (a peer id or a name) */
#define ZXS_MEMBERS_MAX 64u
#define ZXS_ME          "me" /* this node, as owner */

typedef enum { ZXS_HOME, ZXS_OFFICE, ZXS_TEAM, ZXS_CLOUDTOP, ZXS_PARK, ZXS_KINDS } zxs_kind_t;
typedef enum { ZXS_OWNER, ZXS_MEMBER, ZXS_GUEST, ZXS_PUBLIC, ZXS_ROLES } zxs_role_t;
enum { ZXS_VIEW = 1, ZXS_WRITE = 2 };

typedef struct {
    char who[ZXS_WHO_MAX + 1];
    uint8_t role; /* ZXS_MEMBER or ZXS_GUEST */
} zxs_member_t;

typedef struct {
    char id[ZXF_SPACE_ID_MAX + 1]; /* "home" for home */
    uint8_t kind;
    char name[ZXS_NAME_MAX + 1];
    char owner[ZXS_WHO_MAX + 1]; /* ZXS_ME, or who invited this node */
    uint8_t role;                /* this node's role (S2) */
    zxs_member_t m[ZXS_MEMBERS_MAX];
    uint32_t n;
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

/* The store (manifests through zxv_files.h F7). Each returns ZXF_OK or a
 * ZXF_E_* code. */
void zxs_home(zxs_space_t *s);
int zxs_load(const char *id, zxs_space_t *s); /* "home" works too */
int zxs_save(const zxs_space_t *s, bool exclusive);
/* A new space this node owns, its id made from the name (unique). */
int zxs_create(zxs_kind_t kind, const char *name, zxs_space_t *out);
/* Record a space this node was invited into. */
int zxs_join(const char *id, zxs_kind_t kind, const char *name, const char *owner, zxs_role_t role,
             zxs_space_t *out);
/* fn for home and then each space, in id order. */
typedef int (*zxs_each_fn)(void *ctx, const zxs_space_t *s);
int zxs_each(zxs_each_fn fn, void *ctx);

/* The manifest as JSON (for files and for zxvd's replies), and back. */
int32_t zxs_json(const zxs_space_t *s, char *out, uint32_t cap);
int zxs_parse(const char *js, uint32_t n, zxs_space_t *s);

#endif
