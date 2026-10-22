/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cartulary_rules.h — Cartulary's rules in C, the reference its Sutra,
 * KRON, ALGOL 60, Eve, Trebago and MYRA blocks are checked against
 * (blocks/cartulary), with the rules it composes from nearer the core.
 * Pure integer functions, no state: the model (cartulary_model.c) and the
 * language test (tests/cartulary_zxapp.inc) both include this.
 *
 *   CR1 SCHEDULE. A retention schedule row is a fixed business record:
 *       class code, whole years, months (cartulary_schedule). A row is
 *       kept floor(36525 * (12 * years + months) / 1200) days (a mean
 *       month of 30.4375 days: 100 years is 36525 days). The rows are
 *       defaults taken, not legal advice; each must hold CR4.
 *   CR2 DAYS LEFT (retention_left): filed + CR1's days - today, days since
 *       1970; 0 or fewer is due, below 0 past due. filed and today
 *       0..999999, years 0..999, months 0..99 (Sutra's pictures), else
 *       refused.
 *   CR3 MAY GO (version_may_go): left <= 0 (down to -9999999, Sutra's
 *       s9(7)), hold 0 and keepok 1, keepsake.keep_ok's answer for what
 *       stays. A hold always wins.
 *   CR4 LIMITS (retention_ok): 1..36525 days, a hold flag 0 or 1.
 *   CR5 GROWTH (bytes_projected): bytes[0..n) at the end of n months,
 *       oldest first; bytes[n-1] + ahead * (bytes[n-1] - bytes[0]) / (n-1)
 *       floored (toward minus infinity), never below 0. n 2..1200, each
 *       month 0..999999999999 bytes, ahead 0..1200 months, else refused.
 *   CR6 GRID (class_year_cell): row i's count of rows with its class and
 *       its year, itself included.
 *   CR7 NODE (record_node_ok) fits ^ZXCART: class 4 digits, record 9,
 *       version 4, filed 6, hold 1, sign aside.
 *   CR8 VERSION STATE (lpres_state_t): the content ids two sources give
 *       for one record's version (0: that source has none) and the
 *       owner's ruling (0 none, 1 or 2 the source it keeps): NEITHER when
 *       neither has it, TRUE when one has it or both give the same id,
 *       BOTH when they differ and no ruling is made (both are kept side
 *       by side), TRUE again once the owner rules 1 or 2.
 *   Composed, as the parts' own rules: banker.total (an exact sum,
 *   refused past 64 bits), banker.entry_ok (folder and seq 9 digits, size
 *   18, mtime 12), keepsake.removed (what keeping `keep` takes of
 *   `snapshots`) and keepsake.keep_ok (keep 1..4294967295), and Witness's
 *   chain and record (witness_rules.h WR1, WR3). */
#ifndef CARTULARY_RULES_H
#define CARTULARY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"
#include "witness_rules.h"

#define CARTULARY_DAY_MAX     INT64_C(999999)       /* CR2: 9(6) days since 1970 */
#define CARTULARY_YEARS_MAX   999                   /* CR2: 9(3) */
#define CARTULARY_MONTHS_MAX  99                    /* CR2: 9(2) */
#define CARTULARY_LEFT_MIN    INT64_C(-9999999)     /* CR3: s9(7) */
#define CARTULARY_KEEP_MIN    1                     /* CR4 */
#define CARTULARY_KEEP_MAX    36525                 /* CR4: 100 years */
#define CARTULARY_BYTES_MAX   INT64_C(999999999999) /* CR5 */
#define CARTULARY_AHEAD_MAX   1200                  /* CR5: 100 years of months */
#define CARTULARY_HISTORY_MAX 1200u                 /* CR5: months of bytes at most */
#define CARTULARY_CLASSES     5                     /* CR1 */
#define CARTULARY_KEPT_MAX    INT64_C(4294967295)   /* keepsake.keep_ok */

/* CR1: the schedule row of class `cls` (1..CARTULARY_CLASSES); false for
 * a class that has none. */
static inline bool cartulary_schedule(int64_t cls, int64_t *years, int64_t *months)
{
    static const int64_t y[CARTULARY_CLASSES] = {0, 3, 7, 10, 100};
    static const int64_t m[CARTULARY_CLASSES] = {6, 0, 0, 0, 0};
    if (cls < 1 || cls > CARTULARY_CLASSES || !years || !months) return false;
    *years = y[cls - 1];
    *months = m[cls - 1];
    return true;
}

/* CR1: a row's days, -1 outside Sutra's pictures */
static inline int64_t cartulary_span(int64_t years, int64_t months)
{
    if (years < 0 || years > CARTULARY_YEARS_MAX || months < 0 || months > CARTULARY_MONTHS_MAX)
        return -1;
    return (12 * years + months) * 36525 / 1200;
}

/* CR2 */
static inline bool cartulary_left(int64_t filed, int64_t years, int64_t months, int64_t today,
                                  int64_t *left)
{
    int64_t span = cartulary_span(years, months);
    if (span < 0 || filed < 0 || filed > CARTULARY_DAY_MAX || today < 0 ||
        today > CARTULARY_DAY_MAX || !left)
        return false;
    *left = filed + span - today;
    return true;
}

/* CR3 */
static inline bool cartulary_may_go(int64_t left, int64_t hold, int64_t keepok)
{
    return left >= CARTULARY_LEFT_MIN && left <= 0 && hold == 0 && keepok == 1;
}

/* CR4 */
static inline bool cartulary_retention_ok(int64_t days, int64_t hold)
{
    return days >= CARTULARY_KEEP_MIN && days <= CARTULARY_KEEP_MAX && hold >= 0 && hold <= 1;
}

/* CR5 */
static inline bool cartulary_projected(const int64_t *bytes, uint32_t n, int64_t ahead,
                                       int64_t *out)
{
    if (!bytes || n < 2u || n > CARTULARY_HISTORY_MAX || ahead < 0 || ahead > CARTULARY_AHEAD_MAX ||
        !out)
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (bytes[i] < 0 || bytes[i] > CARTULARY_BYTES_MAX) return false;
    int64_t last = bytes[n - 1u], d = (int64_t) n - 1;
    int64_t num = ahead * (last - bytes[0]); /* at most 1200 * 10^12 either way */
    int64_t q = num / d;
    if (num % d != 0 && num < 0) q--; /* the floor, toward minus infinity */
    *out = last + q < 0 ? 0 : last + q;
    return true;
}

/* CR6 */
static inline int64_t cartulary_cell(const int64_t *cls, const int64_t *year, uint32_t n,
                                     uint32_t i)
{
    int64_t c = 0;
    if (!cls || !year || i >= n) return 0;
    for (uint32_t j = 0; j < n; j++) c += cls[j] == cls[i] && year[j] == year[i];
    return c;
}

/* CR7 */
static inline bool cartulary_node_ok(int64_t cls, int64_t record, int64_t version, int64_t filed,
                                     int64_t hold)
{
    return witness_digits_fit(cls, 4) && witness_digits_fit(record, 9) &&
           witness_digits_fit(version, 4) && witness_digits_fit(filed, 6) &&
           witness_digits_fit(hold, 1);
}

/* CR8 */
static inline lpres_state_t cartulary_version_state(int64_t a, int64_t b, int64_t ruling)
{
    if (!a && !b) return LPRES_STATE_NEITHER;
    if (!a || !b || a == b) return LPRES_STATE_TRUE;
    return ruling == 1 || ruling == 2 ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
}

/* banker.total: false past 64 bits or for a negative size */
static inline bool cartulary_total(const int64_t *size, uint32_t n, int64_t *out)
{
    int64_t s = 0;
    if ((!size && n) || !out) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (size[i] < 0 || s > INT64_MAX - size[i]) return false;
        s += size[i];
    }
    *out = s;
    return true;
}

/* banker.entry_ok: ^ZXHOARD(folder:i9, seq:i9) = size:i18 ^ mtime:i12 */
static inline bool cartulary_entry_ok(int64_t folder, int64_t seq, int64_t size, int64_t mtime)
{
    return witness_digits_fit(folder, 9) && witness_digits_fit(seq, 9) &&
           witness_digits_fit(size, 18) && witness_digits_fit(mtime, 12);
}

/* keepsake.keep_ok and keepsake.removed */
static inline bool cartulary_keep_ok(int64_t keep)
{
    return keep >= 1 && keep <= CARTULARY_KEPT_MAX;
}

static inline int64_t cartulary_removed(int64_t snapshots, int64_t keep)
{
    return snapshots > keep ? snapshots - keep : 0;
}

#endif
