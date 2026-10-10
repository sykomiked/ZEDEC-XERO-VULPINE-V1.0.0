/* test_phase_coordinator.c — Phase Coordinator Tests */
#include <assert.h>
#include <stdio.h>
#include "phase_coordinator.h"
#include "m5_types.h"

static void test_coverage_hyperbola(void)
{
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);

    tick.r = (rational_t){2, 1};
    tick.ell = TRIT_TRUE;
    assert(phase_coordinator_tick(&tick) == 0);

    tick.r = (rational_t){1, 2};
    tick.ell = TRIT_TRUE;
    assert(phase_coordinator_tick(&tick) == -1);

    tick.r = (rational_t){1, 2};
    tick.ell = TRIT_GLUT;
    assert(phase_coordinator_tick(&tick) == -1); /* 0.5 * 0.5 = 0.25 < 1.8 */
}

static void test_exec_profiles(void)
{
    phase_tick_t tick;

    phase_coordinator_init(&tick, EXEC_DC);
    tick.r = (rational_t){2, 1}; /* Ensure coverage hyperbola holds */
    ordinal_t before = tick.omega;
    phase_coordinator_tick(&tick);
    assert(tick.omega == before + 1);

    phase_coordinator_init(&tick, EXEC_AC);
    tick.r = (rational_t){2, 1}; /* Ensure coverage hyperbola holds */
    double iphi_before = tick.iphi.i;
    phase_coordinator_tick(&tick);
    assert(tick.iphi.i > iphi_before);

    phase_coordinator_init(&tick, EXEC_PC);
    tick.r = (rational_t){2, 1}; /* Ensure coverage hyperbola holds */
    int64_t num_before = tick.r.num;
    phase_coordinator_tick(&tick);
    assert(tick.r.num > num_before);
}

int main(void)
{
    test_coverage_hyperbola();
    test_exec_profiles();
    printf("All Phase Coordinator tests passed\n");
    return 0;
}
