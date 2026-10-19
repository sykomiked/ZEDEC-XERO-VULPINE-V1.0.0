/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_build_cpu.h — the C reference for the build-scaffolding kernel:
 * the SAME source (zvt_seed_build_kern.h) compiled as plain C, one cell per
 * call, so the CPU reference and the GPU kernels share the source by
 * construction. */
#ifndef ZVT_SEED_BUILD_CPU_H
#define ZVT_SEED_BUILD_CPU_H

#include <stdint.h>

#define ZK_DEV

#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_build_kern.h"
#undef ZVT_KSRC

/* one lattice cell, sequential: stage, task offset, dependencies. */
static inline void zvt_seed_build_cpu_cell(uint32_t i, uint32_t j,
                                           uint32_t *stage, uint32_t *task,
                                           uint32_t *ndep,
                                           uint32_t *di, uint32_t *dj)
{
    *stage = zk_build_stage(i, j);
    *task  = zk_build_task(i, j);
    *ndep  = zk_build_dep(i, j, di, dj);
}

#endif /* ZVT_SEED_BUILD_CPU_H */