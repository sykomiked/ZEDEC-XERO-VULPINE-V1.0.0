/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sundial_model.h — Sundial, the clock, as a model (zxm.h): the time in the
 * owner's zone and in world clocks, alarms on days of the week with a
 * snooze, and one countdown timer. No drawing, no allocation, integers
 * only. The rules are sundial_rules.h (SD1-SD4); the day of the week is
 * Solstice's (solstice_rules.h SO2) and a timer's time left is shown as
 * Halloo shows a length (halloo_rules.h HR4).
 *
 * The time is zxvd's wall clock, sys.info's time_ms. zxvd has no alarm,
 * timer, time-zone or notice-post method, and none is invented: alarms and
 * the timer are kept here, a zone is one of SD4's standard offsets (no
 * daylight saving), and what is due is composed as text (sundial_t.notice)
 * for the view to show; nothing is posted.
 *
 *   SM1 TIME. sundial_refresh reads time_ms; nothing else asks zxvd. A
 *       time before 1970 or past year 9999 is refused, and a failed read
 *       keeps the last time. Everything below works on that time
 *       (sundial_t.now_ms), so a view refreshes, then asks.
 *   SM2 LOCAL. A zone's local time is the UTC time plus its offset (SD4):
 *       the minute of the day by SD1, the date and weekday of the local day
 *       (solstice_date_of, solstice_weekday).
 *   SM3 ALARMS. Up to SUNDIAL_ALARMS_MAX, each at a minute of the owner's
 *       local day on a mask of weekdays (bit 0 Monday), with a label of
 *       printable bytes. Every change is checked against SD3 before it is
 *       made. An alarm rings in the minute its wait (SD2) is 0.
 *   SM4 SNOOZE. Snoozing puts one ring off by the snooze (SD3, 1..60
 *       minutes, 10 to begin with) from now.
 *   SM5 TIMER. One timer of 1..SUNDIAL_TIMER_MAX minutes counting down
 *       from when it starts; its time left is never below zero, and a
 *       timer at zero is done. */
#ifndef SUNDIAL_MODEL_H
#define SUNDIAL_MODEL_H

#include "halloo_rules.h"
#include "solstice_rules.h"
#include "sundial_rules.h"
#include "zxm.h"

#define SUNDIAL_WORLD        8u  /* world clocks */
#define SUNDIAL_LABEL_MAX    32u /* an alarm's label, bytes */
#define SUNDIAL_NOTICE_MAX   1023u
#define SUNDIAL_SNOOZE_START 10
#define SUNDIAL_MS_MAX       (SOLSTICE_SECONDS_MAX * 1000) /* 9999-12-31 23:59:59.999 */
#define SUNDIAL_CLOCK_TEXT   48u                           /* room for sundial_clock_text */

typedef struct {
    int64_t at;   /* minute of the local day, 0..1439 */
    int64_t mask; /* days, 1..127, bit 0 Monday .. bit 6 Sunday */
    bool on;
    char label[SUNDIAL_LABEL_MAX + 1];
} sundial_alarm_t;

/* One zone's local time (SM2). */
typedef struct {
    int64_t offset;  /* minutes from UTC */
    int64_t minute;  /* of the day, 0..1439 */
    int64_t weekday; /* 1 Monday .. 7 Sunday */
    int64_t year, month, day;
} sundial_local_t;

typedef struct {
    zxm_link_t *link;
    bool have;      /* a refresh has succeeded (SM1) */
    int64_t now_ms; /* zxvd's wall clock, ms since 1970, UTC */
    int64_t zone;   /* the owner's zone, an SD4 index */
    int64_t world[SUNDIAL_WORLD];
    uint32_t nworld;
    sundial_alarm_t alarm[SUNDIAL_ALARMS_MAX];
    uint32_t nalarm;
    int64_t snooze;     /* minutes */
    int64_t snoozed_ms; /* a snoozed ring is due at this time; 0 none */
    int64_t timer_min;  /* the timer's length in minutes; 0 none */
    int64_t timer_start_ms;
    char notice[SUNDIAL_NOTICE_MAX + 1];
} sundial_t;

void sundial_init(sundial_t *s, zxm_link_t *link);
/* SM1: read zxvd's time. */
int sundial_refresh(sundial_t *s);

/* SM2: zone `zone`'s local time now; ZXM_E_STATE before a refresh. */
int sundial_local(const sundial_t *s, int64_t zone, sundial_local_t *out);
int sundial_set_zone(sundial_t *s, int64_t zone);
/* "UTC+05:45 Tuesday 14:05" for zone `zone` now, into out (cap at least
 * SUNDIAL_CLOCK_TEXT). */
int sundial_clock_text(const sundial_t *s, int64_t zone, char *out, uint32_t cap);
/* World clocks: add a zone (not twice), remove one by its place. */
int sundial_world_add(sundial_t *s, int64_t zone);
int sundial_world_remove(sundial_t *s, uint32_t index);

/* SM3: a new alarm, its place in *index. */
int sundial_alarm_add(sundial_t *s, int64_t at, int64_t mask, const char *label, uint32_t *index);
int sundial_alarm_remove(sundial_t *s, uint32_t index);
int sundial_alarm_set_on(sundial_t *s, uint32_t index, bool on);
/* The alarm that rings first from now in the owner's zone: its place and
 * the minutes until it rings (SD2). ZXM_E_STATE when no alarm is on or the
 * time is not read yet. */
int sundial_next_alarm(const sundial_t *s, uint32_t *index, int64_t *wait);

/* SM4 */
int sundial_set_snooze(sundial_t *s, int64_t minutes);
int sundial_snooze_now(sundial_t *s);

/* SM5 */
int sundial_timer_start(sundial_t *s, int64_t minutes);
int sundial_timer_stop(sundial_t *s);
/* ms left, never below 0; -1 when no timer runs. */
int64_t sundial_timer_left_ms(const sundial_t *s);
/* The time left as h * 10000 + m * 100 + s (halloo_hms), -1 with no timer. */
int64_t sundial_timer_hms(const sundial_t *s);

/* What is due now: the alarms ringing this minute, a snoozed ring that is
 * due and a timer that is done. Their number in *n and one line each in
 * s->notice (a line that does not fit is counted, not written). A due
 * snooze and a done timer are cleared once told. */
int sundial_due(sundial_t *s, uint32_t *n);

#endif
