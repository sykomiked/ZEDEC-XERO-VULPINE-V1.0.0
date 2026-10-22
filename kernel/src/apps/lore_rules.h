/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lore_rules.h — Lore's rules in C, the reference its Eve, KRON and MYRA
 * blocks are checked against (blocks/lore). Pure integer functions, no
 * state: the model (lore_model.c) and the language test
 * (tests/lore_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 * A space's kind orders it from the most private to the most open, as
 * zxvd orders them for a crossing (zxv_spaces.h S3): home 0 < office 1 <
 * team 2 < cloudtop 3 < park 4. A leave is the owner's word that the
 * companion (swarm.ask) may read the files under one folder, in any space
 * no more private than the leave's floor, from when it is given until it
 * expires.
 *
 *   LR1 MAY THE AI READ THIS FILE: a live leave is given (leave, exactly
 *       1: given, not revoked and not expired), the file's folder is
 *       granted by it (granted, exactly 1: the file is the leave's folder
 *       or under it), and the file is not from a more private space than
 *       the leave reaches (room, the file's space kind minus the leave's
 *       floor, 0..4). Anything else is no: a flag that is no flag (not 0
 *       or 1) is never read as yes, and a room below 0 is a more private
 *       space.
 *   LR2 DAYS LEFT ON A LEAVE that expires at `expires` seen at `now`, both
 *       seconds since 1970: Lockbox's days_left (lockbox_rules.h LR2),
 *       floor((expires - now) / 86400), never below 0; refused outside
 *       0..253402300799.
 *   LR3 A LEAVE RECORD of ^ZXLORE holds integers of at most the places
 *       its pieces declare, sign aside (blocks/lore/leave.myra): leave 3
 *       (its number), reach 1 (the most private space kind it reaches), depth 2 (the folder's
 *       segments; zxvd's index goes 32 deep), given 12 and expires 12
 *       (seconds since 1970).
 *   LR4 A LEAVE THE OWNER MAY GIVE: a floor that is a space kind (0..4),
 *       a folder at most 32 segments deep, and given before expires, each
 *       inside Lockbox's stamps (0..253402300799); it must also make a
 *       record (LR3). */
#ifndef LORE_RULES_H
#define LORE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lockbox_rules.h"

#define LORE_REFUSED     (-1)
#define LORE_KIND_HOME   0
#define LORE_KIND_PARK   4                     /* the most open kind */
#define LORE_ROOM_MAX    4                     /* LR1: park minus home */
#define LORE_DEPTH_MAX   32                    /* LR4: zxvd's index goes no deeper */
#define LORE_LEAVE_FIELD 999                   /* LR3: i3 */
#define LORE_KIND_FIELD  9                     /* LR3: i1 */
#define LORE_DEPTH_FIELD 99                    /* LR3: i2 */
#define LORE_STAMP_FIELD INT64_C(999999999999) /* LR3: i12 */

/* LR1 */
static inline bool lore_ai_read_ok(int64_t leave, int64_t granted, int64_t room)
{
    return leave == 1 && granted == 1 && room >= 0 && room <= LORE_ROOM_MAX;
}

/* LR1: the room between a file's space and a leave's floor; LORE_REFUSED
 * past the five kinds (a room of -1 is also a more private space, so a
 * refusal is never read as room) */
static inline int64_t lore_room(int64_t kind, int64_t reach)
{
    if (kind < LORE_KIND_HOME || kind > LORE_KIND_PARK) return LORE_REFUSED;
    if (reach < LORE_KIND_HOME || reach > LORE_KIND_PARK) return LORE_REFUSED;
    return kind - reach;
}

/* LR2 */
static inline int64_t lore_leave_days_left(int64_t expires, int64_t now)
{
    return lockbox_days_left(expires, now);
}

static inline bool lore_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* LR3 */
static inline bool lore_leave_record_ok(int64_t leave, int64_t reach, int64_t depth, int64_t given,
                                        int64_t expires)
{
    return lore_fits(leave, LORE_LEAVE_FIELD) && lore_fits(reach, LORE_KIND_FIELD) &&
           lore_fits(depth, LORE_DEPTH_FIELD) && lore_fits(given, LORE_STAMP_FIELD) &&
           lore_fits(expires, LORE_STAMP_FIELD);
}

/* LR4 */
static inline bool lore_leave_fits(int64_t leave, int64_t reach, int64_t depth, int64_t given,
                                   int64_t expires)
{
    return leave >= 0 && leave <= LORE_LEAVE_FIELD && reach >= LORE_KIND_HOME &&
           reach <= LORE_KIND_PARK && depth >= 0 && depth <= LORE_DEPTH_MAX && given >= 0 &&
           given < expires && expires <= LOCKBOX_STAMP_MAX &&
           lore_leave_record_ok(leave, reach, depth, given, expires);
}

#endif
