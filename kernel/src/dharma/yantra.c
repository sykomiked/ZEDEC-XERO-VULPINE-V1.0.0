/* yantra.c — Yantra: hardware topology implementation. See yantra.h. */
#include "yantra.h"

yantra_topology_t yantra_contain(cyc13_t signal, l13_dim_t dim) {
    yantra_topology_t y;
    y.dim = dim;
    y.container = l13_project(signal, dim);
    return y;
}

rational_t yantra_bindu(const yantra_topology_t *y) {
    return y->container.c[0];
}
