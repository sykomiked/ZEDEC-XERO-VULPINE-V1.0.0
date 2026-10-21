/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_cuda.c — zt_cuda.h on NVIDIA GPUs. Hosted Linux and Windows; plain C11.
 *
 * The driver API (libcuda / nvcuda.dll) and NVRTC are loaded with dlopen
 * (LoadLibrary on Windows) and declared here by hand, so the build needs no
 * CUDA toolkit; only the functions below are used, by their stable ABI names
 * (the _v2 ones where the driver has them).
 *
 * The kernels are the Metal build's, ported line for line (zt_metal.m):
 *   - products: a block of ZC_R warps takes ZC_R rows, a warp one row; lane
 *     l sums the zt_metal_math.h terms of sub-blocks l, l + 32, ... for each
 *     token of its tile (1 when decoding, ZC_T for a prompt batch); the
 *     lanes' exact 64-bit sums are added with warp shuffles and rounded
 *     once, as zt_kern_round does;
 *   - every other zt_gpu.h kernel: a wrapper, one thread per output.
 * Integer sums are exact in any order, so the result is the CPU's bits.
 *
 * The source is compiled by NVRTC when zt_cuda_new runs: to a cubin for the
 * GPU's own architecture, or to PTX the driver compiles when this NVRTC
 * does not know that architecture. -default-device makes zt_metal_math.h's
 * plain functions device functions, so the shared file needs no CUDA
 * keywords. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include "zt_cuda.h"
#include "zt_gpu.h"

#define ZC_R       4u /* rows (warps) per block */
#define ZC_T       8u /* tokens per tile when B > 1 */
#define ZC_REGIONS 16u
#define ZC_TPB     256u /* threads per block for the layer kernels */

/* ---- the driver API and NVRTC, declared by hand ---- */

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;
typedef struct CUevent_st *CUevent;
typedef int nvrtcResult;
typedef struct _nvrtcProgram *nvrtcProgram;

#define CU_DEVICE_ATTRIBUTE_WARP_SIZE                10
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR 75
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR 76
#define CU_MEMHOSTREGISTER_PORTABLE                  1u
#define CU_MEMHOSTREGISTER_DEVICEMAP                 2u

typedef struct {
    CUresult (*Init)(unsigned);
    CUresult (*DeviceGetCount)(int *);
    CUresult (*DeviceGet)(CUdevice *, int);
    CUresult (*DeviceGetName)(char *, int, CUdevice);
    CUresult (*DeviceGetAttribute)(int *, int, CUdevice);
    CUresult (*DevicePrimaryCtxRetain)(CUcontext *, CUdevice);
    CUresult (*DevicePrimaryCtxRelease)(CUdevice);
    CUresult (*CtxSetCurrent)(CUcontext);
    CUresult (*ModuleLoadData)(CUmodule *, const void *);
    CUresult (*ModuleUnload)(CUmodule);
    CUresult (*ModuleGetFunction)(CUfunction *, CUmodule, const char *);
    CUresult (*MemAlloc)(CUdeviceptr *, size_t);
    CUresult (*MemFree)(CUdeviceptr);
    CUresult (*MemAllocHost)(void **, size_t);
    CUresult (*MemFreeHost)(void *);
    CUresult (*MemHostRegister)(void *, size_t, unsigned);
    CUresult (*MemHostUnregister)(void *);
    CUresult (*MemHostGetDevicePointer)(CUdeviceptr *, void *, unsigned);
    CUresult (*MemcpyHtoD)(CUdeviceptr, const void *, size_t);
    CUresult (*MemcpyHtoDAsync)(CUdeviceptr, const void *, size_t, CUstream);
    CUresult (*MemcpyDtoHAsync)(void *, CUdeviceptr, size_t, CUstream);
    CUresult (*LaunchKernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                             unsigned, CUstream, void **, void **);
    CUresult (*StreamCreate)(CUstream *, unsigned);
    CUresult (*StreamDestroy)(CUstream);
    CUresult (*StreamSynchronize)(CUstream);
    CUresult (*EventCreate)(CUevent *, unsigned);
    CUresult (*EventDestroy)(CUevent);
    CUresult (*EventRecord)(CUevent, CUstream);
    CUresult (*EventElapsedTime)(float *, CUevent, CUevent);
    CUresult (*GetErrorString)(CUresult, const char **);
} cu_api_t;

typedef struct {
    nvrtcResult (*CreateProgram)(nvrtcProgram *, const char *, const char *, int,
                                 const char *const *, const char *const *);
    nvrtcResult (*CompileProgram)(nvrtcProgram, int, const char *const *);
    nvrtcResult (*GetPTXSize)(nvrtcProgram, size_t *);
    nvrtcResult (*GetPTX)(nvrtcProgram, char *);
    nvrtcResult (*GetCUBINSize)(nvrtcProgram, size_t *); /* NVRTC 11.1+; may be null */
    nvrtcResult (*GetCUBIN)(nvrtcProgram, char *);
    nvrtcResult (*GetProgramLogSize)(nvrtcProgram, size_t *);
    nvrtcResult (*GetProgramLog)(nvrtcProgram, char *);
    nvrtcResult (*DestroyProgram)(nvrtcProgram *);
} nvrtc_api_t;

static void *lib_open(const char *name)
{
#ifdef _WIN32
    return (void *) LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void lib_close(void *h)
{
    if (!h) return;
#ifdef _WIN32
    FreeLibrary((HMODULE) h);
#else
    dlclose(h);
#endif
}

/* store symbol `name` of h into the function pointer at fp; false if missing */
static bool lib_sym(void *h, const char *name, void *fp)
{
#ifdef _WIN32
    FARPROC s = GetProcAddress((HMODULE) h, name);
#else
    void *s = dlsym(h, name);
#endif
    memcpy(fp, &s, sizeof s);
    return s != NULL;
}

static void *open_first(const char *const *names, const char *env)
{
    const char *e = getenv(env);
    if (e && *e) return lib_open(e);
    for (; *names; names++) {
        void *h = lib_open(*names);
        if (h) return h;
    }
    return NULL;
}

static const char *const k_cuda_libs[] = {
#ifdef _WIN32
    "nvcuda.dll",
#else
    "libcuda.so.1", "libcuda.so",
#endif
    NULL};

static const char *const k_nvrtc_libs[] = {
#ifdef _WIN32
    "nvrtc64_130_0.dll", "nvrtc64_120_0.dll", "nvrtc64_112_0.dll",
#else
    "libnvrtc.so", "libnvrtc.so.13", "libnvrtc.so.12", "libnvrtc.so.11.2",
#endif
    NULL};

static bool load_cu(void *h, cu_api_t *c)
{
    bool ok = true;
#define S(f, n) ok &= lib_sym(h, n, &c->f)
    S(Init, "cuInit");
    S(DeviceGetCount, "cuDeviceGetCount");
    S(DeviceGet, "cuDeviceGet");
    S(DeviceGetName, "cuDeviceGetName");
    S(DeviceGetAttribute, "cuDeviceGetAttribute");
    S(DevicePrimaryCtxRetain, "cuDevicePrimaryCtxRetain");
    S(DevicePrimaryCtxRelease, "cuDevicePrimaryCtxRelease_v2");
    S(CtxSetCurrent, "cuCtxSetCurrent");
    S(ModuleLoadData, "cuModuleLoadData");
    S(ModuleUnload, "cuModuleUnload");
    S(ModuleGetFunction, "cuModuleGetFunction");
    S(MemAlloc, "cuMemAlloc_v2");
    S(MemFree, "cuMemFree_v2");
    S(MemAllocHost, "cuMemAllocHost_v2");
    S(MemFreeHost, "cuMemFreeHost");
    S(MemHostRegister, "cuMemHostRegister_v2");
    S(MemHostUnregister, "cuMemHostUnregister");
    S(MemHostGetDevicePointer, "cuMemHostGetDevicePointer_v2");
    S(MemcpyHtoD, "cuMemcpyHtoD_v2");
    S(MemcpyHtoDAsync, "cuMemcpyHtoDAsync_v2");
    S(MemcpyDtoHAsync, "cuMemcpyDtoHAsync_v2");
    S(LaunchKernel, "cuLaunchKernel");
    S(StreamCreate, "cuStreamCreate");
    S(StreamDestroy, "cuStreamDestroy_v2");
    S(StreamSynchronize, "cuStreamSynchronize");
    S(EventCreate, "cuEventCreate");
    S(EventDestroy, "cuEventDestroy_v2");
    S(EventRecord, "cuEventRecord");
    S(EventElapsedTime, "cuEventElapsedTime");
    S(GetErrorString, "cuGetErrorString");
#undef S
    return ok;
}

static bool load_nvrtc(void *h, nvrtc_api_t *n)
{
    bool ok = true;
#define S(f, s) ok &= lib_sym(h, s, &n->f)
    S(CreateProgram, "nvrtcCreateProgram");
    S(CompileProgram, "nvrtcCompileProgram");
    S(GetPTXSize, "nvrtcGetPTXSize");
    S(GetPTX, "nvrtcGetPTX");
    S(GetProgramLogSize, "nvrtcGetProgramLogSize");
    S(GetProgramLog, "nvrtcGetProgramLog");
    S(DestroyProgram, "nvrtcDestroyProgram");
#undef S
    /* optional: older NVRTC has PTX only */
    if (!lib_sym(h, "nvrtcGetCUBINSize", &n->GetCUBINSize) ||
        !lib_sym(h, "nvrtcGetCUBIN", &n->GetCUBIN))
        n->GetCUBINSize = 0, n->GetCUBIN = 0;
    return ok;
}

/* ---- the kernels' source ---- */

#define ZT_METAL_SRC(...) #__VA_ARGS__
static const char k_math[] =
#include "zt_metal_math.h"
    ;

static const char k_prelude[] = "typedef long long zl;\n"
                                "typedef unsigned long long zw;\n"
                                "typedef int zi;\n"
                                "typedef unsigned int zu;\n"
                                "typedef short zs;\n"
                                "typedef unsigned char zb;\n"
                                "typedef signed char zc;\n"
                                "#define ZM_DEV\n"
                                "typedef unsigned int zm_ctr;\n"
                                "static inline void zm_count(zm_ctr *c, zu n)\n"
                                "{\n"
                                "    if (n) atomicAdd(c, n);\n"
                                "}\n"
                                "#define ZK_A zu i, zg_p p, zb *b0, zb *b1, zb *b2, zb *b3, "
                                "zb *b4, zb *b5, zm_ctr *ctr\n";

static const char k_kernels[] =
    "\n"
    "struct zm_p { zu rows, nb, astride, B, rowbytes; };\n"
    "\n"
    "/* lane 0 gets the sum of all 32 lanes' values (exact: integers) */\n"
    "static inline zl zm_warp_sum(zl v)\n"
    "{\n"
    "    for (int off = 16; off > 0; off >>= 1) v += __shfl_down_sync(0xffffffffu, v, off);\n"
    "    return v;\n"
    "}\n"
    "\n"
    "template <zu QT, zu T>\n"
    "static inline void zm_body(const zb *w, const zs *a, const zb *ash, const zi *asum, zi *y,\n"
    "                           zm_p p)\n"
    "{\n"
    "    zu sg = threadIdx.x >> 5, lane = threadIdx.x & 31u;\n"
    "    zu r = blockIdx.x * 4u + sg;\n"
    "    if (r >= p.rows) return; /* the whole warp: r depends on sg only */\n"
    "    zu t0 = blockIdx.y * T, nt = p.B - t0 < T ? p.B - t0 : T, sb = p.astride / 32u;\n"
    "    const zb *row = w + (zw) r * p.rowbytes;\n"
    "    zl acc[T];\n"
    "    for (zu t = 0; t < T; t++) acc[t] = 0;\n"
    "    for (zu b = lane; b < p.nb; b += 32u) {\n"
    "        for (zu t = 0; t < T; t++) {\n"
    "            if (t < nt) {\n"
    "                zw tok = t0 + t;\n"
    "                acc[t] += zm_sub(QT, row, b, a + tok * p.astride + b * 32u,\n"
    "                                 (zi) ash[tok * sb + b], asum[tok * sb + b]);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    for (zu t = 0; t < T; t++) {\n"
    "        zl v = zm_warp_sum(acc[t]);\n"
    "        if (lane == 0 && t < nt) y[(zw) (t0 + t) * p.rows + r] = zm_round(v);\n"
    "    }\n"
    "}\n"
    "\n"
    "#define ZM_KERNEL(name, QT, T) \\\n"
    "    extern \"C\" __global__ void name(const zb *w, const zs *a, const zb *ash, \\\n"
    "                                     const zi *asum, zi *y, zm_p p) \\\n"
    "    { zm_body<QT, T>(w, a, ash, asum, y, p); }\n"
    "ZM_KERNEL(zm_q4k_1, 12u, 1u)\n"
    "ZM_KERNEL(zm_q6k_1, 14u, 1u)\n"
    "ZM_KERNEL(zm_q80_1, 8u, 1u)\n"
    "ZM_KERNEL(zm_q4k_t, 12u, 8u)\n"
    "ZM_KERNEL(zm_q6k_t, 14u, 8u)\n"
    "ZM_KERNEL(zm_q80_t, 8u, 8u)\n";

/* one wrapper per zt_gpu.h kernel: thread i of p.u[0] */
static const char k_wrap[] =
    "extern \"C\" __global__ void %s_k(zb *b0, zb *b1, zb *b2, zb *b3, zb *b4, zb *b5,\n"
    "                                  zm_ctr *ctr, zg_p p)\n"
    "{\n"
    "    zu i = blockIdx.x * blockDim.x + threadIdx.x;\n"
    "    if (i < p.u[0]) %s(i, p, b0, b1, b2, b3, b4, b5, ctr);\n"
    "}\n";

char *zt_cuda_source(void)
{
    size_t n = strlen(k_prelude) + strlen(k_math) + strlen(k_kernels) + 1;
    for (int k = 0; k < ZG_NK; k++) n += sizeof k_wrap + 2 * strlen(zg_kernel_name[k]);
    char *src = malloc(n);
    if (!src) return NULL;
    size_t at = (size_t) snprintf(src, n, "%s%s%s", k_prelude, k_math, k_kernels);
    for (int k = 0; k < ZG_NK; k++)
        at += (size_t) snprintf(src + at, n - at, k_wrap, zg_kernel_name[k], zg_kernel_name[k]);
    return src;
}

/* ---- the backend ---- */

typedef struct {
    uint32_t rows, nb, astride, B, rowbytes;
} zc_p_t;

typedef struct {
    const uint8_t *base;
    uint64_t len;
    CUdeviceptr d;
    bool shared; /* the caller's memory itself, mapped (else a device copy) */
} region_t;

/* device memory with a page-locked host twin of the same size */
typedef struct {
    CUdeviceptr d;
    uint8_t *h;
    uint64_t cap, used;
} twin_t;

struct zt_cuda {
    void *libcu, *libnvrtc;
    cu_api_t cu;
    nvrtc_api_t rt;
    CUdevice dev;
    CUcontext ctx;
    CUmodule mod;
    CUstream st;
    CUevent e0, e1;
    CUfunction mm[2][3]; /* [tiled][q4k, q6k, q80] */
    CUfunction zk[ZG_NK];
    region_t reg[ZC_REGIONS];
    uint32_t n_reg;
    twin_t in, out, work, cst;
    char name[128];
    zt_cuda_stats_t st_;
    uint32_t min_tokens;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void say(char *err, uint32_t cap, const char *what, const char *why)
{
    if (!err || !cap) return;
    snprintf(err, cap, "%s%s%s", what, why && *why ? ": " : "", why ? why : "");
}

static const char *cu_str(const zt_cuda_t *g, CUresult r)
{
    const char *s = NULL;
    if (g->cu.GetErrorString) g->cu.GetErrorString(r, &s);
    return s ? s : "unknown CUDA error";
}

/* compile the source for compute capability maj.min; *img is malloc'd */
static int compile(zt_cuda_t *g, int maj, int min, char **img, char *err, uint32_t errcap)
{
    char *src = zt_cuda_source();
    if (!src) return -1;
    char arch[48];
    int r = -1;
    for (int pass = 0; pass < 2 && r; pass++) {
        bool cubin = pass == 0;
        if (cubin && !g->rt.GetCUBIN) continue;
        snprintf(arch, sizeof arch, "--gpu-architecture=%s_%d%d", cubin ? "sm" : "compute", maj,
                 min);
        const char *opts[] = {arch, "-default-device", "--std=c++14"};
        nvrtcProgram prog = NULL;
        if (g->rt.CreateProgram(&prog, src, "zt_cuda.cu", 0, NULL, NULL)) continue;
        if (g->rt.CompileProgram(prog, 3, opts) == 0) {
            size_t sz = 0;
            if ((cubin ? g->rt.GetCUBINSize(prog, &sz) : g->rt.GetPTXSize(prog, &sz)) == 0 && sz) {
                *img = malloc(sz);
                if (*img && (cubin ? g->rt.GetCUBIN(prog, *img) : g->rt.GetPTX(prog, *img)) == 0)
                    r = 0;
                else
                    free(*img), *img = NULL;
            }
        } else if (!cubin || !g->rt.GetCUBIN) {
            size_t ls = 0;
            if (g->rt.GetProgramLogSize(prog, &ls) == 0 && ls > 1) {
                char *log = malloc(ls);
                if (log && g->rt.GetProgramLog(prog, log) == 0)
                    say(err, errcap, "the CUDA kernels did not compile", log);
                free(log);
            }
        }
        g->rt.DestroyProgram(&prog);
    }
    free(src);
    return r;
}

zt_cuda_t *zt_cuda_new(int ordinal, char *err, uint32_t errcap)
{
    zt_cuda_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->libcu = open_first(k_cuda_libs, "ZT_CUDA_LIB");
    if (!g->libcu || !load_cu(g->libcu, &g->cu)) {
        say(err, errcap, "no CUDA driver", g->libcu ? "libcuda lacks a needed function" : 0);
        zt_cuda_free(g);
        return NULL;
    }
    int n = 0, maj = 0, min = 0, warp = 0;
    CUresult cr = g->cu.Init(0);
    if (!cr) cr = g->cu.DeviceGetCount(&n);
    if (cr || ordinal < 0 || ordinal >= n) {
        say(err, errcap, "no CUDA GPU", cr ? cu_str(g, cr) : 0);
        zt_cuda_free(g);
        return NULL;
    }
    if ((cr = g->cu.DeviceGet(&g->dev, ordinal)) ||
        (cr = g->cu.DeviceGetName(g->name, (int) sizeof g->name - 1, g->dev)) ||
        (cr = g->cu.DeviceGetAttribute(&maj, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                                       g->dev)) ||
        (cr = g->cu.DeviceGetAttribute(&min, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                                       g->dev)) ||
        (cr = g->cu.DeviceGetAttribute(&warp, CU_DEVICE_ATTRIBUTE_WARP_SIZE, g->dev)) ||
        (cr = g->cu.DevicePrimaryCtxRetain(&g->ctx, g->dev)) ||
        (cr = g->cu.CtxSetCurrent(g->ctx))) {
        say(err, errcap, "cannot open the CUDA GPU", cu_str(g, cr));
        zt_cuda_free(g);
        return NULL;
    }
    if (warp != 32) { /* the product kernels assume 32-lane warps */
        say(err, errcap, "the CUDA GPU's warps are not 32 lanes", 0);
        zt_cuda_free(g);
        return NULL;
    }
    g->libnvrtc = open_first(k_nvrtc_libs, "ZT_NVRTC_LIB");
    if (!g->libnvrtc || !load_nvrtc(g->libnvrtc, &g->rt)) {
        say(err, errcap, "no NVRTC (the CUDA run-time compiler, libnvrtc)", 0);
        zt_cuda_free(g);
        return NULL;
    }
    char *img = NULL;
    if (compile(g, maj, min, &img, err, errcap)) {
        if (err && errcap && !*err) say(err, errcap, "the CUDA kernels did not compile", 0);
        zt_cuda_free(g);
        return NULL;
    }
    cr = g->cu.ModuleLoadData(&g->mod, img);
    free(img);
    if (cr) {
        say(err, errcap, "the CUDA kernels did not load", cu_str(g, cr));
        zt_cuda_free(g);
        return NULL;
    }
    static const char *names[2][3] = {{"zm_q4k_1", "zm_q6k_1", "zm_q80_1"},
                                      {"zm_q4k_t", "zm_q6k_t", "zm_q80_t"}};
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < 3; k++)
            if ((cr = g->cu.ModuleGetFunction(&g->mm[t][k], g->mod, names[t][k]))) break;
    for (int k = 0; k < ZG_NK && !cr; k++) {
        char fn[64];
        snprintf(fn, sizeof fn, "%s_k", zg_kernel_name[k]);
        cr = g->cu.ModuleGetFunction(&g->zk[k], g->mod, fn);
    }
    if (!cr) cr = g->cu.StreamCreate(&g->st, 0);
    if (!cr) cr = g->cu.EventCreate(&g->e0, 0);
    if (!cr) cr = g->cu.EventCreate(&g->e1, 0);
    if (cr) {
        say(err, errcap, "a CUDA kernel cannot run on this GPU", cu_str(g, cr));
        zt_cuda_free(g);
        return NULL;
    }
    return g;
}

static void twin_free(zt_cuda_t *g, twin_t *t)
{
    if (t->d) g->cu.MemFree(t->d);
    if (t->h) g->cu.MemFreeHost(t->h);
    memset(t, 0, sizeof *t);
}

void zt_cuda_free(zt_cuda_t *g)
{
    if (!g) return;
    if (g->ctx) {
        g->cu.CtxSetCurrent(g->ctx);
        for (uint32_t i = 0; i < g->n_reg; i++) {
            if (g->reg[i].shared)
                g->cu.MemHostUnregister((void *) g->reg[i].base);
            else
                g->cu.MemFree(g->reg[i].d);
        }
        twin_free(g, &g->in);
        twin_free(g, &g->out);
        twin_free(g, &g->work);
        twin_free(g, &g->cst);
        if (g->e0) g->cu.EventDestroy(g->e0);
        if (g->e1) g->cu.EventDestroy(g->e1);
        if (g->st) g->cu.StreamDestroy(g->st);
        if (g->mod) g->cu.ModuleUnload(g->mod);
        g->cu.DevicePrimaryCtxRelease(g->dev);
    }
    lib_close(g->libnvrtc);
    lib_close(g->libcu);
    free(g);
}

const char *zt_cuda_name(const zt_cuda_t *g)
{
    return g ? g->name : "";
}

int zt_cuda_map(zt_cuda_t *g, const void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZC_REGIONS) return -1;
    if (g->cu.CtxSetCurrent(g->ctx)) return -1;
    CUdeviceptr d = 0;
    if (g->cu.MemAlloc(&d, (size_t) len)) return -1;
    if (g->cu.MemcpyHtoD(d, base, (size_t) len)) {
        g->cu.MemFree(d);
        return -1;
    }
    g->reg[g->n_reg++] = (region_t){base, len, d, false};
    return 0;
}

int zt_cuda_map_shared(zt_cuda_t *g, void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZC_REGIONS) return -1;
    if (g->cu.CtxSetCurrent(g->ctx)) return -1;
    CUdeviceptr d = 0;
    if (g->cu.MemHostRegister(base, (size_t) len,
                              CU_MEMHOSTREGISTER_PORTABLE | CU_MEMHOSTREGISTER_DEVICEMAP))
        return -1;
    if (g->cu.MemHostGetDevicePointer(&d, base, 0)) {
        g->cu.MemHostUnregister(base);
        return -1;
    }
    g->reg[g->n_reg++] = (region_t){base, len, d, true};
    g->st_.nocopy++;
    return 0;
}

static int find_region(const zt_cuda_t *g, const uint8_t *p, uint64_t len, uint64_t *off)
{
    for (uint32_t i = 0; i < g->n_reg; i++) {
        const region_t *r = &g->reg[i];
        if (p >= r->base && (uint64_t) (p - r->base) <= r->len &&
            len <= r->len - (uint64_t) (p - r->base)) {
            *off = (uint64_t) (p - r->base);
            return (int) i;
        }
    }
    return -1;
}

/* make t hold at least need bytes on both sides (contents not kept) */
static int grow(zt_cuda_t *g, twin_t *t, uint64_t need)
{
    t->used = need;
    if (t->cap >= need && t->d) return 0;
    twin_free(g, t);
    uint64_t sz = need + need / 4 + 4096;
    if (g->cu.MemAlloc(&t->d, (size_t) sz)) return -1;
    if (g->cu.MemAllocHost((void **) &t->h, (size_t) sz)) {
        twin_free(g, t);
        return -1;
    }
    t->cap = sz;
    t->used = need;
    return 0;
}

static CUresult launch_mm(zt_cuda_t *g, int k, uint32_t B, zc_p_t *p, CUdeviceptr w, CUdeviceptr a,
                          CUdeviceptr ash, CUdeviceptr asum, CUdeviceptr y)
{
    uint32_t tiled = B > 1, T = tiled ? ZC_T : 1;
    void *args[] = {&w, &a, &ash, &asum, &y, p};
    return g->cu.LaunchKernel(g->mm[tiled][k], (p->rows + ZC_R - 1) / ZC_R, (B + T - 1) / T, 1,
                              32 * ZC_R, 1, 1, 0, g->st, args, NULL);
}

static void add_gpu_time(zt_cuda_t *g)
{
    float ms = 0;
    if (g->cu.EventElapsedTime(&ms, g->e0, g->e1) == 0 && ms > 0)
        g->st_.gpu_ns += (uint64_t) ((double) ms * 1e6);
}

#define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

int zt_cuda_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    zt_cuda_t *g = fctx;
    if (!g || n == 0 || n > 8) return 1;
    uint64_t t_in = now_ns();
    g->st_.calls++;
    if (jobs[0].B < g->min_tokens) {
        g->st_.declined++;
        return 1;
    }
    int ri[8];
    uint64_t woff[8], aoff[8], soff[8], hoff[8], yoff[8];
    uint64_t in_bytes = 0, out_bytes = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        bool ok = j->raw && j->B && j->cols % 32 == 0 && j->astride >= j->cols &&
                  j->astride % 32 == 0 &&
                  (j->qtype == 8 || ((j->qtype == 12 || j->qtype == 14) && j->cols % 256 == 0));
        uint64_t rb = j->qtype == 12   ? (uint64_t) (nb / 8) * 144
                      : j->qtype == 14 ? (uint64_t) (nb / 8) * 210
                                       : (uint64_t) nb * 34;
        if (ok) ri[i] = find_region(g, j->raw, rb * j->rows, &woff[i]);
        if (!ok || ri[i] < 0) {
            g->st_.declined++;
            return 1;
        }
        /* inputs: one copy per distinct activation set (q/k/v share theirs) */
        if (i && j->a == jobs[i - 1].a && j->ash == jobs[i - 1].ash &&
            j->asum == jobs[i - 1].asum && j->astride == jobs[i - 1].astride &&
            j->B == jobs[i - 1].B) {
            aoff[i] = aoff[i - 1], hoff[i] = hoff[i - 1], soff[i] = soff[i - 1];
        } else {
            uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
            aoff[i] = in_bytes;
            soff[i] = aoff[i] + AL((uint64_t) j->B * j->astride * 2);
            hoff[i] = soff[i] + AL(nbk * 4);
            in_bytes = hoff[i] + AL(nbk);
        }
        yoff[i] = out_bytes;
        out_bytes += AL((uint64_t) j->B * j->rows * 4);
    }
    if (g->cu.CtxSetCurrent(g->ctx) || grow(g, &g->in, in_bytes) || grow(g, &g->out, out_bytes)) {
        g->st_.declined++;
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        if (i && aoff[i] == aoff[i - 1]) continue;
        uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
        memcpy(g->in.h + aoff[i], j->a, (size_t) j->B * j->astride * 2);
        memcpy(g->in.h + soff[i], j->asum, (size_t) nbk * 4);
        memcpy(g->in.h + hoff[i], j->ash, (size_t) nbk);
    }
    CUresult cr = g->cu.MemcpyHtoDAsync(g->in.d, g->in.h, (size_t) in_bytes, g->st);
    if (!cr) cr = g->cu.EventRecord(g->e0, g->st);
    for (uint32_t i = 0; i < n && !cr; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        int k = j->qtype == 12 ? 0 : j->qtype == 14 ? 1 : 2;
        zc_p_t p = {j->rows, nb, j->astride, j->B,
                    k == 0   ? (nb / 8) * 144
                    : k == 1 ? (nb / 8) * 210
                             : nb * 34};
        cr = launch_mm(g, k, j->B, &p, g->reg[ri[i]].d + woff[i], g->in.d + aoff[i],
                       g->in.d + hoff[i], g->in.d + soff[i], g->out.d + yoff[i]);
    }
    if (!cr) cr = g->cu.EventRecord(g->e1, g->st);
    if (!cr) cr = g->cu.MemcpyDtoHAsync(g->out.h, g->out.d, (size_t) out_bytes, g->st);
    CUresult cs = g->cu.StreamSynchronize(g->st);
    if (cr || cs) {
        g->st_.declined++;
        return 1;
    }
    add_gpu_time(g);
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        const zt_fx *src = (const zt_fx *) (g->out.h + yoff[i]);
        for (uint32_t t = 0; t < j->B; t++)
            memcpy(j->y + (uint64_t) t * j->ystride, src + (uint64_t) t * j->rows,
                   (size_t) j->rows * 4);
    }
    g->st_.products += n;
    g->st_.wall_ns += now_ns() - t_in;
    return 0;
}

void zt_kern_cuda(zt_kern_t *out, const zt_kern_t *base, zt_cuda_t *g)
{
    *out = *base;
    out->mm_full = g ? zt_cuda_mm : 0;
    out->fwd_full = 0;
    out->fctx = g;
}

/* ---- the whole forward pass (zt_gpu.h) ---- */

static void *be_work(void *ctx, uint64_t bytes)
{
    zt_cuda_t *g = ctx;
    return grow(g, &g->work, bytes) ? NULL : g->work.h;
}

static void *be_cst(void *ctx, uint64_t bytes)
{
    zt_cuda_t *g = ctx;
    return grow(g, &g->cst, bytes ? bytes : 4) ? NULL : g->cst.h;
}

/* the device address of an op's buffer argument; 1 if it has none */
static int be_addr(const zt_cuda_t *g, const zg_buf_t *b, CUdeviceptr *out)
{
    switch (b->kind) {
    case ZG_B_WORK:
        *out = g->work.d + b->off;
        return 0;
    case ZG_B_CONST:
        *out = g->cst.d + b->off;
        return 0;
    case ZG_B_HOST: {
        uint64_t off;
        int ri = find_region(g, b->host, b->len, &off);
        if (ri < 0 || (b->inplace && !g->reg[ri].shared)) return 1;
        *out = g->reg[ri].d + off;
        return 0;
    }
    default:
        *out = g->work.d;
        return 0; /* unused argument */
    }
}

static int be_run(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst)
{
    zt_cuda_t *g = ctx;
    (void) work, (void) cst;
    /* check every op before anything runs: a pass is all or nothing */
    for (uint32_t i = 0; i < n; i++) {
        const zg_op_t *o = &ops[i];
        CUdeviceptr d;
        if (o->k >= ZG_NK) return 1;
        if (o->k == ZG_K_MM && o->u[0] && o->u[4] != 12 && o->u[4] != 14 && o->u[4] != 8) return 1;
        for (int j = 0; j < 6; j++)
            if (be_addr(g, &o->b[j], &d)) return 1;
    }
    if (g->cu.CtxSetCurrent(g->ctx)) return 1;
    CUresult cr = g->cu.MemcpyHtoDAsync(g->work.d, g->work.h, (size_t) g->work.used, g->st);
    if (!cr) cr = g->cu.MemcpyHtoDAsync(g->cst.d, g->cst.h, (size_t) g->cst.used, g->st);
    if (!cr) cr = g->cu.EventRecord(g->e0, g->st);
    for (uint32_t i = 0; i < n && !cr; i++) {
        const zg_op_t *o = &ops[i];
        if (!o->u[0]) continue;
        CUdeviceptr b[6];
        for (int j = 0; j < 6; j++) be_addr(g, &o->b[j], &b[j]);
        if (o->k == ZG_K_MM) {
            int k = o->u[4] == 12 ? 0 : o->u[4] == 14 ? 1 : 2;
            zc_p_t p = {o->u[1], o->u[2], o->u[3], o->u[6], o->u[5]};
            cr = launch_mm(g, k, o->u[6], &p, b[0], b[1], b[2], b[3], b[4]);
            continue;
        }
        CUdeviceptr ctr = g->work.d; /* the saturation counter, work offset 0 */
        struct {
            uint32_t u[16];
        } p; /* zg_p's layout, passed by value */
        memcpy(&p, o->u, sizeof p);
        void *args[] = {&b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &ctr, &p};
        cr = g->cu.LaunchKernel(g->zk[o->k], (o->u[0] + ZC_TPB - 1) / ZC_TPB, 1, 1, ZC_TPB, 1, 1, 0,
                                g->st, args, NULL);
    }
    if (!cr) cr = g->cu.EventRecord(g->e1, g->st);
    if (!cr) cr = g->cu.MemcpyDtoHAsync(g->work.h, g->work.d, (size_t) g->work.used, g->st);
    CUresult cs = g->cu.StreamSynchronize(g->st);
    if (cr || cs) return 1;
    add_gpu_time(g);
    return 0;
}

int zt_cuda_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                uint32_t *n_sat)
{
    zt_cuda_t *g = fctx;
    if (!g) return 1;
    uint64_t t0 = now_ns();
    zg_backend_t be = {g, be_work, be_cst, be_run};
    int r = zg_forward(&be, m, s, B, logits, first, n_sat);
    if (r) {
        g->st_.passes_declined++;
        return r;
    }
    g->st_.passes++;
    g->st_.pass_ops += zg_last_ops();
    g->st_.wall_ns += now_ns() - t0;
    return 0;
}

void zt_kern_cuda_resident(zt_kern_t *out, const zt_kern_t *base, zt_cuda_t *g)
{
    zt_kern_cuda(out, base, g);
    out->fwd_full = g ? zt_cuda_fwd : 0;
}

void zt_cuda_set_min_tokens(zt_cuda_t *g, uint32_t n)
{
    if (g) g->min_tokens = n;
}

void zt_cuda_stats(const zt_cuda_t *g, zt_cuda_stats_t *out)
{
    if (g && out) *out = g->st_;
}
