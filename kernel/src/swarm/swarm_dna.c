/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_dna.c — digital DNA. See swarm_dna.h. */
#include "swarm_dna.h"

/* splitmix64: a small, well-mixed, fully deterministic generator. */
static uint64_t mix(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

swarm_dna_t swarm_dna_from_seed(uint64_t seed) {
    swarm_dna_t d;
    uint64_t s = seed;
    uint64_t r = mix(&s);
    d.seed = seed;
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) d.gene[g] = (uint8_t)(r >> (8u * g));
    d.parent_a = d.parent_b = 0;
    d.generation = 0;
    return d;
}

static uint16_t permille(uint8_t g) { return (uint16_t)(((uint32_t)g * 1000u + 127u) / 255u); }

swarm_traits_t swarm_dna_express(const swarm_dna_t *d, uint8_t num_specialties) {
    swarm_traits_t t;
    t.home.emotion   = (swarm_emotion_t)(d->gene[SWARM_GENE_EMOTION] % SWARM_EMO_COUNT);
    t.home.intensity = (uint8_t)(d->gene[SWARM_GENE_TEMPERAMENT] % (SWARM_EMO_MAX_INTENSITY + 1u));
    t.curiosity_permille = permille(d->gene[SWARM_GENE_CURIOSITY]);
    t.caution_permille   = permille(d->gene[SWARM_GENE_CAUTION]);
    t.warmth_permille    = permille(d->gene[SWARM_GENE_WARMTH]);
    t.pace_permille      = permille(d->gene[SWARM_GENE_PACE]);
    t.verbosity_permille = permille(d->gene[SWARM_GENE_VERBOSITY]);
    t.specialty = num_specialties ? (uint8_t)(d->gene[SWARM_GENE_SPECIALTY] % num_specialties) : 0;
    return t;
}

swarm_dna_t swarm_dna_combine(const swarm_dna_t *a, const swarm_dna_t *b, uint64_t nonce) {
    uint64_t s = a->seed ^ (b->seed * 0x9E3779B97F4A7C15ull) ^ nonce;
    uint64_t mask = mix(&s);
    uint64_t mut  = mix(&s);
    swarm_dna_t c;
    c.seed = mix(&s);
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) {
        int v = ((mask >> g) & 1u) ? a->gene[g] : b->gene[g];
        int step = (int)(((uint32_t)(mut >> (8u * g)) & 0xFFu) % 7u) - 3;   /* -3 .. +3, F(4) = 3 */
        v += step;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        c.gene[g] = (uint8_t)v;
    }
    c.parent_a = a->seed;
    c.parent_b = b->seed;
    c.generation = (a->generation > b->generation ? a->generation : b->generation) + 1u;
    return c;
}

uint64_t swarm_dna_fitness(uint64_t gated_value, uint64_t social) { return gated_value + social; }

bool swarm_dna_select(const uint64_t *fitness, uint32_t n,
                      uint32_t *parent_a, uint32_t *parent_b, uint32_t *replace) {
    if (!fitness || n < 3 || !parent_a || !parent_b || !replace) return false;
    uint32_t a = 0, b = 1, w = 0;
    if (fitness[1] > fitness[0]) { a = 1; b = 0; }
    for (uint32_t i = 2; i < n; i++) {
        if (fitness[i] > fitness[a])      { b = a; a = i; }
        else if (fitness[i] > fitness[b]) { b = i; }
    }
    for (uint32_t i = 1; i < n; i++) if (fitness[i] < fitness[w]) w = i;
    if (w == a || w == b) {                 /* all tied: replace the first non-parent */
        for (uint32_t i = 0; i < n; i++) if (i != a && i != b) { w = i; break; }
    }
    *parent_a = a; *parent_b = b; *replace = w;
    return true;
}
