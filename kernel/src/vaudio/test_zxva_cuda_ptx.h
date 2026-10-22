/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_cuda_ptx.h — stand-ins for the CUDA built-ins zxva_cuda.c's
 * kernel uses, so clang compiles zxva_cuda_source() to PTX with no CUDA
 * toolkit installed (tools/zxva/test_zxva_gpu.sh). Put before the source. */
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))
#define __shared__ __attribute__((shared))
struct zxs_dim3 {
    unsigned x, y, z;
};
extern const __device__ zxs_dim3 threadIdx, blockIdx, blockDim;
__device__ void __syncthreads(void);
__device__ unsigned atomicMax(unsigned *a, unsigned v);
