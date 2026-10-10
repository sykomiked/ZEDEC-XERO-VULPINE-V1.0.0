#include "telemetry_core.h"
#include "axiom_matrix_core.h"
#include <string.h>

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

/* choice = floor(|value| * bound), capped at bound. |value| is Q16.16, so a
 * magnitude below one unit (1/65536, the integer stand-in for the old 1e-9
 * floor) gives 0. */
ordinal_t choice_resolve_from_telemetry(const telemetry_t *t, uint32_t paradox_level)
{
    uint32_t bound = fib_bound(paradox_level);
    uint64_t mag = cq16_abs(t->value);
    if (mag == 0) return 0;
    if (mag >= ((uint64_t) 1 << 32)) return bound; /* |value| >= 65536 always caps */
    ordinal_t choice = (ordinal_t) ((mag * bound) >> Q16_SHIFT);
    if (choice > bound) choice = bound;
    return choice;
}

void run_telemetry_recursion_demo(void) {
    axiom_matrix_t m;
    m.size = 256;
    zxv_cq16_t entries[256];
    memset(entries, 0, sizeof(entries));
    m.entries = entries;

    rational_t r = {1, 1};
    phase_t p = {Q16_ONE, 0};
    collapse_t c = {{0, 0}};

    for (ordinal_t tick = 0; tick < TELEMETRY_DEMO_TICKS; tick++) {
        /* tick * (1 + 0.5i), Q16.16 */
        zxv_cq16_t val = cq16((int64_t) tick * Q16_ONE, (int64_t) tick * (Q16_ONE / 2));
        axiom_matrix_set(&m, tick, r, TRIT_TRUE, p, c, val);
    }

    zxv_cq16_t total = cq16(0, 0);
    for (ordinal_t tick = 0; tick < TELEMETRY_DEMO_TICKS; tick++) {
        telemetry_t t = emit_and_observe(&m, tick);
        total = cq16_add(total, t.value);
    }
    (void)total;
}

/* ---- DECLARATION -----------------------------------------------------------

 * DECLARED, NOT ROOTED.
 *
 * This module used to depend on libgcc's __muldc3 (double-complex multiply),
 * which is why it was never given a GC root. The value path is now Q16.16
 * integer arithmetic (zxv_cq16_t, zxv_fixed.h), so that reason is gone; it
 * stays ZXV_NO_BRINGUP only because nothing in the boot path calls it yet.
 * Rooting it is the one-word change ZXV_NO_BRINGUP -> ZXV_BRINGUP.
 */
#include "zxv_decl.h"
ZXV_DECLARE(telemetry,
    ZXV_PROVIDES(telemetry_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);
