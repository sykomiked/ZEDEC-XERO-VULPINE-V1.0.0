/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_reserve.c — fractal emotional reserve. See swarm_reserve.h. */
#include "swarm_reserve.h"

static swarm_feeling_t feeling_of(const swarm_emotion_state_t *e, uint32_t model_id) {
    for (uint32_t i = 0; i < e->num; i++)
        if (e->model_id[i] == model_id) return e->feeling[i];
    swarm_feeling_t none = { SWARM_EMO_NEUTRAL, 0 };
    return none;
}

swarm_status_t swarm_reserve_build(const swarm_budget_t *b, const swarm_emotion_state_t *e,
                                   uint32_t owner_id, swarm_reserve_t *out) {
    if (!b || !e || !out) return SWARM_ERR_ARG;
    const swarm_slot_t *own = 0;
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].model_id == owner_id) own = &b->slots[i];
    if (!own) return SWARM_ERR_NO_MODEL;

    out->owner_id  = owner_id;
    out->level     = own->level;
    out->num_peers = 0;
    out->total     = 0;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++) out->summary[d][k] = 0;

    for (uint32_t i = 0; i < b->num_slots; i++) {
        const swarm_slot_t *s = &b->slots[i];
        if (!s->active) continue;
        swarm_feeling_t f = feeling_of(e, s->model_id);
        uint64_t c = swarm_feeling_charge(f);
        if (s->level == own->level) {                                  /* F1 */
            out->peer_id[out->num_peers] = s->model_id;
            out->peer[out->num_peers]    = f;
            out->num_peers++;
        } else {                                                       /* F2 */
            out->summary[s->level][f.emotion] += c;
        }
        out->total += c;                                               /* F3 */
    }
    return SWARM_OK;
}

bool swarm_reserve_audit(const swarm_reserve_t *r, const swarm_budget_t *b,
                         const swarm_emotion_state_t *e) {
    if (!r || !b || !e) return false;
    uint64_t truth[SWARM_MAX_LEVELS][SWARM_EMO_COUNT];
    uint64_t total = 0, seen = 0;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++) truth[d][k] = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        const swarm_slot_t *s = &b->slots[i];
        if (!s->active) continue;
        swarm_feeling_t f = feeling_of(e, s->model_id);
        uint64_t c = swarm_feeling_charge(f);
        truth[s->level][f.emotion] += c;
        total += c;
    }
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) {
        if (d == r->level) continue;
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++)
            if (r->summary[d][k] != truth[d][k]) return false;
    }
    for (uint32_t p = 0; p < r->num_peers; p++) seen += swarm_feeling_charge(r->peer[p]);
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        if (d != r->level)
            for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++) seen += r->summary[d][k];
    return r->total == total && seen == total;
}
