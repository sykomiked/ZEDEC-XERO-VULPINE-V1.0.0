/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_cuda.h — the analyser's heavy stage (zxva_analyse.h's backend: the
 * windowed zero-phase FFT, magnitudes and peak mask of a batch of frames) on
 * an NVIDIA GPU. HOSTED Linux and Windows only (tools, the app, servers such
 * as an RTX A6000 box); never part of a kernel image.
 *
 * Nothing CUDA is needed to BUILD it: the driver (libcuda) and the run-time
 * compiler (NVRTC) are opened with dlopen when zxva_cuda_new runs, as
 * kernel/src/tensor/zt_cuda.c does. With no driver, NVRTC or GPU it returns
 * null and says why; the analyser then runs on the CPU reference.
 *
 * The kernel is zxva_stft_math.h + zxva_stft_kern.h, the C reference's own
 * integer steps compiled for the GPU, so every word it returns is the CPU's
 * (test_zxva_cuda checks it on a GPU; test_zxva_cuda_emu.cpp checks the
 * kernel source on the CPU with no GPU, and clang compiles it to PTX).
 *
 *     zxva_cuda_t *g = zxva_cuda_new(0, err, sizeof err);  (null: no GPU)
 *     zxva_backend_t be;
 *     zxva_cuda_backend(g, &be);
 *     cfg.backend = &be;          (zxva_an_cfg_t; then zxva_analyse as usual)
 *
 * WHAT IS AND IS NOT IMPLEMENTED
 *   - One block of 256 threads per frame, scratch in device memory (2N words
 *     a frame), batches of up to 256 frames per launch (be.batch).
 *   - Inputs are re-sent only when their bytes change (a host copy is kept
 *     and compared), so the channel crosses the bus once per channel.
 *   - Synchronous: one launch per backend call, results copied back before
 *     it returns. No streams overlap, no multi-GPU.
 *   - Not yet run on NVIDIA hardware from this repository's CI; the offline
 *     checks above are what ran. */
#ifndef ZXV_ZXVA_CUDA_H
#define ZXV_ZXVA_CUDA_H

#include <stdint.h>

#include "zxva_analyse.h"

typedef struct zxva_cuda zxva_cuda_t;

/* GPU number `ordinal` with the kernel compiled, or null (err says why). */
zxva_cuda_t *zxva_cuda_new(int ordinal, char *err, uint32_t errcap);
void zxva_cuda_free(zxva_cuda_t *g);
const char *zxva_cuda_name(const zxva_cuda_t *g);

/* fill b to run the stage on g (b->name "cuda") */
void zxva_cuda_backend(zxva_cuda_t *g, zxva_backend_t *b);

/* the CUDA C++ source NVRTC compiles (malloc'd; for the offline checks) */
char *zxva_cuda_source(void);

/* milliseconds of kernel time since zxva_cuda_new (events around launches) */
double zxva_cuda_kernel_ms(const zxva_cuda_t *g);

#endif
