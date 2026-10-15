/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_vk.h — the forward pass on any Vulkan GPU (AMD Radeon, Arm Mali,
 * Qualcomm Adreno, Intel, NVIDIA), for HOSTED Linux, Windows and Android
 * builds. The same two levels as zt_metal.h and zt_cuda.h:
 *
 *   - zt_kern_vk: the matrix products (zt_kern_t.mm_full) for GGUF Q8_0,
 *     Q4_K and Q6_K rows;
 *   - zt_kern_vk_resident: the whole forward pass (zt_gpu.h's op list,
 *     zt_kern_t.fwd_full) in one command buffer.
 *
 * The kernels are zt_vk.comp, zt_metal_math.h's integer steps in GLSL, built
 * to SPIR-V by glslangValidator when this file is compiled (kernel/Makefile
 * vk-shaders; the build defines ZT_VK_SPV_H as the generated header). So
 * every logit is bit for bit what the CPU computes (rule R1; test_zt_vk
 * checks it, also on lavapipe, Mesa's Vulkan on the CPU, with no GPU).
 *
 * No Vulkan library is linked: the loader (libvulkan.so.1, vulkan-1.dll) is
 * opened when zt_vk_new runs. With no loader, no GPU with 64-bit integers in
 * shaders (shaderInt64), or a build without the Vulkan headers or the SPIR-V,
 * zt_vk_new returns null and says why; callers stay on the CPU.
 *
 *     zt_vk_t *g = zt_vk_new(-1, err, sizeof err);   (null: no usable GPU)
 *     zt_vk_map(g, file, file_bytes);     (weights: in VRAM once)
 *     zt_kern_t k;
 *     zt_kern_vk(&k, cpu_kernels, g);
 *     m.kern = &k;
 *
 * Memory. zt_vk_map puts a read-only region (the weights) on the GPU once.
 * zt_vk_map_shared shares a region the GPU writes (the model state, whose
 * KV cache the CPU path reads too): imported in place where the driver
 * allows it (VK_EXT_external_memory_host), else mirrored and copied in and
 * out around each pass (slower, the same bits). Work memory is host-visible
 * GPU memory. A product whose weights are larger than the GPU lets one
 * binding see (maxStorageBufferRange, 128 MB on some phones) is split by
 * rows. One forward pass at a time may use a zt_vk_t.
 *
 * Test knobs (environment): ZT_VK_NO_IMPORT=1 mirrors shared regions as a
 * driver without host import would; ZT_VK_MAX_RANGE=<bytes> lowers the
 * binding limit so products split by rows as on a phone; ZT_VK_LIB names the
 * loader. */
#ifndef ZT_VK_H
#define ZT_VK_H

#include "zt_kern.h"

typedef struct zt_vk zt_vk_t;

/* GPU number `index` in the loader's order, or -1 for the first discrete
 * GPU (else the first one), with the kernels loaded; null with err when no
 * usable GPU (errcap > 0). ZT_VK_DEVICE=<n> overrides -1. */
zt_vk_t *zt_vk_new(int index, char *err, uint32_t errcap);
void zt_vk_free(zt_vk_t *g);

/* The GPU's name ("AMD Radeon RX 7900 XTX", "llvmpipe ..."). */
const char *zt_vk_name(const zt_vk_t *g);

/* Put [base, base + len) on the GPU (weights; later host writes are not
 * seen). 0 on success. At most 16 regions, mapped and shared together. */
int zt_vk_map(zt_vk_t *g, const void *base, uint64_t len);

/* Share [base, base + len) with the GPU, which writes it (the model state).
 * 0 on success. */
int zt_vk_map_shared(zt_vk_t *g, void *base, uint64_t len);

/* zt_kern_t.mm_full (fctx is the zt_vk_t). */
int zt_vk_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n);

/* out = base's kernels with the matrix products on the GPU. */
void zt_kern_vk(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g);

/* out = zt_kern_vk's, plus the whole forward pass on the GPU. Needs the
 * model state shared with zt_vk_map_shared. A pass it cannot take falls
 * back to the per-product path, then to the CPU, with the same bits. */
void zt_kern_vk_resident(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g);

/* zt_kern_t.fwd_full (fctx is the zt_vk_t). */
int zt_vk_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
              uint32_t *n_sat);

/* Decline products of fewer than n tokens (default 0: run every one). */
void zt_vk_set_min_tokens(zt_vk_t *g, uint32_t n);

/* The same fields as zt_metal_stats_t, so callers print either. */
typedef struct {
    uint64_t calls;           /* zt_vk_mm calls */
    uint64_t products;        /* products run on the GPU */
    uint64_t declined;        /* calls left to the CPU */
    uint64_t gpu_ns;          /* GPU time of the products and passes (timestamps; 0 if none) */
    uint64_t wall_ns;         /* wall time inside zt_vk_mm and zt_vk_fwd */
    uint32_t nocopy;          /* shared regions imported in place (not mirrored) */
    uint64_t passes;          /* whole forward passes run on the GPU */
    uint64_t passes_declined; /* passes left to the per-product path or the CPU */
    uint64_t pass_ops;        /* dispatches in those passes */
} zt_vk_stats_t;
void zt_vk_stats(const zt_vk_t *g, zt_vk_stats_t *out);

#endif /* ZT_VK_H */
