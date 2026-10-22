/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ferry_rules.h — Ferry's rules in C, the reference its Sutra, Trebago and
 * KRON blocks are checked against (blocks/ferry), and the C of the shared
 * blocks it uses. Pure integer functions, no state: the model
 * (ferry_model.c) and the language test (tests/ferry_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 * Sizes are bytes, 0..FERRY_BYTES_MAX (Sutra's 9(15), just under a
 * petabyte); anything else is refused.
 *
 *   FR1 ROOM AFTER THE MOVE. avail - need - spare: what the target may
 *       still write, less the bytes the plan moves and the bytes the owner
 *       keeps free there. Below 0 the plan is that many bytes short.
 *       Refused (FERRY_NO_ROOM) when an input is out of range.
 *   FR2 FITS ON THE TARGET. need + spare <= avail, every input in range;
 *       an input out of range is no measurement: false.
 *   FR3 AN ITEM MAY MOVE. kind 1 a file, 2 a space, 3 a setting, 4 a
 *       regulated setting, 5 a key; approved 0..9 (one digit), 1 the
 *       owner's yes. Kinds 1..3 move; 4 and 5 only when approved is 1.
 *   FR4 THE PLAN'S TOTAL (banker.total): the exact sum of n sizes, each
 *       and the sum in range, else FERRY_REFUSED.
 *   FR5 A SHARE OF THE PLAN (banker.share): floor(10000 * size / total),
 *       a total of 0 counted as 1; size 0..total, else FERRY_REFUSED.
 *   FR6 PROGRESS x100 (used_share.used_x100): floor(10000 * (total -
 *       left) / total); refused when total is 0 or left is not 0..total. */
#ifndef FERRY_RULES_H
#define FERRY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define FERRY_REFUSED      (-1)
#define FERRY_NO_ROOM      INT64_MIN         /* FR1's refusal: any other value is an answer */
#define FERRY_BYTES_MAX    999999999999999LL /* Sutra's 9(15) */
#define FERRY_APPROVED_MAX 9                 /* Sutra's 9 */

enum {
    FERRY_K_FILE = 1,
    FERRY_K_SPACE = 2,
    FERRY_K_SETTING = 3,
    FERRY_K_REGULATED = 4,
    FERRY_K_KEY = 5,
};

static inline bool ferry_bytes_ok(int64_t b)
{
    return b >= 0 && b <= FERRY_BYTES_MAX;
}

/* FR1 */
static inline int64_t ferry_room(int64_t avail, int64_t need, int64_t spare)
{
    if (!ferry_bytes_ok(avail) || !ferry_bytes_ok(need) || !ferry_bytes_ok(spare))
        return FERRY_NO_ROOM;
    return avail - need - spare; /* at least -2 * FERRY_BYTES_MAX: inside 64 bits */
}

/* FR2 */
static inline bool ferry_fits(int64_t avail, int64_t need, int64_t spare)
{
    int64_t room = ferry_room(avail, need, spare);
    return room != FERRY_NO_ROOM && room >= 0;
}

/* FR3 */
static inline bool ferry_move_ok(int64_t kind, int64_t approved)
{
    if (kind < FERRY_K_FILE || kind > FERRY_K_KEY || approved < 0 || approved > FERRY_APPROVED_MAX)
        return false;
    return kind < FERRY_K_REGULATED || approved == 1;
}

/* FR4 */
static inline int64_t ferry_total(const int64_t *size, uint32_t n)
{
    int64_t s = 0;
    if (!size) return FERRY_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!ferry_bytes_ok(size[i]) || s > FERRY_BYTES_MAX - size[i]) return FERRY_REFUSED;
        s += size[i];
    }
    return s;
}

/* FR5 */
static inline int64_t ferry_share_x100(int64_t size, int64_t total)
{
    if (!ferry_bytes_ok(total) || size < 0 || size > (total ? total : 1)) return FERRY_REFUSED;
    /* size * 10000 is below 10^19: past int64, inside uint64 */
    return (int64_t) ((uint64_t) size * 10000u / (uint64_t) (total ? total : 1));
}

/* FR6 */
static inline int64_t ferry_progress_x100(int64_t total, int64_t left)
{
    if (total <= 0 || total > FERRY_BYTES_MAX || left < 0 || left > total) return FERRY_REFUSED;
    return (int64_t) ((uint64_t) (total - left) * 10000u / (uint64_t) total); /* as FR5 */
}

#endif
