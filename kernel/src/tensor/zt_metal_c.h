/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metal_c.h — zt_metal_math.h compiled as C: the prelude that the Metal
 * build's (zt_metal.m) mirrors, then the kernels. For zt_gpu.c and the tests;
 * include once per translation unit. */
#ifndef ZT_METAL_C_H
#define ZT_METAL_C_H

#include <stdbool.h>
#include <stdint.h>

typedef int64_t zl;
typedef uint64_t zw;
typedef int32_t zi;
typedef uint32_t zu;
typedef int16_t zs;
typedef uint8_t zb;
typedef int8_t zc;
typedef uint32_t zm_ctr; /* atomic_uint on the GPU */

static inline void zm_count(zm_ctr *c, zu n)
{
    *c += n;
}

#define ZM_DEV
#define ZK_A              zu i, zg_p p, zb *b0, zb *b1, zb *b2, zb *b3, zb *b4, zb *b5, zm_ctr *ctr
#define ZT_METAL_SRC(...) __VA_ARGS__
#include "zt_metal_math.h"
#undef ZT_METAL_SRC

#endif /* ZT_METAL_C_H */
