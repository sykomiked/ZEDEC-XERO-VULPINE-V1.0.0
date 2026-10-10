#include <stdio.h>
#include <assert.h>
#include "m5_types.h"
#include "iphase_core.h"

/* Phase vectors are Q16.16 (phase_t), routes come back as zxv_cq16_t.
 * pi/2 in Q16.16 is round(1.5707963 * 65536) = 102944. */
#define HALF_PI_Q16 102944

int main(void)
{
    iphase_init();

    lattice_node_id_t node_a = {.node_id = 1, .dim_level = 0};
    lattice_node_id_t node_b = {.node_id = 2, .dim_level = 0};
    lattice_node_id_t node_c = {.node_id = 3, .dim_level = 0};
    lattice_node_id_t node_x = {.node_id = 9, .dim_level = 0};

    phase_t vector_ab = {.r = Q16_ONE, .i = 0};
    phase_t vector_bc = {.r = Q16_ONE, .i = HALF_PI_Q16};
    phase_t vector_ca = {.r = Q16_ONE, .i = -HALF_PI_Q16};

    iphase_add_entry(node_b, vector_ab);
    iphase_add_entry(node_c, vector_bc);
    iphase_add_entry(node_a, vector_ca);

    zxv_cq16_t result_ab = iphase_route(node_a, node_b);
    assert(cq16_eq(result_ab, cq16(Q16_ONE, 0)));

    zxv_cq16_t result_bc = iphase_route(node_b, node_c);
    assert(cq16_eq(result_bc, cq16(Q16_ONE, HALF_PI_Q16)));

    zxv_cq16_t result_ca = iphase_route(node_c, node_a);
    assert(cq16_eq(result_ca, cq16(Q16_ONE, -HALF_PI_Q16)));

    /* |1 + i*pi/2| = 1.8621 -> floor(sqrt(65536^2 + 102944^2)) = 122034 */
    uint64_t mag = cq16_abs(result_bc);
    assert(mag == 122034);

    /* an unknown destination routes to zero */
    assert(cq16_is_zero(iphase_route(node_a, node_x)));

    printf("All IPHASE tests passed\n");
    return 0;
}
