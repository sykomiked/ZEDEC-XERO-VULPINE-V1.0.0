/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_phase.c — reduced phase counting and golden geometry. See
 * swarm_phase.h. */
#include "swarm_phase.h"
#include "swarm_budget.h" /* swarm_fib, swarm_muldiv */

void swarm_phase_init(swarm_phase_t *p, uint64_t long_count)
{
    p->long_count = long_count;
    p->reduced = (uint8_t) swarm_en_root(long_count);
}

void swarm_phase_step(swarm_phase_t *p, uint32_t ticks)
{
    p->long_count += ticks;
    p->reduced = (uint8_t) ((p->reduced - 1u + ticks % 9u) % 9u + 1u);
}

swarm_phase_reading_t swarm_phase_read(const swarm_phase_t *p, bool acting_now)
{
    swarm_phase_reading_t r;
    swarm_numen_t n = swarm_numen(p->reduced);
    r.reading = acting_now ? 0 : p->reduced;
    r.vortex = n.vortex;
    r.loop_step = n.loop_step;
    return r;
}

bool swarm_phase_check(const swarm_phase_t *p)
{
    return p->reduced == swarm_en_root(p->long_count);
}

swarm_geo_t swarm_geo_point(uint64_t k, uint32_t fib_index)
{
    swarm_geo_t g;
    uint64_t rem;
    if (fib_index < 5) fib_index = 5;
    if (fib_index > 93) fib_index = 93;
    g.n = swarm_fib(fib_index);
    (void) swarm_muldiv(k, 1, g.n, &rem);
    g.x = rem;
    (void) swarm_muldiv(g.x, swarm_fib(fib_index - 1), g.n, &rem);
    g.y = rem;
    g.layer = 0;
    return g;
}

swarm_geo_t swarm_geo_claim(const swarm_claim_t *c)
{
    swarm_geo_t g = swarm_geo_point(c->topic, (uint32_t) c->level + 5u);
    g.layer = c->space == SWARM_SPACE_POS ? 1 : c->space == SWARM_SPACE_NEG ? -1 : 0;
    return g;
}
