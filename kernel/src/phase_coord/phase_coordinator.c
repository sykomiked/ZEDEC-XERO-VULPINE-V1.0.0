/* phase_coordinator.c — Phase Coordinator Implementation */
#include "phase_coordinator.h"
#include "m5_types.h"

static exec_profile_t s_exec_profile = EXEC_DC;

static inline bool coverage_hyperbola_holds(rational_t r, trit_t ell) {
    return m5_coverage_cmp(r, ell, 9, 5) >= 0; /* r * ell >= 1.8, exact */
}

void phase_coordinator_init(phase_tick_t *tick, exec_profile_t profile) {
    tick->omega = 0;
    tick->r = (rational_t){1, 1};
    tick->ell = TRIT_TRUE;
    tick->iphi.r = 0;
    tick->iphi.i = 0;
    tick->chi = (collapse_t){{0, 0}};
    s_exec_profile = profile;
}

int phase_coordinator_tick(phase_tick_t *tick) {
    if (!coverage_hyperbola_holds(tick->r, tick->ell)) {
        return -1;
    }

    switch (s_exec_profile) {
        case EXEC_DC:
            tick->omega++;
            break;
        case EXEC_AC:
            tick->iphi.i += Q16_CONST(1, 10); /* +0.1 in Q16.16 */
            break;
        case EXEC_PC:
            tick->r = rational_normalize((rational_t){tick->r.num + 1, tick->r.den});
            break;
        default:
            return -1;
    }

    return 0;
}
