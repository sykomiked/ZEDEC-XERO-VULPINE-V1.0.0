/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* megarom_synth.c — synthesize the MegaROM from the whole corpus: its MECHANICS
 * AND STORIES are drawn from all 144k+ games, but the presentation goes far
 * beyond those generations (beyond-PS6 holographic graphics — content decoupled
 * from source generation).
 *
 * Two contributions from every game, separated honestly:
 *   MECHANICS — every runnable game contributes; Chiglet's ISF collapses the
 *     redundant ones so the MegaROM keeps the DISTINCT best (distinct_mechanics).
 *   STORIES — only the story-capable games carry narrative. Older Atari/arcade
 *     titles are mechanics-only; a game is story-capable when it has substantial
 *     CONTENT (ROM size) AND structured presentation (display programming and/or
 *     an interrupt-driven flow — cutscenes/dialogue need both). This mirrors the
 *     real history: NES/SNES-era and beyond carry stories; the classics do not.
 *
 * Honest scope: this classifies + counts the contributors and sets the render
 * target. Actually SYNTHESIZING a combined narrative from the story games is
 * Chiglet's job (Phase B, via Sutra) — not fabricated here. */
#include "megarom_synth.h"
#include "chiglet.h"

int gu_story_capable(const surplus_real_t v[GU_DIM]){
    /* content substantial AND (display programmed OR interrupt-driven) */
    /* '>' works on both host (double) and target (int64 Q32.32) surplus_real_t */
    int content   = (v[5] > SR_FROM_FLOAT(0.40));
    int display   = (v[2] > SR_FROM_FLOAT(0.40));
    int interrupt = (v[4] > SR_FROM_FLOAT(0.40));
    return (content && (display || interrupt)) ? 1 : 0;
}

void megarom_synthesize(const surplus_real_t ev[][GU_DIM], uint32_t k,
                        megarom_profile_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    out->total = k;
    /* MECHANICS: every game contributes; Chiglet keeps the DISTINCT ones. */
    out->mechanic_contributors = k;
    uint32_t distinct = 0;
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev, k, GU_DIM, &distinct);
    out->distinct_mechanics = distinct;
    /* STORIES: only the story-capable games carry narrative. */
    for (uint32_t i = 0; i < k; i++)
        if (gu_story_capable(ev[i])) out->story_contributors++;
    /* PRESENTATION: rendered above the source generation — the holographic,
     * nonlinear engine (holo_shade), toward beyond-PS6 fidelity. */
    out->render_beyond_ps6 = 1;
}

/* Self-check: a mix of mechanics-only classics (low content) and a story-era
 * RPG (high content + display + interrupt). All contribute mechanics; only the
 * RPG contributes a story; the render target is beyond-PS6. Returns 1 on pass. */
int megarom_synth_selfcheck(uint32_t *distinct_out, uint32_t *stories_out){
    surplus_real_t ev[3][GU_DIM] = {
        /* maze (classic, mechanics-only): tiny content */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.8),
          SR_FROM_FLOAT(0.2),SR_FROM_FLOAT(0.1),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.3) },
        /* shooter (classic, mechanics-only): low content */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.2),
          SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.3),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.7) },
        /* RPG (story-era): big content + display + interrupt-driven */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.3),
          SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.3) },
    };
    megarom_profile_t p;
    megarom_synthesize((const surplus_real_t (*)[GU_DIM])ev, 3u, &p);
    if (distinct_out) *distinct_out = p.distinct_mechanics;
    if (stories_out)  *stories_out  = p.story_contributors;
    /* all 3 contribute mechanics; only the RPG is a story; render beyond-PS6 */
    return (p.mechanic_contributors == 3 &&
            p.distinct_mechanics    == 3 &&
            p.story_contributors    == 1 &&
            p.render_beyond_ps6     == 1) ? 1 : 0;
}
