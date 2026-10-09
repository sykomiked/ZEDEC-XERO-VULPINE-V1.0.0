/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_quality.c — the quality gate. See swarm_quality.h. */
#include "swarm_quality.h"

bool swarm_quality_passes(uint32_t r_milli, uint32_t l_milli) {
    if (l_milli > 1000u) return false;               /* a share cannot exceed 1 */
    return (uint64_t)r_milli * l_milli >= SWARM_Q_GATE_MILLI2;
}

uint32_t swarm_quality_max_passes(uint32_t num_levels) {
    if (num_levels == 0 || num_levels > SWARM_MAX_LEVELS) return 0;
    return (uint32_t)swarm_fib(num_levels + 2u);
}

swarm_q_action_t swarm_quality_next(uint32_t r_milli, uint32_t l_milli,
                                    uint32_t passes_done, uint32_t num_levels) {
    if (swarm_quality_passes(r_milli, l_milli)) return SWARM_Q_PRESENT;
    if (passes_done < swarm_quality_max_passes(num_levels)) return SWARM_Q_REVISE;
    return SWARM_Q_PRESENT_FLAGGED;
}

swarm_status_t swarm_quality_credit(swarm_market_t *m, uint32_t model_id, swarm_cap_t form,
                                    uint64_t amount, uint32_t r_milli, uint32_t l_milli) {
    if (!swarm_quality_passes(r_milli, l_milli)) return SWARM_ERR_ARG;
    return swarm_market_credit(m, model_id, form, amount);
}
