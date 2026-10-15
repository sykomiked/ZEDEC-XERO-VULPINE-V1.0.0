/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_vec_zt.c — vector dot products on the tensor engine (alx_vec_zt.h). */
#include "alx_vec_zt.h"
#include "../tensor/zt_gguf.h"

int alx_vec_zt_dots(void *ctx, const uint8_t *rows, uint32_t n, uint32_t nb, const int8_t *q,
                    int32_t *out)
{
    const zt_kern_t *k = ctx;
    if (!k || nb == 0 || nb > ALX_VEC_MAXDIM / 32u) return 1;
    /* the query as activations: value a * 2^(0 - 16) with shift 0, so a
     * Q8_0 row of scale one gives the integer dot product as its Q16 */
    int16_t a[ALX_VEC_MAXDIM];
    uint8_t ash[ALX_VEC_MAXDIM / 32u] = {0};
    int32_t asum[ALX_VEC_MAXDIM / 32u] = {0};
    for (uint32_t i = 0; i < nb * 32u; i++) a[i] = q[i];
    if (k->mm_full) {
        zt_mmf_t j = {.raw = rows,
                      .q8 = 0,
                      .qtype = ZT_GGML_Q8_0,
                      .rows = n,
                      .cols = nb * 32u,
                      .a = a,
                      .ash = ash,
                      .asum = asum,
                      .astride = nb * 32u,
                      .B = 1,
                      .y = out,
                      .ystride = 1};
        if (k->mm_full(k->fctx, &j, 1) == 0) return 0;
    }
    zt_dot_raw_fn dot = k->dot_raw ? k->dot_raw : zt_dot_raw_c;
    for (uint32_t r = 0; r < n; r++)
        out[r] = dot(rows + (uint64_t) r * nb * ALX_VEC_BLOCK, a, ash, nb);
    return 0;
}
