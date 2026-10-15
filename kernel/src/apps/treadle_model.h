/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* treadle_model.h — Treadle, automation, as a model (zxm.h): tasks that run
 * on a schedule or on an event, within the owner's limits, each run a
 * request to the swarm (swarm.ask) whose answer is held for the owner. The
 * rules are treadle_rules.h (TR1-TR3), the same ones the app's Sutra, KRON,
 * C++, Eve and MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   TM1 THE TASKS are held here: zxvd has no scheduler method. A task is
 *       added only when it fits TR1's domain (on a schedule) and makes a
 *       TR3 record; otherwise nothing changes. A task added is HELD: it
 *       never runs until the owner approves it, and the owner can hold it
 *       again or remove it at any time.
 *   TM2 THE LIMITS: the owner's daily cap of tokens (0, the start, lets
 *       nothing run) and the swarm's tokens per cycle (swarm.state). A run
 *       goes only when its cost fits both: TR2 with left = cap - spent
 *       today - cost, and cost <= tokens per cycle. Nothing runs before the
 *       swarm's limit has been read.
 *   TM3 A TICK at a minute of a day runs every approved task on a schedule
 *       whose next run (TR1) is that minute, once a minute. An event of a
 *       kind runs every approved task waiting for that kind. Days never go
 *       back; a new day starts the spend from 0.
 *   TM4 HELD (zxm.h M3): a run's answer, and a task the swarm suggests,
 *       wait in one of TREADLE_HELD slots for the owner's accept or
 *       reject; neither changes a task by itself. With every slot waiting,
 *       a due run waits too (backpressure, counted in `waiting`).
 *   TM5 SPEND (zxm.h M4): a run adds the tokens it took (exact or
 *       estimated) to today's spend, saturating, never wrapping. */
#ifndef TREADLE_MODEL_H
#define TREADLE_MODEL_H

#include "treadle_rules.h"
#include "zxm.h"

#define TREADLE_TASKS 16u  /* tasks kept */
#define TREADLE_NAME  31u  /* a task's name */
#define TREADLE_EVENT 15u  /* a notice kind, as zxvd's notes give it */
#define TREADLE_ASK   511u /* the request a run sends */
#define TREADLE_HELD  4u   /* answers waiting for the owner's word */

enum { TREADLE_ON_TIME = 0, TREADLE_ON_EVENT = 1 };
enum { TREADLE_AI_RUN = 0, TREADLE_AI_SUGGEST = 1, TREADLE_AI_COUNT = 2 };

extern const zxm_ai_action_t TREADLE_AI[TREADLE_AI_COUNT];

typedef struct {
    char name[TREADLE_NAME + 1];
    char event[TREADLE_EVENT + 1]; /* on an event: the notice kind */
    char ask[TREADLE_ASK + 1];
    uint8_t kind;     /* TREADLE_ON_TIME or TREADLE_ON_EVENT */
    bool approved;    /* TM1: false is held */
    uint16_t start;   /* on a schedule: minute of the day, 0..1439 */
    uint16_t period;  /* minutes, 1..1440 */
    uint32_t cost;    /* tokens a run is expected to take */
    uint32_t runs;    /* times it ran */
    int64_t last_day; /* the day of its last run; -1: never */
    int32_t last_min; /* the minute of it */
} treadle_task_t;

typedef struct {
    zxm_held_t answer;
    int32_t task; /* the task it came from; -1: a suggestion */
} treadle_held_t;

typedef struct {
    zxm_link_t *link;
    treadle_task_t task[TREADLE_TASKS];
    uint32_t ntask;
    uint32_t cap;       /* TM2: the owner's tokens a day */
    uint64_t spent;     /* today's */
    int64_t day;        /* the day `spent` counts; -1: none yet */
    uint64_t per_cycle; /* swarm.state tokens_per_cycle */
    bool swarm_seen;
    treadle_held_t held[TREADLE_HELD];
    uint32_t ran, over, waiting; /* the last tick or event: ran, over a limit, waiting */
} treadle_t;

void treadle_init(treadle_t *t, zxm_link_t *link);
/* TM2: the owner's cap, 0..TREADLE_TOKENS_MAX. */
int treadle_set_cap(treadle_t *t, uint32_t cap);
/* TM1: a held task on a schedule, or on an event kind; *idx is its index. */
int treadle_add_timed(treadle_t *t, const char *name, const char *ask, uint16_t start,
                      uint16_t period, uint32_t cost, uint32_t *idx);
int treadle_add_event(treadle_t *t, const char *name, const char *ask, const char *event,
                      uint32_t cost, uint32_t *idx);
int treadle_approve(treadle_t *t, uint32_t idx);
int treadle_hold(treadle_t *t, uint32_t idx);
int treadle_remove(treadle_t *t, uint32_t idx);
/* TR1 for a task on a schedule at minute `now`; -1 for an event task, an
 * index past the tasks or a minute outside the day. */
int64_t treadle_next(const treadle_t *t, uint32_t idx, int64_t now);
/* TM2: swarm.state's tokens per cycle. */
int treadle_refresh(treadle_t *t);
/* TM3: day >= 0 (days since 1970, never back), minute 0..1439. */
int treadle_tick(treadle_t *t, int64_t day, uint32_t minute);
int treadle_event(treadle_t *t, int64_t day, uint32_t minute, const char *kind);
/* TM4: ask the swarm to suggest a task for `request`; held, never added. */
int treadle_suggest(treadle_t *t, const char *request);
int treadle_accept(treadle_t *t, uint32_t slot);
int treadle_reject(treadle_t *t, uint32_t slot);
/* How many held answers wait for the owner. */
uint32_t treadle_pending(const treadle_t *t);

#endif
