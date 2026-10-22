/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dibber_model.h — Dibber, the garden planner, as a model (zxm.h): a
 * plot's beds, what is planted in each year by year, the soil readings the
 * owner takes and the waterings they log, planned against the week's water
 * budget and the weather, kept in the Hoard for where the plot is. No
 * drawing, no allocation, integers only; the rules it runs are
 * dibber_rules.h (DR1-DR8) and its parts' (Furrow, Solstice, Weatherglass,
 * Understory).
 *
 * zxvd methods used: files.read, files.write and place.here. zxvd has no
 * weather source, no soil sensor and no notification method: a reading of
 * the weather and of the soil is handed in (the owner's, or Weatherglass's
 * from its approved bridge), and a notice is composed as text for the
 * view.
 *
 *   DM1 THE PLOT is a number (1 .. 999999) and its beds, each a number (1 ..
 *       9999) and an area in square decimetres (1 .. 999999), at most
 *       DIBBER_BEDS, each fitting ^ZXDIBBER (DR7); a bed is kept once.
 *   DM2 PLANTINGS: a crop family (1 .. 9999) on a bed in a year (1 ..
 *       9999), at most DIBBER_CROPS. A planting that puts a family on a
 *       bed it held in another year of the plan is refused (DR6: no repeat
 *       on the same bed); one bed holds one family a year.
 *   DM3 SOIL: a reading of a bed's moisture on a day (days since 1970, 0 ..
 *       SOLSTICE_DAY_MAX), 0 .. 100 percent, at most DIBBER_SOILS. The
 *       readings of a bed on a day are joined four-valued (DR8).
 *   DM4 WATER: the week's budget in centilitres (0 .. DIBBER_BUDGET_MAX)
 *       and the waterings the owner logs (a bed, a day, centilitres). The
 *       week of a day is it and the six before (understory.rolling_total,
 *       width 7). dibber_may_water says whether a watering of a depth may
 *       go now: within its limits (DR3), and within the budget and with no
 *       frost (DR2; understory.over_budget names an overrun, and the frost
 *       flag is weatherglass.warn_flags' of a reading that fits
 *       weatherglass.forecast_ok). Advice only: Dibber never controls a
 *       pump or a valve; the owner waters and logs it.
 *   DM5 THE BED SHEET: each bed's week of water at a depth (DR1) in
 *       centilitres, a column summed over a range of beds
 *       (furrow.range_total); the supply list's costs (DR4) summed the
 *       same way.
 *   DM6 THE SUGGESTION for a bed on a day: soil BOTH is held (both
 *       readings named) and a re-test is suggested; no reading, test the
 *       soil; too dry, water first; a frost warning, wait (the temperature
 *       shown in C and F, weatherglass.temp_f_tenths); else sow.
 *   DM7 DATES: the next day of a repeating task (solstice.next_occurrence)
 *       and the days to harvest from a run of daily means (DR5).
 *   DM8 THE FILE. DIBBER_FILE in the Hoard holds the plot, beds,
 *       plantings, soil readings, waterings and budget, one line each,
 *       written whole (files.write) only on the owner's word and read back
 *       with files.read; a file that does not parse leaves what is open as
 *       it was. Where the plot is comes from place.here. */
#ifndef DIBBER_MODEL_H
#define DIBBER_MODEL_H

#include "dibber_rules.h"
#include "weatherglass_model.h"
#include "zxm.h"

#define DIBBER_FILE       "Dibber.plan"
#define DIBBER_BEDS       64u
#define DIBBER_CROPS      256u
#define DIBBER_SOILS      128u
#define DIBBER_WATERINGS  256u
#define DIBBER_SUPPLIES   64u
#define DIBBER_TEXT_MAX   16384u
#define DIBBER_NOTICE_MAX 256u
#define DIBBER_FAMILY_MAX INT64_C(9999)
#define DIBBER_YEAR_MAX   INT64_C(9999)
#define DIBBER_BUDGET_MAX INT64_C(999999999999) /* centilitres: 10^10 l */

/* DM6 */
enum {
    DIBBER_SOW = 1,         /* moist enough and no frost: sow */
    DIBBER_WAIT_FROST = 2,  /* moist enough, frost warned: wait */
    DIBBER_WATER_FIRST = 3, /* too dry to sow */
    DIBBER_TEST_SOIL = 4,   /* no reading that day */
    DIBBER_HELD = 5         /* the readings disagree: held, re-test */
};

/* DM4 */
enum {
    DIBBER_MAY_WATER = 1,   /* within the budget, no frost */
    DIBBER_OVER_BUDGET = 2, /* the watering would pass the week's budget */
    DIBBER_FROST = 3,       /* a frost warning */
    DIBBER_PAST_LIMITS = 4  /* the depth or the waterings past their limits */
};

typedef struct {
    int64_t bed, area; /* dm2 */
} dibber_bed_t;

typedef struct {
    int64_t bed, year, family;
} dibber_crop_t;

typedef struct {
    int64_t bed, day, pct;
} dibber_soil_t;

typedef struct {
    int64_t bed, day, cl; /* centilitres */
} dibber_watering_t;

/* a weather reading as Weatherglass keeps it (WG4) */
typedef struct {
    int64_t stamp, temp, wind, rain;
} dibber_reading_t;

typedef struct {
    zxm_link_t *link;
    int64_t plot;
    dibber_bed_t bed[DIBBER_BEDS];
    uint32_t nbeds;
    dibber_crop_t crop[DIBBER_CROPS];
    uint32_t ncrops;
    dibber_soil_t soil[DIBBER_SOILS];
    uint32_t nsoil;
    dibber_watering_t water[DIBBER_WATERINGS];
    uint32_t nwater;
    int64_t budget; /* centilitres a week */
    bool here_known;
    int64_t here_lat, here_lon;
    bool dirty; /* changed since saved or loaded */
} dibber_t;

/* DM1: ZXM_E_ARG for a plot outside 1 .. 999999 (then plot 0, nothing kept) */
int dibber_init(dibber_t *d, zxm_link_t *link, int64_t plot);
int dibber_add_bed(dibber_t *d, int64_t bed, int64_t area);
const dibber_bed_t *dibber_find_bed(const dibber_t *d, int64_t bed);
/* DM2 */
int dibber_plant(dibber_t *d, int64_t bed, int64_t year, int64_t family);
/* DM3 */
int dibber_soil(dibber_t *d, int64_t bed, int64_t day, int64_t pct);
lpres_state_t dibber_soil_on(const dibber_t *d, int64_t bed, int64_t day);
/* DM4 */
int dibber_set_budget(dibber_t *d, int64_t centilitres);
int dibber_log_water(dibber_t *d, int64_t bed, int64_t day, int64_t centilitres);
/* the week's water up to day (it and the six before), centilitres, or
 * DIBBER_REFUSED */
int64_t dibber_week_used(const dibber_t *d, int64_t day);
/* DIBBER_MAY_WATER .. DIBBER_PAST_LIMITS, or a ZXM_E_* code; depth mm in
 * one watering, times the waterings planned this week */
int dibber_may_water(const dibber_t *d, int64_t bed, int64_t day, int64_t depth, int64_t times,
                     const dibber_reading_t *r);
/* DM5: a bed's week at depth mm, centilitres, or DIBBER_REFUSED */
int64_t dibber_bed_week_cl(const dibber_t *d, int64_t bed, int64_t depth);
int dibber_sheet_total(const dibber_t *d, int64_t lo, int64_t hi, int64_t depth, int64_t *total);
int dibber_supply_total(const int64_t *count, const int64_t *price, uint32_t n, int64_t *total);
/* DM6: DIBBER_SOW .. DIBBER_HELD with its notice, or a ZXM_E_* code */
int dibber_suggest(const dibber_t *d, int64_t bed, int64_t day, const dibber_reading_t *r,
                   char *notice, uint32_t cap);
/* DM7 */
int64_t dibber_next_task(int64_t start, int64_t every, int64_t today);
int64_t dibber_harvest(const int64_t *mean, uint32_t n, int64_t base, int64_t need);
/* DM8 */
int dibber_save(dibber_t *d, bool owner_confirmed);
int dibber_load(dibber_t *d);
int dibber_here(dibber_t *d);

#endif
