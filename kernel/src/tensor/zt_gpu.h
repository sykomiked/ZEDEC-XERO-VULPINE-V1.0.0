/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_gpu.h — the whole forward pass as one list of GPU kernel launches
 * (zt_kern.h fwd_full), for HOSTED builds. Roadmap 3.9, next step.
 *
 * Why: with only the matrix products on the GPU (zt_metal.h mm_full), every
 * product is a round trip (the CPU norms, rotates and attends between them),
 * about 197 per token on Qwen2.5-1.5B, ~110 us each on an M3 Max. Here every
 * step of every layer runs on the device: RMS norm, the activation
 * quantiser, the products and their bias, RoPE, the KV store, attention,
 * the residual and SiLU, then the output norm and the logits. The CPU looks
 * up the token rows and the RoPE angles, starts the list once, and reads the
 * logits back once: one hand-off per forward pass.
 *
 * Exact: every kernel is zt_metal_math.h, zt_model.c's own integer steps,
 * one output per thread, so the logits are bit for bit the CPU's
 * (test_zt_gpu runs the list in C, kernel by kernel, against zt_model.c).
 *
 * A backend gives three things: host-visible work memory, a constants
 * buffer (gains, biases, the exp2 table), and a way to run a list.
 * zt_metal.m is one (Apple GPUs, unified memory); zg_sim below is the C one
 * the tests use. A Vulkan or CUDA backend implements the same three. */
#ifndef ZT_GPU_H
#define ZT_GPU_H

#include "zt_model.h"

/* kernels (zt_metal_math.h zk_*) */
enum {
    ZG_K_RMS_INV,
    ZG_K_RMS_APPLY,
    ZG_K_ACT,
    ZG_K_MM,
    ZG_K_BIAS,
    ZG_K_ROPE,
    ZG_K_KV,
    ZG_K_ATT_Q,
    ZG_K_ATT_SCORE,
    ZG_K_ATT_MAX,
    ZG_K_ATT_EXP,
    ZG_K_ATT_SUM,
    ZG_K_ATT_WV,
    ZG_K_RESID,
    ZG_K_SILU,
    ZG_NK
};
/* their names in the shader ("zk_rms_inv", ...) */
extern const char *const zg_kernel_name[ZG_NK];

/* where a buffer argument points */
enum { ZG_B_NONE, ZG_B_WORK, ZG_B_CONST, ZG_B_HOST };

typedef struct {
    uint32_t kind;
    uint64_t off;     /* ZG_B_WORK / ZG_B_CONST: byte offset */
    const void *host; /* ZG_B_HOST: weights or the KV cache, in place */
    uint64_t len;     /* ZG_B_HOST: bytes the kernel may touch from host */
    bool inplace;     /* ZG_B_HOST: must be that memory itself, never a copy (the KV cache) */
} zg_buf_t;

typedef struct {
    uint32_t k;     /* ZG_K_* */
    uint32_t u[16]; /* zg_p: u[0] = threads; ZG_K_MM: u[0] = rows * B */
    zg_buf_t b[6];
} zg_op_t;

typedef struct {
    void *ctx;
    /* host-visible work memory of at least bytes (null: decline) */
    void *(*work)(void *ctx, uint64_t bytes);
    /* host-visible constants memory of at least bytes, filled on every
     * pass (gains, biases and the exp2 table: ~0.6 MB on Qwen2.5-1.5B, a
     * memcpy in unified memory; no cache that a reloaded model could
     * outlive) */
    void *(*cst)(void *ctx, uint64_t bytes);
    /* run the ops in order (each sees every earlier op's writes), with the
     * work buffer's first 4 bytes as the saturation counter; 0 = done */
    int (*run)(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst);
} zg_backend_t;

/* Whether the pass can run on a backend: Q8_0 / Q4_K / Q6_K weights in place
 * (cols a multiple of 32, of 256 for K-quants), RoPE over the whole head. */
bool zg_supported(const zt_model_t *m);

/* zt_kern.h fwd_full on a backend (be is the zg_backend_t). */
int zg_forward(const zg_backend_t *be, const zt_model_t *m, zt_model_state_t *s, uint32_t B,
               zt_fx *logits, uint32_t first, uint32_t *n_sat);

/* ops of the last zg_forward on this thread (for stats and tests) */
uint32_t zg_last_ops(void);

/* ---- the C backend: every kernel run as C, one output at a time ----
 * Slow; it exists to prove the op list (zg_forward's offsets, shapes and
 * order) against zt_model.c without a GPU. */
typedef struct zg_sim zg_sim_t;
zg_sim_t *zg_sim_new(void);
void zg_sim_free(zg_sim_t *g);
/* zt_kern.h fwd_full; fctx is the zg_sim_t */
int zg_sim_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
               uint32_t *n_sat);

#endif /* ZT_GPU_H */
