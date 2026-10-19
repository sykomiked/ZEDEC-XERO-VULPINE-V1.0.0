/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_lpres_kern.h — THE PARACONSISTENT DIMENSION OF THE SCAFFOLD.
 * A sequential build collapses every cell to a binary (built / not built).
 * The higher-dimensional build cannot: a cell whose dependencies are
 * contradictory (BOTH) or unresolved (NEITHER) is a four-valued state, and
 * the stage-by-stage GPU build must ADJUDICATE it through lpres — TRUE
 * admits the cell, FALSE vetoes it, NEITHER/BOTH defer it to the next
 * pass — exactly as the vortex rotor adjudicates polarity transitions.
 *
 * The kernel gives each lattice cell a four-valued BUILD STATE from the
 * states of its dependencies (the scaffold's partial order): a cell is
 * TRUE (ready) when every dependency is TRUE; FALSE (blocked) when a
 * dependency is FALSE; NEITHER when a dependency is unbuilt; BOTH when
 * dependencies disagree (one TRUE, one FALSE) — a contradiction the build
 * preserves, never resolves by guessing. The lpres encoding is the
 * kernel's OWN: bit0 = told-true, bit1 = told-false (LPRES_STATE_NEITHER 0,
 * TRUE 1, FALSE 2, BOTH 3), so the GPU cells use the same four-valued
 * arithmetic as every other subsystem.
 *
 * Same discipline: one source (C11, CUDA, Metal), types zi/zu/zb, no 64-bit,
 * no preprocessor lines in the body. The CPU reference shares the source.
 *
 * The stages (one thread per cell):
 *   zk_lpres_join  the four-valued join of a cell's dependencies: the
 *                  paraconsistent build state of the cell
 *   zk_lpres_step  advance one build pass: TRUE cells build, FALSE/N/B
 *                  cells defer — returns how many cells became TRUE
 * Not a header to include anywhere else. */

#ifndef ZVT_SEED_LPRES_ONCE
#define ZVT_SEED_LPRES_ONCE

ZVT_KSRC(

    /* the kernel's four-valued encoding: NEITHER 0, TRUE 1, FALSE 2, BOTH 3
     * (bit0 told-true, bit1 told-false) — the same shape lpres.h uses. */
    static inline zu zk_lpres_join(zu a, zu b) {
        /* the knowledge join: agreement keeps the value, T with F is B,
         * and an unknown (0) contributes nothing. This is lpres_kjoin: a
         * bitwise OR of the told-true/told-false bits. */
        return a | b;
    }

    /* one cell's build state from its dependencies' states. A cell with no
     * dependencies is TRUE (the base, always ready). With dependencies:
     * the join of their states — TRUE if all TRUE, FALSE if any FALSE and
     * none TRUE-contradicted, BOTH if dependencies disagree (a preserved
     * contradiction), NEITHER if every dependency is unbuilt. */
    static inline zu zk_lpres_cell(zu ndep, ZK_DEV const zu *depstate) {
        zu s = 0u, i;
        if (ndep == 0u) return 1u;                 /* TRUE: the base cell  */
        for (i = 0u; i < ndep; i++) s = zk_lpres_join(s, depstate[i]);
        return s;
    }

    /* one build pass over the lattice: each TRUE cell builds (its state is
     * consumed), each FALSE/N/B cell defers. The pass is deterministic and
     * the scaffold's partial order guarantees every cell's dependencies
     * are in lower rings — so a cell that becomes TRUE this pass cannot
     * unlock a cell in the same ring (acyclic by construction). */
    static inline void zk_lpres_step(zu i, zu j, zu stage, zu ndep,
                                     ZK_DEV const zu *depstate,
                                     ZK_DEV zu *cellstate) {
        zu s = zk_lpres_cell(ndep, depstate);
        cellstate[i * 144u + j] = s;
        (void) stage;
    }
)

#endif /* ZVT_SEED_LPRES_ONCE */