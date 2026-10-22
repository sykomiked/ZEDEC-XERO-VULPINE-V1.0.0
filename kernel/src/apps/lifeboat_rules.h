/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lifeboat_rules.h — Lifeboat's rules in C, the reference its Eve, KRON,
 * Forth (Cairndrake), BASIC (Hatchling), Sutra and MYRA blocks are checked
 * against (blocks/lifeboat). Pure integer functions, no state: the model
 * (lifeboat_model.c) and the language test (tests/lifeboat_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   LB1 A CHECK RESULT is a flag, 0 (no) or 1 (yes): journal (zxvd's
 *       journal chain verified, journal.verify), books (the books hold,
 *       ledger.check) and tested (the newest backup read back whole,
 *       backup.verify). room is a count of bytes: what this user may still
 *       write (sys.disk avail) minus the floor the owner keeps free, so it
 *       is below 1 when the disk is at or under the floor. A flag that is
 *       no flag, or a room past 18 digits either way, is no check result.
 *   LB2 SAFE TO START NORMALLY (start_ok) when the journal is intact, the
 *       books hold, the room is above the floor and a backup is tested:
 *       journal = books = tested = 1 and room 1..LIFEBOAT_BYTES_MAX.
 *       Anything else, a flag that is no flag among it, is not safe.
 *   LB3 THE SAME RULE AS A NUMBER (start_n): 1 safe, 0 not, and
 *       LIFEBOAT_REFUSED for a flag that is no flag or a room past 18
 *       digits either way.
 *   LB4 THE NEXT REPAIR STEP (repair_step), the first that applies, in
 *       order: 1 free space (room below 1: nothing can be written back
 *       until there is room), 2 test a backup (tested 0: nothing is
 *       restored from a backup that has not read back whole), 3 restore
 *       the books from the tested backup (books 0), 4 keep the broken
 *       journal as it is and tell the owner (journal 0: zxvd has no method
 *       that repairs it), 0 none: start normally. So the step is 0 exactly
 *       when start_ok holds. Outside LB1's domain LIFEBOAT_REFUSED.
 *   LB5 A RESTORE FITS (restore_fits) when what it needs, with the floor
 *       kept free, is at most what may be written: need + reserve <=
 *       free, each 0..LIFEBOAT_BYTES_MAX and need + reserve too (Sutra's
 *       9(18)); anything else does not fit.
 *   LB6 A RECOVERY RECORD of ^ZXLIFEBOAT holds integers of at most the
 *       digits its pieces declare, sign aside (blocks/lifeboat/rescue.myra):
 *       seq 9, step 1, journal 1, books 1, tested 1, room 18, stamp 15 (ms
 *       since 1970). */
#ifndef LIFEBOAT_RULES_H
#define LIFEBOAT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define LIFEBOAT_BYTES_MAX 999999999999999999LL /* 18 digits, Sutra's 9(18) */
#define LIFEBOAT_REFUSED   (-1)

enum {
    LIFEBOAT_STEP_NONE = 0,    /* start normally */
    LIFEBOAT_STEP_SPACE = 1,   /* free space */
    LIFEBOAT_STEP_TEST = 2,    /* test a backup */
    LIFEBOAT_STEP_RESTORE = 3, /* restore the books from the tested backup */
    LIFEBOAT_STEP_JOURNAL = 4  /* keep the broken journal, tell the owner */
};

/* LB1 */
static inline bool lifeboat_flag(int64_t f)
{
    return f == 0 || f == 1;
}

static inline bool lifeboat_results(int64_t journal, int64_t books, int64_t room, int64_t tested)
{
    return lifeboat_flag(journal) && lifeboat_flag(books) && lifeboat_flag(tested) &&
           room >= -LIFEBOAT_BYTES_MAX && room <= LIFEBOAT_BYTES_MAX;
}

/* LB2 */
static inline bool lifeboat_start_ok(int64_t journal, int64_t books, int64_t room, int64_t tested)
{
    return journal == 1 && books == 1 && tested == 1 && room >= 1 && room <= LIFEBOAT_BYTES_MAX;
}

/* LB3 */
static inline int64_t lifeboat_start_n(int64_t journal, int64_t books, int64_t room, int64_t tested)
{
    if (!lifeboat_results(journal, books, room, tested)) return LIFEBOAT_REFUSED;
    return lifeboat_start_ok(journal, books, room, tested) ? 1 : 0;
}

/* LB4 */
static inline int64_t lifeboat_repair_step(int64_t journal, int64_t books, int64_t room,
                                           int64_t tested)
{
    if (!lifeboat_results(journal, books, room, tested)) return LIFEBOAT_REFUSED;
    if (room < 1) return LIFEBOAT_STEP_SPACE;
    if (tested == 0) return LIFEBOAT_STEP_TEST;
    if (books == 0) return LIFEBOAT_STEP_RESTORE;
    if (journal == 0) return LIFEBOAT_STEP_JOURNAL;
    return LIFEBOAT_STEP_NONE;
}

/* LB5 */
static inline bool lifeboat_restore_fits(int64_t need, int64_t avail, int64_t reserve)
{
    if (need < 0 || need > LIFEBOAT_BYTES_MAX || avail < 0 || avail > LIFEBOAT_BYTES_MAX ||
        reserve < 0 || reserve > LIFEBOAT_BYTES_MAX)
        return false;
    if (need > LIFEBOAT_BYTES_MAX - reserve) return false; /* past 9(18): refused */
    return need + reserve <= avail;
}

/* LB6: does x have at most `digits` digits, sign aside? */
static inline bool lifeboat_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool lifeboat_record_fits(int64_t seq, int64_t step, int64_t journal, int64_t books,
                                        int64_t tested, int64_t room, int64_t stamp)
{
    return lifeboat_digits_fit(seq, 9) && lifeboat_digits_fit(step, 1) &&
           lifeboat_digits_fit(journal, 1) && lifeboat_digits_fit(books, 1) &&
           lifeboat_digits_fit(tested, 1) && lifeboat_digits_fit(room, 18) &&
           lifeboat_digits_fit(stamp, 15);
}

/* used_share.used_x100 (used_of_capacity), as the model works it out for
 * the disk holding the Hoard: floor(10000 * (total - left) / total);
 * LIFEBOAT_REFUSED when total is 0 or left is more than total, or a
 * product past 64 bits. */
static inline int64_t lifeboat_used_x100(int64_t total, int64_t left)
{
    if (total < 1 || left < 0 || left > total) return LIFEBOAT_REFUSED;
    int64_t used = total - left;
    if (used > INT64_MAX / 10000) return LIFEBOAT_REFUSED;
    return used * 10000 / total;
}

#endif
