/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* commons_rules.h — Commons's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/commons). Pure
 * integer functions, no state: the model (commons_model.c) and the
 * language test (tests/commons_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   CR1 MAY I POST HERE: the owner is a member of the room, the room is
 *       joined and its feature switch is on, each given as a flag that is
 *       exactly 1 (room_post_ok). Anything else, 2 among it, is no.
 *   CR2 TEXT LIMITS: a message is 1..1024 bytes (message_text_ok) and a
 *       reaction 1..32 bytes (reaction_text_ok), zxvd's own limits for
 *       chat.post, social.post, pspace.post, chat.react and social.react.
 *   CR3 UNREAD: a room's unread posts are newest - seen, never below 0
 *       (room_unread), where newest counts the posts known in the room and
 *       seen the posts the owner has read; a count below 0 or past 12
 *       digits is refused.
 *   CR4 A ROOM RECORD of ^ZXCOMMONS (blocks/commons/room.myra) holds
 *       integers of at most these digits, sign aside: room 3, kind 1,
 *       newest and seen 12, members 6.
 *   CR5 A MESSAGE RECORD of ^ZXCOMMONSMSG (blocks/commons/message.myra):
 *       room 3, pos 12, lamport 18, stamp 13 (ms since 1970, to the year
 *       2286), bytes 4. */
#ifndef COMMONS_RULES_H
#define COMMONS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define COMMONS_TEXT_MAX    1024u
#define COMMONS_REACT_MAX   32u
#define COMMONS_COUNT_MAX   INT64_C(999999999999)       /* 12 digits: Trebago's guard, MYRA i12 */
#define COMMONS_LAMPORT_MAX INT64_C(999999999999999999) /* i18 */
#define COMMONS_STAMP_MAX   INT64_C(9999999999999)      /* i13 */
#define COMMONS_MEMBERS_MAX INT64_C(999999)             /* i6 */
#define COMMONS_ROOM_FIELD  INT64_C(999)                /* i3 */
#define COMMONS_BYTES_FIELD INT64_C(9999)               /* i4 */
#define COMMONS_REFUSED     (-1)

/* CR1 */
static inline bool commons_room_post_ok(int64_t member, int64_t joined, int64_t enabled)
{
    return member == 1 && joined == 1 && enabled == 1;
}

/* CR2 */
static inline bool commons_message_text_ok(int64_t bytes)
{
    return bytes >= 1 && bytes <= (int64_t) COMMONS_TEXT_MAX;
}

static inline bool commons_reaction_text_ok(int64_t bytes)
{
    return bytes >= 1 && bytes <= (int64_t) COMMONS_REACT_MAX;
}

/* CR3: COMMONS_REFUSED when a count is outside 0..COMMONS_COUNT_MAX */
static inline int64_t commons_room_unread(int64_t newest, int64_t seen)
{
    if (newest < 0 || newest > COMMONS_COUNT_MAX || seen < 0 || seen > COMMONS_COUNT_MAX)
        return COMMONS_REFUSED;
    return newest > seen ? newest - seen : 0;
}

/* CR4, CR5: does x have at most `digits` digits, sign aside? */
static inline bool commons_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool commons_room_record_fits(int64_t room, int64_t kind, int64_t newest,
                                            int64_t seen, int64_t members)
{
    return commons_digits_fit(room, 3) && commons_digits_fit(kind, 1) &&
           commons_digits_fit(newest, 12) && commons_digits_fit(seen, 12) &&
           commons_digits_fit(members, 6);
}

static inline bool commons_message_record_fits(int64_t room, int64_t pos, int64_t lamport,
                                               int64_t stamp, int64_t bytes)
{
    return commons_digits_fit(room, 3) && commons_digits_fit(pos, 12) &&
           commons_digits_fit(lamport, 18) && commons_digits_fit(stamp, 13) &&
           commons_digits_fit(bytes, 4);
}

#endif
