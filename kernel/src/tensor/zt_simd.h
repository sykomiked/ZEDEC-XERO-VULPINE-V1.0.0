/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd.h — faster kernel sets for the forward pass's matrix products, for
 * HOSTED builds only (the app, bench/, tests). Kernel images never link
 * zt_simd*.c: they keep zt_kern.h's portable C reference.
 *
 * Every kernel set returns exactly the C reference's bits (test_zt_simd
 * checks each one against it on random and edge-case inputs, and on whole
 * forward passes). Using one:
 *
 *     zt_model_load(&m, ...);
 *     m.kern = zt_simd_default();   (best SIMD for this CPU + a thread pool)
 *
 * Kernel sets:
 *   c        the C reference (zt_kern.h); always present
 *   avx2     x86-64 AVX2 (run-time CPU check)
 *   avx512vnni  avx512, with the AVX512-VNNI batched (prefill) tile
 *   avx512   x86-64 AVX-512 F/BW/VL/DQ (run-time CPU check)
 *   sve      AArch64 SVE, any vector length (Linux: run-time AT_HWCAP check)
 *   neon     AArch64 Advanced SIMD (every AArch64 CPU, Apple M-series too)
 *   rvv      RISC-V vector 1.0, any VLEN (Linux: run-time AT_HWCAP check;
 *            compiled in only when zt_simd_rvv.c is built with the V
 *            extension, e.g. -march=rv64gcv for that file)
 * Threads: zt_pool_* splits a matrix's rows across worker threads (pthreads;
 * not on Windows builds, where the kernels run on the calling thread). Each
 * output row is computed by one thread with the same kernel, so the result
 * does not depend on the thread count or on scheduling. */
#ifndef ZT_SIMD_H
#define ZT_SIMD_H

#include "zt_kern.h"

/* The kernel sets this CPU can run, fastest first, the C reference last.
 * Returns how many were written (at most cap). */
uint32_t zt_simd_list(const zt_kern_t **out, uint32_t cap);
/* Upper bound on the kernel sets zt_simd_list can return (all ISAs). */
#define ZT_SIMD_SETS 7u
/* The fastest kernel set this CPU can run (never null). */
const zt_kern_t *zt_simd_best(void);
/* A kernel set by name ("c", "avx2", "avx512", "avx512vnni", "sve", "neon", "rvv"), or null when it
 * is not compiled in or this CPU cannot run it. */
const zt_kern_t *zt_simd_find(const char *name);

/* ---- threads ---- */
typedef struct zt_pool zt_pool_t;

/* Logical CPUs online (1 when unknown). */
uint32_t zt_pool_ncpu(void);
/* A pool where the caller plus n_threads - 1 workers share each matrix.
 * Null when n_threads < 2, on allocation failure, or without pthreads. */
zt_pool_t *zt_pool_new(uint32_t n_threads);
void zt_pool_free(zt_pool_t *p);
/* out = base's kernels, with rows split over the pool (pool may be null:
 * then out runs on the calling thread). One pool serves one forward pass at
 * a time. */
void zt_kern_threaded(zt_kern_t *out, const zt_kern_t *base, zt_pool_t *pool);

/* The process-wide default, made once (thread-safe): zt_simd_best() plus a
 * pool of zt_pool_ncpu() threads (at most 16). The environment can override:
 * ZT_KERN=name picks a kernel set, ZT_THREADS=n the thread count (1 = none).
 * One forward pass at a time may use it. */
const zt_kern_t *zt_simd_default(void);

/* Per-ISA entry points (zt_simd_x86.c, zt_simd_sve.c, zt_simd_neon.c,
 * zt_simd_rvv.c): the kernel set, or
 * null when it is not compiled for this target or the CPU lacks it. */
const zt_kern_t *zt_simd_x86_avx2(void);
const zt_kern_t *zt_simd_x86_avx512(void);
const zt_kern_t *zt_simd_x86_avx512vnni(void);
const zt_kern_t *zt_simd_sve(void);
const zt_kern_t *zt_simd_neon(void);
const zt_kern_t *zt_simd_rvv(void);

#endif /* ZT_SIMD_H */
