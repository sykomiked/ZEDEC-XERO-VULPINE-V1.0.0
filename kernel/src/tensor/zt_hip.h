/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_hip.h — the forward pass on an AMD GPU (ROCm / HIP), for HOSTED Linux
 * and Windows builds (the app, bench/, tests, servers). The same two levels
 * as zt_cuda.h, with the same kernels:
 *
 *   - zt_kern_hip: the matrix products (zt_kern_t.mm_full) for GGUF Q8_0,
 *     Q4_K and Q6_K rows;
 *   - zt_kern_hip_resident: the whole forward pass (zt_gpu.h's op list,
 *     zt_kern_t.fwd_full), every layer and the logits in one stream.
 *
 * The kernels are zt_metal_math.h, the C reference's integer steps, compiled
 * for the GPU at run time, so every logit is bit for bit what the CPU
 * computes (rule R1; test_zt_hip checks it on random matrices and whole
 * passes). They are correct on 64-lane wavefronts (GCN, CDNA) and 32-lane
 * ones (RDNA in wave32 mode) alike: the products sum in groups of 32 lanes.
 *
 * No ROCm SDK is needed to BUILD this file: it loads the HIP runtime
 * (libamdhip64) and its run-time compiler (hiprtc) with dlopen when
 * zt_hip_new runs, so one binary runs everywhere and only uses a GPU where
 * one is installed. With no runtime, no hiprtc or no GPU, zt_hip_new returns
 * null and says why; callers stay on the CPU. ZT_HIP_LIB and ZT_HIPRTC_LIB
 * name the libraries when they are not on the loader's path.
 *
 *     zt_hip_t *g = zt_hip_new(0, err, sizeof err);   (null: no GPU here)
 *     zt_hip_map(g, file, file_bytes);    (weights: copied to VRAM once)
 *     zt_kern_t k;
 *     zt_kern_hip(&k, cpu_kernels, g);
 *     m.kern = &k;
 *
 * Memory. A discrete GPU has its own memory, so zt_hip_map COPIES a region
 * to the device once (the weights never change). The model state is
 * different: the KV cache is written by the GPU and read by the CPU path
 * too, so zt_kern_hip_resident needs it shared in place with
 * zt_hip_map_shared (page-locked and mapped, read over the bus). The pass's
 * own work memory lives on the device and crosses the bus twice per pass:
 * inputs in, logits and hidden state out. One forward pass at a time may use
 * a zt_hip_t. */
#ifndef ZT_HIP_H
#define ZT_HIP_H

#include "zt_kern.h"

typedef struct zt_hip zt_hip_t;

/* GPU number `ordinal` (0: the first) with the kernels compiled, or null
 * (no HIP runtime, no hiprtc, no such GPU, or the kernels did not compile:
 * err says why when errcap > 0). */
zt_hip_t *zt_hip_new(int ordinal, char *err, uint32_t errcap);
void zt_hip_free(zt_hip_t *g);

/* The GPU's name ("AMD Instinct MI210"). */
const char *zt_hip_name(const zt_hip_t *g);

/* Copy [base, base + len) to the GPU, to be found there by host address
 * (weights). 0 on success. At most 16 regions, mapped and shared together. */
int zt_hip_map(zt_hip_t *g, const void *base, uint64_t len);

/* Share [base, base + len) with the GPU in place, without a copy: page-locks
 * it and maps it into the GPU's address space (the model state, whose KV
 * cache the GPU writes). base should be page aligned. 0 on success. */
int zt_hip_map_shared(zt_hip_t *g, void *base, uint64_t len);

/* zt_kern_t.mm_full (fctx is the zt_hip_t). */
int zt_hip_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n);

/* out = base's kernels with the matrix products on the GPU. */
void zt_kern_hip(zt_kern_t *out, const zt_kern_t *base, zt_hip_t *g);

/* out = zt_kern_hip's, plus the whole forward pass on the GPU. Needs the
 * model state shared with zt_hip_map_shared. A pass it cannot take falls
 * back to the per-product path, then to the CPU, with the same bits. */
void zt_kern_hip_resident(zt_kern_t *out, const zt_kern_t *base, zt_hip_t *g);

/* zt_kern_t.fwd_full (fctx is the zt_hip_t). */
int zt_hip_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
               uint32_t *n_sat);

/* Decline products of fewer than n tokens (default 0: run every one). */
void zt_hip_set_min_tokens(zt_hip_t *g, uint32_t n);

/* The same fields as zt_metal_stats_t and zt_cuda_stats_t, so callers print
 * any of them. */
typedef struct {
    uint64_t calls;           /* zt_hip_mm calls */
    uint64_t products;        /* products run on the GPU */
    uint64_t declined;        /* calls left to the CPU */
    uint64_t gpu_ns;          /* GPU time of the products and passes (stream events) */
    uint64_t wall_ns;         /* wall time inside zt_hip_mm and zt_hip_fwd */
    uint32_t nocopy;          /* regions shared in place (zt_hip_map_shared) */
    uint64_t passes;          /* whole forward passes run on the GPU */
    uint64_t passes_declined; /* passes left to the per-product path or the CPU */
    uint64_t pass_ops;        /* kernel launches in those passes */
} zt_hip_stats_t;
void zt_hip_stats(const zt_hip_t *g, zt_hip_stats_t *out);

/* The HIP C++ source zt_hip_new compiles (prelude, zt_metal_math.h, the
 * product kernels and one wrapper per zt_gpu.h kernel), for tests that
 * compile it offline. Caller frees. */
char *zt_hip_source(void);

#endif /* ZT_HIP_H */
