/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_harmonic.c — harmonic cycles. See swarm_harmonic.h. */
#include "swarm_harmonic.h"
#include "swarm_budget.h"

uint32_t swarm_harmonic_period(uint32_t n) {
    if (n == 0 || n > SWARM_HARMONICS) return 0;
    return SWARM_FUNDAMENTAL_TICKS / n;          /* 32-bit, exact for 1 .. 11 */
}

uint32_t swarm_harmonics_due(uint64_t tick) {
    uint32_t mask = 0;
    for (uint32_t n = 1; n <= SWARM_HARMONICS; n++) {
        uint64_t r;
        swarm_muldiv(tick, 1, swarm_harmonic_period(n), &r);   /* tick mod period */
        if (r == 0) mask |= 1u << (n - 1u);
    }
    return mask;
}

swarm_band_t swarm_harmonic_band(uint32_t n) {
    if (n == 0 || n > SWARM_HARMONICS) return SWARM_BAND_NONE;
    if (n <= 3) return SWARM_BAND_GROWTH;
    if (n <= 8) return SWARM_BAND_THOUGHT;
    return SWARM_BAND_REFLEX;
}
