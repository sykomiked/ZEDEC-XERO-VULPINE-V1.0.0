#include "axiom_matrix_core.h"
#include "e8.h"
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
        case ISOMETRY_LIFT_M8: {
            /* The M5 -> M8 lift, implemented in src/e8 via the icosian
             * construction of E8 (phi -> icosahedron -> 600-cell -> icosian
             * ring -> E8, the provably optimal 8-dimensional sphere packing).
             *
             * Two things must hold for a matrix to be symmetric under it, and
             * both are checked rather than assumed:
             *
             *   1. the lift must actually BE an isometry. e8_selfcheck()
             *      recomputes the Gram matrix from the basis quaternions,
             *      proves unimodularity by exact integer elimination, confirms
             *      2I closure and the 240-root count, and verifies the phi^2
             *      shell identity. If the geometry is broken there is nothing
             *      to be symmetric under, so we answer false rather than pass.
             *   2. the form must be Hermitian, because an isometric embedding
             *      carries the form along with it — a non-Hermitian form does
             *      not survive the lift as a form. */
            if (e8_selfcheck() != 0) return false;
            for (uint64_t i = 0; i < m->size; i++) {
                if (m->entries[i] != 0.0 && conj(m->entries[i]) != m->entries[i])
                    return false;
            }
            return true;
        }

        case ISOMETRY_LIFT_M13:
        case ISOMETRY_PROJECT_BACK:
            /* NOT IMPLEMENTED, AND SAYING SO. These previously fell into a
             * `default` that ran the Hermitian test and returned true — an
             * unearned pass for a transform with no implementation, which is
             * the same class of defect as a self-check built from literals.
             * Attesting symmetry under a map we do not have is a lie, so we
             * decline. (M13 is the natural next build: cyc13_t in
             * src/sephirot already provides an exact 13-dimensional lattice
             * with a Galois group, which is what the lift would land in.) */
            return false;

        default:
            /* An unknown isometry is not a licence to guess. */
            return false;
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
