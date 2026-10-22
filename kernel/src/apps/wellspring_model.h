/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wellspring_model.h — Wellspring, a private health and fitness log, as a
 * model (zxm.h): activity minutes, sleep minutes, water ml and resting
 * heart rate by day, weekly totals, progress toward weekly goals, and
 * whether a day's readings are inside their safe ranges.
 *
 * ADVICE ONLY. Wellspring never diagnoses anything. A reading outside its
 * safe range is shown with a note to talk to a clinician, and the ranges
 * are general defaults, not a judgement about any one person.
 *
 * The log is one UTF-8 text file in the Banker, WELLSPRING_LOG, read with
 * files.read and written whole with files.write. zxvd has no health
 * method, and none is used; zxvd has no notification method either, so
 * the model only composes the notice text (wellspring_t.notice) for the
 * view to show. No drawing, no allocation, integers only.
 *
 *   WL1 The file's first line is "wellspring 1".
 *   WL2 Then up to one "goal <kind> <weekly amount>" line for each kind
 *       that has a goal (activity, sleep, water).
 *   WL3 Then one "<day> <kind> <amount>" line per entry, in the order
 *       added: day is days since 1970-01-01, kind is WELLSPRING_ACTIVITY
 *       .. WELLSPRING_PULSE, amount is in the kind's unit.
 *   WL4 Every line ends with "\n", numbers are plain decimal digits.
 *       Anything else is not a log: the load is refused (ZXM_E_PROTOCOL)
 *       and every change is refused (ZXM_E_STATE) until a load succeeds,
 *       so a file the model cannot read is never overwritten.
 *   WL5 Changes are checked, written whole, and only then kept: a write
 *       zxvd refuses leaves the model as it was. */
#ifndef WELLSPRING_MODEL_H
#define WELLSPRING_MODEL_H

#include "zxm.h"

#define WELLSPRING_LOG        "Wellspring.log" /* in the Banker's home */
#define WELLSPRING_ENTRIES    1024u
#define WELLSPRING_DAY_MAX    999999    /* six digits: days since 1970 */
#define WELLSPRING_AMOUNT_MAX 999999999 /* nine digits, any kind */
#define WELLSPRING_TEXT_MAX   24576u    /* the whole log, every line full */
#define WELLSPRING_NOTICE_MAX 511u
#define WELLSPRING_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

enum {
    WELLSPRING_ACTIVITY = 1, /* minutes of activity */
    WELLSPRING_SLEEP = 2,    /* minutes of sleep */
    WELLSPRING_WATER = 3,    /* ml of water */
    WELLSPRING_PULSE = 4,    /* resting heart rate, beats a minute */
};
#define WELLSPRING_KINDS 4u

/* The safe ranges, both ends inside: defaults taken from general adult
 * guidance (a resting heart rate of 60 to 100 widened down to 40 for the
 * well trained; 7 to 9 hours of sleep; 1.5 to 4 litres of drinking water,
 * around the 2.7 and 3.7 litre total-water intakes), never a diagnosis.
 * blocks/wellspring/safe.eve and safe.kron say the same. */
#define WELLSPRING_PULSE_LO 40
#define WELLSPRING_PULSE_HI 100
#define WELLSPRING_SLEEP_LO 420
#define WELLSPRING_SLEEP_HI 540
#define WELLSPRING_WATER_LO 1500
#define WELLSPRING_WATER_HI 4000

/* Weekly goals until the owner sets their own: 150 minutes of activity,
 * 7 nights of 8 hours, 7 days of 2 litres. */
#define WELLSPRING_GOAL_ACTIVITY 150
#define WELLSPRING_GOAL_SLEEP    3360
#define WELLSPRING_GOAL_WATER    14000

#define WELLSPRING_NO_RANGE (-2) /* wellspring_band: lo above hi */

typedef struct {
    int32_t day;
    int32_t kind;
    int64_t amount;
} wellspring_entry_t;

/* One day's readings (wellspring_day). value[k] is the day's total of
 * activity, sleep or water, or its last heart rate; has[k] says whether
 * anything of kind k was logged that day; band[k] is wellspring_band of
 * value[k] against kind k's safe range (0 when not logged, and always 0
 * for activity, which has no safe range). */
typedef struct {
    int32_t day;
    bool has[WELLSPRING_KINDS + 1];
    int64_t value[WELLSPRING_KINDS + 1];
    int32_t band[WELLSPRING_KINDS + 1];
    uint32_t outside; /* logged readings outside their range */
    bool safe;        /* heart rate, sleep and water all logged and inside */
} wellspring_day_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;                        /* a load succeeded: changes may be written (WL4) */
    int64_t goal[WELLSPRING_KINDS + 1]; /* weekly; 0 = none; [0] and pulse unused */
    wellspring_entry_t e[WELLSPRING_ENTRIES];
    uint32_t n;
    char notice[WELLSPRING_NOTICE_MAX + 1]; /* advice for the view (wellspring_day) */
} wellspring_t;

/* The rules, as the app's logic says them (wellspring.zxapp). */

/* -1 below lo, 0 inside lo..hi, 1 above hi; WELLSPRING_NO_RANGE when lo > hi. */
int32_t wellspring_band(int64_t reading, int64_t lo, int64_t hi);
/* A day's heart rate, sleep and water all inside their safe ranges. */
bool wellspring_safe(int64_t bpm, int64_t sleep, int64_t water);
/* The exact sum of n amounts, added from the first; -1 when amount is
 * NULL, an amount is negative or a partial sum passes INT64_MAX. */
int64_t wellspring_week_total(const int64_t *amount, uint32_t n);
/* floor(10000 * done / goal); -1 when goal <= 0, done < 0 or 10000 * done
 * passes INT64_MAX. */
int64_t wellspring_progress_x100(int64_t done, int64_t goal);
/* A log entry fits its record: day 0..WELLSPRING_DAY_MAX, a known kind,
 * amount 0..WELLSPRING_AMOUNT_MAX. */
bool wellspring_log_ok(int64_t day, int64_t kind, int64_t amount);

/* The log. */
void wellspring_init(wellspring_t *w, zxm_link_t *link);
/* Read WELLSPRING_LOG; a log not made yet is an empty log (ZXM_OK). */
int wellspring_load(wellspring_t *w);
int wellspring_add(wellspring_t *w, int64_t day, int64_t kind, int64_t amount);
int wellspring_remove(wellspring_t *w, uint32_t index);
/* A weekly goal for activity, sleep or water: 0 clears it. */
int wellspring_set_goal(wellspring_t *w, int64_t kind, int64_t weekly);

/* The week of days first .. first + 6: kind's exact total (not for the
 * heart rate, which is a reading, not an amount: ZXM_E_ARG). */
int wellspring_week(const wellspring_t *w, int64_t first, int64_t kind, int64_t *total);
/* That total toward kind's weekly goal, x100 (ZXM_E_STATE with no goal). */
int wellspring_progress(const wellspring_t *w, int64_t first, int64_t kind, int64_t *x100);
/* One day's readings against their safe ranges; writes the advice to
 * w->notice. */
int wellspring_day(wellspring_t *w, int64_t day, wellspring_day_t *d);

#endif
