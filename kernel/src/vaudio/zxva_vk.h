/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_vk.h — the analyser's heavy stage (zxva_analyse.h's backend) on a
 * Vulkan device: NVIDIA (an RTX A6000 server), AMD, Intel, Arm, Qualcomm, or
 * lavapipe (Mesa's Vulkan on the CPU, which is how CI and this repository's
 * offline checks run it). HOSTED only; never part of a kernel image.
 *
 * The loader (libvulkan) is opened at run time, not linked. The kernel is
 * zxva_vk.comp built to SPIR-V by gen_zxva_vk_spv.sh at build time and
 * included as ZXVA_VK_SPV_H; it includes zxva_stft_math.h and
 * zxva_stft_kern.h, the same source the C reference runs, so the words it
 * returns are the CPU's (test_zxva_vk compares every one). Built without the
 * Vulkan headers or without ZXVA_VK_SPV_H, the file compiles to stubs whose
 * zxva_vk_new returns null with the reason, so every hosted build links.
 *
 *     zxva_vk_t *g = zxva_vk_new(-1, err, sizeof err);  (-1: first discrete
 *                                                         GPU, else any)
 *     zxva_backend_t be;
 *     zxva_vk_backend(g, &be);
 *     cfg.backend = &be;
 *
 * WHAT IS AND IS NOT IMPLEMENTED
 *   - Needs shaderInt64; one workgroup of 256 per frame; up to 256 frames per
 *     dispatch; host-visible buffers (no staging copies; on a discrete GPU
 *     the kernel reads them over the bus, which is fine for this stage's
 *     ratio of arithmetic to bytes but not optimal).
 *   - Inputs are rewritten only when their bytes change.
 *   - Synchronous: one submit and fence wait per backend call.
 *   - Ran here on lavapipe only; not yet on NVIDIA hardware. */
#ifndef ZXV_ZXVA_VK_H
#define ZXV_ZXVA_VK_H

#include <stdint.h>

#include "zxva_analyse.h"

typedef struct zxva_vk zxva_vk_t;

/* device `index` (-1: ZXVA_VK_DEVICE, else the first discrete GPU, else the
 * first device), or null (err says why) */
zxva_vk_t *zxva_vk_new(int index, char *err, uint32_t errcap);
void zxva_vk_free(zxva_vk_t *g);
const char *zxva_vk_name(const zxva_vk_t *g);
void zxva_vk_backend(zxva_vk_t *g, zxva_backend_t *b); /* b->name "vulkan" */
/* milliseconds of GPU time (timestamps) since zxva_vk_new; 0 without them */
double zxva_vk_kernel_ms(const zxva_vk_t *g);

#endif
