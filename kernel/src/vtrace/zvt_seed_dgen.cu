/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_dgen.cu — the D-dimensional scaffold kernel as NVRTC would
 * compile it: the ZVT_KSRC body plus the __global__ wrapper dispatching one
 * thread per cell. The C reference (zvt_seed_dcpu.h) shares the same body.
 *
 * A cell is D coordinates; the wrapper reads them from a flat coordinate
 * array (cell i's coords at co[i*D .. i*D+D-1]) and writes the stage, the
 * dependency list (up to D predecessor pairs) and the paraconsistent build
 * state. D is a runtime parameter, so the SAME kernel wires 2, 3, ... 144
 * dimensions. */
typedef unsigned int zu;
typedef int zi;
typedef unsigned char zb;
#define ZK_DEV

#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_dkern.h"
#undef ZVT_KSRC

extern "C" __global__ void zvk_d_cell(const zu *co, zu n, zu D, zu *stage, zu *ndep, zu *dia,
                                      zu *djc, zu *cellstate)
{
    zu i = blockIdx.x * 256u + threadIdx.x;
    if (i < n) {
        zu c[144];
        zu dep[144];
        zu a;
        for (a = 0u; a < D; a++) c[a] = co[i * 144u + a]; /* stride 144  */
        stage[i] = zk_d_stage(D, c);
        ndep[i] = zk_d_deps(D, c, stage[i], &dia[i * 144u], &djc[i * 144u]);
        /* dependencies' build states: for this proof every predecessor is
         * given a deterministic state from its coordinates — the cell's
         * paraconsistent state is then the join. A real build feeds the
         * predecessors' own computed states; the join is the same either
         * way. */
        for (a = 0u; a < ndep[i]; a++) {
            zu x = dia[i * 144u + a], v = djc[i * 144u + a];
            dep[a] = (v + x) % 4u; /* NEITHER 0, TRUE 1, FALSE 2, BOTH 3 */
        }
        cellstate[i] = zk_d_lpres(ndep[i], dep);
    }
}