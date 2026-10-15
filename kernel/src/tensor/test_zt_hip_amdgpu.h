/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_hip_amdgpu.h — the HIP built-ins the kernels use, so clang can
 * compile zt_hip_source() for AMD GPUs (-x hip --cuda-device-only
 * -nogpuinc -nogpulib) with no ROCm installed (make test-hip-offline,
 * verify-all). Put before the source. Each is what ROCm's own headers define
 * (clr hipamd/include/hip/amd_detail: amd_hip_runtime.h, amd_warp_functions.h,
 * amd_hip_atomic.h), built on the same clang built-ins, so the code clang
 * emits here is the code hiprtc emits on a real system. The source itself
 * turns zt_metal_math.h's plain functions into device functions. */
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))
struct zst_dim3 {
    unsigned x, y, z;
};
__device__ static inline zst_dim3 zst_thread_idx(void)
{
    return {__builtin_amdgcn_workitem_id_x(), __builtin_amdgcn_workitem_id_y(),
            __builtin_amdgcn_workitem_id_z()};
}
__device__ static inline zst_dim3 zst_block_idx(void)
{
    return {__builtin_amdgcn_workgroup_id_x(), __builtin_amdgcn_workgroup_id_y(),
            __builtin_amdgcn_workgroup_id_z()};
}
/* the workgroup size from the HSA dispatch packet (u16 at bytes 4, 6, 8) */
__device__ static inline zst_dim3 zst_block_dim(void)
{
    const __attribute__((address_space(4))) unsigned short *d =
        (const __attribute__((address_space(4))) unsigned short *) __builtin_amdgcn_dispatch_ptr();
    return {d[2], d[3], d[4]};
}
#define threadIdx zst_thread_idx()
#define blockIdx  zst_block_idx()
#define blockDim  zst_block_dim()

/* amd_warp_functions.h: __lane_id and __shfl_down (int, long long) */
__device__ static inline unsigned zst_lane_id(void)
{
    return __builtin_amdgcn_mbcnt_hi(-1, __builtin_amdgcn_mbcnt_lo(-1, 0));
}
__device__ static inline int __shfl_down(int var, unsigned lane_delta, int width)
{
    int self = (int) zst_lane_id();
    int index = self + (int) lane_delta;
    index = (int) ((self & (width - 1)) + lane_delta) >= width ? self : index;
    return __builtin_amdgcn_ds_bpermute(index << 2, var);
}
__device__ static inline long long __shfl_down(long long var, unsigned lane_delta, int width)
{
    int tmp[2];
    __builtin_memcpy(tmp, &var, sizeof(tmp));
    tmp[0] = __shfl_down(tmp[0], lane_delta, width);
    tmp[1] = __shfl_down(tmp[1], lane_delta, width);
    unsigned long long t = ((unsigned long long) (unsigned) tmp[1] << 32) | (unsigned) tmp[0];
    long long r;
    __builtin_memcpy(&r, &t, sizeof(r));
    return r;
}

/* amd_hip_atomic.h: atomicAdd(unsigned *, unsigned) */
__device__ static inline unsigned atomicAdd(unsigned *a, unsigned v)
{
    return __hip_atomic_fetch_add(a, v, __ATOMIC_RELAXED, __HIP_MEMORY_SCOPE_AGENT);
}
