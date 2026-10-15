/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_wgpu.h — the forward pass on WebGPU: natively through a WebGPU library
 * (wgpu-native, Dawn) for HOSTED builds, and in a browser tab through
 * Emscripten's WebGPU bindings (emdawnwebgpu), phones included. The same two
 * levels as zt_vk.h:
 *
 *   - zt_kern_wgpu: the matrix products (zt_kern_t.mm_full) for GGUF Q8_0,
 *     Q4_K and Q6_K rows;
 *   - zt_kern_wgpu_resident: the whole forward pass (zt_gpu.h's op list,
 *     zt_kern_t.fwd_full) in one submission.
 *
 * The kernels are zt_wgpu.wgsl: zt_vk.comp's build without 64-bit integers
 * (WGSL has none, and no 8- or 16-bit storage), line for line, so every
 * logit is bit for bit what the CPU computes (rule R1: integers only;
 * test_zt_wgpu checks it). The build turns the WGSL into a C string
 * (gen_zt_wgpu_wgsl.sh; the build defines ZT_WGPU_WGSL_H as that header).
 *
 * The API is the standard C one, webgpu.h (webgpu-native/webgpu-headers, as
 * wgpu-native and Dawn ship it as <webgpu/webgpu.h>). Natively no WebGPU
 * library is linked: it is opened when zt_wgpu_new runs (ZT_WGPU_LIB names
 * it, else libwgpu_native.so, libwebgpu_dawn.so and their macOS and Windows
 * names), and must be built from the same webgpu.h as this file. Under
 * Emscripten the functions are the bindings themselves, and waiting for the
 * GPU uses wgpuInstanceWaitAny, which needs -sASYNCIFY or -sJSPI (a browser
 * cannot block otherwise). With no library, no adapter, or a build without
 * webgpu.h or the WGSL, zt_wgpu_new returns null and says why; callers stay
 * on the CPU.
 *
 *     zt_wgpu_t *g = zt_wgpu_new(err, sizeof err);   (null: no usable GPU)
 *     zt_wgpu_map(g, file, file_bytes);              (weights: on the GPU once)
 *     zt_kern_t k;
 *     zt_kern_wgpu(&k, cpu_kernels, g);
 *     m.kern = &k;
 *
 * Sizes from the adapter's limits. The products' workgroup (rows x threads
 * per row) and token tile come from maxComputeInvocationsPerWorkgroup,
 * maxComputeWorkgroupSizeX, maxComputeWorkgroupStorageSize and
 * maxStorageBufferBindingSize: 4 rows x 32 threads x 8 tokens (8 KiB of
 * workgroup memory) where a binding may reach 1 GiB (desktops), 2 x 32 x 4
 * (2 KiB) below that (phones and WebGPU's default limits), smaller still
 * where the limits demand it. Products larger than one binding are split by
 * rows; weights larger than one buffer (maxBufferSize, 256 MiB by default)
 * are held in overlapping buffers, each row inside one. Integer sums are
 * exact, so the sizes never change a bit.
 *
 * Memory. WebGPU cannot share host memory with the GPU: zt_wgpu_map copies a
 * read-only region (the weights) to the GPU once; zt_wgpu_map_shared mirrors
 * a region the GPU writes (the model state): the bytes a pass reads are
 * copied in before it, and the bytes it writes in place (the KV cache) are
 * copied back after it. One forward pass at a time may use a zt_wgpu_t.
 *
 * Test knobs (environment): ZT_WGPU_TILE=R,L,T sets the products' rows per
 * workgroup, threads per row and tokens per tile (used when the limits allow
 * it); ZT_WGPU_MAX_BINDING=<bytes> lowers the binding limit (row slicing as
 * on a phone); ZT_WGPU_MAX_BUFFER=<bytes> lowers the buffer limit (weights in
 * several buffers); ZT_WGPU_LIB names the library. */
#ifndef ZT_WGPU_H
#define ZT_WGPU_H

#include "zt_kern.h"

typedef struct zt_wgpu zt_wgpu_t;

/* The adapter WebGPU prefers for high performance, with the kernels built;
 * null with err when there is no usable one (errcap > 0). */
zt_wgpu_t *zt_wgpu_new(char *err, uint32_t errcap);
void zt_wgpu_free(zt_wgpu_t *g);

/* The adapter's name ("llvmpipe (LLVM 17.0.6, 256 bits)", "Apple M3 Max"). */
const char *zt_wgpu_name(const zt_wgpu_t *g);

/* The sizes in use: rows per product workgroup, threads per row, tokens per
 * tile, and threads per layer-kernel workgroup. */
typedef struct {
    uint32_t rows, lanes, tile, wg;
    uint64_t max_binding, max_buffer; /* the limits the slicing uses */
} zt_wgpu_sizes_t;
void zt_wgpu_sizes(const zt_wgpu_t *g, zt_wgpu_sizes_t *out);

/* Put [base, base + len) on the GPU (weights; later host writes are not
 * seen). 0 on success. At most 16 regions, mapped and shared together. */
int zt_wgpu_map(zt_wgpu_t *g, const void *base, uint64_t len);

/* Mirror [base, base + len), which the GPU writes (the model state), on the
 * GPU. 0 on success. Must fit one buffer (maxBufferSize). */
int zt_wgpu_map_shared(zt_wgpu_t *g, void *base, uint64_t len);

/* zt_kern_t.mm_full (fctx is the zt_wgpu_t). */
int zt_wgpu_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n);

/* out = base's kernels with the matrix products on the GPU. */
void zt_kern_wgpu(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g);

/* out = zt_kern_wgpu's, plus the whole forward pass on the GPU. Needs the
 * model state mapped with zt_wgpu_map_shared. A pass it cannot take falls
 * back to the per-product path, then to the CPU, with the same bits. */
void zt_kern_wgpu_resident(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g);

/* zt_kern_t.fwd_full (fctx is the zt_wgpu_t). */
int zt_wgpu_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                uint32_t *n_sat);

/* Decline products of fewer than n tokens (default 0: run every one). */
void zt_wgpu_set_min_tokens(zt_wgpu_t *g, uint32_t n);

/* The same fields as zt_vk_stats_t, so callers print either. */
typedef struct {
    uint64_t calls;           /* zt_wgpu_mm calls */
    uint64_t products;        /* products run on the GPU */
    uint64_t declined;        /* calls left to the CPU */
    uint64_t gpu_ns;          /* 0: no GPU timer is used */
    uint64_t wall_ns;         /* wall time inside zt_wgpu_mm and zt_wgpu_fwd */
    uint32_t nocopy;          /* 0: WebGPU always mirrors shared regions */
    uint64_t passes;          /* whole forward passes run on the GPU */
    uint64_t passes_declined; /* passes left to the per-product path or the CPU */
    uint64_t pass_ops;        /* dispatches in those passes */
} zt_wgpu_stats_t;
void zt_wgpu_stats(const zt_wgpu_t *g, zt_wgpu_stats_t *out);

#endif /* ZT_WGPU_H */
