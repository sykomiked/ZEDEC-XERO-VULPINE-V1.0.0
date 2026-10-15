/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_screen.h — screen time behind Shepherd (screen.*): how long each app
 * is used each day, and the limits a guardian sets.
 *
 *   SC1 COUNTING. The desktop reports the app in front once a minute while
 *       someone is using the machine (screen.tick); each report counts one
 *       minute for that app and for the day, unless it comes less than
 *       ZSC_TICK_GAP seconds after the last one counted (a second window
 *       reporting the same minute). Days are local calendar days. The 31
 *       newest days are kept.
 *   SC2 LIMITS. A limit names an app, or "*" for all apps together: minutes
 *       a day (or none), and the hours it may be used (from..to, minutes of
 *       the day; equal means any time; from after to wraps past midnight).
 *       An app is allowed when its own limit and the "*" limit both allow it.
 *   SC3 GUARDIAN. Limits can be locked with a passphrase. While locked,
 *       changing a limit, granting time and unlocking need it. Five wrong
 *       passphrases in a row make the next tries wait ZSC_WAIT_S seconds.
 *       The passphrase is kept only as a salted, iterated SHA-256.
 *   SC4 MORE TIME. The guardian can grant extra minutes for today, to one
 *       app or to "*"; they lapse at midnight.
 *   SC5 ANSWERS. A check says whether the app may be used now, how many
 *       minutes are left (-1: no limit) and, when not, why in plain words.
 *       Time used past a limit is still counted: the record stays true.
 *   SC6 THE FILE. One text file, 0600, rewritten whole and swapped in place,
 *       ending in the SHA-256 of what is above it. A damaged file is moved
 *       aside as "<file>.damaged" and screen time starts empty, with `reset`
 *       set so Shepherd tells the guardian.
 *   SC7 HOSTED. On Linux, macOS and Windows the person whose account runs
 *       zxvd can delete the file or stop the desktop's reports, so limits
 *       there are an agreement, not a wall; seats on ZXV's own OS enforce
 *       them later.
 *
 * zxvd uses one thread (zxvd_log.h L3); so does this. */
#ifndef ZXV_SCREEN_H
#define ZXV_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#define ZSC_APP_MAX  31u /* bytes of an app's name */
#define ZSC_APPS     64u /* apps counted in a day; limits */
#define ZSC_DAYS     31u
#define ZSC_PATH_MAX 900u
#define ZSC_PASS_MIN 4u
#define ZSC_PASS_MAX 128u
#define ZSC_TICK_GAP 45
#define ZSC_TRIES    5u
#define ZSC_WAIT_S   60
#define ZSC_ALL      "*"

enum {
    ZSC_E_ARG = -1,     /* a bad app name, minutes or hours */
    ZSC_E_PASS = -2,    /* locked, and the passphrase is wrong or missing */
    ZSC_E_WAIT = -3,    /* too many wrong passphrases: wait */
    ZSC_E_FULL = -4,    /* ZSC_APPS apps or limits already */
    ZSC_E_IO = -5,      /* the file could not be written */
    ZSC_E_NOTFOUND = -6 /* no such limit */
};

typedef struct {
    char app[ZSC_APP_MAX + 1];
    uint32_t minutes;
} zsc_use_t;

typedef struct {
    uint32_t date; /* YYYYMMDD, local */
    uint32_t total;
    uint32_t n;
    zsc_use_t a[ZSC_APPS];
} zsc_day_t;

typedef struct {
    char app[ZSC_APP_MAX + 1]; /* an app, or ZSC_ALL */
    int32_t minutes;           /* a day; -1: no daily cap */
    uint16_t from, to;         /* minutes of the day; equal: any time */
    uint32_t extra_date;       /* the day `extra` is for */
    uint32_t extra;            /* minutes granted for that day (SC4) */
} zsc_limit_t;

typedef struct {
    bool allowed;
    bool counted;   /* this tick counted a minute (SC1) */
    uint32_t used;  /* the app's minutes today */
    uint32_t total; /* all apps' minutes today */
    int32_t left;   /* minutes left today; -1: no limit */
    char why[96];   /* "" when allowed */
} zsc_answer_t;

/* Load the file at `path` (made when missing); NULL or "" keeps screen time
 * in memory only. 0, or ZSC_E_ARG / ZSC_E_IO. A damaged file is moved aside
 * (SC6) and 0 is returned with zsc_was_reset() true. */
int zsc_open(const char *path);
void zsc_close(void);
bool zsc_was_reset(void);
/* The clock (tests): seconds since 1970; NULL restores time(). */
void zsc_set_clock(int64_t (*now)(void));

int zsc_tick(const char *app, zsc_answer_t *a);
int zsc_check(const char *app, zsc_answer_t *a);

bool zsc_locked(void);
/* Set or change the passphrase (old: the current one, when locked); an empty
 * or NULL `pass` removes the lock. */
int zsc_lock(const char *old, const char *pass);
/* minutes -1: no daily cap. */
int zsc_set_limit(const char *pass, const char *app, int32_t minutes, uint16_t from, uint16_t to);
int zsc_clear_limit(const char *pass, const char *app);
int zsc_grant(const char *pass, const char *app, uint32_t minutes);
/* Seconds until a passphrase is tried again (SC3); 0 when it can be now. */
int64_t zsc_wait(void);

uint32_t zsc_limit_count(void);
const zsc_limit_t *zsc_limit(uint32_t i);
/* Days kept, newest first; today is there once something was counted. */
uint32_t zsc_day_count(void);
const zsc_day_t *zsc_day(uint32_t i);
/* Today's date, YYYYMMDD local. */
uint32_t zsc_today(void);
const char *zsc_strerror(int code);

#endif
