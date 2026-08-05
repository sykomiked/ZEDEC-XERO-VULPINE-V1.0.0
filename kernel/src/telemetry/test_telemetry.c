#include "telemetry_core.h"
#include "axiom_matrix_core.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    axiom_matrix_t m;
    m.size = 256;
    double complex entries[256];
    memset(entries, 0, sizeof(entries));
    m.entries = entries;

    rational_t r = {1, 1};
    phase_t p = {1.0, 0.0};
    collapse_t c = {{0, 0}};

    axiom_matrix_set(&m, 5, r, TRIT_TRUE, p, c, 3.0 + 4.0 * I);
    telemetry_t t = emit_and_observe(&m, 5);
    assert(cabs(t.value) == 5.0);

    assert(fib_bound(0) == 1);
    assert(fib_bound(1) == 1);
    assert(fib_bound(5) == 8);
    assert(fib_bound(10) == 89);

    ordinal_t choice = choice_resolve_from_telemetry(&t, 3);
    assert(choice > 0);

    printf("All Telemetry tests passed\n");
    return 0;
}
