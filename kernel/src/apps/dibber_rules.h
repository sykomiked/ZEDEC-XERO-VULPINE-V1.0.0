/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dibber_rules.h — Dibber's rules in C, the reference its Trebago, KRON,
 * Eve, Sutra and MYRA blocks are checked against (blocks/dibber). Pure
 * integer functions, no state: the model (dibber_model.c) and the language
 * test (tests/dibber_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs. Water is counted in
 * hundredths of a litre (centilitres), exact.
 *
 *   DR1 WATER PER BED PER WEEK (bed_water_litres): area square decimetres
 *       under depth millimetres is area * depth / 100 litres (a square
 *       metre under a millimetre is a litre), an exact reduced rational;
 *       area 0 .. DIBBER_AREA_MAX, depth 0 .. DIBBER_DEPTH_MAX, else
 *       refused. The task's "area x depth = litres" is a hundred times too
 *       much for square decimetres; the units are kept and the /100 added.
 *   DR2 MAY WATER NOW (water_now_ok), advisory: excess (the week's water
 *       used plus this watering less the week's budget, centilitres) 0 or
 *       less, down to -DIBBER_EXCESS_MAX, AND frost 0 (no frost flag in
 *       weatherglass.warn_flags). Each field against its own range, so Eve
 *       holds it.
 *   DR3 WATER LIMITS (watering_limit_ok), advisory: one watering 1 ..
 *       DIBBER_ONE_MAX mm (a default taken: more runs off) and 1 ..
 *       DIBBER_TIMES_MAX waterings a week.
 *   DR4 A SEED OR SUPPLY LINE'S COST (seed_supply_cost): count * price
 *       minor units, count 0 .. DIBBER_COUNT_MAX (Sutra's 9(6)), price 0 ..
 *       DIBBER_PRICE_MAX (9(9)); else refused.
 *   DR5 DAYS TO HARVEST (harvest_gdd_days): from the day of sowing, each
 *       day adds max(0, mean - base) tenths of a degree day; the harvest is
 *       due on the first day (1 the day of sowing) whose running sum
 *       reaches need. mean -1000 .. 1000 tenths of a degree C each, base 0
 *       .. DIBBER_BASE_MAX, need 1 .. DIBBER_NEED_MAX, at least one day;
 *       need never reached in the days given is no date: refused.
 *   DR6 THE BED GRID (bed_family_repeats): one planting a row (bed, year,
 *       crop family); row i's count of other years in the grid that put
 *       the same family on the same bed. 0 is no repeat; Dibber plans no
 *       repeat on a bed.
 *   DR7 THE PLOT HIERARCHY (plot_bed_record_ok) fits ^ZXDIBBER(plot, bed)
 *       = area: plot at most 6 digits, bed 4, area 9, sign aside.
 *   DR8 SOIL MOISTURE (dibber_soil_state, kept in C): a reading of
 *       DIBBER_SOW_MOIST percent or more says the bed is moist enough to
 *       sow (TRUE), less says it is not (FALSE). Two readings of one bed on
 *       one day are joined four-valued (lpres_kjoin): one saying each is
 *       BOTH, and the planting suggestion is held until a re-test.
 *
 * The parts' rules are their own headers (furrow_rules.h,
 * solstice_rules.h, understory_rules.h) and Weatherglass's model. Anything
 * outside is refused (DIBBER_REFUSED), never rounded or wrapped. */
#ifndef DIBBER_RULES_H
#define DIBBER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "furrow_rules.h"
#include "lpres.h"
#include "solstice_rules.h"
#include "understory_rules.h"

#define DIBBER_REFUSED    INT64_MIN
#define DIBBER_AREA_MAX   INT64_C(999999)             /* DR1: dm2 */
#define DIBBER_DEPTH_MAX  INT64_C(999)                /* DR1: mm a week */
#define DIBBER_EXCESS_MAX INT64_C(999999999999999999) /* DR2: 18 digits */
#define DIBBER_ONE_MAX    INT64_C(40)                 /* DR3: mm in one watering */
#define DIBBER_TIMES_MAX  INT64_C(14)                 /* DR3: waterings a week */
#define DIBBER_COUNT_MAX  INT64_C(999999)             /* DR4: 9(6) */
#define DIBBER_PRICE_MAX  INT64_C(999999999)          /* DR4: 9(9) */
#define DIBBER_MEAN_MAX   INT64_C(1000)               /* DR5: tenths of a degree C */
#define DIBBER_BASE_MAX   INT64_C(400)
#define DIBBER_NEED_MAX   INT64_C(9999999)
#define DIBBER_PLOT_MAX   INT64_C(999999)    /* DR7: 6 digits */
#define DIBBER_BED_MAX    INT64_C(9999)      /* 4 */
#define DIBBER_AREA_DIG   INT64_C(999999999) /* 9 */
#define DIBBER_SOW_MOIST  INT64_C(30)        /* DR8: percent, a default taken */
#define DIBBER_WEEK       7                  /* days in the water budget's week */

/* DR1: false outside the domain; else num/den reduced, den > 0 */
static inline bool dibber_bed_litres(int64_t area, int64_t depth, int64_t *num, int64_t *den)
{
    if (!num || !den || area < 0 || area > DIBBER_AREA_MAX || depth < 0 || depth > DIBBER_DEPTH_MAX)
        return false;
    int64_t n = area * depth, d = 100, a = n, b = d;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    *num = n / a;
    *den = d / a;
    return true;
}

/* DR2 */
static inline bool dibber_water_now_ok(int64_t excess, int64_t frost)
{
    return excess >= -DIBBER_EXCESS_MAX && excess <= 0 && frost == 0;
}

/* DR3 */
static inline bool dibber_watering_limit_ok(int64_t depth, int64_t times)
{
    return depth >= 1 && depth <= DIBBER_ONE_MAX && times >= 1 && times <= DIBBER_TIMES_MAX;
}

/* DR4 */
static inline int64_t dibber_supply_cost(int64_t count, int64_t price)
{
    if (count < 0 || count > DIBBER_COUNT_MAX || price < 0 || price > DIBBER_PRICE_MAX)
        return DIBBER_REFUSED;
    return count * price; /* at most 999999 * 999999999 < 2^63 */
}

/* DR5 */
static inline int64_t dibber_harvest_days(const int64_t *mean, uint32_t n, int64_t base,
                                          int64_t need)
{
    int64_t c = 0;
    if (!mean || !n || base < 0 || base > DIBBER_BASE_MAX || need < 1 || need > DIBBER_NEED_MAX)
        return DIBBER_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (mean[i] < -DIBBER_MEAN_MAX || mean[i] > DIBBER_MEAN_MAX) return DIBBER_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        c += mean[i] > base ? mean[i] - base : 0; /* at most 2000 a day */
        if (c >= need) return (int64_t) i + 1;
    }
    return DIBBER_REFUSED;
}

/* DR6 */
static inline int64_t dibber_family_repeats(const int64_t *bed, const int64_t *year,
                                            const int64_t *family, uint32_t n, uint32_t i)
{
    int64_t c = 0;
    if (!bed || !year || !family || i >= n) return DIBBER_REFUSED;
    for (uint32_t j = 0; j < n; j++)
        c += bed[j] == bed[i] && family[j] == family[i] && year[j] != year[i];
    return c;
}

/* DR7 */
static inline bool dibber_plot_record_ok(int64_t plot, int64_t bed, int64_t area)
{
    return plot >= -DIBBER_PLOT_MAX && plot <= DIBBER_PLOT_MAX && bed >= -DIBBER_BED_MAX &&
           bed <= DIBBER_BED_MAX && area >= -DIBBER_AREA_DIG && area <= DIBBER_AREA_DIG;
}

/* DR8: one reading's word, 0 .. 100 percent */
static inline lpres_state_t dibber_soil_word(int64_t pct)
{
    return pct >= DIBBER_SOW_MOIST ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* DR8: the readings of one bed on one day joined; NEITHER for none */
static inline lpres_state_t dibber_soil_state(const int64_t *pct, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; pct && i < n; i++) s = lpres_kjoin(s, dibber_soil_word(pct[i]));
    return s;
}

#endif
