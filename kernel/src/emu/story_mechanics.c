/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* story_mechanics.c — unify the story-bearing consoles into ONE universe. See
 * story_mechanics.h. Signatures are era/capability priors (honest: not parsed
 * plots); refined by real behaviour as each console becomes emulatable. */
#include "story_mechanics.h"
#include "chiglet.h"

/* Story-bearing consoles (Atari/arcade classics excluded — pre-story). Dims:
 * narrative_depth dialogue branching character_roster world_scale progression
 * cinematics era. */
static const story_universe_t g_consoles[] = {
    { "NES",            { SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.40),
                          SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.20) } },
    { "Game Boy",       { SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.55),
                          SR_FROM_FLOAT(0.35),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.25) } },
    { "Sega Genesis",   { SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.50),
                          SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.45),SR_FROM_FLOAT(0.40) } },
    { "PC Engine",      { SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.50),
                          SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.40) } },
    { "SNES",           { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.75),
                          SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.45) } },
    { "Game Boy Advance",{ SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.72),
                          SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.55) } },
    { "Sega Saturn",    { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.75),
                          SR_FROM_FLOAT(0.72),SR_FROM_FLOAT(0.78),SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.60) } },
    { "PlayStation",    { SR_FROM_FLOAT(0.92),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.62),SR_FROM_FLOAT(0.82),
                          SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.82),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.65) } },
    { "Nintendo 64",    { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.62),SR_FROM_FLOAT(0.62),
                          SR_FROM_FLOAT(0.92),SR_FROM_FLOAT(0.72),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.70) } },
    { "Nintendo DS",    { SR_FROM_FLOAT(0.82),SR_FROM_FLOAT(0.88),SR_FROM_FLOAT(0.72),SR_FROM_FLOAT(0.72),
                          SR_FROM_FLOAT(0.62),SR_FROM_FLOAT(0.82),SR_FROM_FLOAT(0.62),SR_FROM_FLOAT(0.78) } },
    { "PSP",            { SR_FROM_FLOAT(0.86),SR_FROM_FLOAT(0.86),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.76),
                          SR_FROM_FLOAT(0.72),SR_FROM_FLOAT(0.82),SR_FROM_FLOAT(0.88),SR_FROM_FLOAT(0.80) } },
    { "PlayStation 2",  { SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.92),SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.85),
                          SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.98),SR_FROM_FLOAT(0.85) } },
};
#define N_CONSOLES ((int)(sizeof g_consoles / sizeof g_consoles[0]))

const story_universe_t *story_console_table(int *count_out){
    if (count_out) *count_out = N_CONSOLES;
    return g_consoles;
}

void story_unify(const story_universe_t *u, uint32_t k, story_unified_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    out->total = k;
    if (k == 0) return;
    /* pack signatures for the ISF distinct-count */
    static surplus_real_t ev[CHG_MAX_EXPERTS][SM_DIM];
    uint32_t kk = k > CHG_MAX_EXPERTS ? CHG_MAX_EXPERTS : k;
    for (uint32_t i = 0; i < kk; i++)
        for (int d = 0; d < SM_DIM; d++) ev[i][d] = u[i].sig[d];
    uint32_t distinct = 0;
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev, kk, SM_DIM, &distinct);
    out->distinct_archetypes = distinct;
    /* coherence = mean pairwise relationship over ALL k (the social interaction
     * graph). A finite, non-zero mean over every pair = one connected universe. */
    surplus_real_t sum = SR_ZERO; uint32_t pairs = 0;
    for (uint32_t i = 0; i < k; i++)
        for (uint32_t j = i + 1; j < k; j++){
            sum = SR_ADD(sum, chg_interaction(u[i].sig, u[j].sig, SM_DIM));
            pairs++;
        }
    out->coherence = pairs ? SR_DIV(sum, SR_FROM_INT((int64_t)pairs)) : SR_ZERO;
    /* Unified iff the diversity survived (>=3 distinct archetypes) AND every
     * universe is connected (coherence strictly between 0 and 1 — related, not
     * identical, not disjoint). */
    int diverse   = (distinct >= 3);
    int connected = (out->coherence > SR_ZERO) && (out->coherence < SR_ONE);
    out->unified  = (diverse && connected) ? 1 : 0;
}

int story_mechanics_selfcheck(uint32_t *distinct_out, uint32_t *coherence_permille_out){
    /* A diverse span across eras: NES(0), Game Boy(1), SNES(4), N64(8),
     * Nintendo DS(9), PlayStation(7) — 8-bit through 3D/CD, home + handheld. */
    story_universe_t sel[6] = {
        g_consoles[0], g_consoles[1], g_consoles[4], g_consoles[8], g_consoles[9], g_consoles[7]
    };
    story_unified_t r;
    story_unify(sel, 6u, &r);
    if (distinct_out) *distinct_out = r.distinct_archetypes;
    if (coherence_permille_out)
        *coherence_permille_out = (uint32_t)((double)r.coherence / (double)SR_ONE * 1000.0);
    return r.unified;
}
