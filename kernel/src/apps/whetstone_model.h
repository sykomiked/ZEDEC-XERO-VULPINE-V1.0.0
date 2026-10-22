/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* whetstone_model.h — Whetstone, the build and test runner, as a model
 * (zxm.h): run the targets of a plan as Playground templates and show the
 * verdict. The rules are whetstone_rules.h.
 *
 * zxvd methods used: files.read, play.run and sys.info. Nothing else is
 * called and none is invented: zxvd has no build or test-run method of its
 * own, so a target is a Playground template, run on scratch state, never
 * the books; and zxvd has no notification method, so the notice text is
 * the model's, for the view to show. No drawing, no allocation, integers
 * only.
 *
 *   WM1 THE PLAN: one files.read of a text file in the Hoard (at most
 *       WHETSTONE_PLAN_MAX bytes), one target a line: a template id
 *       (1..63 bytes, no '/', as play.run takes it), then optionally its
 *       retries and its timeout in seconds. Blank lines and lines that
 *       start with '#' are passed over. A target without limits takes
 *       WHETSTONE_DEFAULT_RETRIES and WHETSTONE_DEFAULT_TIMEOUT. A line
 *       that is no target, limits outside WR5, or more than
 *       WHETSTONE_TARGETS targets refuse the whole plan, and the plan held
 *       before stays.
 *   WM2 LIMITS: a target's retries and timeout change only inside WR5 and
 *       only on the owner's word (owner_confirmed, checked after the
 *       arguments and before anything changes).
 *   WM3 A RUN (whetstone_run): play.run of the target's template, timed by
 *       zxvd's own clock (sys.info time_ms before and after). Checked
 *       first: a plan, the target, room for its record (WHETSTONE_RECORDS)
 *       and its limits (WR5). A try past its timeout is counted at the
 *       timeout and says NEITHER. Only a NEITHER is tried again, at most
 *       retries times: a FALSE is a failure and a BOTH is held for the
 *       person, never retried away. The last try's steps are counted
 *       (TRUE passed, FALSE failed, BOTH held, NEITHER unknown), the tries'
 *       milliseconds summed (WR4), and the record checked (WR3, WR6)
 *       before it is kept. An answer that is not zxvd's keeps nothing.
 *   WM4 THE VERDICT (whetstone_summary): every kept run's verdict
 *       conjoined with lpres_conjoin (WR1; none is NEITHER), the runs
 *       counted by verdict (WR8), the steps' counts and time summed (WR3,
 *       WR4), and the share of the results that passed (WR7). WR2 must
 *       agree with WR1, or the summary is refused.
 *   WM5 CLEARING: the kept runs are dropped only on the owner's word. */
#ifndef WHETSTONE_MODEL_H
#define WHETSTONE_MODEL_H

#include "whetstone_rules.h"
#include "zxm.h"

#define WHETSTONE_NAME            63u   /* a template id (zxvplay.h ZP_MAX_NAME) */
#define WHETSTONE_TARGETS         32u   /* targets in a plan */
#define WHETSTONE_RECORDS         64u   /* runs kept in a session */
#define WHETSTONE_STEPS           12u   /* zxvplay.h ZP_MAX_STEPS */
#define WHETSTONE_PLAN_MAX        4095u /* bytes of a plan file */
#define WHETSTONE_NOTICE          383u
#define WHETSTONE_DEFAULT_RETRIES INT64_C(0)
#define WHETSTONE_DEFAULT_TIMEOUT INT64_C(60) /* seconds: Colonnade's minute a run */

typedef struct {
    char id[WHETSTONE_NAME + 1];
    int64_t retries;
    int64_t timeout_s;
} whetstone_target_t;

typedef struct {
    char target[WHETSTONE_NAME + 1]; /* the template id, kept when the plan changes */
    int64_t place;                   /* the target's place in the plan it ran from */
    int64_t verdict;                 /* lpres_state_t */
    int64_t passed, failed, held, unknown;
    int64_t ms;     /* every try, each counted at most at its timeout */
    int64_t tries;  /* 1 + the retries taken */
    bool timed_out; /* the last try passed its timeout */
} whetstone_record_t;

typedef struct {
    int64_t verdict; /* WR1 */
    bool passed;     /* WR2 */
    int64_t runs, runs_true, runs_false, runs_both, runs_neither;
    int64_t passed_n, failed_n, held_n, unknown_n; /* the steps, summed */
    int64_t ms;                                    /* WR4 */
    int64_t pass_x100; /* WR7: of the results; WHETSTONE_REFUSED for none */
} whetstone_summary_t;

typedef struct {
    zxm_link_t *link;
    /* WM1 */
    bool planned;
    uint32_t ntargets;
    whetstone_target_t target[WHETSTONE_TARGETS];
    /* WM3 */
    uint32_t nrecords;
    whetstone_record_t record[WHETSTONE_RECORDS];
    char notice[WHETSTONE_NOTICE + 1];
} whetstone_t;

void whetstone_init(whetstone_t *w, zxm_link_t *link);
/* WM1 */
int whetstone_load_plan(whetstone_t *w, const char *space, const char *path);
/* WM2: ZXM_E_ARG for a target that is none or limits outside WR5;
 * ZXM_E_STATE without the owner's word. */
int whetstone_set_limits(whetstone_t *w, uint32_t target, int64_t retries, int64_t timeout_s,
                         bool owner_confirmed);
/* WM3 */
int whetstone_run(whetstone_t *w, uint32_t target);
/* WM4 */
int whetstone_summary(whetstone_t *w, whetstone_summary_t *out);
/* WM5 */
int whetstone_clear(whetstone_t *w, bool owner_confirmed);

#endif
