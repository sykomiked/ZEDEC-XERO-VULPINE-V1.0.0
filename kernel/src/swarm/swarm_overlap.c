/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_overlap.c — shared computation. See swarm_overlap.h. */
#include "swarm_overlap.h"

static uint64_t fnv_u64(uint64_t h, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) {
        h ^= (v >> (8u * i)) & 0xFFu;
        h *= 0x100000001B3ull;
    }
    return h;
}

uint64_t swarm_overlap_key(uint32_t model, uint64_t context_hash, uint64_t question_hash)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = fnv_u64(h, model);
    h = fnv_u64(h, context_hash);
    return fnv_u64(h, question_hash);
}

void swarm_overlap_init(swarm_overlap_t *o)
{
    if (!o) return;
    o->num_jobs = 0;
    o->tokens_saved = 0;
}

int32_t swarm_overlap_request(swarm_overlap_t *o, uint64_t key, uint32_t model_id)
{
    if (!o) return -1;
    swarm_job_t *j = 0;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < o->num_jobs; i++)
        if (o->job[i].key == key && !o->job[i].settled) {
            j = &o->job[i];
            idx = i;
        }
    if (!j) {
        if (o->num_jobs >= SWARM_OVERLAP_JOBS) return -1;
        idx = o->num_jobs++;
        j = &o->job[idx];
        j->key = key;
        j->num_sharers = 0;
        j->settled = false;
    }
    for (uint32_t s = 0; s < j->num_sharers; s++)
        if (j->sharer[s] == model_id) return (int32_t) idx;
    if (j->num_sharers >= SWARM_MAX_MODELS) return -1;
    j->sharer[j->num_sharers++] = model_id;
    return (int32_t) idx;
}

swarm_status_t swarm_overlap_settle(swarm_overlap_t *o, swarm_budget_t *b, uint32_t job,
                                    uint64_t cost)
{
    if (!o || !b || job >= o->num_jobs || o->job[job].settled) return SWARM_ERR_ARG;
    if (!b->cycle_open) return SWARM_ERR_NO_CYCLE;
    swarm_job_t *j = &o->job[job];
    uint64_t ones[SWARM_MAX_MODELS];
    uint64_t share[SWARM_MAX_MODELS];
    for (uint32_t s = 0; s < j->num_sharers; s++) ones[s] = 1;
    swarm_split_lr(cost, ones, j->num_sharers, share);
    for (uint32_t s = 0; s < j->num_sharers; s++) /* V3 */
        if (swarm_budget_remaining(b, j->sharer[s]) < share[s]) return SWARM_ERR_FULL;
    for (uint32_t s = 0; s < j->num_sharers; s++) {
        uint64_t g;
        swarm_budget_consume(b, j->sharer[s], share[s], &g);
    }
    if (j->num_sharers > 1) o->tokens_saved += cost * (j->num_sharers - 1u); /* V2 */
    j->settled = true;
    return SWARM_OK;
}
