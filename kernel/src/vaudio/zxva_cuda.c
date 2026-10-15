/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_cuda.c — zxva_cuda.h. Hosted; plain C11. The driver API and NVRTC are
 * declared here by hand and loaded with dlopen (LoadLibrary on Windows), the
 * way kernel/src/tensor/zt_cuda.c does, so the build needs no CUDA toolkit. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include "zxva_cuda.h"

#define ZXC_TPB   256u /* threads per block (one block per frame) */
#define ZXC_BATCH 256u /* frames per launch */

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

#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR 75
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR 76

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
    CUresult (*MemcpyHtoD)(CUdeviceptr, const void *, size_t);
    CUresult (*MemcpyDtoH)(void *, CUdeviceptr, size_t);
    CUresult (*LaunchKernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                             unsigned, CUstream, void **, void **);
    CUresult (*CtxSynchronize)(void);
    CUresult (*EventCreate)(CUevent *, unsigned);
    CUresult (*EventDestroy)(CUevent);
    CUresult (*EventRecord)(CUevent, CUstream);
    CUresult (*EventSynchronize)(CUevent);
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
    S(MemcpyHtoD, "cuMemcpyHtoD_v2");
    S(MemcpyDtoH, "cuMemcpyDtoH_v2");
    S(LaunchKernel, "cuLaunchKernel");
    S(CtxSynchronize, "cuCtxSynchronize");
    S(EventCreate, "cuEventCreate");
    S(EventDestroy, "cuEventDestroy_v2");
    S(EventRecord, "cuEventRecord");
    S(EventSynchronize, "cuEventSynchronize");
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
    if (!lib_sym(h, "nvrtcGetCUBINSize", &n->GetCUBINSize) ||
        !lib_sym(h, "nvrtcGetCUBIN", &n->GetCUBIN))
        n->GetCUBINSize = 0, n->GetCUBIN = 0;
    return ok;
}

/* ---- the kernel's source ---- */
#define ZXVA_K_SRC(...) #__VA_ARGS__
static const char k_math[] =
#include "zxva_stft_math.h"
    ;
static const char k_body[] =
#include "zxva_stft_kern.h"
    ;
#undef ZXVA_K_SRC

static const char k_prelude[] =
    "typedef long long zl;\n"
    "typedef unsigned long long zw;\n"
    "typedef int zi;\n"
    "typedef unsigned int zu;\n"
    "#define ZKC(t, v) ((t) (v))\n"
    "#define ZK_FN static __device__ inline\n"
    "struct zxva_kp { zu n, N, L, num, den, c0lo, c0hi, lenlo, lenhi, k0lo, k0hi,\n"
    "                 floor_mag, rel, nf; };\n"
    "#define ZK_TID threadIdx.x\n"
    "#define ZK_GID blockIdx.x\n"
    "#define ZK_NT blockDim.x\n"
    "#define ZK_BARRIER() __syncthreads()\n"
    "#define ZK_ATOMIC_MAX(v) atomicMax(&s_top, (v))\n";

static const char k_head[] =
    "\nextern \"C\" __global__ void zxva_stft_k(const zi *x, const zi *win, const zi *tw,\n"
    "    zi *scr, zi *re, zi *im, zu *mag, zu *mask, zu *top, zxva_kp p)\n"
    "{\n"
    "    __shared__ zu s_top;\n    ";
static const char k_tail[] = "\n}\n";

char *zxva_cuda_source(void)
{
    size_t n =
        strlen(k_prelude) + strlen(k_math) + strlen(k_head) + strlen(k_body) + strlen(k_tail) + 4;
    char *s = malloc(n);
    if (!s) return NULL;
    int w = snprintf(s, n, "%s%s\n%s%s%s", k_prelude, k_math, k_head, k_body, k_tail);
    if (w < 0 || (size_t) w >= n) {
        free(s);
        return NULL;
    }
    return s;
}

/* ---- the backend ---- */
typedef struct {
    CUdeviceptr d;
    size_t cap;
    void *shadow; /* host copy of what the device holds (inputs) */
    size_t valid; /* bytes of shadow that match the device */
} dbuf_t;

enum { B_X, B_WIN, B_TW, B_SCR, B_RE, B_IM, B_MAG, B_MASK, B_TOP, B_N };

struct zxva_cuda {
    void *libcu, *libnvrtc;
    cu_api_t cu;
    nvrtc_api_t rt;
    CUdevice dev;
    CUcontext ctx;
    CUmodule mod;
    CUfunction fn;
    CUevent e0, e1;
    dbuf_t b[B_N];
    char name[128];
    double kern_ms;
};

static void say(char *err, uint32_t cap, const char *what, const char *why)
{
    if (!err || !cap) return;
    snprintf(err, cap, "%s%s%s", what, why && *why ? ": " : "", why ? why : "");
}

static const char *cu_str(const zxva_cuda_t *g, CUresult r)
{
    const char *s = NULL;
    if (g->cu.GetErrorString) g->cu.GetErrorString(r, &s);
    return s ? s : "unknown CUDA error";
}

static int compile(zxva_cuda_t *g, int maj, int min, char **img, char *err, uint32_t errcap)
{
    char *src = zxva_cuda_source();
    if (!src) return -1;
    int r = -1;
    for (int pass = 0; pass < 2 && r; pass++) {
        bool cubin = pass == 0;
        if (cubin && !g->rt.GetCUBIN) continue;
        char arch[48];
        snprintf(arch, sizeof arch, "--gpu-architecture=%s_%d%d", cubin ? "sm" : "compute", maj,
                 min);
        const char *opts[] = {arch, "--std=c++14"};
        nvrtcProgram prog = NULL;
        if (g->rt.CreateProgram(&prog, src, "zxva_stft.cu", 0, NULL, NULL)) continue;
        if (g->rt.CompileProgram(prog, 2, opts) == 0) {
            size_t sz = 0;
            if ((cubin ? g->rt.GetCUBINSize(prog, &sz) : g->rt.GetPTXSize(prog, &sz)) == 0 && sz) {
                *img = malloc(sz);
                if (*img && (cubin ? g->rt.GetCUBIN(prog, *img) : g->rt.GetPTX(prog, *img)) == 0)
                    r = 0;
                else
                    free(*img), *img = NULL;
            }
        } else {
            size_t ls = 0;
            if (g->rt.GetProgramLogSize(prog, &ls) == 0 && ls > 1) {
                char *log = malloc(ls);
                if (log && g->rt.GetProgramLog(prog, log) == 0)
                    say(err, errcap, "the kernel did not compile", log);
                free(log);
            }
        }
        g->rt.DestroyProgram(&prog);
    }
    free(src);
    return r;
}

zxva_cuda_t *zxva_cuda_new(int ordinal, char *err, uint32_t errcap)
{
    if (err && errcap) err[0] = 0;
    zxva_cuda_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->libcu = open_first(k_cuda_libs, "ZXVA_CUDA_LIB");
    if (!g->libcu || !load_cu(g->libcu, &g->cu)) {
        say(err, errcap, "no CUDA driver", g->libcu ? "libcuda lacks a needed function" : 0);
        zxva_cuda_free(g);
        return NULL;
    }
    int n = 0, maj = 0, min = 0;
    CUresult cr = g->cu.Init(0);
    if (!cr) cr = g->cu.DeviceGetCount(&n);
    if (cr || ordinal < 0 || ordinal >= n) {
        say(err, errcap, "no CUDA GPU", cr ? cu_str(g, cr) : 0);
        zxva_cuda_free(g);
        return NULL;
    }
    if ((cr = g->cu.DeviceGet(&g->dev, ordinal)) ||
        (cr = g->cu.DeviceGetName(g->name, (int) sizeof g->name - 1, g->dev)) ||
        (cr = g->cu.DeviceGetAttribute(&maj, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                                       g->dev)) ||
        (cr = g->cu.DeviceGetAttribute(&min, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                                       g->dev)) ||
        (cr = g->cu.DevicePrimaryCtxRetain(&g->ctx, g->dev)) ||
        (cr = g->cu.CtxSetCurrent(g->ctx))) {
        say(err, errcap, "cannot open the CUDA GPU", cu_str(g, cr));
        zxva_cuda_free(g);
        return NULL;
    }
    g->libnvrtc = open_first(k_nvrtc_libs, "ZXVA_NVRTC_LIB");
    if (!g->libnvrtc || !load_nvrtc(g->libnvrtc, &g->rt)) {
        say(err, errcap, "no NVRTC (the CUDA run-time compiler, libnvrtc)", 0);
        zxva_cuda_free(g);
        return NULL;
    }
    char *img = NULL;
    if (compile(g, maj, min, &img, err, errcap)) {
        if (err && errcap && !*err) say(err, errcap, "the kernel did not compile", 0);
        zxva_cuda_free(g);
        return NULL;
    }
    cr = g->cu.ModuleLoadData(&g->mod, img);
    free(img);
    if (!cr) cr = g->cu.ModuleGetFunction(&g->fn, g->mod, "zxva_stft_k");
    if (!cr) cr = g->cu.EventCreate(&g->e0, 0);
    if (!cr) cr = g->cu.EventCreate(&g->e1, 0);
    if (cr) {
        say(err, errcap, "the kernel cannot run on this GPU", cu_str(g, cr));
        zxva_cuda_free(g);
        return NULL;
    }
    return g;
}

void zxva_cuda_free(zxva_cuda_t *g)
{
    if (!g) return;
    if (g->ctx) {
        g->cu.CtxSetCurrent(g->ctx);
        for (int i = 0; i < B_N; i++) {
            if (g->b[i].d) g->cu.MemFree(g->b[i].d);
            free(g->b[i].shadow);
        }
        if (g->e0) g->cu.EventDestroy(g->e0);
        if (g->e1) g->cu.EventDestroy(g->e1);
        if (g->mod) g->cu.ModuleUnload(g->mod);
        g->cu.DevicePrimaryCtxRelease(g->dev);
    }
    lib_close(g->libnvrtc);
    lib_close(g->libcu);
    free(g);
}

const char *zxva_cuda_name(const zxva_cuda_t *g)
{
    return g ? g->name : "";
}

double zxva_cuda_kernel_ms(const zxva_cuda_t *g)
{
    return g ? g->kern_ms : 0;
}

static int ensure(zxva_cuda_t *g, dbuf_t *b, size_t bytes)
{
    if (bytes == 0) bytes = 4;
    if (b->cap >= bytes) return 0;
    if (b->d) g->cu.MemFree(b->d);
    b->d = 0;
    b->cap = 0;
    b->valid = 0;
    if (g->cu.MemAlloc(&b->d, bytes)) return -1;
    b->cap = bytes;
    return 0;
}

/* send an input when its bytes differ from what the device holds */
static int put(zxva_cuda_t *g, dbuf_t *b, const void *h, size_t bytes)
{
    if (b->valid == bytes && b->shadow && memcmp(b->shadow, h, bytes) == 0) return 0;
    if (ensure(g, b, bytes)) return -1;
    void *sh = realloc(b->shadow, bytes ? bytes : 1);
    if (!sh) return -1;
    b->shadow = sh;
    memcpy(sh, h, bytes);
    if (g->cu.MemcpyHtoD(b->d, h, bytes)) {
        b->valid = 0;
        return -1;
    }
    b->valid = bytes;
    return 0;
}

static int cuda_stft(zxva_backend_t *be, const zxva_stft_plan_t *p, uint64_t k0, uint32_t nf,
                     int32_t *re, int32_t *im, uint32_t *mag, uint32_t *mask, uint32_t *top)
{
    zxva_cuda_t *g = be->ctx;
    if (!g || !p || nf == 0 || p->N < 2 || p->len > 0xFFFFFFFFull) return -1;
    if (g->cu.CtxSetCurrent(g->ctx)) return -1;
    uint32_t N = p->N, H = N / 2 + 1, mw = zxva_mask_words(N);
    if (put(g, &g->b[B_X], p->x, (size_t) p->len * 4) ||
        put(g, &g->b[B_WIN], p->win, (size_t) p->n * 4) ||
        put(g, &g->b[B_TW], p->tw, (size_t) N * 4))
        return -1;
    if (ensure(g, &g->b[B_SCR], (size_t) nf * 2 * N * 4) ||
        ensure(g, &g->b[B_RE], (size_t) nf * H * 4) ||
        ensure(g, &g->b[B_IM], (size_t) nf * H * 4) ||
        ensure(g, &g->b[B_MAG], (size_t) nf * H * 4) ||
        ensure(g, &g->b[B_MASK], (size_t) nf * mw * 4) || ensure(g, &g->b[B_TOP], (size_t) nf * 4))
        return -1;
    struct {
        uint32_t n, N, L, num, den, c0lo, c0hi, lenlo, lenhi, k0lo, k0hi, floor_mag, rel, nf;
    } kp = {p->n,
            N,
            p->L,
            p->num,
            p->den,
            (uint32_t) (uint64_t) p->c0,
            (uint32_t) ((uint64_t) p->c0 >> 32),
            (uint32_t) p->len,
            (uint32_t) (p->len >> 32),
            (uint32_t) k0,
            (uint32_t) (k0 >> 32),
            p->floor_mag,
            p->rel,
            nf};
    void *args[] = {&g->b[B_X].d,  &g->b[B_WIN].d, &g->b[B_TW].d,   &g->b[B_SCR].d, &g->b[B_RE].d,
                    &g->b[B_IM].d, &g->b[B_MAG].d, &g->b[B_MASK].d, &g->b[B_TOP].d, &kp};
    g->cu.EventRecord(g->e0, 0);
    if (g->cu.LaunchKernel(g->fn, nf, 1, 1, ZXC_TPB, 1, 1, 0, 0, args, NULL)) return -1;
    g->cu.EventRecord(g->e1, 0);
    if (g->cu.EventSynchronize(g->e1)) return -1;
    float ms = 0;
    if (g->cu.EventElapsedTime(&ms, g->e0, g->e1) == 0) g->kern_ms += ms;
    if (g->cu.MemcpyDtoH(re, g->b[B_RE].d, (size_t) nf * H * 4) ||
        g->cu.MemcpyDtoH(im, g->b[B_IM].d, (size_t) nf * H * 4) ||
        g->cu.MemcpyDtoH(mag, g->b[B_MAG].d, (size_t) nf * H * 4) ||
        g->cu.MemcpyDtoH(mask, g->b[B_MASK].d, (size_t) nf * mw * 4) ||
        g->cu.MemcpyDtoH(top, g->b[B_TOP].d, (size_t) nf * 4))
        return -1;
    return 0;
}

void zxva_cuda_backend(zxva_cuda_t *g, zxva_backend_t *b)
{
    memset(b, 0, sizeof *b);
    b->name = "cuda";
    b->ctx = g;
    b->stft = cuda_stft;
    b->batch = ZXC_BATCH;
}
