/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* retort_model.h — Retort, run and compare models, as a model (zxm.h):
 * contestants answer the same work and are compared on their score, their
 * latency and the energy each answer takes. The rules are retort_rules.h.
 *
 * zxvd methods used: model.info, swarm.ask, play.run, aipi.execute and
 * power.status. Nothing else is called and none is invented. No drawing,
 * no allocation, integers only; the time a run takes comes from the
 * caller's clock (milliseconds), since a model makes no system calls.
 *
 *   RM1 THE SLOT: model.info's loaded, can_generate, name, arch, n_layers
 *       and n_vocab are kept; an answer without its loaded and name is not
 *       zxvd's (ZXM_E_PROTOCOL). The slot's model is the only contestant
 *       of kind RETORT_KIND_ASK, and it can be added only once the slot is
 *       read; with no model loaded swarm.ask reads shorthand back, and the
 *       notice says so.
 *   RM2 LIMITS: the owner's limits on one run (prompt bytes, fuel,
 *       seconds) are set only when they fit RT3; until then the defaults
 *       RETORT_DEFAULT_* hold. A prompt longer than its limit is refused
 *       before anything is asked, and so is a function whose fuel is past
 *       the fuel limit.
 *   RM3 CONTESTANTS: up to RETORT_CONTESTANTS, each a kind and a name: the
 *       slot's model (asked through swarm.ask), a Playground template
 *       (play.run, on scratch state, never the books) or a published
 *       function (its CID, run by aipi.execute with its fuel).
 *   RM4 A RUN: its limits checked first, then the call, timed by the
 *       caller's clock, then power.status's total_uj read as its energy,
 *       then the record made to fit ^ZXRETORT (RT5), then kept. A run past
 *       its seconds is kept as over time: graded 0, its latency kept at
 *       most RETORT_MS_KEPT and left out of the latency fold. A clock that
 *       goes back counts the run as 0 ms (the default taken: the run did
 *       happen). Up to RETORT_RUNS are kept; past them ZXM_E_STATE and
 *       nothing is called.
 *   RM5 GRADES: a template's verdict grades its run (TRUE 100, FALSE 0;
 *       BOTH and NEITHER wait for a person, ungraded); a function's result
 *       grades its run against the owner's expected value (equal 100, else
 *       0); an asked answer is graded by the owner (retort_grade), 0..100
 *       points, once. Ungraded runs count in no score.
 *   RM6 THE COMPARISON (retort_compare): per contestant, its score total
 *       (RT1) over the graded runs, its share of all the points given
 *       (RT6, banker.share), its fastest and slowest run inside its
 *       seconds (RT2), and its energy per answer (RT4) over every run. */
#ifndef RETORT_MODEL_H
#define RETORT_MODEL_H

#include "retort_rules.h"
#include "zxm.h"

#define RETORT_TEXT        63u              /* a name, arch, template id or status */
#define RETORT_CID         127u             /* a function's CID, aipi.execute's maxLength */
#define RETORT_ANSWER      1023u            /* the last answer kept for the owner to read */
#define RETORT_SUMMARY     383u             /* the notice */
#define RETORT_CONTESTANTS 8u               /* model:i2 holds far more */
#define RETORT_RUNS        64u              /* run:i3 holds far more */
#define RETORT_ARGS        8u               /* aipi.execute's args */
#define RETORT_MS_KEPT     INT64_C(9999999) /* ms:i7: an over-time run is at least this */

#define RETORT_DEFAULT_PROMPT  INT64_C(4095)
#define RETORT_DEFAULT_FUEL    INT64_C(1000000)
#define RETORT_DEFAULT_SECONDS INT64_C(60)

/* a contestant's kind (RT5's kind) */
enum { RETORT_KIND_ASK = 0, RETORT_KIND_PLAY = 1, RETORT_KIND_AIPI = 2 };

typedef int64_t (*retort_clock_fn)(void *ctx); /* milliseconds, any origin */

typedef struct {
    int64_t kind;
    char name[RETORT_CID + 1]; /* the model's name, a template id or a CID */
    int64_t fuel;              /* a function's fuel_max; 0 for the others */
} retort_contestant_t;

typedef struct {
    int64_t model; /* contestant index */
    int64_t kind;
    int64_t prompt; /* bytes sent: the prompt, the template id or the args as JSON */
    int64_t fuel;
    int64_t ms;
    int64_t score; /* 0..100, or RETORT_UNGRADED */
    int64_t uj;
    bool over; /* past its seconds (RM4) */
} retort_run_t;

typedef struct {
    int64_t total;      /* RT1 */
    int64_t share_x100; /* RT6 */
    int64_t graded;
    int64_t runs;
    int64_t fastest, slowest; /* RT2; -1 with no run inside its seconds */
    int64_t uj_num, uj_den;   /* RT4; 0/0 with no run */
} retort_standing_t;

typedef struct {
    zxm_link_t *link;
    retort_clock_fn clock;
    void *clock_ctx;
    /* RM1 */
    bool known, loaded, can_generate;
    char name[RETORT_TEXT + 1], arch[RETORT_TEXT + 1];
    int64_t layers, vocab;
    /* RM2 */
    int64_t lim_prompt, lim_fuel, lim_seconds;
    /* RM3 */
    uint32_t ncontestants;
    retort_contestant_t contestant[RETORT_CONTESTANTS];
    /* RM4, RM5 */
    uint32_t nruns;
    retort_run_t run[RETORT_RUNS];
    char answer[RETORT_ANSWER + 1];
    char notice[RETORT_SUMMARY + 1];
} retort_t;

void retort_init(retort_t *t, zxm_link_t *link, retort_clock_fn clock, void *clock_ctx);
/* RM1: model.info. */
int retort_read_slot(retort_t *t);
/* RM2: ZXM_E_ARG when the limits do not fit RT3 (the old ones stay). */
int retort_set_limits(retort_t *t, int64_t prompt, int64_t fuel, int64_t seconds);
/* RM3: the new contestant's index, or a negative ZXM_E_*: ZXM_E_STATE when
 * the list is full or the slot is unread (or already added) for an ask;
 * ZXM_E_ARG on an empty name or one that does not fit, a CID that is no
 * CID, or a function's fuel outside 1..RETORT_FUEL_MAX. */
int32_t retort_add(retort_t *t, int64_t kind, const char *name, int64_t fuel);
/* RM4: one run of a contestant of the matching kind. */
int retort_ask(retort_t *t, uint32_t who, const char *prompt);
int retort_play(retort_t *t, uint32_t who);
int retort_execute(retort_t *t, uint32_t who, const int64_t *args, uint32_t nargs, int64_t expect);
/* RM5: the owner's grade of an ungraded asked run; ZXM_E_STATE when it is
 * graded or no asked run, ZXM_E_ARG for a score outside 0..100. */
int retort_grade(retort_t *t, uint32_t run, int64_t score);
/* RM6: the standing of every contestant, out[0..ncontestants). */
int retort_compare(retort_t *t, retort_standing_t *out, uint32_t cap);
/* RT5 on run i (false when there is none). */
bool retort_record_ok(const retort_t *t, uint32_t i);

#endif
