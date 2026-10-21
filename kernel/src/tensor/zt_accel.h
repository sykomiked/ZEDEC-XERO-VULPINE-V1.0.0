/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_accel.h — one GPU for the forward pass, whichever this machine has, for
 * HOSTED builds (the app, zxvd, bench/). It opens the first backend that
 * finds a GPU and hands back its kernels; every backend returns the C
 * reference's bits (rule R1), so a caller never needs to know which one ran:
 *
 *   - Metal (zt_metal.h), Apple GPUs, in macOS builds with ZT_HAVE_METAL
 *     (zt_metal.m and the Metal and Foundation frameworks linked);
 *   - CUDA (zt_cuda.h), NVIDIA, then HIP (zt_hip.h), AMD through ROCm, then
 *     Vulkan (zt_vk.h), any Vulkan GPU, then WebGPU (zt_wgpu.h), through a
 *     native WebGPU library, in every other build. All four open their
 *     driver at run time, so they cost nothing where there is no GPU;
 *     zt_cuda.c, zt_hip.c, zt_vk.c and zt_wgpu.c must be linked;
 *   - WebGPU alone in an Emscripten build (a browser tab).
 *
 * ZT_GPU=metal|cuda|hip|vulkan|webgpu picks one; ZT_GPU=cpu (or off, or
 * none) asks for none, so the caller stays on the CPU. ZT_CUDA_DEVICE and ZT_HIP_DEVICE
 * pick the GPU by number (default 0); ZT_VK_DEVICE does for Vulkan.
 *
 *     zt_accel_t *a = zt_accel_open(err, sizeof err);   (null: CPU only, err says why)
 *     zt_accel_map(a, file, file_bytes);                (the weights)
 *     zt_accel_map_state(a, state_mem, state_bytes);    (for whole passes)
 *     zt_kern_t k;
 *     zt_accel_kern(a, &k, cpu_kernels, true);
 *     m.kern = &k;
 *
 * A pass or product the GPU cannot take falls back to the CPU with the same
 * bits. Shared state memory should be aligned to zt_accel_state_align()
 * and its length rounded to it (Metal shares page-aligned memory in place,
 * else copies). Regions stay mapped until zt_accel_free, which must come
 * before the mapped memory is freed. One forward pass at a time may use a
 * zt_accel_t. */
#ifndef ZT_ACCEL_H
#define ZT_ACCEL_H

#include <stdbool.h>

#include "zt_kern.h"

typedef struct zt_accel zt_accel_t;

/* The first GPU found (or the one ZT_GPU names), or null with err saying
 * why when errcap > 0 (no GPU, no driver, ZT_GPU=cpu, or none built in). */
zt_accel_t *zt_accel_open(char *err, uint32_t errcap);
/* The backend named api ("metal", "cuda", "hip", "vulkan", "webgpu"), whatever ZT_GPU
 * says; null with err as above. zt_choose.h opens each in turn to measure. */
zt_accel_t *zt_accel_open_api(const char *api, char *err, uint32_t errcap);
/* The backends built in, in the default order; returns how many (at most
 * cap written). */
uint32_t zt_accel_apis(const char **out, uint32_t cap);
void zt_accel_free(zt_accel_t *a);

/* "metal", "cuda", "hip", "vulkan" or "webgpu". */
const char *zt_accel_api(const zt_accel_t *a);
/* The GPU's name ("Apple M3 Max", "NVIDIA RTX A6000"). */
const char *zt_accel_name(const zt_accel_t *a);

/* Weights: [base, base + len) on the GPU, read only. 0 on success. */
int zt_accel_map(zt_accel_t *a, const void *base, uint64_t len);
/* Model state the GPU writes (the KV cache), shared with the CPU. 0 on
 * success. Each backend holds at most 16 regions, weights included. */
int zt_accel_map_state(zt_accel_t *a, void *base, uint64_t len);

/* The GPU takes products and whole passes of lo..hi tokens and leaves the
 * rest to the CPU kernels (default 0..UINT32_MAX: all). zt_choose.h sets
 * this from what it measured, e.g. prompts on the GPU and one-token steps
 * on the CPU when that is faster. May change between passes. */
void zt_accel_set_tokens(zt_accel_t *a, uint32_t lo, uint32_t hi);
/* zt_accel_set_tokens(a, n, UINT32_MAX). */
void zt_accel_set_min_tokens(zt_accel_t *a, uint32_t n);

/* out = base with the matrix products on the GPU; resident: the whole
 * forward pass too (needs the state mapped with zt_accel_map_state). Call
 * once per zt_accel_t: out's hooks point into it. */
void zt_accel_kern(zt_accel_t *a, zt_kern_t *out, const zt_kern_t *base, bool resident);

/* Alignment and length granule for zt_accel_map_state memory (65536: every
 * page size the backends meet, 4 KiB to 64 KiB). */
uint64_t zt_accel_state_align(void);

/* The backends' common counters (zt_metal_stats_t and its twins). */
typedef struct {
    uint64_t calls;           /* product calls offered */
    uint64_t products;        /* products run on the GPU */
    uint64_t declined;        /* calls left to the CPU */
    uint64_t gpu_ns;          /* GPU time (0 where the backend has no timer) */
    uint64_t wall_ns;         /* wall time inside the backend */
    uint64_t passes;          /* whole forward passes on the GPU */
    uint64_t passes_declined; /* passes left to the products or the CPU */
    uint64_t pass_ops;        /* dispatches in those passes */
} zt_accel_stats_t;
void zt_accel_stats(const zt_accel_t *a, zt_accel_stats_t *out);

#endif /* ZT_ACCEL_H */
