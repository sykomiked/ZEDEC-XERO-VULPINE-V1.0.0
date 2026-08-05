#include "axiom_matrix_core.h"
#include <stdlib.h>
#include <string.h>

void axiom_matrix_set(axiom_matrix_t *m, ordinal_t ord, rational_t rat,
                      trit_t trit, phase_t phase, collapse_t collapse,
                      double complex value) {
    if (!m || m->size == 0) return;
    uint64_t idx = axiom_matrix_hash(ord, rat, trit, phase, collapse) % m->size;
    m->entries[idx] = value;
}

double complex axiom_matrix_get(const axiom_matrix_t *m, ordinal_t ord,
                                rational_t rat, trit_t trit, phase_t phase,
                                collapse_t collapse) {
    if (!m || m->size == 0) return 0.0;
    uint64_t idx = axiom_matrix_hash(ord, rat, trit, phase, collapse) % m->size;
    return m->entries[idx];
}

bool axiom_matrix_is_symmetric(const axiom_matrix_t *m, isometry_id_t isometry) {
    if (!m || m->size == 0) return true;
    switch (isometry) {
        case ISOMETRY_IDENTITY:
            for (uint64_t i = 0; i < m->size; i++) {
                if (m->entries[i] != 0.0 && conj(m->entries[i]) != m->entries[i])
                    return false;
            }
            return true;
        case ISOMETRY_SWAP_R_L:
            for (uint64_t i = 0; i < m->size / 2; i++) {
                if (m->entries[i] != m->entries[m->size - 1 - i])
                    return false;
            }
            return true;
        case ISOMETRY_NEGATE_PHASE:
            for (uint64_t i = 0; i < m->size; i++) {
                if (m->entries[i] != 0.0 && cimag(m->entries[i]) != 0.0)
                    return false;
            }
            return true;
        default:
            for (uint64_t i = 0; i < m->size; i++) {
                if (m->entries[i] != 0.0 && conj(m->entries[i]) != m->entries[i])
                    return false;
            }
            return true;
    }
}

double complex axiom_matrix_project_tick(const axiom_matrix_t *m, ordinal_t tick) {
    if (!m || m->size == 0) return 0.0;
    double complex sum = 0.0;
    for (uint64_t i = 0; i < m->size; i++) {
        sum += m->entries[i];
    }
    (void)tick;
    return sum;
}
