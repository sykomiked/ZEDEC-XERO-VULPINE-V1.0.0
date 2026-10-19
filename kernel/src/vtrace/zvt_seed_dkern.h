/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_dkern.h — THE D-DIMENSIONAL SCAFFOLD: the parallel build wired
 * across ALL 144 dimensions of the lattice simultaneously. A sequential
 * build is stuck in 2D (one pass, one order). A per-cell-parallel kernel
 * treats a cell's D COORDINATES as its identity: the same source handles
 * a 2D, 3D, ... 144D lattice — each thread computes the cell's Fibonacci
 * stage from its max coordinate, its D dependency predecessors (one per
 * axis, all in strictly lower rings), and the paraconsistent four-valued
 * join of those dependencies' build states.
 *
 * THE PARACONSISTENT DIMENSION. A cell whose dependencies are contradictory
 * (BOTH) or unresolved (NEITHER) is a four-valued state — the build
 * ADJUDICATES it through lpres (TRUE admits, FALSE vetoes, NEITHER/BOTH
 * defer), never collapses it to a binary. The encoding is the kernel's OWN:
 * bit0 told-true, bit1 told-false (NEITHER 0, TRUE 1, FALSE 2, BOTH 3), so
 * the GPU cells use the same four-valued arithmetic as every subsystem.
 *
 * Same discipline as zvt_kern.h: one source (C11, CUDA, Metal), types
 * zi/zu/zb, no 64-bit arithmetic, no preprocessor lines in the body. The
 * CPU reference shares the source; bit-identical by construction.
 *
 * The stages (one thread per cell, D coordinates):
 *   zk_d_stage    the Fibonacci ring of a cell from its max coordinate
 *   zk_d_deps     the D dependency predecessors (lower rings), count
 *   zk_d_lpres    the four-valued join of the dependencies' states
 * Not a header to include anywhere else. */

#ifndef ZVT_SEED_DKERN_ONCE
#define ZVT_SEED_DKERN_ONCE

ZVT_KSRC(

    /* the DISTINCT Fibonacci ladder (ring boundaries): ring 1 = base
     * (max 0), ring k holds cells with F_{k-1} <= max < F_k, k in 2..11;
     * ring 12 is never assigned (max < 144). */
    static inline zu zk_d_fib(zu k) {
        static const zu f[12] = {0u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u};
        return f[k];
    }

    /* the ring of a D-dimensional cell: from the max of its coordinates. */
    static inline zu zk_d_stage(zu d, ZK_DEV const zu *c) {
        zu m = 0u, k, a;
        for (a = 0u; a < d; a++)
            if (c[a] > m) m = c[a];
        if (m == 0u) return 1u;
        for (k = 2u; k <= 11u; k++)
            if (m < zk_d_fib(k)) return k;
        return 11u;
    }

    /* the dependency predecessors of a D-dimensional cell: one per axis,
     * the cell one ring down on that axis (coordinate clamped to prev-1,
     * the max coord of the previous ring) — always in a strictly lower
     * ring (the scaffold's partial order, acyclic by construction).
     * Writes up to D predecessors, returns the count. */
    static inline zu zk_d_deps(zu d, ZK_DEV const zu *c, zu stage, ZK_DEV zu *di, ZK_DEV zu *dj) {
        zu n = 0u, a, prev;
        if (stage <= 1u) return 0u; /* the base has no deps  */
        prev = zk_d_fib(stage - 1u);
        for (a = 0u; a < d && n < d; a++) {
            if (c[a] >= prev) {
                di[n] = a;         /* the axis              */
                dj[n] = prev - 1u; /* the clamped predecessor*/
                n++;
            }
        }
        return n;
    }

    /* the paraconsistent build state of a cell from its dependencies'
     * states: TRUE if every dependency is TRUE, FALSE if any is FALSE and
     * none contradicts, BOTH if dependencies disagree (preserved, never
     * resolved by guessing), NEITHER if every dependency is unbuilt.
     * The join is lpres_kjoin: a bitwise OR of the told-true/told-false
     * bits. */
    static inline zu zk_d_lpres(zu ndep, ZK_DEV const zu *depstate) {
        zu s = 0u, i;
        if (ndep == 0u) return 1u; /* TRUE: the base cell  */
        for (i = 0u; i < ndep; i++) s = s | depstate[i];
        return s;
    })

#endif /* ZVT_SEED_DKERN_ONCE */