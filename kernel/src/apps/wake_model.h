/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wake_model.h — Wake, the event console, as a model (zxm.h): follows
 * zxvd's events (log.tail) into a ring, filters them by level and by text,
 * says when events were missed, and can ask AI what the warnings mean.
 * Read only. No drawing, no allocation. */
#ifndef WAKE_MODEL_H
#define WAKE_MODEL_H

#include "zxm.h"

#define WAKE_KEEP 512u /* events kept in the model */
#define WAKE_TEXT 160u
#define WAKE_FIND 63u /* bytes of the text filter */

enum { WAKE_INFO, WAKE_WARN, WAKE_ERROR };

typedef struct {
    uint64_t seq, time_ms;
    uint8_t level;
    char source[16];
    char text[WAKE_TEXT + 1];
} wake_event_t;

typedef struct {
    zxm_link_t *link;
    wake_event_t ev[WAKE_KEEP]; /* a ring, oldest first from `first` */
    uint32_t first, n;
    uint64_t next;     /* log.tail's since for the next read */
    uint64_t missed;   /* events zxvd dropped before Wake read them */
    bool paused;       /* follow does nothing while paused */
    uint8_t min_level; /* the view's filter */
    char find[WAKE_FIND + 1];
    zxm_held_t held;
} wake_t;

enum { WAKE_AI_EXPLAIN, WAKE_AI_COUNT };
extern const zxm_ai_action_t WAKE_AI[WAKE_AI_COUNT];

void wake_init(wake_t *w, zxm_link_t *link);
/* Read every event since the last read (several log.tail calls if needed);
 * *added = how many came in. Does nothing while paused. */
int wake_follow(wake_t *w, uint32_t *added);
void wake_pause(wake_t *w, bool paused);
void wake_clear(wake_t *w); /* forget what is shown; keeps following from now */
/* The view's filter: events at or above level whose source or text holds
 * `find` (ASCII case ignored; "" = all). */
int wake_filter(wake_t *w, uint8_t min_level, const char *find);
/* The i-th event that passes the filter, oldest first; NULL past the end. */
uint32_t wake_count(const wake_t *w);
const wake_event_t *wake_event(const wake_t *w, uint32_t i);

/* AI: explain the warnings and errors shown (held, M3). */
int wake_ai(wake_t *w, int32_t action);
int wake_ai_accept(wake_t *w);
int wake_ai_reject(wake_t *w);

#endif
