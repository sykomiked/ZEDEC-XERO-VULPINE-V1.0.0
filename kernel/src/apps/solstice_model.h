/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* solstice_model.h — Solstice, the calendar, as a model (zxm.h): events as
 * files in the Hoard, one file each, read with files.list and files.read,
 * written with files.write and put in the Trash with files.trash; when each
 * repeating event next falls, which events overlap, a month's grid and the
 * notices that are due.
 *
 * Times are seconds since 1970 in UTC: zxvd has no time-zone method, and
 * none is invented, so the view adds the owner's offset itself. zxvd has
 * no notification method either, so the model only composes the notice
 * text (solstice_t.notice) for the view to show. No drawing, no
 * allocation, integers only. The rules are solstice_rules.h (SO1-SO5).
 *
 *   SM1 FOLDER. The events are the files SOLSTICE_DIR/<n>.event, n 1 ..
 *       SOLSTICE_ID_MAX in plain decimal digits with no leading zero.
 *       Other names in the folder are not Solstice's and are left alone.
 *   SM2 FILE. "solstice 1\n", then "<start> <finish> <every> <alarm>\n",
 *       then "<title>\n": start and finish in seconds, every in days (0:
 *       once), alarm in minutes before the start; numbers in plain decimal
 *       digits. An event file that does not say this is not read as an
 *       event: it is counted in `skipped` and never overwritten.
 *   SM3 VALID. 0 <= start < finish <= SOLSTICE_SECONDS_MAX, every 0 ..
 *       SOLSTICE_DAY_MAX and, when it repeats, no longer than its repeat
 *       (finish - start <= every days), alarm 0 .. SOLSTICE_ALARM_MAX, and
 *       a title of 1 .. SOLSTICE_TITLE_MAX bytes, none of them a control
 *       byte. The fields fit their ^ZXEVENT record (SO5).
 *   SM4 NEW NUMBERS. A new event takes one past the largest number in the
 *       folder, its file written with exclusive set, so no event is ever
 *       written over by another.
 *   SM5 CHECKED FIRST. Every change is checked before anything is asked,
 *       and kept only after zxvd has written it: a refused write leaves
 *       the model as it was. Nothing is written before a load succeeds. */
#ifndef SOLSTICE_MODEL_H
#define SOLSTICE_MODEL_H

#include "solstice_rules.h"
#include "zxm.h"

#define SOLSTICE_DIR        "Solstice" /* in the Hoard's home */
#define SOLSTICE_EVENTS     256u
#define SOLSTICE_ID_MAX     999999999 /* nine digits (^ZXEVENT) */
#define SOLSTICE_TITLE_MAX  120u
#define SOLSTICE_ALARM_MAX  9999 /* minutes, four digits (^ZXEVENT) */
#define SOLSTICE_TEXT_MAX   512u /* one event file */
#define SOLSTICE_NOTICE_MAX 1023u
#define SOLSTICE_LIST_MAX   1024u    /* names in the folder one load reads */
#define SOLSTICE_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int64_t id;
    int64_t start;  /* seconds since 1970, UTC */
    int64_t finish; /* not included */
    int64_t every;  /* days between repeats; 0: once */
    int64_t alarm;  /* minutes before the start its notice is due */
    char title[SOLSTICE_TITLE_MAX + 1];
} solstice_event_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: events may be written (SM5) */
    bool folder; /* SOLSTICE_DIR exists */
    solstice_event_t e[SOLSTICE_EVENTS];
    uint32_t n;
    uint32_t skipped; /* event files that are not events (SM2) */
    int64_t last_id;  /* the largest number in the folder (SM4) */
    char notice[SOLSTICE_NOTICE_MAX + 1];
} solstice_t;

/* A month as a calendar page draws it. */
typedef struct {
    int64_t first_weekday; /* of the 1st, 1 Monday .. 7 Sunday */
    int64_t days;          /* 28 .. 31 */
    int64_t first_day;     /* the 1st in days since 1970 */
} solstice_month_t;

/* SM3: may an event say this? */
bool solstice_event_valid(int64_t start, int64_t finish, int64_t every, int64_t alarm);
bool solstice_title_ok(const char *title);

/* The start of e's next occurrence still running or to come at `now`
 * (its finish after now): ZXM_E_STATE when there is none (a single event
 * already over, or one past year 9999). */
int solstice_next_start(const solstice_event_t *e, int64_t now, int64_t *at);
/* Do a's and b's next occurrences at `now` overlap (SO4)? false when
 * either has none. */
int solstice_clash(const solstice_event_t *a, const solstice_event_t *b, int64_t now, bool *clash);
/* Year 1970 .. 9999 and month 1 .. 12 (SO1, SO2). */
int solstice_month(int64_t year, int64_t month, solstice_month_t *m);

/* The events. */
void solstice_init(solstice_t *s, zxm_link_t *link);
/* Read every event in SOLSTICE_DIR; no folder yet is no events (ZXM_OK). */
int solstice_load(solstice_t *s);
/* A new event; its number in *id. */
int solstice_add(solstice_t *s, int64_t start, int64_t finish, int64_t every, int64_t alarm,
                 const char *title, int64_t *id);
/* Put event `index` in the Trash. */
int solstice_remove(solstice_t *s, uint32_t index);

/* How many other events overlap event `index`'s next occurrence. */
int solstice_clashes(const solstice_t *s, uint32_t index, int64_t now, uint32_t *count);
/* The events with an occurrence starting on `day` (days since 1970), in
 * the order loaded: their indexes in idx (up to cap), their number in *n. */
int solstice_on_day(const solstice_t *s, int64_t day, uint32_t *idx, uint32_t cap, uint32_t *n);
/* The events whose notice is due at `now`: from its next start minus its
 * alarm up to that start itself, so an alarm of 0 is due at the start
 * second. Their number in *n and the notice text in s->notice, one line
 * each (a line that does not fit is counted, not written). */
int solstice_due(solstice_t *s, int64_t now, uint32_t *n);

#endif
