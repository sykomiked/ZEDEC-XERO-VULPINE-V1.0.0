#include "compat_layer.h"
#include "m5_types.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    phase_tick_t pt = {
        .omega = 10,
        .r = {.num = 1, .den = 1},
        .ell = TRIT_TRUE,
        .iphi = {.r = 0.0, .i = 0.0},
        .chi = {.bits = {0, 0}}
    };

    long legacy_scalar = project_to_legacy(&pt);
    assert(legacy_scalar == 10);

    m8_process_t m8_process = lift_to_m8(&pt);
    assert(m8_process.base.omega == pt.omega);
    assert(rational_mag(m8_process.base.r) == rational_mag(pt.r));
    assert(m8_process.base.ell == pt.ell);
    assert(m8_process.base.iphi.r == pt.iphi.r && m8_process.base.iphi.i == pt.iphi.i);
    assert(memcmp(m8_process.base.chi.bits, pt.chi.bits, sizeof(pt.chi.bits)) == 0);

    printf("Compatibility layer tests passed.\n");
    return 0;
}
