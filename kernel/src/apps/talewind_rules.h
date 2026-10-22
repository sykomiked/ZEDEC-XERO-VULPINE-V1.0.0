/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talewind_rules.h — Talewind's rules in C, the reference its Eve, KRON,
 * Pascal and MYRA blocks are checked against (blocks/talewind). Pure
 * integer functions, no state: the model (talewind_model.c) and the
 * language test (tests/talewind_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 * A story is a graph: passages (its nodes, numbered 1..64) and choices
 * (its edges, at most 4 leaving a passage). The reader holds flags, a mask
 * of 16 (bit k is flag k + 1); a choice needs some flags and sets some.
 * Every passage has an age rating: 0 all ages, 1 seven and up, 2 twelve and
 * up, 3 sixteen and up, 4 adults only (a default taken: five steps, the
 * common shape of the age labels in use); the reader's ceiling is the
 * highest rating the owner allows them.
 *
 *   TR1 A CHOICE IS SHOWN when, each part given as its margin: lacking,
 *       the flags it needs that the reader does not hold (TR2), is exactly
 *       0; and ratingroom, the reader's ceiling less the rating of the
 *       passage it leads to (TR6), is 0..4. Anything else is not shown.
 *   TR2 FLAGS LACKING: how many flags of need are not in held, each mask
 *       0..65535; refused (TALEWIND_REFUSED) outside that.
 *   TR3 FLAGS AFTER a choice: held with every flag of sets added (held OR
 *       sets), each mask 0..65535; refused outside that.
 *   TR4 A PASSAGE RECORD of ^ZXTALE holds integers of at most the places
 *       its pieces declare, sign aside (blocks/talewind/passage.myra):
 *       story 6, passage 2, rating 1, bytes 4.
 *   TR5 A CHOICE RECORD of ^ZXTALECH, the same way (choice.myra): story 6,
 *       passage 2, choice 1, target 2, need 5, sets 5.
 *   TR6 RATING ROOM: ceiling - rating, each 0..4; TALEWIND_NO_ROOM (a room
 *       TR1 never shows) when either is outside. */
#ifndef TALEWIND_RULES_H
#define TALEWIND_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TALEWIND_FLAGS_MAX     65535 /* TR2, TR3: 16 flags */
#define TALEWIND_RATING_MAX    4     /* TR1, TR6: adults only */
#define TALEWIND_REFUSED       (-1)
#define TALEWIND_NO_ROOM       (-100) /* TR6: below every real room */
#define TALEWIND_STORY_FIELD   999999 /* TR4, TR5: i6 */
#define TALEWIND_PASSAGE_FIELD 99     /* i2 */
#define TALEWIND_RATING_FIELD  9      /* i1 */
#define TALEWIND_BYTES_FIELD   9999   /* i4 */
#define TALEWIND_CHOICE_FIELD  9      /* i1 */
#define TALEWIND_MASK_FIELD    99999  /* i5 */

static inline bool talewind_mask_ok(int64_t m)
{
    return m >= 0 && m <= TALEWIND_FLAGS_MAX;
}

/* TR1 */
static inline bool talewind_choice_shown(int64_t lacking, int64_t ratingroom)
{
    return lacking == 0 && ratingroom >= 0 && ratingroom <= TALEWIND_RATING_MAX;
}

/* TR2 */
static inline int64_t talewind_flags_lacking(int64_t need, int64_t held)
{
    if (!talewind_mask_ok(need) || !talewind_mask_ok(held)) return TALEWIND_REFUSED;
    int64_t lacking = 0;
    for (int64_t m = need & ~held; m; m >>= 1) lacking += m & 1;
    return lacking;
}

/* TR3 */
static inline int64_t talewind_flags_after(int64_t held, int64_t sets)
{
    if (!talewind_mask_ok(held) || !talewind_mask_ok(sets)) return TALEWIND_REFUSED;
    return held | sets;
}

static inline bool talewind_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* TR4 */
static inline bool talewind_passage_ok(int64_t story, int64_t passage, int64_t rating,
                                       int64_t bytes)
{
    return talewind_fits(story, TALEWIND_STORY_FIELD) &&
           talewind_fits(passage, TALEWIND_PASSAGE_FIELD) &&
           talewind_fits(rating, TALEWIND_RATING_FIELD) &&
           talewind_fits(bytes, TALEWIND_BYTES_FIELD);
}

/* TR5 */
static inline bool talewind_choice_ok(int64_t story, int64_t passage, int64_t choice,
                                      int64_t target, int64_t need, int64_t sets)
{
    return talewind_fits(story, TALEWIND_STORY_FIELD) &&
           talewind_fits(passage, TALEWIND_PASSAGE_FIELD) &&
           talewind_fits(choice, TALEWIND_CHOICE_FIELD) &&
           talewind_fits(target, TALEWIND_PASSAGE_FIELD) &&
           talewind_fits(need, TALEWIND_MASK_FIELD) && talewind_fits(sets, TALEWIND_MASK_FIELD);
}

/* TR6 */
static inline int64_t talewind_rating_room(int64_t ceiling, int64_t rating)
{
    if (ceiling < 0 || ceiling > TALEWIND_RATING_MAX || rating < 0 || rating > TALEWIND_RATING_MAX)
        return TALEWIND_NO_ROOM;
    return ceiling - rating;
}

#endif
