/*
 * axiom_matrix_core.h — Axiom Matrix Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef AXIOM_MATRIX_CORE_H
#define AXIOM_MATRIX_CORE_H

#include "m5_types.h"

#define AXIOM_MATRIX_HASH_SIZE 1048576
#define AXIOM_MATRIX_HASH_MASK (AXIOM_MATRIX_HASH_SIZE - 1)

typedef struct axiom_matrix_entry {
    ordinal_t ord;
    rational_t rat;
    trit_t trit;
    phase_t phase;
    collapse_t collapse;
    zxv_cq16_t value; /* Q16.16 complex */
} axiom_matrix_entry_t;

static inline uint64_t axiom_matrix_hash(ordinal_t ord, rational_t rat, trit_t trit, phase_t phase, collapse_t collapse) {
    uint64_t hash = (uint64_t)ord;
    hash = (hash << 32) | (uint64_t)rat.num;
    hash = (hash << 16) | (uint64_t)rat.den;
    hash = (hash << 3) | (uint64_t)trit;
    hash = (hash << 2) | (uint64_t) (phase.r >> Q16_SHIFT); /* integer part of the Q16.16 phase */
    hash = (hash << 1) | (uint64_t)(phase.i > 0);
    hash = (hash << 5) | (uint64_t)collapse.bits[0];
    hash = (hash << 5) | (uint64_t)collapse.bits[1];
    return hash & AXIOM_MATRIX_HASH_MASK;
}

void axiom_matrix_set(axiom_matrix_t *m, ordinal_t ord, rational_t rat, trit_t trit, phase_t phase,
                      collapse_t collapse, zxv_cq16_t value);
zxv_cq16_t axiom_matrix_get(const axiom_matrix_t *m, ordinal_t ord, rational_t rat, trit_t trit,
                            phase_t phase, collapse_t collapse);
bool axiom_matrix_is_symmetric(const axiom_matrix_t *m, isometry_id_t isometry);
zxv_cq16_t axiom_matrix_project_tick(const axiom_matrix_t *m, ordinal_t tick);

#endif