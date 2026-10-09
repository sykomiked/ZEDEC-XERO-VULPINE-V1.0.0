/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_governor.c — self-aware compute. See swarm_governor.h. */
#include "swarm_governor.h"
#include "swarm_budget.h"

static uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }

swarm_compute_budget_t swarm_governor_budget(const swarm_hw_scan_t *hw, uint64_t agent_mb,
                                             uint64_t tokens_per_core) {
    swarm_compute_budget_t g = { 0, 0, 0, 0, 0, 0 };
    if (!hw || hw->cores == 0 || hw->mem_total_mb == 0) return g;

    uint64_t cores_milli = (uint64_t)hw->cores * 1000u;
    if (hw->remote) {                                                        /* G1 */
        g.cores_milli = (uint32_t)swarm_muldiv(cores_milli, 99, 100, 0);
        g.mem_mb      = swarm_muldiv(hw->mem_total_mb, 99, 100, 0);
        g.gpu_mem_mb  = swarm_muldiv(hw->gpu_mem_mb, 99, 100, 0);
    } else {                                                                 /* G2 */
        uint64_t free_cores = cores_milli > hw->other_load_milli
                                  ? cores_milli - hw->other_load_milli : 0;
        g.cores_milli = (uint32_t)min_u64(swarm_muldiv(free_cores, 13, 21, 0),
                                          swarm_muldiv(cores_milli, 13, 21, 0));
        uint64_t floor_mb = swarm_muldiv(hw->mem_total_mb, 1, 8, 0);         /* G3 */
        uint64_t usable   = hw->mem_free_mb > floor_mb ? hw->mem_free_mb - floor_mb : 0;
        g.mem_mb     = min_u64(swarm_muldiv(usable, 13, 21, 0),
                               swarm_muldiv(hw->mem_total_mb, 13, 21, 0));
        g.gpu_mem_mb = swarm_muldiv(hw->gpu_mem_mb, 13, 21, 0);
    }

    /* G4: whole Fibonacci levels while memory covers them. */
    uint64_t used = 0;
    if (agent_mb > 0) {
        for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) {
            uint64_t cost = (uint64_t)swarm_level_capacity(d) * agent_mb;
            if (used + cost > g.mem_mb) break;
            used += cost;
            g.num_levels++;
            g.num_agents += swarm_level_capacity(d);
        }
    }
    /* G5 */
    g.tokens_per_cycle = g.num_levels ? swarm_muldiv(g.cores_milli, tokens_per_core, 1000, 0) : 0;
    if (g.tokens_per_cycle > SWARM_MAX_TOKENS_PER_CYCLE) g.tokens_per_cycle = SWARM_MAX_TOKENS_PER_CYCLE;
    return g;
}
