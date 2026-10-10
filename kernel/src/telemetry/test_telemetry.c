#include "telemetry_core.h"
#include "axiom_matrix_core.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Telemetry values are Q16.16 complex integers (zxv_cq16_t). */
int main(void)
{
    axiom_matrix_t m;
    m.size = 256;
    zxv_cq16_t entries[256];
    memset(entries, 0, sizeof(entries));
    m.entries = entries;

    rational_t r = {1, 1};
    phase_t p = {Q16_ONE, 0};
    collapse_t c = {{0, 0}};

    axiom_matrix_set(&m, 5, r, TRIT_TRUE, p, c, cq16(3 * Q16_ONE, 4 * Q16_ONE));
    telemetry_t t = emit_and_observe(&m, 5);
    assert(cq16_abs(t.value) == (uint64_t)5 * Q16_ONE); /* |3+4i| = 5 exactly */

    assert(fib_bound(0) == 1);
    assert(fib_bound(1) == 1);
    assert(fib_bound(5) == 8);
    assert(fib_bound(10) == 89);
    assert(fib_bound(99) == 144);

    /* |v| = 5 >= 1, so the choice caps at the bound */
    assert(choice_resolve_from_telemetry(&t, 3) == 3);

    /* |v| = 0.5 -> floor(0.5 * 8) = 4 */
    telemetry_t half = {cq16(Q16_ONE / 2, 0), 0};
    assert(choice_resolve_from_telemetry(&half, 5) == 4);
    /* |0.3 + 0.4i| = 0.5 (floor in Q16.16 is 32767 or 32768) -> 3 or 4 of 8 */
    telemetry_t tri = {cq16(Q16_CONST(3, 10), Q16_CONST(4, 10)), 0};
    ordinal_t ch = choice_resolve_from_telemetry(&tri, 5);
    assert(ch == 3 || ch == 4);
    /* zero magnitude -> 0 */
    telemetry_t zero = {cq16(0, 0), 0};
    assert(choice_resolve_from_telemetry(&zero, 5) == 0);
    /* huge magnitude (parts above 2^31) still caps, no overflow */
    telemetry_t big = {cq16((int64_t)1 << 40, -((int64_t)1 << 40)), 0};
    assert(choice_resolve_from_telemetry(&big, 10) == 89);

    run_telemetry_recursion_demo();

    printf("All Telemetry tests passed\n");
    return 0;
}
