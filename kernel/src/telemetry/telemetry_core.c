#include "telemetry_core.h"
#include "axiom_matrix_core.h"
#include <string.h>
#include <math.h>

static uint32_t fib_table[] = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};
#define FIB_TABLE_SIZE (sizeof(fib_table) / sizeof(fib_table[0]))

uint32_t fib_bound(uint32_t paradox_level) {
    if (paradox_level < FIB_TABLE_SIZE) return fib_table[paradox_level];
    return fib_table[FIB_TABLE_SIZE - 1];
}

telemetry_t emit_and_observe(const axiom_matrix_t *A, ordinal_t tick) {
    telemetry_t t;
    t.value = axiom_matrix_project_tick(A, tick);
    t.recursion_depth = 0;
    return t;
}

ordinal_t choice_resolve_from_telemetry(const telemetry_t *t, uint32_t paradox_level) {
    telemetry_t self_observation = *t;
    uint32_t bound = fib_bound(paradox_level);
    double mag = cabs(self_observation.value);
    if (mag < 1e-9) return 0;
    ordinal_t choice = (ordinal_t)(mag * bound);
    if (choice > bound) choice = bound;
    return choice;
}

void run_telemetry_recursion_demo(void) {
    axiom_matrix_t m;
    m.size = 256;
    double complex entries[256];
    memset(entries, 0, sizeof(entries));
    m.entries = entries;

    rational_t r = {1, 1};
    phase_t p = {1.0, 0.0};
    collapse_t c = {{0, 0}};

    for (ordinal_t tick = 0; tick < TELEMETRY_DEMO_TICKS; tick++) {
        double complex val = (double complex)tick * (1.0 + 0.5 * I);
        axiom_matrix_set(&m, tick, r, TRIT_TRUE, p, c, val);
    }

    double complex total = 0.0 + 0.0 * I;
    for (ordinal_t tick = 0; tick < TELEMETRY_DEMO_TICKS; tick++) {
        telemetry_t t = emit_and_observe(&m, tick);
        total += t.value;
    }
    (void)total;
}
