/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_vec_zt.h — the vector plane's dot products on the tensor engine's
 * kernels: its whole-matrix hook first (the GPU, zt_metal.h, where there is
 * one), then its SIMD row kernel, else alx_vec.c's loop. Vector rows are
 * GGUF Q8_0 rows with a scale of exactly one, so every path returns the same
 * integers (test_alx_vec checks this). Hosted builds. */
#ifndef ZXV_ALX_VEC_ZT_H
#define ZXV_ALX_VEC_ZT_H

#include "alx_vec.h"
#include "../tensor/zt_kern.h"

/* alx_vec_dots_fn with ctx a const zt_kern_t * */
int alx_vec_zt_dots(void *ctx, const uint8_t *rows, uint32_t n, uint32_t nb, const int8_t *q,
                    int32_t *out);

#endif
