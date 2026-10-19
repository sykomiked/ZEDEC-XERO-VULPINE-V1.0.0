/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_build_kern.h — THE 144-DIMENSIONAL LATTICE AS THE PARALLEL BUILD
 * SCAFFOLDING. The lattice is not just geometry to compute; it IS the
 * scheduler. Every one of the 20,736 cells is a build unit, the 12
 * Fibonacci rings are the build stages, and the lattice's partial order is
 * the dependency rule — so a GPU pipeline that launches stage by stage is
 * race-free and deterministic by construction, no locks, no barriers beyond
 * the 12 ring boundaries.
 *
 * THE SCAFFOLD:
 *   ring k contains the F_k^2 - F_{k-1}^2 cells the dimension jump adds
 *   (F0 = 0: ring 1 = 1 cell, the base; rings 2..12 add 0,3,5,16,39,105,
 *   272,715,1869,4896,12815; the sum is exactly 144^2 = 20736).
 *   A cell's STAGE is its ring (1..12): the build phase it belongs to.
 *   A cell's DEPENDENCIES are cells in strictly lower rings — the lattice
 *   is a partial order, so the stage-by-stage build is acyclic: stage k
 *   reads only stages < k. The GPU can run all cells of a stage in
 *   parallel (one thread each) and only the 12 ring boundaries serialize.
 *   A cell's TASK is its offset within its ring's shell: the deterministic
 *   order the fabricated module is assembled into the plant.
 *
 * Same discipline as zvt_kern.h / zvt_seed_kern.h: one source (C11, CUDA,
 * Metal), types zi/zu/zb, no 64-bit arithmetic, no preprocessor lines in
 * the body. The CPU reference shares the source; bit-identical by
 * construction.
 *
 * The stages (one thread per cell):
 *   zk_build_stage  the ring (1..12) of cell (i, j)
 *   zk_build_dep    the dependency cells of (i, j) — all in lower rings;
 *                   writes up to 2 deps, returns the count
 *   zk_build_task   the cell's offset within its ring's shell (0-based)
 * Not a header to include anywhere else. */

#ifndef ZVT_SEED_BUILD_ONCE
#define ZVT_SEED_BUILD_ONCE

ZVT_KSRC(

    /* the ring of cell (i, j): ring 1 = the base (m == 0); ring k (2..11)
     * holds cells with F_{k-1} <= m < F_k on the DISTINCT ladder
     * (0,1,2,3,5,8,13,21,34,55,89,144), so the shells partition the whole
     * lattice: ring k's shell is F_k^2 - F_{k-1}^2, and the 11 shells sum
     * to exactly 144^2 = 20736. Ring 12 is never assigned (m < 144 always). */
    static inline zu zk_build_stage(zu i, zu j) {
        zu m = i > j ? i : j;
        static const zu f[12] = { 0u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u };
        zu k;
        if (m == 0u) return 1u;
        for (k = 2u; k <= 11u; k++)
            if (m < f[k]) return k;
        return 11u;
    }

    /* the lower-ring dependencies of cell (i, j): the scaffold's partial
     * order. A cell at ring k reads the cell one ring down on each axis
     * (the F_{k-1} predecessor), so stage k never reads stage >= k.
     * Writes up to 2 dependency cells, returns how many. */
    static inline zu zk_build_dep(zu i, zu j, ZK_DEV zu *di, ZK_DEV zu *dj) {
        zu k = zk_build_stage(i, j);
        zu fk, f0 = 0u, f1 = 1u, t, n = 0u, p;
        if (k <= 1u) return 0u;                   /* the base has no deps   */
        for (p = 1u; p < k; p++) { t = f0 + f1; f0 = f1; f1 = t; }
        fk = f1;                                  /* F_k (p == k)           */
        /* the F_{k-1} predecessor cell on each axis, clamped into the
         * previous ring's square. */
        if (i >= fk) { if (n < 2u) { di[n] = i - fk; dj[n] = j < fk ? j : fk - 1u; n++; } }
        if (j >= fk) { if (n < 2u) { di[n] = i < fk ? i : fk - 1u; dj[n] = j - fk; n++; } }
        return n;
    }

    /* the cell's task offset within its ring's shell: the deterministic
     * assembly order of the fabricated module. Ring k's shell is the
     * annulus [prev, F_k)^2 minus [0, prev)^2, F_k^2 - F_{k-1}^2 cells,
     * indexed in a canonical row-major sweep: first the cells in the new
     * rows (i in [prev, F_k), j in [0, F_k)), then the cells in the new
     * columns (i in [0, prev), j in [prev, F_k)). */
    static inline zu zk_build_task(zu i, zu j) {
        static const zu f[12] = { 0u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u };
        zu k = zk_build_stage(i, j);
        zu prev = f[k - 1u];
        zu side = f[k] - prev;                    /* the shell's width      */
        if (i >= prev)
            return (i - prev) * f[k] + j;         /* a new-row cell          */
        return (side * f[k]) + (i * side) + (j - prev);  /* a new-column cell */
    }
)

#endif /* ZVT_SEED_BUILD_ONCE */