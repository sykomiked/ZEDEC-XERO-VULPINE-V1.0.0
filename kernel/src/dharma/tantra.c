/* tantra.c — Tantra: the integration engine implementation. See tantra.h. */
#include "tantra.h"

void tantra_init(tantra_engine_t *t, scheduler_t *sched, oseq_state_t *oseq) {
    dharma_init(&t->dharma, sched, oseq);
    t->sched = sched;
    t->oseq = oseq;
}

void tantra_weave(tantra_engine_t *t, uint32_t task_id, const uint8_t *seed, uint32_t len,
                   l13_dim_t container_dim, event_locality_t locality) {
    cyc13_t signal = mantra_encode(seed, len);
    yantra_topology_t container = yantra_contain(signal, container_dim);
    rational_t weight = mantra_amplitude(container.container);
    ordinal_t now = t->oseq->current_cycle;

    karma_event_t k;
    switch (locality) {
        case EVENT_OMNILOCAL:
            k = karma_cause_omnilocal(now, VEIL_AIN_SOPH_AUR, weight);
            break;
        case EVENT_POLYLOCAL:
            k = karma_cause_polylocal(1ULL << (task_id % 64), now, VEIL_AIN_SOPH_AUR, weight);
            break;
        case EVENT_MULTI_LOCAL: {
            uint32_t ids[1] = {task_id};
            k = karma_cause_multilocal(ids, 1, now, VEIL_AIN_SOPH_AUR, weight);
            break;
        }
        case EVENT_NON_LOCAL:
        case EVENT_LOCAL:
        default:
            /* NON_LOCAL needs a second task_id and is not expressible
             * through this single-task API -- callers wanting a
             * Bell/EPR-correlated weave should call karma_cause_nonlocal
             * and dharma_emit directly. Falls back to LOCAL here. */
            k = karma_cause(task_id, now, VEIL_AIN_SOPH_AUR, weight);
            break;
    }
    dharma_emit(&t->dharma, k);
}

tantra_result_t tantra_paradox_trap(const bodhi_state_t *b) {
    bool rail_a = b->coherence.num != 0;   /* "true" rail asserted */
    bool rail_b = b->dispersion.num != 0;  /* "false" rail asserted */
    if (rail_a && rail_b && !b->transcendent) {
        return TANTRA_PARADOX_TRAPPED; /* 1-1-0: both rails high, output forced clear */
    }
    return TANTRA_CLEAR;
}

tantra_result_t tantra_run(tantra_engine_t *t) {
    dharma_advance(&t->dharma);
    bodhi_state_t b = bodhi_observe(&t->dharma);
    return tantra_paradox_trap(&b);
}

/* ---- DECLARATION -----------------------------------------------------------

 * The engine that drives one dharma cycle. tantra.o's `nm -u` names ELEVEN
 * symbols across FIVE modules: bodhi_observe, dharma_{advance,emit,init},
 * karma_cause*, mantra_{amplitude,encode}, yantra_contain.
 *
 * MB_MAX_CAPS IS 4 AND THIS MODULE HAS 5 REAL EDGES. A fifth ZXV_REQUIRES
 * argument does not compile (zxv_decl.h has no ZXV_CAPS5), which is the
 * correct outcome -- modbind would otherwise read past a 4-element array. The
 * four declared are the ones that gate a cycle: without dharma_core there is
 * no set to advance, without karma nothing to cause, without mantra nothing to
 * encode, without bodhi nothing observed. THE FIFTH, DECLARED NOWHERE AND
 * WRITTEN DOWN HERE SO IT IS NOT LOST: yantra_ready (yantra_contain). It is
 * the containment boundary, and it is the one a reader is most likely to
 * assume is covered.
 */
#include "zxv_decl.h"
ZXV_DECLARE(tantra,
    ZXV_PROVIDES(tantra_ready),
    ZXV_REQUIRES(dharma_core_ready, karma_ready, mantra_ready, bodhi_ready),
    ZXV_NO_BRINGUP);
