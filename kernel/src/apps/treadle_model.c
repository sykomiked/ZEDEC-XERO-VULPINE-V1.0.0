/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* treadle_model.c — Treadle (see treadle_model.h). */
#include <string.h>

#include "treadle_model.h"

const zxm_ai_action_t TREADLE_AI[TREADLE_AI_COUNT] = {
    {"Run task", "Runner", 400},
    {"Suggest a task", "Planner", 300},
};

static const char *const ASK[TREADLE_AI_COUNT] = {
    "Do this task for the owner. Your answer is held until the owner accepts it.",
    "Suggest one task to automate for this request: what to ask, the minute of the day it "
    "starts and how often it runs. The owner decides; nothing is added by the suggestion.",
};

void treadle_init(treadle_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->day = -1;
    for (uint32_t h = 0; h < TREADLE_HELD; h++) t->held[h].task = -1;
}

static bool good_text(const char *x, size_t max)
{
    size_t n = x ? strlen(x) : 0;
    return n >= 1 && n <= max;
}

int treadle_set_cap(treadle_t *t, uint32_t cap)
{
    if (cap > TREADLE_TOKENS_MAX) return ZXM_E_ARG;
    t->cap = cap;
    return ZXM_OK;
}

/* TM1: checked whole before anything is kept. */
static int add(treadle_t *t, const char *name, const char *ask, uint8_t kind, const char *event,
               uint16_t start, uint16_t period, uint32_t cost, uint32_t *idx)
{
    if (!good_text(name, TREADLE_NAME) || !good_text(ask, TREADLE_ASK) || cost < 1 ||
        cost > TREADLE_TOKENS_MAX)
        return ZXM_E_ARG;
    if (kind == TREADLE_ON_TIME && treadle_next_run(start, period, start) != start)
        return ZXM_E_ARG;
    if (kind == TREADLE_ON_EVENT && !good_text(event, TREADLE_EVENT)) return ZXM_E_ARG;
    if (t->ntask >= TREADLE_TASKS) return ZXM_E_STATE;
    if (!treadle_record_fits(t->ntask + 1, kind, start, period, cost, 0)) return ZXM_E_ARG;
    treadle_task_t *k = &t->task[t->ntask];
    memset(k, 0, sizeof *k);
    memcpy(k->name, name, strlen(name) + 1);
    memcpy(k->ask, ask, strlen(ask) + 1);
    if (kind == TREADLE_ON_EVENT) memcpy(k->event, event, strlen(event) + 1);
    k->kind = kind;
    k->start = start;
    k->period = period;
    k->cost = cost;
    k->last_day = -1;
    k->last_min = -1;
    if (idx) *idx = t->ntask;
    t->ntask++;
    return ZXM_OK;
}

int treadle_add_timed(treadle_t *t, const char *name, const char *ask, uint16_t start,
                      uint16_t period, uint32_t cost, uint32_t *idx)
{
    return add(t, name, ask, TREADLE_ON_TIME, NULL, start, period, cost, idx);
}

int treadle_add_event(treadle_t *t, const char *name, const char *ask, const char *event,
                      uint32_t cost, uint32_t *idx)
{
    return add(t, name, ask, TREADLE_ON_EVENT, event, 0, 0, cost, idx);
}

int treadle_approve(treadle_t *t, uint32_t idx)
{
    if (idx >= t->ntask) return ZXM_E_ARG;
    t->task[idx].approved = true;
    return ZXM_OK;
}

int treadle_hold(treadle_t *t, uint32_t idx)
{
    if (idx >= t->ntask) return ZXM_E_ARG;
    t->task[idx].approved = false;
    return ZXM_OK;
}

/* A held answer keeps its text; one from the removed task is marked as
 * from no task (-2), and later tasks' answers follow their task down. */
int treadle_remove(treadle_t *t, uint32_t idx)
{
    if (idx >= t->ntask) return ZXM_E_ARG;
    for (uint32_t k = idx; k + 1 < t->ntask; k++) t->task[k] = t->task[k + 1];
    t->ntask--;
    memset(&t->task[t->ntask], 0, sizeof t->task[t->ntask]);
    for (uint32_t h = 0; h < TREADLE_HELD; h++) {
        if (t->held[h].task == (int32_t) idx)
            t->held[h].task = -2;
        else if (t->held[h].task > (int32_t) idx)
            t->held[h].task--;
    }
    return ZXM_OK;
}

int64_t treadle_next(const treadle_t *t, uint32_t idx, int64_t now)
{
    if (idx >= t->ntask || t->task[idx].kind != TREADLE_ON_TIME) return -1;
    return treadle_next_run(t->task[idx].start, t->task[idx].period, now);
}

int treadle_refresh(treadle_t *t)
{
    zxm_link_t *l = t->link;
    uint64_t per;
    zxm_begin(l, "swarm.state");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "tokens_per_cycle", &per)) return ZXM_E_PROTOCOL;
    t->per_cycle = per;
    t->swarm_seen = true;
    return ZXM_OK;
}

uint32_t treadle_pending(const treadle_t *t)
{
    uint32_t n = 0;
    for (uint32_t h = 0; h < TREADLE_HELD; h++) n += t->held[h].answer.state == ZXM_HELD_PENDING;
    return n;
}

static int32_t free_slot(const treadle_t *t)
{
    for (uint32_t h = 0; h < TREADLE_HELD; h++)
        if (t->held[h].answer.state != ZXM_HELD_PENDING) return (int32_t) h;
    return -1;
}

/* TM3: a new day starts the spend from 0; a day may never go back. */
static int start_day(treadle_t *t, int64_t day, uint32_t minute)
{
    if (day < 0 || minute >= TREADLE_DAY_MIN || (t->day >= 0 && day < t->day)) return ZXM_E_ARG;
    if (!t->swarm_seen) return ZXM_E_STATE;
    if (day != t->day) {
        t->day = day;
        t->spent = 0;
    }
    t->ran = t->over = t->waiting = 0;
    return ZXM_OK;
}

/* TM2: what is left of today's cap after a run of `cost`. */
static int64_t left_after(const treadle_t *t, uint32_t cost)
{
    uint64_t used = t->spent + cost;
    if (used < t->spent || used > t->cap) return -1;
    return (int64_t) (t->cap - used);
}

/* One run: limits, a held slot, the ask, then the task's books (TM2-TM5). */
static int run(treadle_t *t, uint32_t i, int64_t day, uint32_t minute)
{
    treadle_task_t *k = &t->task[i];
    if (!treadle_budget_ok(k->cost, left_after(t, k->cost)) || k->cost > t->per_cycle) {
        t->over++;
        return ZXM_OK;
    }
    int32_t h = free_slot(t);
    if (h < 0) {
        t->waiting++;
        return ZXM_OK;
    }
    static zxm_held_t got;
    int rc = zxm_ai_ask(t->link, &TREADLE_AI[TREADLE_AI_RUN], TREADLE_AI_RUN, ASK[TREADLE_AI_RUN],
                        k->ask, (uint32_t) strlen(k->ask), &got);
    if (rc) return rc;
    t->held[h].answer = got;
    t->held[h].task = (int32_t) i;
    uint64_t s = t->spent + got.cost.tokens;
    t->spent = s < t->spent ? UINT64_MAX : s;
    k->runs++;
    k->last_day = day;
    k->last_min = (int32_t) minute;
    t->ran++;
    return ZXM_OK;
}

static bool ran_at(const treadle_task_t *k, int64_t day, uint32_t minute)
{
    return k->last_day == day && k->last_min == (int32_t) minute;
}

int treadle_tick(treadle_t *t, int64_t day, uint32_t minute)
{
    int rc = start_day(t, day, minute);
    if (rc) return rc;
    for (uint32_t i = 0; i < t->ntask; i++) {
        const treadle_task_t *k = &t->task[i];
        if (!k->approved || k->kind != TREADLE_ON_TIME || ran_at(k, day, minute)) continue;
        if (treadle_next_run(k->start, k->period, minute) != (int64_t) minute) continue;
        if ((rc = run(t, i, day, minute)) != ZXM_OK) return rc;
    }
    return ZXM_OK;
}

int treadle_event(treadle_t *t, int64_t day, uint32_t minute, const char *kind)
{
    if (!good_text(kind, TREADLE_EVENT)) return ZXM_E_ARG;
    int rc = start_day(t, day, minute);
    if (rc) return rc;
    for (uint32_t i = 0; i < t->ntask; i++) {
        const treadle_task_t *k = &t->task[i];
        if (!k->approved || k->kind != TREADLE_ON_EVENT || strcmp(k->event, kind)) continue;
        if ((rc = run(t, i, day, minute)) != ZXM_OK) return rc;
    }
    return ZXM_OK;
}

int treadle_suggest(treadle_t *t, const char *request)
{
    if (!good_text(request, TREADLE_ASK)) return ZXM_E_ARG;
    int32_t h = free_slot(t);
    if (h < 0) return ZXM_E_STATE;
    static zxm_held_t got;
    int rc = zxm_ai_ask(t->link, &TREADLE_AI[TREADLE_AI_SUGGEST], TREADLE_AI_SUGGEST,
                        ASK[TREADLE_AI_SUGGEST], request, (uint32_t) strlen(request), &got);
    if (rc) return rc;
    t->held[h].answer = got;
    t->held[h].task = -1;
    return ZXM_OK;
}

int treadle_accept(treadle_t *t, uint32_t slot)
{
    if (slot >= TREADLE_HELD) return ZXM_E_ARG;
    return zxm_held_accept(&t->held[slot].answer);
}

int treadle_reject(treadle_t *t, uint32_t slot)
{
    if (slot >= TREADLE_HELD) return ZXM_E_ARG;
    return zxm_held_reject(&t->held[slot].answer);
}
