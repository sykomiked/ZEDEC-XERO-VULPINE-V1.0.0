/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_gpu.h — the vectoriser's per-pixel stages (zvt_kern.h) on a GPU.
 * Hosted builds only (Linux, Windows; dlopen of the vendor libraries at run
 * time, nothing linked at build time). Each backend fills a zvt_backend_t
 * (zui_vtrace.h) whose stages give the CPU reference's bits:
 *
 *   CUDA    zvt_cuda.c: zvt_kern.h itself, stringified, compiled by NVRTC
 *           when zvt_cuda_new runs (cubin for the GPU's architecture, else
 *           PTX); libcuda and libnvrtc opened with dlopen. Offline proof
 *           (no GPU): test_vtrace_gpu.sh compiles the generated source to
 *           PTX for sm_75 / sm_86 / sm_90 with clang and runs the same
 *           kernels on the CPU (test_zvt_cuda_emu.cpp) against the C
 *           reference.
 *   Vulkan  zvt_vk.c + zvt_vk.comp (the GLSL port of zvt_kern.h, line for
 *           line), SPIR-V made by gen_zvt_vk_spv.sh; the loader opened with
 *           dlopen. Built only with the Vulkan headers and ZVT_VK_SPV_H
 *           defined, otherwise stubs. Proved on any Vulkan device present
 *           (lavapipe included) by test_zvt_gpu.
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   On the GPU: smoothing, palette assignment, the edge map, the fidelity
 *   sums (per-row SSE and the SSIM windows) and the scene-change diff.
 *   On the CPU still: median cut and the k-means averaging, labelling and
 *   region merging, chains, curve fitting, layering, emitting and the
 *   zui_vec re-render; so a GPU speeds up the per-pixel share only.
 *   Each call copies its inputs to the device and the results back (no
 *   residency across calls). No Metal backend: zvt_kern.h is written in
 *   the subset Metal also accepts, but no zvt_metal.m exists yet.
 */
#ifndef ZXV_ZVT_GPU_H
#define ZXV_ZVT_GPU_H

#include <stdint.h>

#include "zui_vtrace.h"

typedef struct {
    uint64_t calls, fallbacks; /* stage calls taken, and refused (CPU ran)   */
    uint64_t bytes_in, bytes_out;
    uint64_t ns; /* wall time inside the backend's calls                   */
} zvt_gpu_stats_t;

/* ---- CUDA ---------------------------------------------------------------- */
typedef struct zvt_cuda zvt_cuda_t;
/* ordinal: the device number. NULL with a reason in err when there is no
 * driver, no GPU, no NVRTC, or the kernels do not build. */
zvt_cuda_t *zvt_cuda_new(int ordinal, char *err, uint32_t errcap);
void zvt_cuda_free(zvt_cuda_t *g);
const char *zvt_cuda_name(const zvt_cuda_t *g);
void zvt_cuda_backend(zvt_cuda_t *g, zvt_backend_t *out);
void zvt_cuda_stats(const zvt_cuda_t *g, zvt_gpu_stats_t *out);
/* The CUDA C++ source NVRTC compiles (malloc'd; NULL when out of memory). */
char *zvt_cuda_source(void);

/* ---- Vulkan -------------------------------------------------------------- */
typedef struct zvt_vk zvt_vk_t;
/* index: the physical device, or < 0 for the first discrete GPU, else the
 * first integrated one, else the first (ZVT_VK_DEVICE overrides). */
zvt_vk_t *zvt_vk_new(int index, char *err, uint32_t errcap);
void zvt_vk_free(zvt_vk_t *g);
const char *zvt_vk_name(const zvt_vk_t *g);
void zvt_vk_backend(zvt_vk_t *g, zvt_backend_t *out);
void zvt_vk_stats(const zvt_vk_t *g, zvt_gpu_stats_t *out);

#endif /* ZXV_ZVT_GPU_H */
