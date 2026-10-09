/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_dna.h — digital DNA: the seed that sets an agent's personality.
 *
 *   D1  GENOME.  Every agent (and every instance, as the DNA of its
 *       companion) carries 8 genes, one byte each, grown deterministically
 *       from a 64-bit seed. Same seed, same agent, on every machine.
 *   D2  EXPRESSION.  Genes are read as traits: home emotion, temperament
 *       (resting intensity), curiosity, caution, warmth, pace, verbosity
 *       and specialty. Traits only shape HOW an agent works; they never
 *       override the quality gate or the truth rules.
 *   D3  COMBINATION.  Two agents' DNA combine into a child: each gene comes
 *       from one parent, chosen by a mask grown from both seeds, and a
 *       gene may mutate by at most +/-F(4) = 3 (a small, bounded step).
 *   D4  SELECTION.  Fitness is what the market already measures: gated
 *       value earned plus Social capital (cooperation). On the GROWTH
 *       harmonic the fittest pair breeds and the child replaces the least
 *       fit agent of the same level, so the swarm improves as it is used.
 *   D5  LINEAGE.  Every genome records its parents' seeds and generation,
 *       so any change can be traced and rolled back.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_DNA_H
#define SWARM_DNA_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_emotion.h"

#define SWARM_DNA_GENES 8u

typedef enum {
    SWARM_GENE_EMOTION = 0, /* home emotion */
    SWARM_GENE_TEMPERAMENT, /* resting intensity */
    SWARM_GENE_CURIOSITY,
    SWARM_GENE_CAUTION,
    SWARM_GENE_WARMTH,
    SWARM_GENE_PACE,
    SWARM_GENE_VERBOSITY,
    SWARM_GENE_SPECIALTY
} swarm_gene_t;

typedef struct {
    uint64_t seed;
    uint8_t gene[SWARM_DNA_GENES];
    uint64_t parent_a, parent_b; /* D5: 0 for a founder */
    uint32_t generation;
} swarm_dna_t;

typedef struct {
    swarm_feeling_t home;        /* resting feeling */
    uint16_t curiosity_permille; /* 0 .. 1000 */
    uint16_t caution_permille;
    uint16_t warmth_permille;
    uint16_t pace_permille;
    uint16_t verbosity_permille;
    uint8_t specialty; /* index into the host's specialty table */
} swarm_traits_t;

/* D1 */
swarm_dna_t swarm_dna_from_seed(uint64_t seed);

/* D2 */
swarm_traits_t swarm_dna_express(const swarm_dna_t *d, uint8_t num_specialties);

/* D3: child of a and b; `nonce` makes repeated pairings differ. */
swarm_dna_t swarm_dna_combine(const swarm_dna_t *a, const swarm_dna_t *b, uint64_t nonce);

/* D4: fitness from market results. */
uint64_t swarm_dna_fitness(uint64_t gated_value, uint64_t social);

/* D4: pick parents (two fittest) and the slot to replace (least fit) among
 * n candidates. Ties go to the lower index. Returns false if n < 3. */
bool swarm_dna_select(const uint64_t *fitness, uint32_t n, uint32_t *parent_a, uint32_t *parent_b,
                      uint32_t *replace);

#endif /* SWARM_DNA_H */
