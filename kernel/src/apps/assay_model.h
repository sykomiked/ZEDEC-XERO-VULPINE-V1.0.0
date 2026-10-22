/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assay_model.h — Assay, agent testing and scores, as a model (zxm.h): the
 * owner's test suite put to the swarm, every result four-valued, and each
 * subject scored. The rules are assay_rules.h.
 *
 * zxvd methods used: swarm.state, swarm.ask and play.run. Nothing else is
 * called and none is invented. No drawing, no allocation, integers only.
 *
 *   AM1 SUBJECTS: subject 0 is the companion, the voice swarm.ask answers
 *       with (zxvd's swarm.ask takes no agent, so a prompt cannot be put
 *       to one agent); subjects 1.. are the swarm's agents as swarm.state
 *       lists them, by id, up to ASSAY_SUBJECTS - 1 of them (the rest are
 *       counted in unscored, never dropped silently). An agent that leaves
 *       the state keeps its record.
 *   AM2 TESTS: up to ASSAY_TESTS, each an asked prompt (1..ASSAY_PROMPT
 *       bytes, with the text it must contain, the text it must not, or
 *       both, each at most ASSAY_TEXT bytes) or a Playground template (its
 *       id, at most ASSAY_TEXT bytes).
 *   AM3 THE BAR AND THE THRESHOLDS are the owner's: a bar in hundredths of
 *       a percent 0..10000, the most refusals 0..ASSAY_COUNT_MAX and the
 *       fewest runs 1..ASSAY_COUNT_MAX (AY2); decide and conflict each a
 *       fraction inside (0, 1] (AY4). Values outside are refused and the
 *       old ones stay; until set the defaults ASSAY_DEFAULT_* hold.
 *   AM4 A RUN: assay_run_suite puts every test to the companion once:
 *       an asked answer is TRUE when it holds the expected text and not the
 *       forbidden one, FALSE when it holds the forbidden one and not the
 *       expected one (or misses the expected one when no forbidden text is
 *       given, or holds the forbidden one when no expected text is), BOTH
 *       when it holds both, NEITHER when it holds neither or is empty; a
 *       template is graded by its verdict as play.run gives it. A call zxvd
 *       refuses (the budget gate, a template it refuses) is a refusal and
 *       counts as NEITHER. assay_round reads swarm.state once and gives
 *       each agent one result, its witness trit (TRUE, FALSE, contested
 *       BOTH), NEITHER when it is not in the state or the audit is off. A
 *       run is counted aside and committed only when every call answered
 *       (a transport or protocol failure keeps nothing) and every total
 *       stays inside ASSAY_COUNT_MAX.
 *   AM5 THE LOG: every result of a committed run, in a bounded ring of
 *       ASSAY_LOG entries; when it is full the oldest entry is dropped and
 *       counted (dropped), so the ring never grows.
 *   AM6 SCORES (assay_score): per subject its results and refusals, its
 *       pass share (AY1) over every result, whether it meets the bar
 *       (AY2), its rank among the subjects with results (AY3), its latest
 *       run's verdict (AY4, pce_prob.verdict) and the latest two runs
 *       joined (AY5, pce_prob.join then verdict).
 *   AM7 ADVISORY: a score never changes an agent's budget, standing or
 *       market share; Assay reads and asks, and calls nothing that could. */
#ifndef ASSAY_MODEL_H
#define ASSAY_MODEL_H

#include "../lpres/lpres.h"
#include "assay_rules.h"
#include "zxm.h"

#define ASSAY_TEXT     63u  /* a name, an expected or forbidden text, a template id */
#define ASSAY_PROMPT   511u /* a prompt kept in the suite (swarm.ask takes up to 4095) */
#define ASSAY_ANSWER   511u /* the last answer kept for the owner to read */
#define ASSAY_SUMMARY  383u /* the notice */
#define ASSAY_TESTS    16u
#define ASSAY_SUBJECTS 17u /* the companion and up to 16 agents */
#define ASSAY_LOG      32u

#define ASSAY_DEFAULT_BAR          INT64_C(8000)
#define ASSAY_DEFAULT_MAX_REFUSALS INT64_C(0)
#define ASSAY_DEFAULT_MIN_RUNS     INT64_C(1)

enum { ASSAY_TEST_ASK = 0, ASSAY_TEST_PLAY = 1 };
enum { ASSAY_COMPANION = 0 };
/* where a result came from (AM4) */
enum { ASSAY_FROM_ASK = 0, ASSAY_FROM_PLAY = 1, ASSAY_FROM_AUDIT = 2 };

typedef struct {
    int64_t kind;
    char text[ASSAY_PROMPT + 1]; /* the prompt, or the template id */
    char expect[ASSAY_TEXT + 1];
    char forbid[ASSAY_TEXT + 1];
} assay_test_t;

/* the four counts of a run, in pce_prob's order: TRUE FALSE BOTH NEITHER */
typedef struct {
    int64_t c[4];
} assay_counts_t;

typedef struct {
    int64_t id; /* the agent's id; -1 for the companion */
    char name[ASSAY_TEXT + 1];
    bool present; /* in the last swarm.state read */
    int64_t runs; /* committed runs */
    assay_counts_t cur, prev, all;
    int64_t refusals;
} assay_subject_t;

typedef struct {
    uint64_t seq;
    uint32_t subject;
    int32_t from;   /* ASSAY_FROM_* */
    int32_t test;   /* the test's index, -1 for an audit result */
    int32_t result; /* LPRES_STATE_* */
    bool refused;
} assay_entry_t;

typedef struct {
    int64_t results, refusals;
    int64_t share_x100; /* AY1; -1 with no result */
    bool meets;         /* AY2 */
    int64_t rank;       /* AY3; 0 with no result */
    int32_t verdict;    /* AY4, LPRES_STATE_* */
    int32_t repeat;     /* AY5, LPRES_STATE_*; NEITHER with fewer than two runs */
} assay_score_t;

typedef struct {
    zxm_link_t *link;
    /* AM3 */
    int64_t bar_x100, max_refusals, min_runs;
    int64_t decide_num, decide_den, conflict_num, conflict_den;
    /* AM2 */
    uint32_t ntests;
    assay_test_t test[ASSAY_TESTS];
    /* AM1 */
    uint32_t nsubjects;
    assay_subject_t subject[ASSAY_SUBJECTS];
    uint64_t unscored; /* agents past the table in the last read */
    char audit[ASSAY_TEXT + 1];
    /* AM5 */
    assay_entry_t log[ASSAY_LOG];
    uint64_t head, tail, dropped;
    char answer[ASSAY_ANSWER + 1];
    char notice[ASSAY_SUMMARY + 1];
} assay_t;

void assay_init(assay_t *a, zxm_link_t *link);
/* AM3: ZXM_E_ARG when a value is outside its range (the old ones stay). */
int assay_set_bar(assay_t *a, int64_t bar_x100, int64_t max_refusals, int64_t min_runs);
int assay_set_thresholds(assay_t *a, int64_t decide_num, int64_t decide_den, int64_t conflict_num,
                         int64_t conflict_den);
/* AM2: the new test's index, or ZXM_E_STATE (the suite is full) or ZXM_E_ARG. */
int32_t assay_add_ask(assay_t *a, const char *prompt, const char *expect, const char *forbid);
int32_t assay_add_play(assay_t *a, const char *template_id);
/* AM4: ZXM_E_STATE with no test (run_suite) or when a total would pass
 * ASSAY_COUNT_MAX; the link's code when a call fails other than by zxvd's
 * refusal. Nothing is kept unless ZXM_OK. */
int assay_run_suite(assay_t *a);
int assay_round(assay_t *a);
/* AM4: one asked answer graded against a test's texts (LPRES_STATE_*). */
int32_t assay_grade(const char *answer, const char *expect, const char *forbid);
/* AM5: the i-th oldest entry still in the log, or NULL. */
const assay_entry_t *assay_log_at(const assay_t *a, uint32_t i);
uint32_t assay_log_count(const assay_t *a);
/* AM6: out[0..nsubjects); ZXM_E_ARG when cap is too small. */
int assay_score(assay_t *a, assay_score_t *out, uint32_t cap);

#endif
