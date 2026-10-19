/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_dcpu.h — the C reference for the D-dimensional scaffold kernel:
 * the SAME source (zvt_seed_dkern.h) compiled as plain C, one cell per
 * call, so the CPU reference and the GPU kernels share the source by
 * construction. */
#ifndef ZVT_SEED_DCPU_H
#define ZVT_SEED_DCPU_H

#include <stdint.h>

#define ZK_DEV

#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_dkern.h"
#undef ZVT_KSRC

/* one D-dimensional cell, sequential: stage, deps, paraconsistent state. */
static inline void zvt_seed_d_cpu_cell(uint32_t D, const uint32_t *co, uint32_t *stage,
                                       uint32_t *ndep, uint32_t *dia, uint32_t *djc,
                                       uint32_t *cellstate)
{
    uint32_t dep[144], a;
    *stage = zk_d_stage(D, co);
    *ndep = zk_d_deps(D, co, *stage, dia, djc);
    for (a = 0u; a < *ndep; a++)
        dep[a] = (djc[a] + dia[a]) % 4u; /* the same deterministic states */
    *cellstate = zk_d_lpres(*ndep, dep);
}

#endif /* ZVT_SEED_DCPU_H */