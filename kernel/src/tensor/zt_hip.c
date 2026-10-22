/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_hip.c — zt_hip.h on AMD GPUs (ROCm / HIP). Hosted Linux and Windows;
 * plain C11. zt_cuda.c's design, function for function.
 *
 * The HIP runtime (libamdhip64 / amdhip64_N.dll) and hiprtc are loaded with
 * dlopen (LoadLibrary on Windows) and declared here by hand, so the build
 * needs no ROCm SDK. Every declaration below is the public ABI of ROCm's
 * <hip/hip_runtime_api.h> and <hip/hiprtc.h>, checked against ROCm 5.7, 6.2
 * and 7.0 (identical in all three):
 *   - hipError_t, hiprtcResult: C enums (int); hipDevice_t: int;
 *     hipDeviceptr_t: void *; hipModule_t, hipFunction_t, hipStream_t,
 *     hipEvent_t, hiprtcProgram: opaque struct pointers;
 *   - hipDeviceAttributeCanMapHostMemory = 3, hipDeviceAttributeWarpSize =
 *     87 (enum hipDeviceAttribute_t);
 *   - hipHostRegisterPortable = 1, hipHostRegisterMapped = 2,
 *     hipHostMallocDefault = 0;
 *   - hipDeviceProp_t.gcnArchName (char[256]) at byte 1160 of the ROCm 6+
 *     layout (hipGetDevicePropertiesR0600, sizeof 1472) and at byte 396 of
 *     the ROCm 5 one (hipGetDeviceProperties, sizeof 792); the only field read
 *     from that struct, into a buffer larger than either.
 *
 * The kernels are zt_cuda.c's, which are the Metal build's (zt_metal.m):
 *   - products: a block of ZH_R groups of 32 lanes takes ZH_R rows, a group
 *     one row; lane l sums the zt_metal_math.h terms of sub-blocks l,
 *     l + 32, ... for each token of its tile (1 when decoding, ZH_T for a
 *     prompt batch); the lanes' exact 64-bit sums are added with
 *     __shfl_down(v, off, 32) and rounded once, as zt_kern_round does. The
 *     width 32 keeps each sum inside its group, so a 64-lane wavefront (two
 *     groups) and a 32-lane one give the same bits; no lane leaves before
 *     the shuffles;
 *   - every other zt_gpu.h kernel: a wrapper, one thread per output.
 * Integer sums are exact in any order, so the result is the CPU's bits.
 *
 * The source is compiled by hiprtc when zt_hip_new runs, for the GPU's own
 * gcnArchName (--offload-arch=gfx90a:sramecc+:xnack-, ...), to a code object
 * hipModuleLoadData loads. `#pragma clang force_cuda_host_device` around
 * zt_metal_math.h makes its plain functions device functions (hiprtc is
 * clang), so the shared file needs no HIP keywords. */
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

#include "zt_gpu.h"
#include "zt_hip.h"

#define ZH_R       4u /* rows (32-lane groups) per block */
#define ZH_T       8u /* tokens per tile when B > 1 */
#define ZH_REGIONS 16u
#define ZH_TPB     256u /* threads per block for the layer kernels */

/* ---- the HIP runtime and hiprtc, declared by hand (see the top) ---- */

typedef int hipError_t;
typedef int hipDevice_t;
typedef struct ihipModule_t *hipModule_t;
typedef struct ihipModuleSymbol_t *hipFunction_t;
typedef struct ihipStream_t *hipStream_t;
typedef struct ihipEvent_t *hipEvent_t;
typedef int hiprtcResult;
typedef struct _hiprtcProgram *hiprtcProgram;
/* device addresses are 64-bit; kept as integers here (offsets are added) and
 * handed to the runtime as hipDeviceptr_t (void *) */
typedef uint64_t zh_dptr;

#define HIP_ATTR_CAN_MAP_HOST_MEMORY 3
#define HIP_ATTR_WARP_SIZE           87
#define HIP_HOST_REGISTER_PORTABLE   1u
#define HIP_HOST_REGISTER_MAPPED     2u
#define HIP_HOST_MALLOC_DEFAULT      0u
#define HIP_PROP_BYTES               4096u /* > sizeof hipDeviceProp_t, 1472 (ROCm 6+) */
#define HIP_PROP_ARCH_R0600          1160u /* offsetof(hipDeviceProp_tR0600, gcnArchName) */
#define HIP_PROP_ARCH_R0000          396u  /* offsetof(hipDeviceProp_t, gcnArchName), ROCm 5 */

typedef struct {
    hipError_t (*Init)(unsigned);
    hipError_t (*GetDeviceCount)(int *);
    hipError_t (*DeviceGet)(hipDevice_t *, int);
    hipError_t (*DeviceGetName)(char *, int, hipDevice_t);
    hipError_t (*DeviceGetAttribute)(int *, int, int);
    hipError_t (*GetDeviceProperties)(void *, int); /* R0600 or R0000: see prop_arch */
    hipError_t (*SetDevice)(int);
    hipError_t (*ModuleLoadData)(hipModule_t *, const void *);
    hipError_t (*ModuleUnload)(hipModule_t);
    hipError_t (*ModuleGetFunction)(hipFunction_t *, hipModule_t, const char *);
    hipError_t (*Malloc)(void **, size_t);
    hipError_t (*Free)(void *);
    hipError_t (*HostMalloc)(void **, size_t, unsigned);
    hipError_t (*HostFree)(void *);
    hipError_t (*HostRegister)(void *, size_t, unsigned);
    hipError_t (*HostUnregister)(void *);
    hipError_t (*HostGetDevicePointer)(void **, void *, unsigned);
    hipError_t (*MemcpyHtoD)(void *, const void *, size_t);
    hipError_t (*MemcpyHtoDAsync)(void *, const void *, size_t, hipStream_t);
    hipError_t (*MemcpyDtoHAsync)(void *, void *, size_t, hipStream_t);
    hipError_t (*ModuleLaunchKernel)(hipFunction_t, unsigned, unsigned, unsigned, unsigned,
                                     unsigned, unsigned, unsigned, hipStream_t, void **, void **);
    hipError_t (*StreamCreate)(hipStream_t *);
    hipError_t (*StreamDestroy)(hipStream_t);
    hipError_t (*StreamSynchronize)(hipStream_t);
    hipError_t (*EventCreate)(hipEvent_t *);
    hipError_t (*EventDestroy)(hipEvent_t);
    hipError_t (*EventRecord)(hipEvent_t, hipStream_t);
    hipError_t (*EventElapsedTime)(float *, hipEvent_t, hipEvent_t);
    const char *(*GetErrorString)(hipError_t);
} hip_api_t;

typedef struct {
    hiprtcResult (*CreateProgram)(hiprtcProgram *, const char *, const char *, int,
                                  const char *const *, const char *const *);
    hiprtcResult (*CompileProgram)(hiprtcProgram, int, const char *const *);
    hiprtcResult (*GetCodeSize)(hiprtcProgram, size_t *);
    hiprtcResult (*GetCode)(hiprtcProgram, char *);
    hiprtcResult (*GetProgramLogSize)(hiprtcProgram, size_t *);
    hiprtcResult (*GetProgramLog)(hiprtcProgram, char *);
    hiprtcResult (*DestroyProgram)(hiprtcProgram *);
} hiprtc_api_t;

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

static const char *const k_hip_libs[] = {
#ifdef _WIN32
    "amdhip64_7.dll", "amdhip64_6.dll", "amdhip64.dll",
#else
    "libamdhip64.so",
    "libamdhip64.so.7",
    "libamdhip64.so.6",
    "libamdhip64.so.5",
    "/opt/rocm/lib/libamdhip64.so",
#endif
    NULL};

static const char *const k_hiprtc_libs[] = {
#ifdef _WIN32
    "hiprtc0700.dll",
    "hiprtc0605.dll",
    "hiprtc0604.dll",
    "hiprtc0602.dll",
    "hiprtc0601.dll",
    "hiprtc0507.dll",
    "hiprtc.dll",
#else
    "libhiprtc.so",
    "libhiprtc.so.7",
    "libhiprtc.so.6",
    "libhiprtc.so.5",
    "/opt/rocm/lib/libhiprtc.so",
#endif
    NULL};

static bool load_hip(void *h, hip_api_t *c, bool *r0600)
{
    bool ok = true;
#define S(f, n) ok &= lib_sym(h, n, &c->f)
    S(Init, "hipInit");
    S(GetDeviceCount, "hipGetDeviceCount");
    S(DeviceGet, "hipDeviceGet");
    S(DeviceGetName, "hipDeviceGetName");
    S(DeviceGetAttribute, "hipDeviceGetAttribute");
    S(SetDevice, "hipSetDevice");
    S(ModuleLoadData, "hipModuleLoadData");
    S(ModuleUnload, "hipModuleUnload");
    S(ModuleGetFunction, "hipModuleGetFunction");
    S(Malloc, "hipMalloc");
    S(Free, "hipFree");
    S(HostMalloc, "hipHostMalloc");
    S(HostFree, "hipHostFree");
    S(HostRegister, "hipHostRegister");
    S(HostUnregister, "hipHostUnregister");
    S(HostGetDevicePointer, "hipHostGetDevicePointer");
    S(MemcpyHtoD, "hipMemcpyHtoD");
    S(MemcpyHtoDAsync, "hipMemcpyHtoDAsync");
    S(MemcpyDtoHAsync, "hipMemcpyDtoHAsync");
    S(ModuleLaunchKernel, "hipModuleLaunchKernel");
    S(StreamCreate, "hipStreamCreate");
    S(StreamDestroy, "hipStreamDestroy");
    S(StreamSynchronize, "hipStreamSynchronize");
    S(EventCreate, "hipEventCreate");
    S(EventDestroy, "hipEventDestroy");
    S(EventRecord, "hipEventRecord");
    S(EventElapsedTime, "hipEventElapsedTime");
    S(GetErrorString, "hipGetErrorString");
#undef S
    /* ROCm 6 changed hipDeviceProp_t; its header maps hipGetDeviceProperties
     * to the R0600 entry point, which ROCm 5 does not have */
    *r0600 = lib_sym(h, "hipGetDevicePropertiesR0600", &c->GetDeviceProperties);
    if (!*r0600) ok &= lib_sym(h, "hipGetDeviceProperties", &c->GetDeviceProperties);
    return ok;
}

static bool load_hiprtc(void *h, hiprtc_api_t *n)
{
    bool ok = true;
#define S(f, s) ok &= lib_sym(h, s, &n->f)
    S(CreateProgram, "hiprtcCreateProgram");
    S(CompileProgram, "hiprtcCompileProgram");
    S(GetCodeSize, "hiprtcGetCodeSize");
    S(GetCode, "hiprtcGetCode");
    S(GetProgramLogSize, "hiprtcGetProgramLogSize");
    S(GetProgramLog, "hiprtcGetProgramLog");
    S(DestroyProgram, "hiprtcDestroyProgram");
#undef S
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
                                "__device__ static inline void zm_count(zm_ctr *c, zu n)\n"
                                "{\n"
                                "    if (n) atomicAdd(c, n);\n"
                                "}\n"
                                "#define ZK_A zu i, zg_p p, zb *b0, zb *b1, zb *b2, zb *b3, "
                                "zb *b4, zb *b5, zm_ctr *ctr\n"
                                "#pragma clang force_cuda_host_device begin\n";

static const char k_kernels[] =
    "\n"
    "#pragma clang force_cuda_host_device end\n"
    "\n"
    "struct zm_p { zu rows, nb, astride, B, rowbytes; };\n"
    "\n"
    "/* lane 0 of each group of 32 lanes gets the sum of the group's values\n"
    " * (exact: integers). The width 32 keeps every shuffle inside its group,\n"
    " * on a 64-lane wavefront (two groups) as on a 32-lane one. */\n"
    "__device__ static inline zl zm_group_sum(zl v)\n"
    "{\n"
    "    for (int off = 16; off > 0; off >>= 1) v += __shfl_down(v, (unsigned) off, 32);\n"
    "    return v;\n"
    "}\n"
    "\n"
    "template <zu QT, zu T>\n"
    "__device__ static inline void zm_body(const zb *w, const zs *a, const zb *ash,\n"
    "                                      const zi *asum, zi *y, zm_p p)\n"
    "{\n"
    "    zu sg = threadIdx.x >> 5, lane = threadIdx.x & 31u;\n"
    "    zu r = blockIdx.x * 4u + sg;\n"
    "    bool live = r < p.rows; /* no return: every lane reaches the shuffles */\n"
    "    zu t0 = blockIdx.y * T, nt = p.B - t0 < T ? p.B - t0 : T, sb = p.astride / 32u;\n"
    "    const zb *row = w + (zw) (live ? r : 0u) * p.rowbytes;\n"
    "    zl acc[T];\n"
    "    for (zu t = 0; t < T; t++) acc[t] = 0;\n"
    "    for (zu b = lane; live && b < p.nb; b += 32u) {\n"
    "        for (zu t = 0; t < T; t++) {\n"
    "            if (t < nt) {\n"
    "                zw tok = t0 + t;\n"
    "                acc[t] += zm_sub(QT, row, b, a + tok * p.astride + b * 32u,\n"
    "                                 (zi) ash[tok * sb + b], asum[tok * sb + b]);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    for (zu t = 0; t < T; t++) {\n"
    "        zl v = zm_group_sum(acc[t]);\n"
    "        if (live && lane == 0 && t < nt) y[(zw) (t0 + t) * p.rows + r] = zm_round(v);\n"
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

char *zt_hip_source(void)
{
    size_t n = strlen(k_prelude) + strlen(k_math) + strlen(k_kernels) + 1;
    for (int k = 0; k < ZG_NK; k++) n += sizeof k_wrap + 2 * strlen(zg_kernel_name[k]);
    char *src = malloc(n);
    if (!src) return NULL;
    int w = snprintf(src, n, "%s%s%s", k_prelude, k_math, k_kernels);
    size_t at = 0;
    for (int k = 0; w >= 0 && (size_t) w < n - at; k++) {
        at += (size_t) w;
        if (k == ZG_NK) return src;
        w = snprintf(src + at, n - at, k_wrap, zg_kernel_name[k], zg_kernel_name[k]);
    }
    free(src); /* n is sized to fit, so not reached */
    return NULL;
}

/* ---- the backend ---- */

typedef struct {
    uint32_t rows, nb, astride, B, rowbytes;
} zh_p_t;

typedef struct {
    const uint8_t *base;
    uint64_t len;
    zh_dptr d;
    bool shared; /* the caller's memory itself, mapped (else a device copy) */
} region_t;

/* device memory with a page-locked host twin of the same size */
typedef struct {
    zh_dptr d;
    uint8_t *h;
    uint64_t cap, used;
} twin_t;

struct zt_hip {
    void *libhip, *librtc;
    hip_api_t hp;
    hiprtc_api_t rt;
    int ordinal;
    bool open; /* the device was selected: free releases through it */
    hipModule_t mod;
    hipStream_t st;
    hipEvent_t e0, e1;
    hipFunction_t mm[2][3]; /* [tiled][q4k, q6k, q80] */
    hipFunction_t zk[ZG_NK];
    region_t reg[ZH_REGIONS];
    uint32_t n_reg;
    twin_t in, out, work, cst;
    char name[128];
    char arch[256];
    zt_hip_stats_t st_;
    uint32_t min_tokens;
};

static void *dp(zh_dptr d)
{
    return (void *) (uintptr_t) d;
}

static uint64_t now_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t) (c.QuadPart / f.QuadPart) * 1000000000ull +
           (uint64_t) (c.QuadPart % f.QuadPart) * 1000000000ull / (uint64_t) f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
#endif
}

static void say(char *err, uint32_t cap, const char *what, const char *why)
{
    if (!err || !cap) return;
    snprintf(err, cap, "%s%s%s", what, why && *why ? ": " : "", why ? why : "");
}

static const char *hip_str(const zt_hip_t *g, hipError_t r)
{
    const char *s = g->hp.GetErrorString ? g->hp.GetErrorString(r) : NULL;
    return s ? s : "unknown HIP error";
}

/* the device's gcnArchName ("gfx90a:sramecc+:xnack-") into g->arch */
static hipError_t prop_arch(zt_hip_t *g, bool r0600)
{
    uint8_t *prop = calloc(1, HIP_PROP_BYTES);
    if (!prop) return -1;
    hipError_t r = g->hp.GetDeviceProperties(prop, g->ordinal);
    if (!r) {
        const char *a = (const char *) prop + (r0600 ? HIP_PROP_ARCH_R0600 : HIP_PROP_ARCH_R0000);
        size_t n = strnlen(a, sizeof g->arch - 1);
        memcpy(g->arch, a, n);
        g->arch[n] = 0;
    }
    free(prop);
    return r;
}

/* compile the source for g->arch; *img is malloc'd */
static int compile(zt_hip_t *g, char **img, char *err, uint32_t errcap)
{
    char *src = zt_hip_source();
    if (!src) return -1;
    char arch[300];
    snprintf(arch, sizeof arch, "--offload-arch=%s", g->arch);
    const char *opts[] = {arch, "-O3", "-std=c++14"};
    int r = -1;
    hiprtcProgram prog = NULL;
    if (g->rt.CreateProgram(&prog, src, "zt_hip.hip", 0, NULL, NULL) == 0) {
        if (g->rt.CompileProgram(prog, 3, opts) == 0) {
            size_t sz = 0;
            if (g->rt.GetCodeSize(prog, &sz) == 0 && sz) {
                *img = malloc(sz);
                if (*img && g->rt.GetCode(prog, *img) == 0)
                    r = 0;
                else
                    free(*img), *img = NULL;
            }
        } else {
            size_t ls = 0;
            if (g->rt.GetProgramLogSize(prog, &ls) == 0 && ls > 1) {
                char *log = malloc(ls);
                if (log && g->rt.GetProgramLog(prog, log) == 0)
                    say(err, errcap, "the HIP kernels did not compile", log);
                free(log);
            }
        }
        g->rt.DestroyProgram(&prog);
    }
    free(src);
    return r;
}

zt_hip_t *zt_hip_new(int ordinal, char *err, uint32_t errcap)
{
    zt_hip_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    bool r0600 = false;
    g->libhip = open_first(k_hip_libs, "ZT_HIP_LIB");
    if (!g->libhip || !load_hip(g->libhip, &g->hp, &r0600)) {
        say(err, errcap, "no HIP runtime", g->libhip ? "libamdhip64 lacks a needed function" : 0);
        zt_hip_free(g);
        return NULL;
    }
    int n = 0, warp = 0, canmap = 0;
    hipDevice_t dev = 0;
    hipError_t hr = g->hp.Init(0);
    if (!hr) hr = g->hp.GetDeviceCount(&n);
    if (hr || ordinal < 0 || ordinal >= n) {
        say(err, errcap, "no HIP GPU", hr ? hip_str(g, hr) : 0);
        zt_hip_free(g);
        return NULL;
    }
    g->ordinal = ordinal;
    if ((hr = g->hp.SetDevice(ordinal)) || (hr = g->hp.DeviceGet(&dev, ordinal)) ||
        (hr = g->hp.DeviceGetName(g->name, (int) sizeof g->name - 1, dev)) ||
        (hr = g->hp.DeviceGetAttribute(&warp, HIP_ATTR_WARP_SIZE, ordinal)) ||
        (hr = g->hp.DeviceGetAttribute(&canmap, HIP_ATTR_CAN_MAP_HOST_MEMORY, ordinal)) ||
        (hr = prop_arch(g, r0600))) {
        say(err, errcap, "cannot open the HIP GPU", hip_str(g, hr));
        zt_hip_free(g);
        return NULL;
    }
    g->open = true;
    if (warp != 32 && warp != 64) { /* the products sum in 32-lane groups of a block of 128 */
        say(err, errcap, "the HIP GPU's wavefronts are neither 32 nor 64 lanes", 0);
        zt_hip_free(g);
        return NULL;
    }
    if (!canmap) { /* zt_hip_map_shared needs host memory mapped for the GPU */
        say(err, errcap, "the HIP GPU cannot map host memory", 0);
        zt_hip_free(g);
        return NULL;
    }
    if (strncmp(g->arch, "gfx", 3) != 0) {
        say(err, errcap, "the HIP GPU's architecture name is not gfx...", g->arch);
        zt_hip_free(g);
        return NULL;
    }
    g->librtc = open_first(k_hiprtc_libs, "ZT_HIPRTC_LIB");
    if (!g->librtc || !load_hiprtc(g->librtc, &g->rt)) {
        say(err, errcap, "no hiprtc (the HIP run-time compiler, libhiprtc)", 0);
        zt_hip_free(g);
        return NULL;
    }
    char *img = NULL;
    if (compile(g, &img, err, errcap)) {
        if (err && errcap && !*err) say(err, errcap, "the HIP kernels did not compile", g->arch);
        zt_hip_free(g);
        return NULL;
    }
    hr = g->hp.ModuleLoadData(&g->mod, img);
    free(img);
    if (hr) {
        say(err, errcap, "the HIP kernels did not load", hip_str(g, hr));
        zt_hip_free(g);
        return NULL;
    }
    static const char *names[2][3] = {{"zm_q4k_1", "zm_q6k_1", "zm_q80_1"},
                                      {"zm_q4k_t", "zm_q6k_t", "zm_q80_t"}};
    for (int t = 0; t < 2 && !hr; t++)
        for (int k = 0; k < 3 && !hr; k++)
            hr = g->hp.ModuleGetFunction(&g->mm[t][k], g->mod, names[t][k]);
    for (int k = 0; k < ZG_NK && !hr; k++) {
        char fn[64];
        snprintf(fn, sizeof fn, "%s_k", zg_kernel_name[k]);
        hr = g->hp.ModuleGetFunction(&g->zk[k], g->mod, fn);
    }
    if (!hr) hr = g->hp.StreamCreate(&g->st);
    if (!hr) hr = g->hp.EventCreate(&g->e0);
    if (!hr) hr = g->hp.EventCreate(&g->e1);
    if (hr) {
        say(err, errcap, "a HIP kernel cannot run on this GPU", hip_str(g, hr));
        zt_hip_free(g);
        return NULL;
    }
    return g;
}

static void twin_free(zt_hip_t *g, twin_t *t)
{
    if (t->d) g->hp.Free(dp(t->d));
    if (t->h) g->hp.HostFree(t->h);
    memset(t, 0, sizeof *t);
}

void zt_hip_free(zt_hip_t *g)
{
    if (!g) return;
    if (g->open) {
        g->hp.SetDevice(g->ordinal);
        for (uint32_t i = 0; i < g->n_reg; i++) {
            if (g->reg[i].shared)
                g->hp.HostUnregister((void *) g->reg[i].base);
            else
                g->hp.Free(dp(g->reg[i].d));
        }
        twin_free(g, &g->in);
        twin_free(g, &g->out);
        twin_free(g, &g->work);
        twin_free(g, &g->cst);
        if (g->e0) g->hp.EventDestroy(g->e0);
        if (g->e1) g->hp.EventDestroy(g->e1);
        if (g->st) g->hp.StreamDestroy(g->st);
        if (g->mod) g->hp.ModuleUnload(g->mod);
    }
    lib_close(g->librtc);
    lib_close(g->libhip);
    free(g);
}

const char *zt_hip_name(const zt_hip_t *g)
{
    return g ? g->name : "";
}

int zt_hip_map(zt_hip_t *g, const void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZH_REGIONS) return -1;
    if (g->hp.SetDevice(g->ordinal)) return -1;
    void *d = NULL;
    if (g->hp.Malloc(&d, (size_t) len)) return -1;
    if (g->hp.MemcpyHtoD(d, base, (size_t) len)) {
        g->hp.Free(d);
        return -1;
    }
    g->reg[g->n_reg++] = (region_t){base, len, (zh_dptr) (uintptr_t) d, false};
    return 0;
}

int zt_hip_map_shared(zt_hip_t *g, void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZH_REGIONS) return -1;
    if (g->hp.SetDevice(g->ordinal)) return -1;
    void *d = NULL;
    if (g->hp.HostRegister(base, (size_t) len,
                           HIP_HOST_REGISTER_PORTABLE | HIP_HOST_REGISTER_MAPPED))
        return -1;
    if (g->hp.HostGetDevicePointer(&d, base, 0)) {
        g->hp.HostUnregister(base);
        return -1;
    }
    g->reg[g->n_reg++] = (region_t){base, len, (zh_dptr) (uintptr_t) d, true};
    g->st_.nocopy++;
    return 0;
}

static int find_region(const zt_hip_t *g, const uint8_t *p, uint64_t len, uint64_t *off)
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
static int grow(zt_hip_t *g, twin_t *t, uint64_t need)
{
    t->used = need;
    if (t->cap >= need && t->d) return 0;
    twin_free(g, t);
    uint64_t sz = need + need / 4 + 4096;
    void *d = NULL;
    if (g->hp.Malloc(&d, (size_t) sz)) return -1;
    t->d = (zh_dptr) (uintptr_t) d;
    if (g->hp.HostMalloc((void **) &t->h, (size_t) sz, HIP_HOST_MALLOC_DEFAULT)) {
        t->h = NULL;
        twin_free(g, t);
        return -1;
    }
    t->cap = sz;
    t->used = need;
    return 0;
}

static hipError_t launch_mm(zt_hip_t *g, int k, uint32_t B, zh_p_t *p, zh_dptr w, zh_dptr a,
                            zh_dptr ash, zh_dptr asum, zh_dptr y)
{
    uint32_t tiled = B > 1, T = tiled ? ZH_T : 1;
    void *args[] = {&w, &a, &ash, &asum, &y, p};
    return g->hp.ModuleLaunchKernel(g->mm[tiled][k], (p->rows + ZH_R - 1) / ZH_R, (B + T - 1) / T,
                                    1, 32 * ZH_R, 1, 1, 0, g->st, args, NULL);
}

static void add_gpu_time(zt_hip_t *g)
{
    float ms = 0;
    if (g->hp.EventElapsedTime(&ms, g->e0, g->e1) == 0 && ms > 0)
        g->st_.gpu_ns += (uint64_t) ((double) ms * 1e6);
}

#define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

int zt_hip_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    zt_hip_t *g = fctx;
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
    if (g->hp.SetDevice(g->ordinal) || grow(g, &g->in, in_bytes) || grow(g, &g->out, out_bytes)) {
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
    hipError_t hr = g->hp.MemcpyHtoDAsync(dp(g->in.d), g->in.h, (size_t) in_bytes, g->st);
    if (!hr) hr = g->hp.EventRecord(g->e0, g->st);
    for (uint32_t i = 0; i < n && !hr; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        int k = j->qtype == 12 ? 0 : j->qtype == 14 ? 1 : 2;
        zh_p_t p = {j->rows, nb, j->astride, j->B,
                    k == 0   ? (nb / 8) * 144
                    : k == 1 ? (nb / 8) * 210
                             : nb * 34};
        hr = launch_mm(g, k, j->B, &p, g->reg[ri[i]].d + woff[i], g->in.d + aoff[i],
                       g->in.d + hoff[i], g->in.d + soff[i], g->out.d + yoff[i]);
    }
    if (!hr) hr = g->hp.EventRecord(g->e1, g->st);
    if (!hr) hr = g->hp.MemcpyDtoHAsync(g->out.h, dp(g->out.d), (size_t) out_bytes, g->st);
    hipError_t hs = g->hp.StreamSynchronize(g->st);
    if (hr || hs) {
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

void zt_kern_hip(zt_kern_t *out, const zt_kern_t *base, zt_hip_t *g)
{
    *out = *base;
    out->mm_full = g ? zt_hip_mm : 0;
    out->fwd_full = 0;
    out->fctx = g;
}

/* ---- the whole forward pass (zt_gpu.h) ---- */

static void *be_work(void *ctx, uint64_t bytes)
{
    zt_hip_t *g = ctx;
    return grow(g, &g->work, bytes) ? NULL : g->work.h;
}

static void *be_cst(void *ctx, uint64_t bytes)
{
    zt_hip_t *g = ctx;
    return grow(g, &g->cst, bytes ? bytes : 4) ? NULL : g->cst.h;
}

/* the device address of an op's buffer argument; 1 if it has none */
static int be_addr(const zt_hip_t *g, const zg_buf_t *b, zh_dptr *out)
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
    zt_hip_t *g = ctx;
    (void) work, (void) cst;
    /* check every op before anything runs: a pass is all or nothing */
    for (uint32_t i = 0; i < n; i++) {
        const zg_op_t *o = &ops[i];
        zh_dptr d;
        if (o->k >= ZG_NK) return 1;
        if (o->k == ZG_K_MM && o->u[0] && o->u[4] != 12 && o->u[4] != 14 && o->u[4] != 8) return 1;
        for (int j = 0; j < 6; j++)
            if (be_addr(g, &o->b[j], &d)) return 1;
    }
    if (g->hp.SetDevice(g->ordinal)) return 1;
    hipError_t hr = g->hp.MemcpyHtoDAsync(dp(g->work.d), g->work.h, (size_t) g->work.used, g->st);
    if (!hr) hr = g->hp.MemcpyHtoDAsync(dp(g->cst.d), g->cst.h, (size_t) g->cst.used, g->st);
    if (!hr) hr = g->hp.EventRecord(g->e0, g->st);
    for (uint32_t i = 0; i < n && !hr; i++) {
        const zg_op_t *o = &ops[i];
        if (!o->u[0]) continue;
        zh_dptr b[6];
        for (int j = 0; j < 6; j++) be_addr(g, &o->b[j], &b[j]);
        if (o->k == ZG_K_MM) {
            int k = o->u[4] == 12 ? 0 : o->u[4] == 14 ? 1 : 2;
            zh_p_t p = {o->u[1], o->u[2], o->u[3], o->u[6], o->u[5]};
            hr = launch_mm(g, k, o->u[6], &p, b[0], b[1], b[2], b[3], b[4]);
            continue;
        }
        zh_dptr ctr = g->work.d; /* the saturation counter, work offset 0 */
        struct {
            uint32_t u[16];
        } p; /* zg_p's layout, passed by value */
        memcpy(&p, o->u, sizeof p);
        void *args[] = {&b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &ctr, &p};
        hr = g->hp.ModuleLaunchKernel(g->zk[o->k], (o->u[0] + ZH_TPB - 1) / ZH_TPB, 1, 1, ZH_TPB, 1,
                                      1, 0, g->st, args, NULL);
    }
    if (!hr) hr = g->hp.EventRecord(g->e1, g->st);
    if (!hr) hr = g->hp.MemcpyDtoHAsync(g->work.h, dp(g->work.d), (size_t) g->work.used, g->st);
    hipError_t hs = g->hp.StreamSynchronize(g->st);
    if (hr || hs) return 1;
    add_gpu_time(g);
    return 0;
}

int zt_hip_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
               uint32_t *n_sat)
{
    zt_hip_t *g = fctx;
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

void zt_kern_hip_resident(zt_kern_t *out, const zt_kern_t *base, zt_hip_t *g)
{
    zt_kern_hip(out, base, g);
    out->fwd_full = g ? zt_hip_fwd : 0;
}

void zt_hip_set_min_tokens(zt_hip_t *g, uint32_t n)
{
    if (g) g->min_tokens = n;
}

void zt_hip_stats(const zt_hip_t *g, zt_hip_stats_t *out)
{
    if (g && out) *out = g->st_;
}
