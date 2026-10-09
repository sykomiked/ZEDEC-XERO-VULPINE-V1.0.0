/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* story_mechanics.h — STORY as a logic problem: take the story-bearing consoles
 * (the ones with narrative — NES/SNES/N64 and the handhelds and beyond; the
 * pre-story Atari/arcade classics are excluded) and prove the M5/Chiglet/Sutra
 * logic can hold their DIVERSITY and UNIFY them into ONE coherent universe.
 *
 * Each story-console/game is an 8-dim STORY-MECHANICS signature — how it tells a
 * story, not the plot itself (the plots need per-game content parsing / external
 * data, a later input). The dims:
 *   0 narrative_depth   1 dialogue      2 branching      3 character_roster
 *   4 world_scale       5 progression   6 cinematics     7 era
 * character_roster is the SOCIAL axis (how many characters relate). era ranges
 * 8-bit -> 64-bit and beyond.
 *
 * The unifier runs Chiglet's ISF over the set: it keeps the DISTINCT story
 * archetypes (diversity preserved) and measures the mean pairwise RELATIONSHIP
 * (the social interaction between universes) — a connected relationship graph is
 * the single unified universe. This is the logic test, honest about its input. */
#ifndef ZXV_STORY_MECHANICS_H
#define ZXV_STORY_MECHANICS_H

#include <stdint.h>
#include "surplus.h"

#define SM_DIM 8   /* == CHG_DIM */

/* A story-bearing console/game as a signature + identity. */
typedef struct story_universe {
    const char    *name;
    surplus_real_t sig[SM_DIM];
} story_universe_t;

typedef struct story_unified {
    uint32_t total;             /* story-universes considered                 */
    uint32_t distinct_archetypes;/* distinct story-mechanic archetypes (ISF)   */
    surplus_real_t coherence;   /* mean pairwise relationship (0..1); >0 => one
                                 * connected universe, not disjoint islands    */
    uint8_t  unified;           /* 1 = diversity held AND connected            */
} story_unified_t;

/* The built-in table of story-bearing consoles (era-prior signatures). */
const story_universe_t *story_console_table(int *count_out);

/* Unify k story-universes: distinct archetypes + coherence -> one universe. */
void story_unify(const story_universe_t *u, uint32_t k, story_unified_t *out);

/* On-target self-check: unify a diverse span of story consoles (NES..N64..
 * handhelds). Returns 1 if the logic holds the diversity AND unifies it. */
int  story_mechanics_selfcheck(uint32_t *distinct_out, uint32_t *coherence_permille_out);

#endif /* ZXV_STORY_MECHANICS_H */
