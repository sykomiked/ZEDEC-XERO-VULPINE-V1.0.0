/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_cpu.h — the C reference for the seed-unfold kernels: the SAME
 * source (zvt_seed_kern.h) compiled as plain C, one cell per call, so the
 * CPU reference and the GPU kernels share the source by construction and a
 * GPU result is the CPU's bits. Included by test_zvt_seed_emu.cpp's extern
 * "C" block (the ZK_DEV annotations are empty on the CPU). */
#ifndef ZVT_SEED_CPU_H
#define ZVT_SEED_CPU_H

#include <stdint.h>

/* on the CPU the device-pointer annotation is a no-op */
#define ZK_DEV

#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_kern.h"
#undef ZVT_KSRC

/* one lattice cell, sequential: mode mask, Casimir energy, Cassini sign. */
static inline void zvt_seed_cpu_cell(uint32_t i, uint32_t j,
                                     uint32_t *mode, uint32_t *energy,
                                     int32_t *cassini)
{
    uint32_t e = 0u;
    *mode = zk_zeckendorf(i, &e);
    *energy = e;
    {
        uint32_t k = (i + j) % 12u + 1u;
        int32_t c = zk_cassini(k);
        if ((i % 7u) == (j % 13u)) c = -c;
        *cassini = c;
    }
}

#endif /* ZVT_SEED_CPU_H */