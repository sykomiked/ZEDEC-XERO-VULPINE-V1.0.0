/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cellar_rules.h — Cellar's rules in C, the reference its Eve and KRON
 * blocks are checked against (blocks/cellar), and the C of the shared
 * blocks it uses. Pure integer functions, no state: the model
 * (cellar_model.c) and the language test (tests/cellar_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 * Bytes are 0..CELLAR_BYTES_MAX (18 digits: used_share's Sutra 9(18) and
 * Eve's room range); anything else is refused.
 *
 *   CR1 THE VOLUME IN USE (used_share.used_x100): floor(10000 * (total -
 *       left) / total), x100 (1234 = 12.34%); refused when total is not
 *       1..CELLAR_BYTES_MAX or left is not 0..total. A folder's share of
 *       the volume is the same rule with left = total - the folder's bytes.
 *   CR2 A FOLDER'S SHARE OF THE WHOLE (banker.share, share_of_whole):
 *       floor(10000 * size / whole), a whole of 0 counted as 1; refused
 *       when size is not 0..whole or whole is past CELLAR_BYTES_MAX.
 *   CR3 AN EXACT TOTAL (banker.total): the sum of n sizes, each and the
 *       sum 0..CELLAR_BYTES_MAX, else refused, never wrapped.
 *   CR4 SAFE TO REPAIR (repair_safe_ok, Eve and KRON): room (what this
 *       user may still write less the scratch a repair keeps free) 1..
 *       CELLAR_BYTES_MAX, fill (CR1 of the volume) 0..CELLAR_FILL_MAX, and
 *       approved exactly 1, the owner's yes. Anything else is not safe.
 *       Advisory: the owner confirms every repair. */
#ifndef CELLAR_RULES_H
#define CELLAR_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define CELLAR_REFUSED   (-1)
#define CELLAR_BYTES_MAX 999999999999999999LL /* 18 digits */
#define CELLAR_FILL_MAX  9500                 /* 95.00% full is the most a repair runs on */

/* floor(10000 * part / whole) for 0 <= part <= whole <= CELLAR_BYTES_MAX,
 * whole >= 1: long division a digit at a time, every step below 10^19,
 * so it fits uint64 where 10000 * part would not */
static inline int64_t cellar_x100(int64_t part, int64_t whole)
{
    uint64_t w = (uint64_t) whole, r = (uint64_t) part % w;
    uint64_t q = (uint64_t) part / w;
    for (uint32_t d = 0; d < 4u; d++) {
        r *= 10u;
        q = q * 10u + r / w;
        r %= w;
    }
    return (int64_t) q;
}

/* CR1 */
static inline int64_t cellar_used_x100(int64_t total, int64_t left)
{
    if (total < 1 || total > CELLAR_BYTES_MAX || left < 0 || left > total) return CELLAR_REFUSED;
    return cellar_x100(total - left, total);
}

/* CR2 */
static inline int64_t cellar_share_x100(int64_t size, int64_t whole)
{
    if (whole < 0 || whole > CELLAR_BYTES_MAX) return CELLAR_REFUSED;
    if (whole == 0) whole = 1;
    if (size < 0 || size > whole) return CELLAR_REFUSED;
    return cellar_x100(size, whole);
}

/* CR3 */
static inline int64_t cellar_total(const int64_t *size, uint32_t n)
{
    int64_t s = 0;
    if (!size && n) return CELLAR_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (size[i] < 0 || size[i] > CELLAR_BYTES_MAX - s) return CELLAR_REFUSED;
        s += size[i];
    }
    return s;
}

/* CR4 */
static inline bool cellar_repair_safe(int64_t room, int64_t fill, int64_t approved)
{
    return room >= 1 && room <= CELLAR_BYTES_MAX && fill >= 0 && fill <= CELLAR_FILL_MAX &&
           approved == 1;
}

#endif
