/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_overlap.h — the Venn rule: overlapping work is done once.
 *
 *   V1  A computation is named by a key: the model, a hash of its context
 *       and a hash of the question. Agents asking for the same key share
 *       ONE job instead of each running their own.
 *   V2  When the job is done its token cost is split equally among the
 *       agents that shared it (exact largest-remainder split, R5), so each
 *       pays 1/k of it. The tokens saved, cost x (k - 1), are counted.
 *   V3  The job is charged all-or-nothing: if any sharer cannot cover its
 *       share this cycle, nobody is charged and the job is reported.
 * The overlap of two agents' claims is what the GLUT logic holds as one
 * shared item; this module is its cost side.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_OVERLAP_H
#define SWARM_OVERLAP_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"

#define SWARM_OVERLAP_JOBS 64u

typedef struct {
    uint64_t key;
    uint32_t sharer[SWARM_MAX_MODELS];
    uint32_t num_sharers;
    bool settled;
} swarm_job_t;

typedef struct {
    swarm_job_t job[SWARM_OVERLAP_JOBS];
    uint32_t num_jobs;
    uint64_t tokens_saved;
} swarm_overlap_t;

/* V1: FNV-1a over (model, context hash, question hash). */
uint64_t swarm_overlap_key(uint32_t model, uint64_t context_hash, uint64_t question_hash);

void swarm_overlap_init(swarm_overlap_t *o);

/* V1: join (or open) the job for `key`. Returns its index, or -1 if the
 * table is full. Joining twice is a no-op. */
int32_t swarm_overlap_request(swarm_overlap_t *o, uint64_t key, uint32_t model_id);

/* V2 + V3: charge the job's `cost` to its sharers in the open cycle. */
swarm_status_t swarm_overlap_settle(swarm_overlap_t *o, swarm_budget_t *b, uint32_t job,
                                    uint64_t cost);

#endif /* SWARM_OVERLAP_H */
