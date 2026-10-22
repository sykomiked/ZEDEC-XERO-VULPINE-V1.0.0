/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stillwater_model.h — Stillwater, focus and do not disturb, as a model
 * (zxm.h): a quiet schedule, the notices zxvd keeps (notes.list) with each
 * one held or let through, and the app limits Shepherd sets (screen.status,
 * screen.check). The rules are stillwater_rules.h (SW1-SW4), the same ones
 * the app's Sutra, KRON, Eve and MYRA blocks run. No drawing, no allocation,
 * integers only.
 *
 *   ST1 THE SCHEDULE is held here: zxvd has no method that keeps it, and
 *       no notice-filter method (the bus's do-not-disturb, zx_notify.h N6,
 *       is not on RPC). A schedule is set only when it fits both SW3 and
 *       SW4; otherwise nothing changes.
 *   ST2 QUIET NOW: off is never quiet, focus always is, quiet hours are
 *       the window (SW1) on a day the mask chooses. The part of a window
 *       after midnight belongs to the day it began.
 *   ST3 NOTICES: notes.list gives the newest notes with no time, so the
 *       model takes the time a refresh is given as when it saw each call.
 *       A caller is the source of a call note (call-incoming,
 *       call-missed); each new call (a new note, or its count going up)
 *       is timed against that caller's last one, to the refresh's
 *       accuracy. Two calls first seen in one refresh count as within the
 *       gap since the refresh before, when there was one.
 *   ST4 A WATCHFIRE EMERGENCY is an urgent note whose source is
 *       "watchfire". It is never held (SW2).
 *   ST5 A refresh checks every value before it changes anything: a reply
 *       out of bounds leaves what was shown as it was.
 *   ST6 Advisory and owner-confirmed: notes.mark_read changes the notices
 *       zxvd keeps, so it is sent only on the owner's confirmed word
 *       (ZXM_E_STATE without it, nothing sent, nothing marked here). */
#ifndef STILLWATER_MODEL_H
#define STILLWATER_MODEL_H

#include "stillwater_rules.h"
#include "zxm.h"

#define STILLWATER_NOTES      20u /* notes.list answers the newest 20 */
#define STILLWATER_SOURCE     23u /* zx_notify.h ZXN_SOURCE_MAX - 1 */
#define STILLWATER_TITLE      63u /* ZXN_TITLE_MAX - 1 */
#define STILLWATER_KIND       15u
#define STILLWATER_ALLOWED    16u /* people on the allow list */
#define STILLWATER_CALLERS    16u /* callers whose last call is kept */
#define STILLWATER_APP        31u
#define STILLWATER_APPS       64u
#define STILLWATER_WHY        160u
#define STILLWATER_PRI_URGENT 3u /* zx_notify.h ZXN_PRI_URGENT */
#define STILLWATER_EMERGENCY  "watchfire"

typedef struct {
    uint8_t mode, days; /* SW3 */
    uint16_t from, to;
    char allowed[STILLWATER_ALLOWED][STILLWATER_SOURCE + 1];
    uint32_t nallowed;
} stillwater_schedule_t;

typedef struct {
    uint32_t seq, count;
    uint8_t pri;
    bool read, call;
    char kind[STILLWATER_KIND + 1];
    char source[STILLWATER_SOURCE + 1];
    char title[STILLWATER_TITLE + 1];
    int64_t gap_s; /* seconds since this caller's call before; -1: none */
    bool allowed, emergency, through;
} stillwater_note_t;

typedef struct {
    char source[STILLWATER_SOURCE + 1];
    int64_t at_s; /* when its last call was seen */
} stillwater_caller_t;

typedef struct {
    char app[STILLWATER_APP + 1];
    int32_t minutes; /* a day; -1: no cap */
    uint16_t from, to;
} stillwater_limit_t;

typedef struct {
    char app[STILLWATER_APP + 1];
    bool allowed;
    uint32_t used;
    int32_t left; /* -1: no limit */
    char why[STILLWATER_WHY + 1];
} stillwater_check_t;

typedef struct {
    zxm_link_t *link;
    stillwater_schedule_t sched;
    bool quiet, seen; /* seen: a refresh has run, at_s is its time */
    int64_t at_s;
    stillwater_note_t note[STILLWATER_NOTES];
    uint32_t nnote, unread, held;
    stillwater_caller_t caller[STILLWATER_CALLERS];
    uint32_t ncaller;
    bool locked;
    stillwater_limit_t lim[STILLWATER_APPS];
    uint32_t nlim;
    stillwater_check_t check;
} stillwater_t;

void stillwater_init(stillwater_t *s, zxm_link_t *link);
/* ST1: mode STILLWATER_OFF, _QUIET or _FOCUS; days bit 0 Monday. */
int stillwater_set_schedule(stillwater_t *s, uint8_t mode, uint8_t days, uint16_t from,
                            uint16_t to);
int stillwater_allow(stillwater_t *s, const char *source);
int stillwater_disallow(stillwater_t *s, const char *source);
bool stillwater_allowed(const stillwater_t *s, const char *source);
/* ST2: weekday 0 Monday .. 6 Sunday, minute 0..1439; -1 when out of range. */
int stillwater_quiet_at(const stillwater_schedule_t *sc, uint32_t weekday, uint32_t minute);
/* notes.list and screen.status at the person's local time: now_s seconds
 * (never going back), weekday and minute as above. */
int stillwater_refresh(stillwater_t *s, int64_t now_s, uint32_t weekday, uint32_t minute);
/* screen.check for one app, into s->check. */
int stillwater_check_app(stillwater_t *s, const char *app);
/* notes.mark_read on the owner's word (ST6); *marked is how many zxvd marked. */
int stillwater_mark_read(stillwater_t *s, uint32_t *marked, bool owner_confirmed);
const stillwater_note_t *stillwater_note(const stillwater_t *s, uint32_t seq);

#endif
