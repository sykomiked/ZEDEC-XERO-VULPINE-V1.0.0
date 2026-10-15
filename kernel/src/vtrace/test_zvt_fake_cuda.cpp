/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zvt_fake_cuda.cpp — a stand-in libcuda + libnvrtc (one shared
 * library) for testing zvt_cuda.c's host side with no GPU: device memory is
 * host memory, "compiling" checks that the program is exactly
 * zvt_cuda_source(), and cuLaunchKernel runs the very kernels of that
 * source (built into this library, as test_zvt_cuda_emu.cpp does) over the
 * whole grid it is given. Loaded by test_zvt_gpu through ZVT_CUDA_LIB and
 * ZVT_NVRTC_LIB, it drives every path of zvt_cuda.c: loading, compiling,
 * module and function lookup, buffers, copies, launches and arguments.
 * Not a CUDA implementation; only what zvt_cuda.c calls.
 *
 *   test_zvt_gpu --source > DIR/zvt_cuda_gen.cu
 *   (DIR/zvt_cuda_gen.c: const char zvt_fake_expected[] = { its bytes, 0 };)
 *   g++ -std=c++14 -shared -fPIC -IDIR test_zvt_fake_cuda.cpp DIR/zvt_cuda_gen.c -o libfakecuda.so
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define __global__
struct zst_dim3 {
    unsigned x, y, z;
};
static zst_dim3 threadIdx, blockIdx, blockDim;

#include "zvt_cuda_gen.cu"

static std::string g_src; /* what nvrtcCreateProgram was given */
static int g_launches;

extern "C" {
typedef unsigned long long CUdeviceptr;
extern const char zvt_fake_expected[]; /* the generated source again (a byte array) */

int cuInit(unsigned)
{
    return 0;
}
int cuDeviceGetCount(int *n)
{
    return *n = 1, 0;
}
int cuDeviceGet(int *d, int i)
{
    return *d = i, 0;
}
int cuDeviceGetName(char *s, int n, int)
{
    return snprintf(s, (size_t) n, "fake CUDA (CPU, test_zvt_fake_cuda)"), 0;
}
int cuDeviceGetAttribute(int *v, int a, int)
{
    return *v = a == 75 ? 8 : 6, 0;
}
int cuDevicePrimaryCtxRetain(void **c, int)
{
    return *c = (void *) &g_launches, 0;
}
int cuDevicePrimaryCtxRelease_v2(int)
{
    return 0;
}
int cuCtxSetCurrent(void *)
{
    return 0;
}
int cuCtxSynchronize(void)
{
    return 0;
}
int cuModuleLoadData(void **m, const void *img)
{
    return *m = (void *) img ? (void *) &g_src : 0, img ? 0 : 1;
}
int cuModuleUnload(void *)
{
    return 0;
}
int cuModuleGetFunction(void **f, void *, const char *name)
{
    static const char *const names[] = {"zvk_smooth", "zvk_quant", "zvk_edges",
                                        "zvk_sse",    "zvk_win",   "zvk_diff"};
    for (uintptr_t i = 0; i < 6; i++)
        if (!strcmp(name, names[i])) return *f = (void *) (i + 1), 0;
    return 500; /* CUDA_ERROR_NOT_FOUND */
}
int cuMemAlloc_v2(CUdeviceptr *d, size_t n)
{
    void *p = malloc(n);
    *d = (CUdeviceptr) (uintptr_t) p;
    return p ? 0 : 2;
}
int cuMemFree_v2(CUdeviceptr d)
{
    return free((void *) (uintptr_t) d), 0;
}
int cuMemcpyHtoD_v2(CUdeviceptr d, const void *h, size_t n)
{
    return memcpy((void *) (uintptr_t) d, h, n), 0;
}
int cuMemcpyDtoH_v2(void *h, CUdeviceptr d, size_t n)
{
    return memcpy(h, (const void *) (uintptr_t) d, n), 0;
}
int cuGetErrorString(int, const char **s)
{
    return *s = "fake CUDA error", 0;
}

#define P(i, T) (*(T *) args[i])
#define D(i, T) ((T) (uintptr_t) P(i, CUdeviceptr))
int cuLaunchKernel(void *f, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by,
                   unsigned bz, unsigned, void *, void **args, void **)
{
    if (gy != 1 || gz != 1 || by != 1 || bz != 1 || !bx) return 1;
    blockDim = {bx, 1, 1};
    g_launches++;
    for (unsigned b = 0; b < gx; b++)
        for (unsigned t = 0; t < bx; t++) {
            blockIdx = {b, 0, 0};
            threadIdx = {t, 0, 0};
            switch ((uintptr_t) f) {
            case 1:
                zvk_smooth(D(0, const zu *), D(1, zu *), P(2, zu), P(3, zu), P(4, zu));
                break;
            case 2:
                zvk_quant(D(0, const zu *), P(1, zu), D(2, const zu *), P(3, zu), D(4, zb *));
                break;
            case 3:
                zvk_edges(D(0, const zb *), P(1, zu), P(2, zu), D(3, zb *));
                break;
            case 4:
                zvk_sse(D(0, const zu *), D(1, const zu *), P(2, zu), P(3, zu), D(4, zu *));
                break;
            case 5:
                zvk_win(D(0, const zu *), D(1, const zu *), P(2, zu), P(3, zu), P(4, zu), P(5, zu),
                        P(6, zu), P(7, zu), D(8, zu *));
                break;
            case 6:
                zvk_diff(D(0, const zu *), D(1, const zu *), P(2, zu), P(3, zu), P(4, zu),
                         D(5, zb *), D(6, zu *));
                break;
            default:
                return 1;
            }
        }
    return 0;
}

/* NVRTC: accepts only the source this library was built from */
int nvrtcCreateProgram(void **p, const char *src, const char *, int, const char *const *,
                       const char *const *)
{
    g_src = src;
    return *p = (void *) &g_src, 0;
}
int nvrtcCompileProgram(void *, int n, const char *const *opts)
{
    bool arch = false;
    for (int i = 0; i < n; i++) arch |= !strncmp(opts[i], "--gpu-architecture=sm_86", 24);
    return arch && g_src == zvt_fake_expected ? 0 : 6;
}
int nvrtcGetPTXSize(void *, size_t *n)
{
    return *n = 16, 0;
}
int nvrtcGetPTX(void *, char *s)
{
    return memcpy(s, "// fake ptx\n\0\0\0", 16), 0;
}
int nvrtcGetCUBINSize(void *, size_t *n)
{
    return *n = 16, 0;
}
int nvrtcGetCUBIN(void *, char *s)
{
    return memcpy(s, "fake cubin\0\0\0\0\0", 16), 0;
}
int nvrtcGetProgramLogSize(void *, size_t *n)
{
    return *n = 1, 0;
}
int nvrtcGetProgramLog(void *, char *s)
{
    return *s = 0, 0;
}
int nvrtcDestroyProgram(void **)
{
    return 0;
}
}
