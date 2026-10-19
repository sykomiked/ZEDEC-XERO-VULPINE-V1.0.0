/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_gen.cu — the seed-unfold kernel as NVRTC would compile it: the
 * ZVT_KSRC body (stringified in the CUDA path, plain in the emulator) plus
 * the __global__ wrapper that dispatches one thread per lattice cell. This
 * is the file the emulator includes and the PTX proof compiles; the C
 * reference (zvt_seed_cpu.h) shares the same body, so the GPU result is the
 * CPU's bits by construction.
 *
 * The wrapper is the exact launch shape the GPU uses: ceil(n/256) blocks of
 * 256 threads, thread i handles cell (i % w, i / w) when i < n. */
/* the type aliases: the same prelude zvt_cuda.c stringifies for NVRTC */
typedef unsigned int zu;
typedef int zi;
typedef unsigned char zb;
/* the device-pointer annotation: empty on the CPU reference path */
#define ZK_DEV

/* The wrapper is the exact launch shape the GPU uses: ceil(n/256) blocks of
 * 256 threads, thread i handles cell (i % w, i / w) when i < n. */
#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_kern.h"
#undef ZVT_KSRC

extern "C" __global__ void zvk_seed_cell(const zu *mode_in, const zu *energy_in,
                                         const zi *cassini_in, zu w, zu n,
                                         zu *mode, zu *energy, zi *cassini)
{
    (void) mode_in; (void) energy_in; (void) cassini_in;
    zu i = blockIdx.x * 256u + threadIdx.x;
    if (i < n)
        zk_seed_cell(i % w, i / w, &mode[i], &energy[i], &cassini[i]);
}