/* yantra.h — Yantra: the Dimension of Form (Hardware Topology)
 *
 * Yantra: Yam (to hold/sustain) + Tra (instrument) -- "a container,
 * a machine." A physical Yantra nests geometric shapes (triangles,
 * circles, lotus petals) around a central Bindu. This architecture's
 * literal nested geometry is sephirot.h's Galois subfield lattice:
 * L13_DIM_1 -> L13_DIM_2 -> L13_DIM_3 -> L13_DIM_4 -> L13_DIM_6 ->
 * L13_DIM_12, each level a trace-projection (l13_project) of the
 * same underlying cyc13_t space -- a static, spatial container that
 * a Mantra signal (mantra.h) vibrates through, not a dynamic process
 * itself.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef YANTRA_H
#define YANTRA_H

#include "m5_types.h"
#include "sephirot.h"

typedef struct yantra_topology {
    l13_dim_t dim;        /* the lattice level this container is projected to */
    cyc13_t container;     /* the geometric container: signal projected into that level */
} yantra_topology_t;

/* Contains a Mantra signal within a Yantra of the given dimension
 * level (the trace projection onto that Galois subfield). */
yantra_topology_t yantra_contain(cyc13_t signal, l13_dim_t dim);

/* The Bindu: the central point of the Yantra -- the index-0 (zeta^0)
 * coefficient. Present at every nested dimension level, but NOT
 * numerically invariant across them: index 0 is a fixed point of
 * every Galois automorphism (exponent*0 mod 13 == 0 for any
 * exponent), so l13_project's summation over the dimension's
 * subgroup scales it by the subgroup's order |H| -- exactly 12, 6, 4,
 * 3, 2, 1 for L13_DIM_1..L13_DIM_12 respectively. */
rational_t yantra_bindu(const yantra_topology_t *y);

#endif
