/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_cuda_ptx.h — stand-ins for the CUDA built-ins the kernels use, so
 * clang can compile zt_cuda_source() to PTX with no CUDA toolkit installed
 * (make test-cuda, verify-all). Put before the source; the pragma makes every
 * plain function a device function, as NVRTC's -default-device does. */
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))
struct zst_dim3 {
    unsigned x, y, z;
};
extern const __device__ zst_dim3 threadIdx, blockIdx, blockDim;
__device__ long long __shfl_down_sync(unsigned mask, long long v, unsigned off);
__device__ unsigned atomicAdd(unsigned *a, unsigned v);
#pragma clang force_cuda_host_device begin
