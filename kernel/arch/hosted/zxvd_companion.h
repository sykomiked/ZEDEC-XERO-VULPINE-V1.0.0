/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_companion.h — companion.*: the person's Chiglet companion (the swarm's
 * agent 0, level 0) as zxvd's swarm holds it, read over the socket.
 *
 *   H1  The group (zxvd_companion.c, zxvd_groups.h) has the feature
 *       "companion", is not regulated, and every method is marked `always`
 *       (zxvd_ext.h X4): they move no money and answer with the switch off.
 *   H2  The host hands the group a snapshot function. Each call takes a
 *       fresh snapshot of the companion as the swarm holds it at that
 *       moment; nothing is cached or kept by the group.
 *   H3  companion.dna: the seed (D1, a decimal string), the 8 genes by name
 *       (swarm_gene_t), the expressed traits (D2, swarm_traits_t), the
 *       parents' seeds and the generation (D5).
 *   H4  companion.generation: the generation (D5) and how the companion got
 *       there in this run: its steps (swarm_evolve.h V7), the GROWTH
 *       harmonics seen and the fundamental of its last step.
 *   H5  companion.mood: the companion's current feeling in the emotional
 *       economy (swarm_emotion.h: the emoji, its intensity 0..5, the
 *       Fibonacci charge and the valence), its home feeling from DNA (D2)
 *       and the swarm's cycle mood (E1).
 *   H6  Errors: ZXVD_E_COMPANION (-32070) when there is no companion to
 *       read (no snapshot function, or the swarm has no agent yet).
 * Integers only.
 */
#ifndef ZXVD_COMPANION_H
#define ZXVD_COMPANION_H

#include <stdbool.h>
#include <stdint.h>

#include "swarm_dna.h"

#define ZXVD_E_COMPANION (-32070) /* H6 */

typedef struct {
    uint32_t agent_id;
    swarm_dna_t dna;
    swarm_feeling_t feeling;      /* H5: now, in the emotional economy */
    swarm_feeling_t mood;         /* H5: the swarm's cycle mood */
    const char *const *specialty; /* the host's specialty names; NULL = none */
    uint8_t num_specialties;
    uint64_t steps;     /* H4: V7 steps this run */
    uint64_t growths;   /* H4: GROWTH harmonics (fundamentals) this run */
    uint64_t last_step; /* H4: the fundamental of the last step, 0 = none */
} zxvd_companion_t;

/* H2: fn fills *out and returns true, or false when there is no companion.
 * NULL unsets it. */
void zxvd_companion_set_source(bool (*fn)(zxvd_companion_t *out));

#endif
