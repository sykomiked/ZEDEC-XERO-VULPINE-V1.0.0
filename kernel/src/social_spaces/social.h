/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* social.h — the free-forum social layer: named spaces where people gather.
 *
 * WHAT THIS IS
 * ------------
 * The public rooms of ZXV. Don Tovani's Social Club (entrepreneurs and think
 * tanks, an open forum of development), Zevion's Hideout (a developer's den),
 * and casual socializing. People open spaces, join them, and post.
 *
 * THE MANDATE (true by CONSTRUCTION, not by policy)
 * -------------------------------------------------
 *   1. NOBODY IS EVER PLATFORM-BANNED. Free speech is sacred; any interaction
 *      on the platform is speech. Look through this whole API: there is no
 *      soc_ban, no soc_delete, no soc_silence, no remove-a-person. They are
 *      not "disabled" — they were never written. You cannot call what does
 *      not exist.
 *
 *   2. SAFETY COMES ONLY FROM MATCH-LIKE-WITH-LIKE. The single moderation
 *      tool is soc_sort(): a repeat troll is routed, through concord's
 *      symmetric/rehabilitative quarantine, into a grouping with OTHER
 *      trolls. We don't silence anyone; we just seat the hecklers together
 *      and let them heckle each other. Standing recovers over time (concord),
 *      so it is a corner to grow out of, not a cage.
 *
 *   3. NO ADVERTISEMENTS, ANYWHERE. Grep this file and social.c for "ad",
 *      "sponsored", "promoted": nothing. There is no ad primitive to abuse
 *      because there is no ad primitive at all.
 *
 * Posts are NEVER deleted. A post is an interaction, an interaction is
 * speech, and speech is retained.
 *
 * The safety mechanism is entirely concord's (con_report_boundary /
 * con_standing / con_divided / con_may_match). This module models the SPACES,
 * the MEMBERSHIP, and the no-ban SORTING logic on top of it. It duplicates
 * none of concord's matching math.
 *
 * OPS BOUNDARY: message transport and durable persistence are NOT this
 * module's job. We model the space, its membership, and bounded in-core post
 * retention so the logic is testable; the wire and the disk plug in above.
 *
 * Freestanding: integer only (Q32.32 via surplus_real_t), no libc, no float.
 */
#ifndef ZXV_SOCIAL_H
#define ZXV_SOCIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "../concord/concord.h"

#define SOC_MAX_SPACES     16u
#define SOC_MAX_MEMBERS    CON_MAX_PEOPLE  /* a space can hold the whole commons */
#define SOC_NAME_LEN       32u
#define SOC_MSG_MAX        256u            /* bytes retained per post (ops bound) */
#define SOC_MAX_POSTS      64u             /* posts retained in core, per space   */
#define SOC_QUAR_DURATION  1000u           /* divide lifetime in ticks             */

/* The three kinds of room. Don Tovani's Social Club is SOC_SOCIAL_CLUB;
 * Zevion's Hideout is SOC_DEV_DEN. */
typedef enum {
    SOC_SOCIAL_CLUB = 0,   /* entrepreneurs + think tanks, open forum */
    SOC_DEV_DEN,           /* a developer's den */
    SOC_CASUAL             /* casual socializing */
} soc_space_kind_t;

/* Typed results. NONE of these is a ban — the worst that happens to a person
 * is being seated with their own kind, and even that recovers. */
typedef enum {
    SOC_OK            =  0,
    SOC_ERR_NULL      = -1,
    SOC_ERR_FULL      = -2,   /* no space/member/post slot (ops bound) */
    SOC_ERR_NOTFOUND  = -3,   /* no such space or person */
    SOC_ERR_NOT_MEMBER= -4,   /* person is not in that space */
    SOC_ERR_NO_PEERS  = -5    /* nobody to sort against — a room of one */
} soc_result_t;

typedef struct {
    uint32_t author;
    uint32_t len;
    uint8_t  bytes[SOC_MSG_MAX];
} soc_post_t;

typedef struct {
    bool             open;
    soc_space_kind_t kind;
    char             name[SOC_NAME_LEN];
    uint32_t         member[SOC_MAX_MEMBERS];
    uint32_t         n_members;
    soc_post_t       post[SOC_MAX_POSTS];
    uint32_t         n_posts;               /* monotonic: posts are never removed */
} soc_space_t;

typedef struct {
    soc_space_t   space[SOC_MAX_SPACES];
    uint32_t      n_spaces;
    con_commons_t commons;                  /* the ONE safety mechanism */
} soc_world_t;

/* Bring a world into being: empty rooms, empty commons. */
void soc_init(soc_world_t *w);

/* Open a named space of a given kind. Returns the space index (>=0) or a
 * negative soc_result_t. Don Tovani's Club -> SOC_SOCIAL_CLUB,
 * Zevion's Hideout -> SOC_DEV_DEN. */
int32_t soc_open_space(soc_world_t *w, soc_space_kind_t kind, const char *name);

/* Join a space. Entry is OPEN (the forum is free); the person is also linked
 * into the concord commons so they can be matched/recommended and, if it ever
 * comes to it, sorted. Returns SOC_OK or a negative soc_result_t. Idempotent:
 * re-joining a space you're already in is a no-op success. */
int32_t soc_join(soc_world_t *w, int32_t space, uint32_t person);

/* Post to a space. A post is NEVER deleted; it is retained (bounded in core).
 * Membership is the ONLY requirement — standing is irrelevant, because we do
 * not silence anyone. Returns the new post index (>=0) or negative. */
int32_t soc_post(soc_world_t *w, int32_t space, uint32_t person,
                 const uint8_t *msg, uint32_t len);

/* Is `person` a member of `space`? */
bool soc_is_member(const soc_world_t *w, int32_t space, uint32_t person);

/* Retained-post access — proof that nothing was deleted. */
uint32_t       soc_post_count(const soc_world_t *w, int32_t space);
const uint8_t *soc_post_at(const soc_world_t *w, int32_t space, uint32_t idx,
                           uint32_t *len_out, uint32_t *author_out);

/* THE ONLY MODERATION TOOL. Routes a repeat `troublemaker` into a like-with-
 * like grouping via concord: mutual, temporary divides with the good-standing
 * folks they were bothering, and a slide into quarantine standing where they
 * are matched ONLY with other quarantined people. It REMOVES NO ONE — the
 * troll stays a member of every space and can still post. Returns the size of
 * the heckler's corner they now sit in (count of fellow quarantined people,
 * >=0) or a negative soc_result_t. */
int32_t soc_sort(soc_world_t *w, uint32_t troublemaker);

#endif /* ZXV_SOCIAL_H */
