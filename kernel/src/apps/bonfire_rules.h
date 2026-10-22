/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bonfire_rules.h — Bonfire's rules in C, the reference its Eve, MYRA,
 * Trebago and KRON blocks are checked against (blocks/bonfire). Pure
 * integer functions, no state: the model (bonfire_model.c) and the
 * language test (tests/bonfire_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   BR1 MAY THIS GUEST JOIN: the guest is on the gathering's invite list
 *       and the guest's party still fits, each given as its margin:
 *       listpos, the guest's place on the list counted from 0, 0..99 (the
 *       list's 100 places; -1 for a guest not on it), and seatroom, the
 *       seats left once the party is seated (seats - going - party),
 *       0..9999 (guest_join_ok). Each part is a margin so that each field
 *       is held against its own range (Eve) and as a $H day (MYRA,
 *       0..2980013).
 *   BR2 SEATS LEFT: seats - going, one gathering per row, for
 *       0 <= going <= seats <= 9999; anything else is refused
 *       (seats_left).
 *   BR3 THE PEOPLE COMING: the exact sum of each guest's party where the
 *       guest's answer is yes, one guest per row; answer 0 none yet, 1 yes,
 *       2 no, 3 maybe, party 1..99 (the guest and who comes with them); a
 *       value outside refuses the whole sum (rsvp_headcount). How many
 *       guests gave each answer is Witness's count by kind
 *       (witness.kind_count, witness_rules.h WR2), never written here.
 *   BR4 A GATHERING RECORD of ^ZXBONFIRE (blocks/bonfire/gathering.myra)
 *       holds integers of at most these digits, sign aside: gathering (its
 *       place in the model's list) 3, start (seconds since 1970) 11, seats
 *       4, going 4 and venue (its place in place.list, -1 for none) 3
 *       (event_record_ok).
 *   BR5 AN INVITE RECORD of ^ZXBONFIREINV (blocks/bonfire/invite.myra):
 *       gathering 3 and guest (its place on the list) 3, the keys; answer
 *       1, party 2 and stamp (ms since 1970, when the answer came) 13
 *       (invite_record_ok).
 *   BR6 HOW FULL: the share of the seats taken, in hundredths of a
 *       percent, is used_share.used_x100 with total = seats and left =
 *       seats_left: floor(10000 * (total - left) / total), refused when
 *       total is 0 or left is outside 0..total. */
#ifndef BONFIRE_RULES_H
#define BONFIRE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define BONFIRE_GUESTS    100                    /* BR1: an invite list's places */
#define BONFIRE_SEATS_MAX 9999                   /* BR1, BR2, BR4: i4 */
#define BONFIRE_PARTY_MAX 99                     /* BR3, BR5: i2 */
#define BONFIRE_START_MAX INT64_C(99999999999)   /* BR4: i11, seconds since 1970 */
#define BONFIRE_STAMP_MAX INT64_C(9999999999999) /* BR5: i13, ms since 1970 */
#define BONFIRE_DAY_MAX   INT64_C(2980013)       /* BR1: MYRA's $H day */
#define BONFIRE_NO_ROOM   (-1)                   /* BR1: no seat or no such party */
#define BONFIRE_REFUSED   (-1)

enum { BONFIRE_NONE, BONFIRE_YES, BONFIRE_NO, BONFIRE_MAYBE, BONFIRE_ANSWERS };

/* BR1 */
static inline bool bonfire_guest_join_ok(int64_t listpos, int64_t seatroom)
{
    return listpos >= 0 && listpos < BONFIRE_GUESTS && seatroom >= 0 &&
           seatroom <= BONFIRE_SEATS_MAX;
}

/* BR1: the seats left once a party of `party` is seated; BONFIRE_NO_ROOM
 * for seats, going or a party outside their ranges */
static inline int64_t bonfire_seat_room(int64_t seats, int64_t going, int64_t party)
{
    if (seats < 0 || seats > BONFIRE_SEATS_MAX || going < 0 || going > seats || party < 1 ||
        party > BONFIRE_PARTY_MAX)
        return BONFIRE_NO_ROOM;
    return seats - going - party; /* each bounded: never past int64 */
}

/* BR1: the whole rule */
static inline bool bonfire_guest_may_join(int64_t listpos, int64_t seats, int64_t going,
                                          int64_t party)
{
    return bonfire_guest_join_ok(listpos, bonfire_seat_room(seats, going, party));
}

/* BR2 */
static inline int64_t bonfire_seats_left(int64_t seats, int64_t going)
{
    if (going < 0 || going > seats || seats > BONFIRE_SEATS_MAX) return BONFIRE_REFUSED;
    return seats - going;
}

/* BR3: party[0..n) and answer[0..n) */
static inline int64_t bonfire_rsvp_headcount(const int64_t *party, const int64_t *answer,
                                             uint32_t n)
{
    int64_t s = 0;
    if ((!party || !answer) && n) return BONFIRE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (party[i] < 1 || party[i] > BONFIRE_PARTY_MAX || answer[i] < BONFIRE_NONE ||
            answer[i] >= BONFIRE_ANSWERS)
            return BONFIRE_REFUSED;
        if (answer[i] == BONFIRE_YES) s += party[i]; /* at most 99 a row */
    }
    return s;
}

/* BR4, BR5: does x have at most `digits` digits, sign aside? */
static inline bool bonfire_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

/* BR4 */
static inline bool bonfire_event_record_ok(int64_t gathering, int64_t start, int64_t seats,
                                           int64_t going, int64_t venue)
{
    return bonfire_digits_fit(gathering, 3) && bonfire_digits_fit(start, 11) &&
           bonfire_digits_fit(seats, 4) && bonfire_digits_fit(going, 4) &&
           bonfire_digits_fit(venue, 3);
}

/* BR5 */
static inline bool bonfire_invite_record_ok(int64_t gathering, int64_t guest, int64_t answer,
                                            int64_t party, int64_t stamp)
{
    return bonfire_digits_fit(gathering, 3) && bonfire_digits_fit(guest, 3) &&
           bonfire_digits_fit(answer, 1) && bonfire_digits_fit(party, 2) &&
           bonfire_digits_fit(stamp, 13);
}

/* BR6: used_share.used_x100 */
static inline int64_t bonfire_fill_x100(int64_t seats, int64_t left)
{
    if (seats <= 0 || seats > BONFIRE_SEATS_MAX || left < 0 || left > seats) return BONFIRE_REFUSED;
    return 10000 * (seats - left) / seats;
}

#endif
