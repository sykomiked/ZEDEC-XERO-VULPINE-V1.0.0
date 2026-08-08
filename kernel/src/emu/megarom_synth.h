/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* megarom_synth.h — synthesize the MegaROM from the whole corpus: mechanics AND
 * stories from all 144k+ games, presented beyond their generation (beyond-PS6
 * holographic graphics). See megarom_synth.c. */
#ifndef ZXV_MEGAROM_SYNTH_H
#define ZXV_MEGAROM_SYNTH_H

#include <stdint.h>
#include "game_universe.h"   /* GU_DIM + surplus_real_t */

typedef struct megarom_profile {
    uint32_t total;                 /* game universes considered                  */
    uint32_t mechanic_contributors; /* games contributing mechanics (all runnable) */
    uint32_t distinct_mechanics;    /* DISTINCT mechanic-universes after ISF dedup  */
    uint32_t story_contributors;    /* games carrying narrative (story-capable)     */
    uint8_t  render_beyond_ps6;     /* 1 = presented above source gen (holo engine) */
} megarom_profile_t;

/* Is this game universe story-capable (NES/SNES-era+ narrative) vs mechanics-only? */
int  gu_story_capable(const surplus_real_t v[GU_DIM]);

/* Combine k game universes into the MegaROM's synthesis profile. */
void megarom_synthesize(const surplus_real_t ev[][GU_DIM], uint32_t k,
                        megarom_profile_t *out);

/* On-target self-check. Returns 1 on pass; reports distinct + story counts. */
int  megarom_synth_selfcheck(uint32_t *distinct_out, uint32_t *stories_out);

#endif /* ZXV_MEGAROM_SYNTH_H */
