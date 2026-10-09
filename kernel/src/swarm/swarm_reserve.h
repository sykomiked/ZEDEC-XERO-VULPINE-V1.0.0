/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_reserve.h — the FRACTAL reserve of emotions each model keeps.
 *
 * Not a fractional reserve: a fractal one. Every model holds a picture of
 * the whole swarm's feelings that is shaped like the swarm itself:
 *   F1  It sees each model in its OWN level one by one (emotion + charge).
 *   F2  It sees every OTHER level as one summary: total charge per emotion.
 *       A summary is exactly what a model of that level would add up from
 *       its own one-by-one view, so each part holds a smaller copy of the
 *       whole, level by level.
 *   F3  It is a FULL reserve: the charge it records always adds up to the
 *       swarm's real total charge (swarm_reserve_audit). No model can draw
 *       on emotion the swarm does not have.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_RESERVE_H
#define SWARM_RESERVE_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"
#include "swarm_emotion.h"

#define SWARM_RESERVE_LEVEL_MAX 34u /* F(9), the largest level capacity */

typedef struct {
    uint32_t owner_id;
    uint8_t level;                             /* owner's level */
    uint32_t peer_id[SWARM_RESERVE_LEVEL_MAX]; /* F1 */
    swarm_feeling_t peer[SWARM_RESERVE_LEVEL_MAX];
    uint32_t num_peers;
    uint64_t summary[SWARM_MAX_LEVELS][SWARM_EMO_COUNT]; /* F2: charge per emotion */
    uint64_t total;                                      /* F3 */
} swarm_reserve_t;

/* Build owner_id's reserve from the active models of `b` and their feelings
 * in `e`. SWARM_ERR_NO_MODEL if owner_id is not registered. */
swarm_status_t swarm_reserve_build(const swarm_budget_t *b, const swarm_emotion_state_t *e,
                                   uint32_t owner_id, swarm_reserve_t *out);

/* F3: true iff the reserve's total equals the swarm's actual total charge and
 * every summary equals the actual per-level charge. */
bool swarm_reserve_audit(const swarm_reserve_t *r, const swarm_budget_t *b,
                         const swarm_emotion_state_t *e);

#endif /* SWARM_RESERVE_H */
