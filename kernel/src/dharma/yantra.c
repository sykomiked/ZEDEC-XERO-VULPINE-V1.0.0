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

/* ---- DECLARATION -----------------------------------------------------------

 * Containment. yantra.o's `nm -u` is a single symbol, l13_project -- the
 * Galois trace projection, which sephirot provides as cyc13_ready. One edge,
 * one requirement, nothing inferred.
 */
#include "zxv_decl.h"
ZXV_DECLARE(yantra,
    ZXV_PROVIDES(yantra_ready),
    ZXV_REQUIRES(cyc13_ready),
    ZXV_NO_BRINGUP);
