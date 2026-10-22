/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hamlet_rules.h — Hamlet's rules in C, the reference its Sutra, C++ (as
 * WebAssembly), Eve and MYRA blocks are checked against (blocks/hamlet).
 * Pure integer functions, no state: the model (hamlet_model.c) and the
 * language test (tests/hamlet_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   HR1 GIFT ROOM: what stays of the owner's gift ceiling after a gift,
 *       ceiling - amount in integer minor units, for an amount of 1 up to
 *       the ceiling and a ceiling of at most 18 digits (Sutra's 9(18));
 *       anything else is refused (gift_room).
 *   HR2 SHOW THIS POST TO THIS SEAT: the seat is a member of the post's
 *       group and has not muted it, each given as a margin: roleroom, the
 *       seat's role minus member (none -2, observer -1, member 0, admin 1,
 *       owner 2; a post to no group is 0), is 0..2, and muteroom, minus
 *       the seat's mutes that cover the post (its author, its group), is
 *       exactly 0 (post_show_ok).
 *   HR3 A READ MARKER of ^ZXHAMLET (blocks/hamlet/marker.myra) holds
 *       integers of at most these digits, sign aside: group 3, lamport 18,
 *       stamp (seconds since 1970) 12 (read_marker_ok). */
#ifndef HAMLET_RULES_H
#define HAMLET_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define HAMLET_MINOR_MAX     INT64_C(999999999999999999) /* HR1: 9(18) */
#define HAMLET_ROLE_ROOM_MAX 2                           /* HR2: an owner */
#define HAMLET_DAY_MAX       INT64_C(2980013)            /* HR2: MYRA's $H day */
#define HAMLET_GROUP_FIELD   INT64_C(999)                /* HR3: i3 */
#define HAMLET_LAMPORT_FIELD INT64_C(999999999999999999) /* HR3: i18 */
#define HAMLET_STAMP_FIELD   INT64_C(999999999999)       /* HR3: i12 */
#define HAMLET_REFUSED       (-1)

/* the seat's role in a group, as social.groups names it */
enum {
    HAMLET_ROLE_NONE,
    HAMLET_ROLE_OBSERVER,
    HAMLET_ROLE_MEMBER,
    HAMLET_ROLE_ADMIN,
    HAMLET_ROLE_OWNER
};

/* HR1: HAMLET_REFUSED when it is no gift within the ceiling */
static inline int64_t hamlet_gift_room(int64_t amount, int64_t ceiling)
{
    if (ceiling > HAMLET_MINOR_MAX || amount < 1 || amount > ceiling) return HAMLET_REFUSED;
    return ceiling - amount;
}

/* HR2: the margins from a role (HAMLET_ROLE_*, or -1 for a post to no
 * group) and a count of mutes */
static inline int64_t hamlet_role_room(int64_t role)
{
    return role < 0 ? 0 : role - HAMLET_ROLE_MEMBER;
}

static inline int64_t hamlet_mute_room(int64_t mutes)
{
    return mutes < -INT64_MAX ? INT64_MAX : -mutes;
}

static inline bool hamlet_post_show_ok(int64_t roleroom, int64_t muteroom)
{
    return roleroom >= 0 && roleroom <= HAMLET_ROLE_ROOM_MAX && muteroom == 0;
}

/* HR3: does x have at most `digits` digits, sign aside? */
static inline bool hamlet_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool hamlet_read_marker_ok(int64_t group, int64_t lamport, int64_t stamp)
{
    return hamlet_digits_fit(group, 3) && hamlet_digits_fit(lamport, 18) &&
           hamlet_digits_fit(stamp, 12);
}

#endif
