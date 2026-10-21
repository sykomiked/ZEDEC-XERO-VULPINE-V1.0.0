/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_cuda.h — the forward pass on an NVIDIA GPU (CUDA), for HOSTED Linux and
 * Windows builds (the app, bench/, tests, servers). The same two levels as
 * zt_metal.h, with the same kernels:
 *
 *   - zt_kern_cuda: the matrix products (zt_kern_t.mm_full) for GGUF Q8_0,
 *     Q4_K and Q6_K rows;
 *   - zt_kern_cuda_resident: the whole forward pass (zt_gpu.h's op list,
 *     zt_kern_t.fwd_full), every layer and the logits in one stream.
 *
 * The kernels are zt_metal_math.h, the C reference's integer steps, compiled
 * for the GPU at run time, so every logit is bit for bit what the CPU
 * computes (rule R1; test_zt_cuda checks it on random matrices and whole
 * passes).
 *
 * No CUDA toolkit is needed to BUILD this file: it loads the driver
 * (libcuda) and the run-time compiler (NVRTC) with dlopen when
 * zt_cuda_new runs, so one binary runs everywhere and only uses a GPU where
 * one is installed. With no driver, no NVRTC or no GPU, zt_cuda_new returns
 * null and says why; callers stay on the CPU.
 *
 *     zt_cuda_t *g = zt_cuda_new(0, err, sizeof err);   (null: no GPU here)
 *     zt_cuda_map(g, file, file_bytes);    (weights: copied to VRAM once)
 *     zt_kern_t k;
 *     zt_kern_cuda(&k, cpu_kernels, g);
 *     m.kern = &k;
 *
 * Memory. A discrete GPU has its own memory, so zt_cuda_map COPIES a region
 * to the device once (the weights never change). The model state is
 * different: the KV cache is written by the GPU and read by the CPU path
 * too, so zt_kern_cuda_resident needs it shared in place with
 * zt_cuda_map_shared (page-locked and mapped, read over the bus). The
 * pass's own work memory lives on the device and crosses the bus twice per
 * pass: inputs in, logits and hidden state out. One forward pass at a time
 * may use a zt_cuda_t. */
#ifndef ZT_CUDA_H
#define ZT_CUDA_H

#include "zt_kern.h"

typedef struct zt_cuda zt_cuda_t;

/* GPU number `ordinal` (0: the first) with the kernels compiled, or null
 * (no driver, no NVRTC, no such GPU, or the kernels did not compile: err
 * says why when errcap > 0). */
zt_cuda_t *zt_cuda_new(int ordinal, char *err, uint32_t errcap);
void zt_cuda_free(zt_cuda_t *g);

/* The GPU's name ("NVIDIA RTX A6000"). */
const char *zt_cuda_name(const zt_cuda_t *g);

/* Copy [base, base + len) to the GPU, to be found there by host address
 * (weights). 0 on success. At most 16 regions, mapped and shared together. */
int zt_cuda_map(zt_cuda_t *g, const void *base, uint64_t len);

/* Share [base, base + len) with the GPU in place, without a copy: page-locks
 * it and maps it into the GPU's address space (the model state, whose KV
 * cache the GPU writes). base should be page aligned. 0 on success. */
int zt_cuda_map_shared(zt_cuda_t *g, void *base, uint64_t len);

/* zt_kern_t.mm_full (fctx is the zt_cuda_t). */
int zt_cuda_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n);

/* out = base's kernels with the matrix products on the GPU. */
void zt_kern_cuda(zt_kern_t *out, const zt_kern_t *base, zt_cuda_t *g);

/* out = zt_kern_cuda's, plus the whole forward pass on the GPU. Needs the
 * model state shared with zt_cuda_map_shared. A pass it cannot take falls
 * back to the per-product path, then to the CPU, with the same bits. */
void zt_kern_cuda_resident(zt_kern_t *out, const zt_kern_t *base, zt_cuda_t *g);

/* zt_kern_t.fwd_full (fctx is the zt_cuda_t). */
int zt_cuda_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                uint32_t *n_sat);

/* Decline products of fewer than n tokens (default 0: run every one). */
void zt_cuda_set_min_tokens(zt_cuda_t *g, uint32_t n);

/* The same fields as zt_metal_stats_t, so callers print either. */
typedef struct {
    uint64_t calls;           /* zt_cuda_mm calls */
    uint64_t products;        /* products run on the GPU */
    uint64_t declined;        /* calls left to the CPU */
    uint64_t gpu_ns;          /* GPU time of the products and passes (stream events) */
    uint64_t wall_ns;         /* wall time inside zt_cuda_mm and zt_cuda_fwd */
    uint32_t nocopy;          /* regions shared in place (zt_cuda_map_shared) */
    uint64_t passes;          /* whole forward passes run on the GPU */
    uint64_t passes_declined; /* passes left to the per-product path or the CPU */
    uint64_t pass_ops;        /* kernel launches in those passes */
} zt_cuda_stats_t;
void zt_cuda_stats(const zt_cuda_t *g, zt_cuda_stats_t *out);

/* The CUDA C++ source zt_cuda_new compiles (prelude, zt_metal_math.h, the
 * product kernels and one wrapper per zt_gpu.h kernel), for tests that
 * compile it offline. Caller frees. */
char *zt_cuda_source(void);

#endif /* ZT_CUDA_H */
