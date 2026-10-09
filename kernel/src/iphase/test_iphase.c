#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "m5_types.h"
#include "iphase_core.h"

int main(void)
{
    iphase_init();

    lattice_node_id_t node_a = {.node_id = 1, .dim_level = 0};
    lattice_node_id_t node_b = {.node_id = 2, .dim_level = 0};
    lattice_node_id_t node_c = {.node_id = 3, .dim_level = 0};

    phase_t vector_ab = {.r = 1.0, .i = 0.0};
    phase_t vector_bc = {.r = 1.0, .i = M_PI / 2.0};
    phase_t vector_ca = {.r = 1.0, .i = -M_PI / 2.0};

    iphase_add_entry(node_b, vector_ab);
    iphase_add_entry(node_c, vector_bc);
    iphase_add_entry(node_a, vector_ca);

    double complex result_ab = iphase_route(node_a, node_b);
    assert(result_ab == 1.0 + I * 0.0);

    double complex result_bc = iphase_route(node_b, node_c);
    assert(result_bc == 1.0 + I * M_PI / 2.0);

    double complex result_ca = iphase_route(node_c, node_a);
    assert(result_ca == 1.0 + I * (-M_PI / 2.0));

    printf("All IPHASE tests passed\n");
    return 0;
}
