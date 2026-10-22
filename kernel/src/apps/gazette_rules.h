/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gazette_rules.h — Gazette's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/gazette). Pure
 * integer functions, no state: the model (gazette_model.c) and the
 * language test (tests/gazette_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   GR1 SHOW THIS ITEM: its source is followed (followed exactly 1) and
 *       not muted (muted exactly 0) (feed_item_shown). Anything else, a 2
 *       or a -1 among it, is no.
 *   GR2 THE UNREAD BADGE: over every source, one per row, the sum of its
 *       unread (Commons's room_unread, CR3: newest - seen, never below 0)
 *       where shown is 1, left out where shown is 0 (feed_unread_total);
 *       a count outside 0..12 digits or a shown that is not 0 or 1
 *       refuses the whole sum, and so does a sum past int64.
 *   GR3 A READ-STATE RECORD of ^ZXGAZETTE (blocks/gazette/read.myra)
 *       holds integers of at most these digits, sign aside: source 3,
 *       item 12, lamport 18, stamp 13 (ms since 1970), state 1. */
#ifndef GAZETTE_RULES_H
#define GAZETTE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "commons_rules.h"

#define GAZETTE_SOURCE_FIELD  INT64_C(999)                /* i3 */
#define GAZETTE_ITEM_FIELD    INT64_C(999999999999)       /* i12 */
#define GAZETTE_LAMPORT_FIELD INT64_C(999999999999999999) /* i18 */
#define GAZETTE_STAMP_FIELD   INT64_C(9999999999999)      /* i13 */
#define GAZETTE_STATE_FIELD   INT64_C(9)                  /* i1 */
#define GAZETTE_REFUSED       (-1)

/* GR1 */
static inline bool gazette_feed_item_shown(int64_t followed, int64_t muted)
{
    return followed == 1 && muted == 0;
}

/* GR2: GAZETTE_REFUSED when any row is outside its ranges */
static inline int64_t gazette_feed_unread_total(const int64_t *newest, const int64_t *seen,
                                                const int64_t *shown, uint32_t n)
{
    int64_t total = 0;
    if (n && (!newest || !seen || !shown)) return GAZETTE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        int64_t u = commons_room_unread(newest[i], seen[i]);
        if (u == COMMONS_REFUSED || shown[i] < 0 || shown[i] > 1) return GAZETTE_REFUSED;
        if (!shown[i]) continue;
        if (total > INT64_MAX - u) return GAZETTE_REFUSED;
        total += u;
    }
    return total;
}

/* GR3 */
static inline bool gazette_read_record_fits(int64_t source, int64_t item, int64_t lamport,
                                            int64_t stamp, int64_t state)
{
    return commons_digits_fit(source, 3) && commons_digits_fit(item, 12) &&
           commons_digits_fit(lamport, 18) && commons_digits_fit(stamp, 13) &&
           commons_digits_fit(state, 1);
}

#endif
