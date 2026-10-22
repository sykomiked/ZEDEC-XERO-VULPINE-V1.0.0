/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* understory_rules.h — Understory's rules in C, the reference its Sutra,
 * C++, Eve, KRON, Trebago and MYRA blocks are checked against
 * (blocks/understory). Pure integer functions, no state: the model
 * (understory_model.c) and the language test (tests/understory_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   UR1 ENERGY AT A TARIFF. mj millijoules at tariff minor units of the
 *       owner's currency per kWh (UNDERSTORY_MJ_PER_KWH): floor(mj *
 *       tariff / 3600000000), never rounded up. mj 0..UNDERSTORY_MJ_MAX
 *       (Sutra's 9(12), about 277 kWh) and tariff 0..UNDERSTORY_TARIFF_MAX
 *       (9(6)). Advice only: it is no bill.
 *   UR2 A BUDGET PASSED. The excess is what was used in a window minus its
 *       budget, in the budget's unit; the budget is passed when the excess
 *       is 1..UNDERSTORY_EXCESS_MAX (18 digits). 0 or less is inside it; an
 *       excess past 18 digits is no measurement and is never read as over.
 *   UR3 ROLLING-WINDOW TOTALS. One reading per row in order: row i's total
 *       is the exact sum of rows max(0, i - width + 1)..i. Every amount is
 *       0 or more, width 1..UNDERSTORY_WIDTH_MAX, and no total passes 64
 *       bits; anything else refuses the whole.
 *   UR4 A WATER RECORD fits ^ZXUNDERWATER: day at most 6 digits, source 1
 *       and litres 9, sign aside (MYRA's i6, i1 and i9).
 *   UR5 A LAND RECORD fits ^ZXUNDERLAND: parcel at most 6 digits, kind 1
 *       and area (square metres) 12, sign aside.
 *
 * What a reading or parcel may mean (a day from 0, a known source or kind,
 * no negative amount) is the model's, which is stricter than the record:
 * understory_water_ok and understory_land_ok. Anything else is refused
 * (UNDERSTORY_REFUSED), never rounded or wrapped. */
#ifndef UNDERSTORY_RULES_H
#define UNDERSTORY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define UNDERSTORY_REFUSED    INT64_C(-1)
#define UNDERSTORY_MJ_PER_KWH INT64_C(3600000000)
#define UNDERSTORY_MJ_MAX     INT64_C(999999999999)       /* 9(12) */
#define UNDERSTORY_TARIFF_MAX INT64_C(999999)             /* 9(6) */
#define UNDERSTORY_EXCESS_MAX INT64_C(999999999999999999) /* 18 digits */
#define UNDERSTORY_WIDTH_MAX  INT64_C(999999)
#define UNDERSTORY_DAY_MAX    INT64_C(999999)       /* i6 */
#define UNDERSTORY_DIGIT_MAX  INT64_C(9)            /* i1 */
#define UNDERSTORY_LITRES_MAX INT64_C(999999999)    /* i9 */
#define UNDERSTORY_PARCEL_MAX INT64_C(999999)       /* i6 */
#define UNDERSTORY_AREA_MAX   INT64_C(999999999999) /* i12, square metres */

enum { UNDERSTORY_MAINS = 1, UNDERSTORY_WELL, UNDERSTORY_RAIN, UNDERSTORY_SURFACE };
enum {
    UNDERSTORY_BUILT = 1,
    UNDERSTORY_GARDEN,
    UNDERSTORY_CROP,
    UNDERSTORY_PASTURE,
    UNDERSTORY_WILD
};

/* UR1 */
static inline int64_t understory_energy_cost(int64_t mj, int64_t tariff)
{
    if (mj < 0 || mj > UNDERSTORY_MJ_MAX || tariff < 0 || tariff > UNDERSTORY_TARIFF_MAX)
        return UNDERSTORY_REFUSED;
    return mj * tariff / UNDERSTORY_MJ_PER_KWH; /* at most 999999999999 * 999999 < 2^63 */
}

/* UR2 */
static inline bool understory_over_budget(int64_t excess)
{
    return excess >= 1 && excess <= UNDERSTORY_EXCESS_MAX;
}

/* the first row inside row i's window */
static inline uint32_t understory_window_start(uint32_t i, int64_t width)
{
    int64_t s = (int64_t) i + 1 - width;
    return s > 0 ? (uint32_t) s : 0u;
}

/* UR3: 0, every total in out[0..n-1]; UNDERSTORY_REFUSED and out untouched. */
static inline int understory_rolling(const int64_t *amount, uint32_t n, int64_t width, int64_t *out)
{
    if (!amount || !out || width < 1 || width > UNDERSTORY_WIDTH_MAX) return -1;
    for (uint32_t i = 0; i < n; i++)
        if (amount[i] < 0) return -1;
    for (uint32_t i = 0; i < n; i++) { /* check every total before writing any */
        int64_t s = 0;
        for (uint32_t j = understory_window_start(i, width); j <= i; j++) {
            if (s > INT64_MAX - amount[j]) return -1;
            s += amount[j];
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        int64_t s = 0;
        for (uint32_t j = understory_window_start(i, width); j <= i; j++) s += amount[j];
        out[i] = s;
    }
    return 0;
}

static inline bool understory_digits(int64_t v, int64_t max)
{
    return v >= -max && v <= max;
}

/* UR4 */
static inline bool understory_water_entry_ok(int64_t day, int64_t source, int64_t litres)
{
    return understory_digits(day, UNDERSTORY_DAY_MAX) &&
           understory_digits(source, UNDERSTORY_DIGIT_MAX) &&
           understory_digits(litres, UNDERSTORY_LITRES_MAX);
}

/* UR5 */
static inline bool understory_land_entry_ok(int64_t parcel, int64_t kind, int64_t area)
{
    return understory_digits(parcel, UNDERSTORY_PARCEL_MAX) &&
           understory_digits(kind, UNDERSTORY_DIGIT_MAX) &&
           understory_digits(area, UNDERSTORY_AREA_MAX);
}

/* What the model keeps: the record, and a meaning inside it. */
static inline bool understory_water_ok(int64_t day, int64_t source, int64_t litres)
{
    return understory_water_entry_ok(day, source, litres) && day >= 0 &&
           source >= UNDERSTORY_MAINS && source <= UNDERSTORY_SURFACE && litres >= 0;
}

static inline bool understory_land_ok(int64_t parcel, int64_t kind, int64_t area)
{
    return understory_land_entry_ok(parcel, kind, area) && parcel >= 1 &&
           kind >= UNDERSTORY_BUILT && kind <= UNDERSTORY_WILD && area >= 0;
}

#endif
