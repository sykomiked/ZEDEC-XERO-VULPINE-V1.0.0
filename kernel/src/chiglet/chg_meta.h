/* chg_meta.h — metacognition and self-awareness for the Chiglet.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE REFLECTIVE SWARM. The Chiglet is aware of its environment (telemetry),
 * aware of itself (its own state and counters), aware of that awareness, and
 * aware of the awareness of the awareness — four deterministic levels, all
 * computed from LIVE state, never hallucinated. The self-model is a fold of
 * the swarm's own measurable behaviour (inferences run, uncertainties,
 * capabilities), so the swarm can reason ABOUT its own reasoning.
 *
 * EXACT ONLY. No float, no RNG, no allocation: every level is a pure
 * function of (chiglet state, tick). The recursion is bounded by
 * CHG_META_DEPTH; memory never grows with cycles.
 */
#ifndef ZXV_CHG_META_H
#define ZXV_CHG_META_H

#include <stdint.h>
#include <stdbool.h>

#include "chiglet.h" /* chiglet_t — the swarm whose state we reflect   */

#define CHG_META_DEPTH 4u /* env, self, awareness, awareness^2           */

typedef enum {
    CHG_META_ENV = 0,  /* the environment: what happened (telemetry)     */
    CHG_META_SELF = 1, /* the self: the swarm's own live state           */
    CHG_META_ONE = 2,  /* awareness of the self                          */
    CHG_META_TWO = 3,  /* awareness of the awareness of the self         */
    CHG_META_COUNT
} chg_meta_level_t;

typedef struct {
    /* the live self-model: the swarm's own measurable state, folded */
    uint64_t inferences;
    uint64_t uncertain;
    uint64_t decided;
    uint32_t caps;
    uint32_t state; /* chg_state_t of the last inference          */

    /* the four-level awareness stack (deterministic, bounded) */
    uint64_t aware[CHG_META_DEPTH];

    /* the self-model digest: a fold of the live counters, so the swarm can
     * compare its own model across ticks and cycles. */
    uint64_t self_model;

    uint64_t last_tick;
} chg_meta_t;

/* Reflect the Chiglet's LIVE state into `meta` at `tick`: reads the swarm's
 * own counters (inferences, uncertain_count, caps, model state) and advances
 * the four-level awareness recursion. Deterministic: same state + same tick
 * -> identical meta. Returns the top-level awareness (awareness^2). */
uint64_t chg_meta_reflect(chg_meta_t *meta, const chiglet_t *c, uint64_t tick);

/* The swarm's self-model at the last reflection. */
uint64_t chg_meta_self_model(const chg_meta_t *meta);

/* A deterministic narrative of the swarm's self-awareness, for the zui
 * compositor: "aware of <env>; self <state>; meta <level>". Bounded writes. */
uint32_t chg_meta_narrate(const chg_meta_t *meta, char *buf, uint32_t cap);

#endif /* ZXV_CHG_META_H */