/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* shepherd_model.h — Shepherd, family and screen time, as a model (zxm.h):
 * the minutes each app was used each day of the last week, the limits a
 * guardian sets (minutes a day, the hours an app may be used, more time for
 * today), and the passphrase that locks them (zxvd's screen.*,
 * kernel/arch/hosted/zxv_screen.h). The model never keeps a passphrase: it
 * passes it to zxvd and forgets it. AI can suggest a fair plan from the
 * week's use, held until the guardian reads it (M3); it changes no limit by
 * itself. No drawing, no allocation. */
#ifndef SHEPHERD_MODEL_H
#define SHEPHERD_MODEL_H

#include "zxm.h"

#define SHEPHERD_APP  31u
#define SHEPHERD_APPS 64u
#define SHEPHERD_DAYS 7u
#define SHEPHERD_PASS 128u
#define SHEPHERD_ALL  "*"

typedef struct {
    char app[SHEPHERD_APP + 1];
    int32_t minutes; /* a day; -1: no cap */
    uint16_t from, to;
    uint32_t extra; /* granted for today */
} shepherd_limit_t;

typedef struct {
    char app[SHEPHERD_APP + 1];
    uint32_t minutes;
} shepherd_use_t;

typedef struct {
    uint32_t date; /* YYYYMMDD */
    uint32_t total, n;
    shepherd_use_t a[SHEPHERD_APPS];
} shepherd_day_t;

typedef struct {
    zxm_link_t *link;
    bool locked, reset;
    uint32_t wait_s, today;
    shepherd_limit_t lim[SHEPHERD_APPS];
    uint32_t nlim;
    shepherd_day_t day[SHEPHERD_DAYS]; /* newest first */
    uint32_t ndays;
    zxm_held_t held;
} shepherd_t;

enum { SHEPHERD_AI_PLAN, SHEPHERD_AI_COUNT };
extern const zxm_ai_action_t SHEPHERD_AI[SHEPHERD_AI_COUNT];

void shepherd_init(shepherd_t *s, zxm_link_t *link);
int shepherd_refresh(shepherd_t *s);
/* minutes -1: no daily cap; from == to: any time. pass: NULL or "" when not
 * locked. Each refreshes. Each changes a kept setting (screen.set_limit,
 * screen.clear_limit, screen.grant, screen.lock), so each sends only on the
 * owner's word: owner_confirmed false (after the argument checks) is
 * ZXM_E_STATE, nothing is sent and the model is unchanged. The passphrase is
 * zxvd's check of the guardian; owner_confirmed is the person's own action. */
int shepherd_set_limit(shepherd_t *s, const char *pass, const char *app, int32_t minutes,
                       uint16_t from, uint16_t to, bool owner_confirmed);
int shepherd_clear_limit(shepherd_t *s, const char *pass, const char *app, bool owner_confirmed);
int shepherd_grant(shepherd_t *s, const char *pass, const char *app, uint32_t minutes,
                   bool owner_confirmed);
/* Set or change the passphrase; "" or NULL `pass` unlocks. */
int shepherd_lock(shepherd_t *s, const char *old, const char *pass, bool owner_confirmed);

const shepherd_limit_t *shepherd_limit(const shepherd_t *s, const char *app);
/* An app's minutes over the days listed (the week). */
uint32_t shepherd_week(const shepherd_t *s, const char *app);
/* Today's minutes, all apps. */
uint32_t shepherd_today_total(const shepherd_t *s);

/* AI: a fair plan from the week's use (held, M3). */
int shepherd_ai(shepherd_t *s, int32_t action);
int shepherd_ai_accept(shepherd_t *s);
int shepherd_ai_reject(shepherd_t *s);

#endif
