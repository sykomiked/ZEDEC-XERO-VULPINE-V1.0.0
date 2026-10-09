/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_self.c — interaction rings and reflection. See swarm_self.h. */
#include "swarm_self.h"

static const char *const BUILTIN[SWARM_RING_BUILTIN] = {
    "inner", "being", "user", "system", "swarm", "internet", "world"
};

static void name_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = 0;
}

void swarm_self_init(swarm_self_t *s)
{
    s->count = 0;
    s->has_last = false;
    s->reflections = 0;
    for (uint32_t i = 0; i < SWARM_RING_BUILTIN; i++) (void)swarm_self_ring_add(s, BUILTIN[i]);
}

int32_t swarm_self_ring_add(swarm_self_t *s, const char *name)
{
    if (s->count >= SWARM_SELF_RINGS) return -1;
    swarm_ring_t *r = &s->ring[s->count];
    name_copy(r->name, name, sizeof r->name);
    r->observations = 0;
    r->stress_milli = 0;
    r->last_gap_milli = 0;
    return (int32_t)s->count++;
}

void swarm_self_observe(swarm_self_t *s, uint32_t ring,
                        uint32_t expected_milli, uint32_t observed_milli)
{
    if (ring >= s->count) return;
    swarm_ring_t *r = &s->ring[ring];
    uint32_t gap = expected_milli > observed_milli ? expected_milli - observed_milli
                                                   : observed_milli - expected_milli;
    if (gap > 1000u) gap = 1000u;
    r->last_gap_milli = gap;
    r->stress_milli = (3u * r->stress_milli + gap) / 4u;
    r->observations++;
}

swarm_reflection_t swarm_self_reflect(swarm_self_t *s, bool can_grow)
{
    /* A3: score the last prediction against what happened, on the inner ring. */
    if (s->has_last && s->last.focus < s->count)
        swarm_self_observe(s, SWARM_RING_INNER, s->last.predicted_milli,
                           s->ring[s->last.focus].stress_milli);

    swarm_reflection_t out;
    uint32_t total = 0;
    out.focus = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        total += s->ring[i].observations;
        if (s->ring[i].stress_milli > s->ring[out.focus].stress_milli) out.focus = i;
    }
    uint32_t st = s->ring[out.focus].stress_milli;
    if (st >= SWARM_SELF_RECALIBRATE) {
        out.action = SWARM_ACT_RECALIBRATE;
        out.predicted_milli = st / 2u;        /* expect to halve it */
    } else {
        out.action = can_grow ? SWARM_ACT_GROW : SWARM_ACT_REFINE;
        out.predicted_milli = st;             /* expect to hold it */
    }
    out.numen = swarm_numen(total);
    s->last = out;
    s->has_last = true;
    s->reflections++;
    return out;
}
