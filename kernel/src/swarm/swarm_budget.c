/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_budget.c — Fibonacci-scaled tokens-per-cycle allocation. See swarm_budget.h. */
#include "swarm_budget.h"

uint64_t swarm_fib(uint32_t n) {
    if (n == 0 || n > 93) return 0;   /* F(94) overflows 64 bits */
    uint64_t a = 1, b = 1;            /* F(1), F(2) */
    for (uint32_t i = 3; i <= n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return (n == 1) ? a : b;
}

uint32_t swarm_level_capacity(uint32_t d) {
    if (d >= SWARM_MAX_LEVELS) return 0;
    return (uint32_t)swarm_fib(d + 2);
}

uint64_t swarm_level_weight(uint32_t d, uint32_t num_levels) {
    if (num_levels == 0 || num_levels > SWARM_MAX_LEVELS || d >= num_levels) return 0;
    return swarm_fib(num_levels - d + 1);
}

/* Largest-remainder split of `total` over n items in proportion to w[i].
 * Items with w[i] == 0 get 0. Leftover units go to the largest remainders,
 * ties to the lower index. Requires total * w[i] to fit in 64 bits. */
static void split_largest_remainder(uint64_t total, const uint64_t *w,
                                    uint32_t n, uint64_t *out) {
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += w[i];
    for (uint32_t i = 0; i < n; i++) out[i] = 0;
    if (sum == 0 || total == 0) return;

    uint64_t rem[SWARM_MAX_MODELS];
    bool     taken[SWARM_MAX_MODELS];
    uint64_t given = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t q = total * w[i];
        out[i]   = q / sum;
        rem[i]   = q % sum;
        taken[i] = false;
        given   += out[i];
    }
    uint64_t left = total - given;     /* always < number of nonzero weights */
    while (left > 0) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++) {
            if (taken[i] || w[i] == 0) continue;
            if (best == n || rem[i] > rem[best]) best = i;
        }
        if (best == n) break;          /* unreachable: left < nonzero count */
        out[best]++;
        taken[best] = true;
        left--;
    }
}

static swarm_slot_t *find_slot(swarm_budget_t *b, uint32_t model_id) {
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].model_id == model_id) return &b->slots[i];
    return 0;
}

static const swarm_slot_t *find_slot_c(const swarm_budget_t *b, uint32_t model_id) {
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].model_id == model_id) return &b->slots[i];
    return 0;
}

swarm_status_t swarm_budget_init(swarm_budget_t *b, uint32_t num_levels,
                                 uint64_t tokens_per_cycle) {
    if (!b) return SWARM_ERR_ARG;
    if (num_levels == 0 || num_levels > SWARM_MAX_LEVELS) return SWARM_ERR_ARG;
    if (tokens_per_cycle > SWARM_MAX_TOKENS_PER_CYCLE) return SWARM_ERR_ARG;
    b->num_levels       = num_levels;
    b->tokens_per_cycle = tokens_per_cycle;
    b->cycle            = 0;
    b->cycle_open       = false;
    b->num_slots        = 0;
    b->last_unused      = 0;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) b->level_budget[d] = 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        b->slots[i].model_id = 0;
        b->slots[i].level    = 0;
        b->slots[i].active   = false;
        b->slots[i].allotted = 0;
        b->slots[i].used     = 0;
    }
    return SWARM_OK;
}

swarm_status_t swarm_budget_set_rate(swarm_budget_t *b, uint64_t tokens_per_cycle) {
    if (!b || tokens_per_cycle > SWARM_MAX_TOKENS_PER_CYCLE) return SWARM_ERR_ARG;
    b->tokens_per_cycle = tokens_per_cycle;   /* takes effect next cycle */
    return SWARM_OK;
}

swarm_status_t swarm_budget_register(swarm_budget_t *b, uint32_t model_id,
                                     uint32_t level) {
    if (!b || level >= b->num_levels) return SWARM_ERR_ARG;
    if (find_slot(b, model_id)) return SWARM_ERR_DUPLICATE;
    if (b->num_slots >= SWARM_MAX_MODELS) return SWARM_ERR_FULL;

    uint32_t in_level = 0;
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].level == level) in_level++;
    if (in_level >= swarm_level_capacity(level)) return SWARM_ERR_FULL;   /* R1 */

    swarm_slot_t *s = &b->slots[b->num_slots++];
    s->model_id = model_id;
    s->level    = (uint8_t)level;
    s->active   = true;
    s->allotted = 0;
    s->used     = 0;
    return SWARM_OK;
}

swarm_status_t swarm_budget_set_active(swarm_budget_t *b, uint32_t model_id,
                                       bool active) {
    if (!b) return SWARM_ERR_ARG;
    swarm_slot_t *s = find_slot(b, model_id);
    if (!s) return SWARM_ERR_NO_MODEL;
    s->active = active;
    return SWARM_OK;
}

swarm_status_t swarm_budget_begin_cycle(swarm_budget_t *b) {
    if (!b) return SWARM_ERR_ARG;
    if (b->cycle_open) swarm_budget_end_cycle(b);

    /* R2 + R3: weights of levels with at least one active model. */
    uint64_t weight[SWARM_MAX_LEVELS];
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) weight[d] = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        const swarm_slot_t *s = &b->slots[i];
        if (s->active) weight[s->level] = swarm_level_weight(s->level, b->num_levels);
    }
    split_largest_remainder(b->tokens_per_cycle, weight, b->num_levels,
                            b->level_budget);
    for (uint32_t d = b->num_levels; d < SWARM_MAX_LEVELS; d++) b->level_budget[d] = 0;

    /* R4: equal split inside each level, in slot order. */
    for (uint32_t d = 0; d < b->num_levels; d++) {
        uint32_t idx[SWARM_MAX_MODELS];
        uint64_t ones[SWARM_MAX_MODELS];
        uint64_t share[SWARM_MAX_MODELS];
        uint32_t n = 0;
        for (uint32_t i = 0; i < b->num_slots; i++) {
            if (b->slots[i].level != d) continue;
            b->slots[i].allotted = 0;
            b->slots[i].used     = 0;
            if (!b->slots[i].active) continue;
            idx[n]  = i;
            ones[n] = 1;
            n++;
        }
        split_largest_remainder(b->level_budget[d], ones, n, share);
        for (uint32_t k = 0; k < n; k++) b->slots[idx[k]].allotted = share[k];
    }

    b->cycle++;
    b->cycle_open = true;
    return SWARM_OK;
}

swarm_status_t swarm_budget_consume(swarm_budget_t *b, uint32_t model_id,
                                    uint64_t requested, uint64_t *granted) {
    if (!b || !granted) return SWARM_ERR_ARG;
    *granted = 0;
    if (!b->cycle_open) return SWARM_ERR_NO_CYCLE;
    swarm_slot_t *s = find_slot(b, model_id);
    if (!s) return SWARM_ERR_NO_MODEL;
    uint64_t left = s->allotted - s->used;
    uint64_t g = (requested < left) ? requested : left;
    s->used += g;
    *granted = g;
    return SWARM_OK;
}

uint64_t swarm_budget_remaining(const swarm_budget_t *b, uint32_t model_id) {
    if (!b || !b->cycle_open) return 0;
    const swarm_slot_t *s = find_slot_c(b, model_id);
    if (!s) return 0;
    return s->allotted - s->used;
}

swarm_status_t swarm_budget_end_cycle(swarm_budget_t *b) {
    if (!b) return SWARM_ERR_ARG;
    if (!b->cycle_open) return SWARM_ERR_NO_CYCLE;
    uint64_t unused = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        unused += b->slots[i].allotted - b->slots[i].used;
        b->slots[i].allotted = 0;                       /* R6: expire */
    }
    b->last_unused = unused;
    b->cycle_open  = false;
    return SWARM_OK;
}
