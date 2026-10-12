/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metal.h — the forward pass's matrix products on an Apple GPU (Metal),
 * for HOSTED macOS builds only (the app, bench/, tests). Plan item 3.9.
 *
 * The GPU runs zt_kern.h's whole-matrix hook (zt_kern_t.mm_full) for GGUF
 * Q8_0, Q4_K and Q6_K rows used in place. Its kernels are zt_metal_math.h:
 * the C reference's integer steps, so every product, and so every logit, is
 * bit for bit what the CPU computes (test_zt_metal checks this on random
 * matrices and whole forward passes). Everything else in the forward pass
 * (norms, RoPE, attention, sampling) stays on the CPU kernels it is given.
 *
 *     zt_metal_t *mt = zt_metal_new(err, sizeof err);   (null: no GPU here)
 *     zt_metal_map(mt, file, file_bytes);  (the GGUF bytes the model uses)
 *     zt_kern_t k;
 *     zt_kern_metal(&k, cpu_kernels, mt);
 *     m.kern = &k;
 *
 * Weights are read where they are: a mapped region whose start is page
 * aligned (and whose allocation spans whole pages) is shared with the GPU
 * without a copy; any other is copied once. A product whose weights are not
 * in a mapped region, or of another type (zt_q8_t rows, as --convert loads),
 * is declined and runs on the CPU. One forward pass at a time may use it.
 * The shader is compiled from source when zt_metal_new runs, so the
 * Command Line Tools are enough (no Xcode metal compiler). */
#ifndef ZT_METAL_H
#define ZT_METAL_H

#include "zt_kern.h"

typedef struct zt_metal zt_metal_t;

/* The system's default GPU with the kernels compiled, or null (no Metal
 * device, or the shader did not compile: err says why when errcap > 0). */
zt_metal_t *zt_metal_new(char *err, uint32_t errcap);
void zt_metal_free(zt_metal_t *mt);
/* The GPU's name ("Apple M3 Max"). */
const char *zt_metal_name(const zt_metal_t *mt);
/* Let the GPU read [base, base + len). 0 on success. At most 16 regions. */
int zt_metal_map(zt_metal_t *mt, const void *base, uint64_t len);
/* zt_kern_t.mm_full (fctx is the zt_metal_t). */
int zt_metal_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n);
/* out = base's kernels (CPU, and its threads for the rest of the forward
 * pass) with the matrix products on the GPU. */
void zt_kern_metal(zt_kern_t *out, const zt_kern_t *base, zt_metal_t *mt);

typedef struct {
    uint64_t calls;    /* zt_metal_mm calls */
    uint64_t products; /* products run on the GPU */
    uint64_t declined; /* calls left to the CPU */
    uint64_t gpu_ns;   /* GPU time of the products */
    uint64_t wall_ns;  /* wall time inside zt_metal_mm (copies, encoding, waiting) */
    uint32_t nocopy;   /* mapped regions shared without a copy */
} zt_metal_stats_t;
void zt_metal_stats(const zt_metal_t *mt, zt_metal_stats_t *out);

#endif /* ZT_METAL_H */
