/* bodhi.c — Bodhi: transcendent observation over a Dharma set. See bodhi.h.
 *
 * The empty set (|D| == 0) is never treated as null, void-as-absence,
 * or a zeroed-out "nothing" struct. It is the VACUUM: a definite,
 * present ground state whose structure comes from the ring's own
 * boundary geometry (DHARMA_RING_SIZE, L13_NUM_PHASES) -- exactly as
 * the Casimir effect's zero-point pressure comes from the boundary
 * conditions (plate separation) rather than from any particle between
 * the plates. There is no karma here either, and there is still a
 * measurable, nonzero, structurally-grounded presence: 1/(ring_size *
 * num_phases), the same inverse-power-law DEPENDENCE ON GEOMETRY that
 * gives Casimir pressure its functional shape (~1/d^4 in plate
 * separation d), even though no physical constants are being modeled
 * here -- only the structural form.
 */
#include "bodhi.h"
#include "rmag_core.h"
#include "uvn.h"

/* Exact-rational "a > b" via cross multiplication, never a double
 * comparison -- both a and b are assumed already-normalized (den>0),
 * this codebase's rational_normalize() convention. */
static bool rational_gt_exact(rational_t a, rational_t b) {
    return a.num * b.den > b.num * a.den;
}

bodhi_state_t bodhi_observe(const dharma_set_t *d) {
    bodhi_state_t b;
    b.superposition = cyc13_zero();

    if (d->count == 0) {
        rational_t vacuum = (rational_t){1, (int64_t)DHARMA_RING_SIZE * (int64_t)L13_NUM_PHASES};
        b.superposition = cyc13_scale(cyc13_basis(0), vacuum);
        b.coherence = vacuum;
        b.dispersion = (rational_t){0, 1};
        b.transcendent = true; /* the vacuum IS the source: coherent by construction, not by accumulation */
        return b;
    }

    for (uint32_t i = 0; i < d->count; i++) {
        uint32_t idx = (d->head + i) % DHARMA_RING_SIZE;
        const karma_event_t *k = &d->ring[idx];
        if (!k->resolved) continue;
        cyc13_t contribution = cyc13_scale(cyc13_from_phase(k->effect_phase), k->weight);
        b.superposition = cyc13_add(b.superposition, contribution);
    }

    /* cyc13 index 0 is zeta^0 = phase 13 (Ain Soph Aur), the source
     * component (see cyc13_from_phase's own documentation). */
    b.coherence = b.superposition.c[0];
    rational_t disp = (rational_t){0, 1};
    for (int i = 1; i < L13_NUM_PHASES; i++) {
        disp = rmag_add_quotas(disp, rational_abs_exact(b.superposition.c[i]));
    }
    b.dispersion = disp;
    b.transcendent = rational_gt_exact(rational_abs_exact(b.coherence), rational_abs_exact(b.dispersion));
    return b;
}
