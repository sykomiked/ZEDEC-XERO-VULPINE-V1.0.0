/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_sched.h — timers apps register with zxvd, behind Treadle
 * (schedule.*).
 *
 *   T1 WHAT. A schedule is a time and a name an app registers. zxvd runs
 *      nothing when it comes due: it records a firing that the app reads
 *      (schedule.fired) and, if asked, posts a notice in the app's name.
 *      What the app then does is its own (Treadle asks the owner before
 *      running a task), so a schedule cannot make zxvd call a method.
 *   T2 WHEN. One of:
 *        at     once, at a time (ms since 1970, after now); the schedule
 *               ends when it fires;
 *        every  every `every_s` seconds (ZSD_EVERY_MIN to ZSD_EVERY_MAX),
 *               counted from `start_ms` (default: now);
 *        daily  at a minute of the local day (0-1439, "HH:MM") on the days
 *               of the week in a mask (bit 0 Sunday to bit 6 Saturday).
 *               Local time is the machine's: where a clock change skips
 *               the minute, it fires at the same distance past the change
 *               (as mktime places it); where a minute comes twice, once.
 *   T3 FIRING. zsd_tick fires every schedule that is due: one record in
 *      the ring of the last ZSD_FIRED firings, numbered from 1 and never
 *      reused, even across runs. A firing more than ZSD_LATE_MS after it
 *      was due is "late" (zxvd was not running, or the machine slept); an
 *      `every` schedule that missed several fires once, counting the
 *      missed ones, and goes on from the next time after now.
 *   T4 LIMITS. ZSD_ENTRIES schedules, at most ZSD_PER_APP for one app.
 *      Names are 1 to ZSD_NAME_MAX printable bytes; notes up to
 *      ZSD_NOTE_MAX, no control characters.
 *   T5 THE FILE. Schedules, the firings and the next firing number are one
 *      text file, 0600, rewritten whole and swapped in place, ending in the
 *      SHA-256 of what is above it (as zxv_caps.h K4), written on every
 *      change and every firing. A damaged file is moved aside as
 *      "<file>.damaged" and the schedules start empty (zsd_was_reset).
 *
 * zxvd uses one thread (zxvd_log.h L3); so does this. */
#ifndef ZXV_SCHED_H
#define ZXV_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#define ZSD_APP_MAX   31u
#define ZSD_NAME_MAX  63u
#define ZSD_NOTE_MAX  191u
#define ZSD_ENTRIES   128u
#define ZSD_PER_APP   32u
#define ZSD_FIRED     256u
#define ZSD_EVERY_MIN 60u
#define ZSD_EVERY_MAX (366u * 86400u)
#define ZSD_LATE_MS   60000
#define ZSD_TIME_MAX  INT64_C(253402300799999) /* the last ms of 9999 */
#define ZSD_PATH_MAX  900u

enum {
    ZSD_OK = 0,
    ZSD_E_ARG = -1,     /* a bad app, name, note or time */
    ZSD_E_FULL = -2,    /* no room, or the app has ZSD_PER_APP */
    ZSD_E_IO = -3,      /* the file could not be written */
    ZSD_E_NOTFOUND = -4 /* no such schedule */
};

typedef enum { ZSD_AT, ZSD_EVERY, ZSD_DAILY } zsd_kind_t;

typedef struct {
    uint32_t id; /* given by zsd_add */
    char app[ZSD_APP_MAX + 1];
    char name[ZSD_NAME_MAX + 1];
    char note[ZSD_NOTE_MAX + 1];
    uint8_t kind;     /* zsd_kind_t */
    int64_t at_ms;    /* at: when; every: the start */
    uint32_t every_s; /* every */
    uint16_t minute;  /* daily: of the local day */
    uint8_t days;     /* daily: bit 0 Sunday .. bit 6 Saturday */
    bool notify;      /* post a notice when it fires */
    int64_t next_ms;  /* when it fires next */
    int64_t last_ms;  /* when it last fired; 0 never */
    uint32_t fired;   /* how many times */
} zsd_entry_t;

typedef struct {
    uint64_t seq;
    uint32_t id;
    char app[ZSD_APP_MAX + 1];
    char name[ZSD_NAME_MAX + 1];
    int64_t due_ms, fired_ms;
    uint32_t missed; /* every: the times skipped before this one */
    bool late;
    bool ended; /* at: the schedule is gone */
} zsd_fired_t;

/* Load the file; NULL or "" keeps everything in memory. 0, or ZSD_E_ARG /
 * ZSD_E_IO; a damaged file is moved aside and 0 returned. */
int zsd_open(const char *path);
void zsd_close(void);
bool zsd_was_reset(void);
void zsd_set_clock(int64_t (*now_ms)(void)); /* tests; NULL: the wall clock */
/* T1: called for each firing, after it is recorded (e is the schedule as
 * it fired: an `at` one is gone once this returns). */
void zsd_set_hook(void (*on_fire)(void *ctx, const zsd_fired_t *f, const zsd_entry_t *e),
                  void *ctx);

/* T2, T4: e's app, name, note, kind and its time fields; *id and e's
 * next_ms set. ZSD_E_ARG for a time already past or out of range. */
int zsd_add(zsd_entry_t *e, uint32_t *id);
int zsd_cancel(uint32_t id);
/* `app` NULL or "" for all; up to cap rows, oldest first; the count. */
uint32_t zsd_list(const char *app, zsd_entry_t *out, uint32_t cap);
/* T3: the firings numbered after `after` (for `app`, or all), oldest
 * first, up to cap; *next is the number to pass next time. The count. */
uint32_t zsd_fired(const char *app, uint64_t after, zsd_fired_t *out, uint32_t cap, uint64_t *next);
/* T3: fire what is due; the number fired. */
uint32_t zsd_tick(void);

/* "HH:MM" to a minute of the day; -1 when not one. */
int zsd_minute_parse(const char *s);
const char *zsd_kind_name(zsd_kind_t k);
const char *zsd_strerror(int code);

#endif
