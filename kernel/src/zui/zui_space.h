/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_space.h — places, built like the real world.
 *
 * The shell is a set of SPACES the way a life is a set of places:
 *
 *   HOME      your house: your desktop and your own files. Only you.
 *   OFFICE    a room you invite people into, with roles (member, guest).
 *   TEAM      shared space: every member works in it.
 *   CLOUDTOP  your public face, a front yard. Anyone may look; only you
 *             arrange it. It is public, not necessarily "in the cloud": it can
 *             be served from this device over the mesh.
 *   PARK      the commons: anyone may look and leave something.
 *   HALL      the space between spaces. Nothing is kept here; it is where a
 *             thing crossing from one space to another is checked.
 *
 * Openness rises HOME < OFFICE < TEAM < CLOUDTOP < PARK. The one rule of the
 * hall: nothing reaches a wider audience silently. A crossing toward a more
 * open space is HELD (NEITHER, at the glass) until the person confirms it; a
 * crossing the actor has no right to make is refused (FALSE); anything else
 * proceeds (TRUE). The result is a hexagram tag, so the logic engine and
 * zui_logic draw it like every other unit of work.
 *
 * This is the UI-side model of the rules. Storage and enforcement belong to
 * zxvd (files.* RPC); the daemon must apply the same table, and the view never
 * trusts its own copy for security.
 */
#ifndef ZXV_ZUI_SPACE_H
#define ZXV_ZUI_SPACE_H

#include <stdint.h>
#include <stdbool.h>
#include "lpres_hex.h"

typedef enum { ZSP_HOME = 0, ZSP_OFFICE, ZSP_TEAM, ZSP_CLOUDTOP, ZSP_PARK, ZSP_HALL } zsp_kind_t;

typedef enum { ZSP_OWNER = 0, ZSP_MEMBER, ZSP_GUEST, ZSP_PUBLIC } zsp_role_t;

typedef struct {
    zsp_kind_t kind;
    zsp_role_t role; /* the viewer's role in this space */
} zsp_place_t;

const char *zsp_kind_name(zsp_kind_t k);
int zsp_openness(zsp_kind_t k); /* 0 = most private; HALL = -1 */
bool zsp_can_view(zsp_kind_t k, zsp_role_t r);
bool zsp_can_write(zsp_kind_t k, zsp_role_t r);

/* Check a crossing of one item from `from` to `to`. Returns the tag:
 *   FALSE    / neutral / manifest   refused (no right, or into the HALL)
 *   NEITHER  / neutral / potential  held in the hall: wider audience, confirm
 *   TRUE     / negentropic / manifest proceeds                              */
lpx_t zsp_cross(zsp_place_t from, zsp_place_t to);

/* The person confirmed a held crossing: NEITHER -> TRUE, one Gray step at a
 * time is not needed here because a confirmation is fresh work. Any other
 * tag is returned unchanged (a refusal stays refused). */
lpx_t zsp_confirm(lpx_t held);

/* ---- governance: vertical or horizontal power, chosen per space ----------
 * Each space picks how decisions about it are made (inviting people, changing
 * its rules, opening it wider):
 *   VERTICAL    hierarchical. The owner decides; a member's proposal waits
 *               (NEITHER) for the owner; guests cannot propose.
 *   HORIZONTAL  flat. Any member proposes; it passes when "yes" reaches the
 *               space's quorum with no objection. Quorum reached WITH
 *               objections is a dispute: BOTH, which the action table sends
 *               to people to settle, never decided silently.
 * Spaces founded from the park (shared workspaces, teams inside an
 * organisation) choose their mode at founding and may change it later by
 * their own current rule. */
typedef enum { ZSP_GOV_VERTICAL = 0, ZSP_GOV_HORIZONTAL } zsp_gov_t;

typedef struct {
    zsp_gov_t gov;
    uint8_t quorum_pct; /* HORIZONTAL: share of members needed, 1..100;
                           0 means a simple majority (51) */
} zsp_rules_t;

lpx_t zsp_decide(const zsp_rules_t *rules, zsp_role_t proposer, uint32_t members, uint32_t yes,
                 uint32_t no);

#endif /* ZXV_ZUI_SPACE_H */
