/* test_physics.c — Physics Simulation Engine Tests
 * Tests N-body simulation, conservation laws, measurement/collapse, field computation.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "physics_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== Physics Simulation Engine Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 1024;
    double complex entries[1024];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    physics_state_t sim;
    physics_init(&sim, &matrix);
    assert(sim.num_bodies == 0);

    uint32_t b0 = physics_add_body(&sim, 0.0 + 0.0 * I, 0.0 + 0.0 * I, (rational_t){1000, 1}, (rational_t){1, 1});
    uint32_t b1 = physics_add_body(&sim, 1.0 + 0.0 * I, 0.0 + 0.1 * I, (rational_t){1, 1}, (rational_t){1, 1});
    uint32_t b2 = physics_add_body(&sim, -1.0 + 0.0 * I, 0.0 - 0.1 * I, (rational_t){1, 1}, (rational_t){-1, 1});
    assert(b0 == 0 && b1 == 1 && b2 == 2);
    assert(sim.num_bodies == 3);
    printf("  [PASS] Body creation with rational mass/charge\n");

    double complex f = physics_force(&sim.bodies[0], &sim.bodies[1]);
    assert(cabs(f) > 0);
    printf("  [PASS] Force computation (gravitational + electric)\n");

    double complex field = physics_field_at(&sim, 0.5 + 0.0 * I);
    assert(cabs(field) > 0);
    printf("  [PASS] Field computation at arbitrary point\n");

    for (int i = 0; i < 10; i++) {
        int rc = physics_step(&sim);
        assert(rc == 0);
    }
    assert(sim.current_step == 10);
    printf("  [PASS] 10 simulation steps executed\n");

    assert(physics_measure(&sim, b1) == 0);
    assert(sim.bodies[b1].collapsed == true);
    printf("  [PASS] Measurement collapses body state\n");

    assert(physics_check_conservation(&sim));
    printf("  [PASS] Conservation law check\n");

    printf("=== All physics tests passed ===\n\n");
    return 0;
}
