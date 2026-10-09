/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* console_trajectory.h — every console generation innovates a NEW signature
 * mechanic, yet (Nintendo especially) builds along ONE continuous trajectory.
 * This is the LOGIC problem the user posed: capture each generation's defining
 * innovation as a vector and prove they form a coherent path, not scattered
 * points — a single evolving lineage the MegaROM inherits from.
 *
 * The mechanic-innovation axes (8-dim, == CHG_DIM):
 *   0 playable_ui      the interface-as-play computers lack (all games > 0)
 *   1 spatial_outer    body/room/world sensing — the Wii's motion+space axis
 *   2 inner_space      the player's private/second screen — Wii U's inward turn
 *   3 hybrid_form      portable<->docked, context-shifting form — the Switch
 *   4 network_play     online/distributed multiplayer — PS2-online .. PS6, Xbox, Steam
 *   5 productivity     work-together capacity (Second Life's promise), not just co-play
 *   6 immersion        fidelity/holographic depth toward beyond-PS6
 *   7 era              8-bit .. current, the ordinal trajectory parameter
 *
 * Two verdicts, both honest about the input (these are capability priors, refined
 * as each console becomes emulatable — same discipline as story_mechanics):
 *   - DIVERSITY: distinct innovation archetypes survive (ISF distinct count)
 *   - CONTINUITY: consecutive generations (sorted by era) are RELATED, not jumps
 *     — mean adjacent-pair relationship strictly in (0,1) = one continuous path.
 * Nintendo's own subsequence is checked separately: it should be at least as
 * continuous as the field (they innovate hard but never break their lineage). */
#ifndef ZXV_CONSOLE_TRAJECTORY_H
#define ZXV_CONSOLE_TRAJECTORY_H

#include <stdint.h>
#include "surplus.h"

#define CT_DIM 8   /* == CHG_DIM */

typedef struct console_gen {
    const char    *name;
    surplus_real_t sig[CT_DIM];
    uint8_t        nintendo;   /* 1 if part of the Nintendo lineage */
} console_gen_t;

typedef struct trajectory_result {
    uint32_t       total;
    uint32_t       distinct_archetypes;  /* innovation diversity (ISF)          */
    surplus_real_t continuity;           /* mean adjacent-gen relationship (0..1)*/
    uint8_t        coherent;             /* 1 = diverse AND continuous           */
} trajectory_result_t;

/* The built-in generation table (era-prior innovation signatures). */
const console_gen_t *console_gen_table(int *count_out);

/* Assess a trajectory: the gens must already be in era order. distinct archetypes
 * + mean adjacent-pair continuity -> one continuous, diverse lineage. */
void console_trajectory_assess(const console_gen_t *g, uint32_t n, trajectory_result_t *out);

/* On-target self-check: assess the full field AND the Nintendo subsequence.
 * Returns 1 if both are coherent (diverse innovation on a continuous path).
 * Outputs: full-field distinct archetypes, and Nintendo-lineage continuity permille. */
int console_trajectory_selfcheck(uint32_t *distinct_out, uint32_t *nintendo_continuity_permille_out);

#endif /* ZXV_CONSOLE_TRAJECTORY_H */
