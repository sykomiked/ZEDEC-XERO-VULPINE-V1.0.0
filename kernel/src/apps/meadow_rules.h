/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* meadow_rules.h — Meadow's rules in C, the reference its Eve, MYRA and
 * Trebago blocks are checked against (blocks/meadow). Pure integer
 * functions, no state: the model (meadow_model.c) and the language test
 * (tests/meadow_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   MR1 THE RIGHTS RECORD. What a seat may do in a space follows from the
 *       space's kind alone (zxv_spaces.h S1), and writing (posting) is
 *       open to every role up to one, in the order owner 0, member 1,
 *       guest 2, public 3: home and cloud top to the owner, office and team
 *       to the owner and members, a park to everyone. The seat's role room
 *       is that most open writer minus the seat's role: 0..3 may post,
 *       below 0 may not, and an unknown kind or role is MEADOW_NO_ROOM.
 *   MR2 MAY THIS SEAT POST HERE: the seat's role room is 0..3 and the
 *       post is 1..1024 bytes (pspace.post's limit, vna_space.h
 *       VNA_SPACE_TEXT_MAX), the size given as its two margins, lenleft =
 *       1024 - bytes and lenpast = bytes - 1, each 0..1023
 *       (seat_post_ok). Each part is a margin so that each field is held
 *       against its own range (Eve) and as a $H day (MYRA, 0..2980013).
 *   MR3 NEW EVENTS IN A SPACE: pspace.events numbers a space's events 1, 2,
 *       ... in the order this node applied them and answers the last index
 *       as next; the events applied after since are next - since, one
 *       space per row, for 0 <= since <= next <= 999999999999999999 (the
 *       record's 18 digits); anything else is refused (space_new_events).
 *       Only the last 32 are kept, so more than 32 means some were missed.
 *   MR4 MEMBERS ACROSS SPACES: the exact sum of pspace.members' count of
 *       each space (one per row), each 0..33 (32 members, vna_space.h
 *       VNA_SPACE_MEMBERS, and this node); a count outside refuses the
 *       whole sum (space_members_total).
 *   MR5 A JOINED SPACE RECORD of ^ZXMEADOW (blocks/meadow/joined.myra)
 *       holds integers of at most these digits, sign aside: space (its
 *       place in pspace.list) 1, phase 1 (1 searching, 2 joining, 3 in,
 *       4 not found), members 2, last (the last event index read) 18 and
 *       stamp (ms since 1970, when the last read event was made) 13
 *       (joined_space_ok). */
#ifndef MEADOW_RULES_H
#define MEADOW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define MEADOW_POST_MAX     1024                        /* MR2: pspace.post */
#define MEADOW_ROLE_ROOM    3                           /* MR1: public, in a park */
#define MEADOW_NO_ROOM      (-9)                        /* MR1: no kind or no role */
#define MEADOW_DAY_MAX      INT64_C(2980013)            /* MR2: MYRA's $H day */
#define MEADOW_INDEX_MAX    INT64_C(999999999999999999) /* MR3, MR5: i18 */
#define MEADOW_SPACE_PEOPLE 33                          /* MR4: 32 members and this node */
#define MEADOW_REFUSED      (-1)

/* zxv_spaces.h's kinds and roles, as spaces.list names them */
enum { MEADOW_HOME, MEADOW_OFFICE, MEADOW_TEAM, MEADOW_CLOUDTOP, MEADOW_PARK, MEADOW_KINDS };
enum { MEADOW_OWNER, MEADOW_MEMBER, MEADOW_GUEST, MEADOW_PUBLIC, MEADOW_ROLES };

/* MR1: the most open role that may write in each kind */
static inline int64_t meadow_writer(int64_t kind)
{
    static const int8_t most[MEADOW_KINDS] = {MEADOW_OWNER, MEADOW_MEMBER, MEADOW_MEMBER,
                                              MEADOW_OWNER, MEADOW_PUBLIC};
    return kind < 0 || kind >= MEADOW_KINDS ? MEADOW_NO_ROOM : most[kind];
}

static inline int64_t meadow_role_room(int64_t kind, int64_t role)
{
    int64_t w = meadow_writer(kind);
    if (w == MEADOW_NO_ROOM || role < 0 || role >= MEADOW_ROLES) return MEADOW_NO_ROOM;
    return w - role;
}

/* MR2 */
static inline bool meadow_seat_post_ok(int64_t roleroom, int64_t lenleft, int64_t lenpast)
{
    return roleroom >= 0 && roleroom <= MEADOW_ROLE_ROOM && lenleft >= 0 &&
           lenleft <= MEADOW_POST_MAX - 1 && lenpast >= 0 && lenpast <= MEADOW_POST_MAX - 1;
}

/* MR2: the two margins of a post of `bytes` bytes, and the whole rule */
static inline int64_t meadow_len_left(int64_t bytes)
{
    return bytes < MEADOW_POST_MAX - INT64_MAX ? INT64_MAX : MEADOW_POST_MAX - bytes;
}

static inline int64_t meadow_len_past(int64_t bytes)
{
    return bytes == INT64_MIN ? INT64_MIN : bytes - 1;
}

static inline bool meadow_seat_may_post(int64_t kind, int64_t role, int64_t bytes)
{
    return meadow_seat_post_ok(meadow_role_room(kind, role), meadow_len_left(bytes),
                               meadow_len_past(bytes));
}

/* MR3 */
static inline int64_t meadow_space_new_events(int64_t since, int64_t next)
{
    if (since < 0 || next < since || next > MEADOW_INDEX_MAX) return MEADOW_REFUSED;
    return next - since;
}

/* MR4: counts[0..n) */
static inline int64_t meadow_space_members_total(const int64_t *count, uint32_t n)
{
    int64_t s = 0;
    if (!count && n) return MEADOW_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (count[i] < 0 || count[i] > MEADOW_SPACE_PEOPLE) return MEADOW_REFUSED;
        s += count[i]; /* at most 33 per row: never past int64 for a uint32_t count of rows */
    }
    return s;
}

/* MR5: does x have at most `digits` digits, sign aside? */
static inline bool meadow_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool meadow_joined_space_ok(int64_t space, int64_t phase, int64_t members,
                                          int64_t last, int64_t stamp)
{
    return meadow_digits_fit(space, 1) && meadow_digits_fit(phase, 1) &&
           meadow_digits_fit(members, 2) && meadow_digits_fit(last, 18) &&
           meadow_digits_fit(stamp, 13);
}

#endif
