/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_vision.h — image input for the language model, in integers.
 *
 *   T24 IMAGE ENCODER.  zt_vision_load reads a llama.cpp "clip" projector
 *       GGUF (mmproj) of the SigLIP family with the Idefics3 pixel-shuffle
 *       projector: SmolVLM-256M/500M-Instruct (Apache-2.0). zt_vision_encode
 *       turns an image_size x image_size RGB image into n_out vectors of the
 *       language model's width, which zt_model_eval_embd feeds in place of
 *       image tokens:
 *         pixels to Q16 ((p / 255 - mean) / std, exact rational), the
 *         patch_size x patch_size patches through the patch matrix (+ bias)
 *         and the learned position table; per layer LayerNorm, Q/K/V (+
 *         bias), non-causal attention, output (+ bias) and residual,
 *         LayerNorm, fc1 + GELU (tanh form, as SigLIP) + fc2 and residual;
 *         the final LayerNorm; pixel shuffle by the scale factor (each s x s
 *         block of patches becomes one vector of s*s*width values, rows
 *         then columns, as llama.cpp and transformers order them); the
 *         projector matrix (no bias).
 *       Every step is zt_nn.h's, so the output bits are the same on every
 *       machine and kernel set.
 *       zt_vision_resize scales any RGB image to image_size x image_size by
 *       exact area averaging (integer weights; enlarging repeats pixels).
 *       The caller puts the vectors where the chat format wants the image
 *       (SmolVLM: <fake_token_around_image><global-img>, the n_out vectors,
 *       <fake_token_around_image>), with zt_model_eval for the text around
 *       them.
 *       HONEST LIMITS.  One global view only: llama.cpp and transformers
 *       also cut images larger than image_size into tiles, which adds detail
 *       (and n_out tokens per tile); not done here. The resize is not
 *       transformers' Lanczos, so results on images that need resizing
 *       differ slightly from theirs. Only the Idefics3 projector and SigLIP
 *       encoder are handled; other mmproj types report ZT_NN_ECONFIG.
 * Freestanding: no libc, no malloc, no floating point. */
#ifndef ZT_VISION_H
#define ZT_VISION_H

#include <stdint.h>
#include <stdbool.h>
#include "zt_nn.h"

typedef struct {
    uint32_t image_size, patch, n_embd, n_ff, n_head, n_layer, proj_dim, scale;
    uint32_t eps_f32;
    zt_fx mean[3], inv_std[3]; /* Q16: x = (p / 255 - mean) * inv_std */
    uint32_t n_patch, n_out;   /* patches per image, output vectors */
} zt_vision_cfg_t;

typedef struct {
    zt_mat_t wq, wk, wv, wo, w1, w2;
    const zt_fx *bq, *bk, *bv, *bo, *b1, *b2;
    zt_nn_norm_t ln1, ln2;
} zt_vision_layer_t;

typedef struct {
    zt_vision_cfg_t cfg;
    zt_mat_t patch_w;
    const zt_fx *patch_b, *pos;
    zt_vision_layer_t *layers;
    zt_nn_norm_t post_ln;
    zt_mat_t proj;
    const struct zt_kern *kern; /* null: C reference; a hosted caller may set one */
} zt_vision_t;

int32_t zt_vision_config(const zt_gguf_t *g, zt_vision_cfg_t *c, zt_model_err_t *err);
uint64_t zt_vision_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the encoder (Q8_0 weights are used in place). */
int32_t zt_vision_load(zt_vision_t *v, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       zt_model_err_t *err);

uint64_t zt_vision_work_bytes(const zt_vision_t *v);
/* rgb: image_size x image_size pixels, 3 bytes each, row-major. out: n_out x
 * proj_dim Q16 values. n_sat (may be null) receives the count of values that
 * hit the Q16 range limit. */
int32_t zt_vision_encode(const zt_vision_t *v, const uint8_t *rgb, zt_fx *out, void *work,
                         uint64_t work_bytes, uint32_t *n_sat);

/* Area-average resize of a w x h RGB image (row stride `stride` bytes) to
 * size x size. */
void zt_vision_resize(const uint8_t *src, uint32_t w, uint32_t h, uint32_t stride, uint8_t *dst,
                      uint32_t size);

#endif /* ZT_VISION_H */
