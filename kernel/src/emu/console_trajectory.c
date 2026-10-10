/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* console_trajectory.c — our own console-lineage generations, each named for its
 * INNOVATION (proprietary, descriptive). Real consoles appear only as `ref:`
 * DEVELOPMENT references, never as shipped names. See console_trajectory.h.
 * Signatures are capability priors (honest), refined as each becomes emulatable. */
#include "console_trajectory.h"
#include "chiglet.h"

/* Dims: playable_ui spatial_outer inner_space hybrid_form network_play
 *       productivity immersion era.  Ordered by era (ascending) so adjacent
 *       entries are consecutive generations. nintendo=1 marks our lineage that
 *       mirrors the always-innovate-yet-continuous house (ref: Nintendo). */
static const console_gen_t g_gens[] = {
 /* ref: NES / 8-bit home origin */
 { "Hearth",  { SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.10),
                SR_FROM_FLOAT(0.05),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.15) }, 1 },
 /* ref: SNES / 16-bit richer worlds */
 { "Loom",    { SR_FROM_FLOAT(0.65),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.10),
                SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.30) }, 1 },
 /* ref: N64 / first 3D spatial home */
 { "Pillar",  { SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.10),
                SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.45) }, 1 },
 /* ref: PS2-online / early online console play — networking begins */
 { "Relay",   { SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.15),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.10),
                SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.55) }, 0 },
 /* ref: PS3 / Xbox 360 — HD networked generation */
 { "MeshHD",  { SR_FROM_FLOAT(0.65),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.25),SR_FROM_FLOAT(0.10),
                SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.65) }, 0 },
 /* ref: Wii — outer/spatial: sensing body, room, world */
 { "Aura",    { SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.20),
                SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.35),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.68) }, 1 },
 /* ref: Wii U — inner space: the private/second screen turns inward */
 { "Mirror",  { SR_FROM_FLOAT(0.82),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.30),
                SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.62),SR_FROM_FLOAT(0.78) }, 1 },
 /* ref: PS4 / Xbox One / Steam-style — social + creation + storefront network */
 { "Forge",   { SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.25),SR_FROM_FLOAT(0.35),SR_FROM_FLOAT(0.20),
                SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.80) }, 0 },
 /* ref: Switch — hybrid form: portable<->docked, context-shifting */
 { "Chimera", { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.95),
                SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.55),SR_FROM_FLOAT(0.72),SR_FROM_FLOAT(0.85) }, 1 },
 /* ref: PS5 / Xbox Series — current high-immersion networked */
 { "Prism",   { SR_FROM_FLOAT(0.75),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.30),
                SR_FROM_FLOAT(0.92),SR_FROM_FLOAT(0.75),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.92) }, 0 },
 /* ref: PS6-class up-and-coming — beyond, network + immersion maxed */
 { "Aether",  { SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.60),
                SR_FROM_FLOAT(0.98),SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.98),SR_FROM_FLOAT(1.00) }, 0 },
};
#define N_GENS ((int)(sizeof g_gens / sizeof g_gens[0]))

const console_gen_t *console_gen_table(int *count_out){
    if (count_out) *count_out = N_GENS;
    return g_gens;
}

void console_trajectory_assess(const console_gen_t *g, uint32_t n, trajectory_result_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    out->total = n;
    if (n == 0) return;

    /* Diversity: distinct innovation archetypes (ISF distinct count). */
    static surplus_real_t ev[CHG_MAX_EXPERTS][CT_DIM];
    uint32_t kk = n > CHG_MAX_EXPERTS ? CHG_MAX_EXPERTS : n;
    for (uint32_t i = 0; i < kk; i++)
        for (int d = 0; d < CT_DIM; d++) ev[i][d] = g[i].sig[d];
    uint32_t distinct = 0;
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev, kk, CT_DIM, &distinct);
    out->distinct_archetypes = distinct;

    /* Continuity: mean relationship between ADJACENT (consecutive) generations.
     * chg_interaction ~0 = near-identical, ~1 = orthogonal jump. A lineage means
     * neighbours are RELATED (more alike than orthogonal) yet MOVING (not clones):
     * 0 < mean < 0.5. That is the honest "innovate every gen, one trajectory" test. */
    surplus_real_t sum = SR_ZERO; uint32_t pairs = 0;
    for (uint32_t i = 0; i + 1 < n; i++){
        sum = SR_ADD(sum, chg_interaction(g[i].sig, g[i+1].sig, CT_DIM));
        pairs++;
    }
    out->continuity = pairs ? SR_DIV(sum, SR_FROM_INT((int64_t)pairs)) : SR_ZERO;

    int diverse    = (distinct >= 3);
    int continuous = (out->continuity > SR_ZERO) && (out->continuity < SR_FROM_FLOAT(0.5));
    out->coherent  = (diverse && continuous) ? 1 : 0;
}

int console_trajectory_selfcheck(uint32_t *distinct_out, uint32_t *nintendo_continuity_permille_out){
    trajectory_result_t field;
    console_trajectory_assess(g_gens, (uint32_t)N_GENS, &field);
    if (distinct_out) *distinct_out = field.distinct_archetypes;

    /* Nintendo lineage subsequence (already era-ordered within the field). */
    static console_gen_t nin[N_GENS];
    uint32_t nn = 0;
    for (int i = 0; i < N_GENS; i++) if (g_gens[i].nintendo) nin[nn++] = g_gens[i];
    trajectory_result_t lineage;
    console_trajectory_assess(nin, nn, &lineage);
    if (nintendo_continuity_permille_out)
        *nintendo_continuity_permille_out = SR_TO_PERMILLE(lineage.continuity);

    /* PASS: the whole field is diverse AND continuous, and the always-innovate
     * lineage is itself coherent — distinct mechanics on one continuous path. */
    return (field.coherent && lineage.coherent) ? 1 : 0;
}
