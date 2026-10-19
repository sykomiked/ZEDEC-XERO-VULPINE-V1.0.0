/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_build_gen.cu — the build-scaffolding kernel as NVRTC would
 * compile it: the ZVT_KSRC body plus the __global__ wrapper that dispatches
 * one thread per lattice cell. The C reference (zvt_seed_build_cpu.h)
 * shares the same body; the GPU result is the CPU's bits by construction.
 *
 * The wrapper is the exact launch shape the GPU uses: ceil(n/256) blocks of
 * 256 threads, thread i handles cell (i % w, i / w) when i < n. Each stage
 * is a separate launch (the 12 ring boundaries are the only barriers). */
typedef unsigned int zu;
typedef int zi;
typedef unsigned char zb;
#define ZK_DEV

#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_build_kern.h"
#undef ZVT_KSRC

extern "C" __global__ void zvk_build_cell(const zu *x, const zu *y, zu n,
                                          zu *stage, zu *task, zu *ndep,
                                          zu *di, zu *dj)
{
    zu i = blockIdx.x * 256u + threadIdx.x;
    if (i < n) {
        zu a = x[i], b = y[i];
        stage[i] = zk_build_stage(a, b);
        task[i]  = zk_build_task(a, b);
        ndep[i]  = zk_build_dep(a, b, &di[2u * i], &dj[2u * i]);
    }
}