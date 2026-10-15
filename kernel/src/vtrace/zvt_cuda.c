/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_cuda.c — zvt_gpu.h's CUDA backend. Hosted Linux and Windows; C11.
 *
 * The driver API (libcuda / nvcuda.dll) and NVRTC are loaded at run time
 * and declared here by hand, so the build needs no CUDA toolkit (the
 * pattern of kernel/src/tensor's zt_cuda.c). The kernels are zvt_kern.h
 * itself, stringified, behind one thin wrapper each (one thread per pixel,
 * row or window); integer arithmetic has one answer, so the results are the
 * CPU reference's bits. NVRTC compiles the source for the GPU's own
 * architecture (cubin), or to PTX the driver finishes when this NVRTC does
 * not know the architecture yet. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
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

#include "zvt_gpu.h"

#define ZVC_TPB  256u /* threads per block */
#define ZVC_BUFS 5u

/* ---- the driver API and NVRTC, declared by hand ---- */
typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;
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
    CUresult (*CtxSynchronize)(void);
    CUresult (*ModuleLoadData)(CUmodule *, const void *);
    CUresult (*ModuleUnload)(CUmodule);
    CUresult (*ModuleGetFunction)(CUfunction *, CUmodule, const char *);
    CUresult (*MemAlloc)(CUdeviceptr *, size_t);
    CUresult (*MemFree)(CUdeviceptr);
    CUresult (*MemcpyHtoD)(CUdeviceptr, const void *, size_t);
    CUresult (*MemcpyDtoH)(void *, CUdeviceptr, size_t);
    CUresult (*LaunchKernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                             unsigned, CUstream, void **, void **);
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

/* The loader's reason for the last failed lib_open (into why). */
static void lib_why(char *why, size_t cap)
{
#ifdef _WIN32
    snprintf(why, cap, "LoadLibrary error %lu", (unsigned long) GetLastError());
#else
    const char *d = dlerror();
    snprintf(why, cap, "%s", d ? d : "not found");
#endif
}

/* env's library if set, else the first of names that loads; null with why
 * saying what was tried and what the loader said */
static void *open_first(const char *const *names, const char *env, char *why, size_t cap)
{
    const char *e = getenv(env);
    char d[256];
    if (e && *e) {
        void *h = lib_open(e);
        if (!h) {
            lib_why(d, sizeof d);
            snprintf(why, cap, "%s=%s: %s", env, e, d);
        }
        return h;
    }
    for (const char *const *n = names; *n; n++) {
        void *h = lib_open(*n);
        if (h) return h;
    }
    lib_why(d, sizeof d);
    snprintf(why, cap, "%s not set; %s and the other names not found (%s)", env, names[0], d);
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
    S(CtxSynchronize, "cuCtxSynchronize");
    S(ModuleLoadData, "cuModuleLoadData");
    S(ModuleUnload, "cuModuleUnload");
    S(ModuleGetFunction, "cuModuleGetFunction");
    S(MemAlloc, "cuMemAlloc_v2");
    S(MemFree, "cuMemFree_v2");
    S(MemcpyHtoD, "cuMemcpyHtoD_v2");
    S(MemcpyDtoH, "cuMemcpyDtoH_v2");
    S(LaunchKernel, "cuLaunchKernel");
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

/* ---- the kernels' source ---- */
#define ZVT_KSRC(...) #__VA_ARGS__
static const char k_kern[] =
#include "zvt_kern.h"
    ;
#undef ZVT_KSRC

static const char k_prelude[] = "typedef unsigned int zu;\n"
                                "typedef int zi;\n"
                                "typedef unsigned char zb;\n"
                                "#define ZK_DEV\n";

/* one wrapper per stage; the launch covers count threads */
static const char k_kernels[] =
    "\n"
    "#define ZVI (blockIdx.x * blockDim.x + threadIdx.x)\n"
    "extern \"C\" __global__ void zvk_smooth(const zu *in, zu *out, zu w, zu h, zu thr)\n"
    "{\n"
    "    zu i = ZVI;\n"
    "    if (i < w * h) out[i] = zk_smooth_px(in, w, h, i % w, i / w, thr);\n"
    "}\n"
    "extern \"C\" __global__ void zvk_quant(const zu *px, zu n, const zu *pal, zu npal, zb *idx)\n"
    "{\n"
    "    zu i = ZVI;\n"
    "    if (i < n) idx[i] = (zb) zk_quant_px(px[i], pal, npal);\n"
    "}\n"
    "extern \"C\" __global__ void zvk_edges(const zb *idx, zu w, zu h, zb *edge)\n"
    "{\n"
    "    zu i = ZVI;\n"
    "    if (i >= w * h) return;\n"
    "    zu x = i % w, y = i / w;\n"
    "    zu r = x + 1u < w ? idx[i + 1u] : 0u, d = y + 1u < h ? idx[i + w] : 0u;\n"
    "    edge[i] = (zb) zk_edge_px(idx[i], r, d, x + 1u < w ? 1u : 0u, y + 1u < h ? 1u : 0u);\n"
    "}\n"
    "extern \"C\" __global__ void zvk_sse(const zu *a, const zu *b, zu w, zu h, zu *rows)\n"
    "{\n"
    "    zu y = ZVI;\n"
    "    if (y < h) rows[y] = zk_sse_row(a, b, w, y);\n"
    "}\n"
    "extern \"C\" __global__ void zvk_win(const zu *a, const zu *b, zu w, zu wx, zu wy, zu step,\n"
    "                                    zu nwx, zu nwin, zu *win)\n"
    "{\n"
    "    zu i = ZVI;\n"
    "    if (i < nwin) zk_win(a, b, w, (i % nwx) * step, (i / nwx) * step, wx, wy, win + 5u * i);\n"
    "}\n"
    "extern \"C\" __global__ void zvk_diff(const zu *a, const zu *b, zu w, zu h, zu thr, zb "
    "*mask,\n"
    "                                     zu *rows)\n"
    "{\n"
    "    zu y = ZVI;\n"
    "    if (y < h) rows[y] = zk_diff_row(a, b, w, y, thr, mask);\n"
    "}\n";

static const char *const k_names[] = {"zvk_smooth", "zvk_quant", "zvk_edges",
                                      "zvk_sse",    "zvk_win",   "zvk_diff"};
enum { K_SMOOTH, K_QUANT, K_EDGES, K_SSE, K_WIN, K_DIFF, K_N };

char *zvt_cuda_source(void)
{
    size_t a = strlen(k_prelude), b = strlen(k_kern), c = strlen(k_kernels);
    char *src = (char *) malloc(a + b + c + 2);
    if (!src) return NULL;
    memcpy(src, k_prelude, a);
    memcpy(src + a, k_kern, b);
    src[a + b] = '\n';
    memcpy(src + a + b + 1, k_kernels, c + 1);
    return src;
}

/* ---- the backend ---- */
typedef struct {
    CUdeviceptr d;
    size_t cap;
} dbuf_t;

struct zvt_cuda {
    void *libcu, *libnvrtc;
    cu_api_t cu;
    nvrtc_api_t rt;
    CUdevice dev;
    CUcontext ctx;
    CUmodule mod;
    CUfunction fn[K_N];
    dbuf_t buf[ZVC_BUFS];
    char name[128];
    zvt_gpu_stats_t st;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void say(char *err, uint32_t cap, const char *what, const char *why)
{
    if (!err || !cap) return;
    snprintf(err, cap, "%s%s%s", what, why && *why ? ": " : "", why ? why : "");
}

static const char *cu_str(const zvt_cuda_t *g, CUresult r)
{
    const char *s = NULL;
    if (g->cu.GetErrorString) g->cu.GetErrorString(r, &s);
    return s ? s : "unknown CUDA error";
}

static int compile(zvt_cuda_t *g, int maj, int min, char **img, char *err, uint32_t errcap)
{
    char *src = zvt_cuda_source();
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
        if (g->rt.CreateProgram(&prog, src, "zvt_cuda.cu", 0, NULL, NULL)) continue;
        if (g->rt.CompileProgram(prog, 3, opts) == 0) {
            size_t sz = 0;
            if ((cubin ? g->rt.GetCUBINSize(prog, &sz) : g->rt.GetPTXSize(prog, &sz)) == 0 && sz) {
                *img = (char *) malloc(sz);
                if (*img && (cubin ? g->rt.GetCUBIN(prog, *img) : g->rt.GetPTX(prog, *img)) == 0)
                    r = 0;
                else
                    free(*img), *img = NULL;
            }
        } else {
            size_t ls = 0;
            if (g->rt.GetProgramLogSize(prog, &ls) == 0 && ls > 1) {
                char *log = (char *) malloc(ls);
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

void zvt_cuda_free(zvt_cuda_t *g)
{
    if (!g) return;
    if (g->ctx) {
        g->cu.CtxSetCurrent(g->ctx);
        for (uint32_t i = 0; i < ZVC_BUFS; i++)
            if (g->buf[i].d) g->cu.MemFree(g->buf[i].d);
        if (g->mod) g->cu.ModuleUnload(g->mod);
        g->cu.DevicePrimaryCtxRelease(g->dev);
    }
    lib_close(g->libnvrtc);
    lib_close(g->libcu);
    free(g);
}

zvt_cuda_t *zvt_cuda_new(int ordinal, char *err, uint32_t errcap)
{
    zvt_cuda_t *g = (zvt_cuda_t *) calloc(1, sizeof *g);
    if (!g) return NULL;
    if (err && errcap) err[0] = 0;
    char why[384] = "";
    g->libcu = open_first(k_cuda_libs, "ZVT_CUDA_LIB", why, sizeof why);
    if (!g->libcu || !load_cu(g->libcu, &g->cu)) {
        say(err, errcap, "no CUDA driver", g->libcu ? "libcuda lacks a needed function" : why);
        zvt_cuda_free(g);
        return NULL;
    }
    int n = 0, maj = 0, min = 0;
    CUresult cr = g->cu.Init(0);
    if (!cr) cr = g->cu.DeviceGetCount(&n);
    if (cr || ordinal < 0 || ordinal >= n) {
        say(err, errcap, "no CUDA GPU", cr ? cu_str(g, cr) : 0);
        zvt_cuda_free(g);
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
        zvt_cuda_free(g);
        return NULL;
    }
    g->libnvrtc = open_first(k_nvrtc_libs, "ZVT_NVRTC_LIB", why, sizeof why);
    if (!g->libnvrtc || !load_nvrtc(g->libnvrtc, &g->rt)) {
        say(err, errcap, "no NVRTC (the CUDA run-time compiler, libnvrtc)",
            g->libnvrtc ? "libnvrtc lacks a needed function" : why);
        zvt_cuda_free(g);
        return NULL;
    }
    char *img = NULL;
    if (compile(g, maj, min, &img, err, errcap)) {
        if (err && errcap && !*err) say(err, errcap, "the CUDA kernels did not compile", 0);
        zvt_cuda_free(g);
        return NULL;
    }
    cr = g->cu.ModuleLoadData(&g->mod, img);
    free(img);
    for (int k = 0; k < K_N && !cr; k++)
        cr = g->cu.ModuleGetFunction(&g->fn[k], g->mod, k_names[k]);
    if (cr) {
        say(err, errcap, "the CUDA kernels did not load", cu_str(g, cr));
        zvt_cuda_free(g);
        return NULL;
    }
    return g;
}

const char *zvt_cuda_name(const zvt_cuda_t *g)
{
    return g ? g->name : "";
}

void zvt_cuda_stats(const zvt_cuda_t *g, zvt_gpu_stats_t *out)
{
    if (!out) return;
    if (g)
        *out = g->st;
    else
        memset(out, 0, sizeof *out);
}

/* device buffer i of at least n bytes (contents not kept) */
static int dbuf(zvt_cuda_t *g, uint32_t i, size_t n, CUdeviceptr *d)
{
    if (n < 16) n = 16;
    if (g->buf[i].cap < n) {
        if (g->buf[i].d) g->cu.MemFree(g->buf[i].d);
        g->buf[i].d = 0, g->buf[i].cap = 0;
        size_t want = n + n / 4u + 4096u;
        if (g->cu.MemAlloc(&g->buf[i].d, want)) return -1;
        g->buf[i].cap = want;
    }
    *d = g->buf[i].d;
    return 0;
}

static int put(zvt_cuda_t *g, uint32_t i, const void *h, size_t n, CUdeviceptr *d)
{
    if (dbuf(g, i, n, d)) return -1;
    g->st.bytes_in += n;
    return g->cu.MemcpyHtoD(*d, h, n) ? -1 : 0;
}

static int get(zvt_cuda_t *g, void *h, CUdeviceptr d, size_t n)
{
    g->st.bytes_out += n;
    return g->cu.MemcpyDtoH(h, d, n) ? -1 : 0;
}

static int launch(zvt_cuda_t *g, int k, uint32_t count, void **args)
{
    if (!count) return 0;
    unsigned blocks = (count + ZVC_TPB - 1u) / ZVC_TPB;
    return g->cu.LaunchKernel(g->fn[k], blocks, 1, 1, ZVC_TPB, 1, 1, 0, NULL, args, NULL) ? -1 : 0;
}

/* every stage: set the context, run, account; non-zero means "CPU, please" */
static int begin(zvt_cuda_t *g, uint64_t *t0)
{
    *t0 = now_ns();
    g->st.calls++;
    return g->cu.CtxSetCurrent(g->ctx) ? -1 : 0;
}
static int finish(zvt_cuda_t *g, uint64_t t0, int rc)
{
    g->st.ns += now_ns() - t0;
    if (rc) g->st.fallbacks++;
    return rc;
}

static int cu_smooth(void *ctx, const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h,
                     uint32_t thr)
{
    zvt_cuda_t *g = (zvt_cuda_t *) ctx;
    uint64_t t0;
    size_t n = (size_t) w * h * 4u;
    CUdeviceptr di, dout;
    int rc = begin(g, &t0);
    if (!rc) rc = put(g, 0, in, n, &di);
    if (!rc) rc = dbuf(g, 1, n, &dout);
    void *args[] = {&di, &dout, &w, &h, &thr};
    if (!rc) rc = launch(g, K_SMOOTH, w * h, args);
    if (!rc) rc = get(g, out, dout, n);
    return finish(g, t0, rc);
}

static int cu_quant(void *ctx, const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal,
                    uint8_t *idx)
{
    zvt_cuda_t *g = (zvt_cuda_t *) ctx;
    uint64_t t0;
    CUdeviceptr dp, dpal, di;
    int rc = begin(g, &t0);
    if (!rc) rc = put(g, 0, px, (size_t) n * 4u, &dp);
    if (!rc) rc = put(g, 2, pal, (size_t) npal * 4u, &dpal);
    if (!rc) rc = dbuf(g, 1, n, &di);
    void *args[] = {&dp, &n, &dpal, &npal, &di};
    if (!rc) rc = launch(g, K_QUANT, n, args);
    if (!rc) rc = get(g, idx, di, n);
    return finish(g, t0, rc);
}

static int cu_edges(void *ctx, const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge)
{
    zvt_cuda_t *g = (zvt_cuda_t *) ctx;
    uint64_t t0;
    size_t n = (size_t) w * h;
    CUdeviceptr di, de;
    int rc = begin(g, &t0);
    if (!rc) rc = put(g, 0, idx, n, &di);
    if (!rc) rc = dbuf(g, 1, n, &de);
    void *args[] = {&di, &w, &h, &de};
    if (!rc) rc = launch(g, K_EDGES, w * h, args);
    if (!rc) rc = get(g, edge, de, n);
    return finish(g, t0, rc);
}

static int cu_metric(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                     uint32_t wx, uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win)
{
    zvt_cuda_t *g = (zvt_cuda_t *) ctx;
    uint64_t t0;
    size_t n = (size_t) w * h * 4u;
    uint32_t nwx = (w - wx) / step + 1u, nwy = (h - wy) / step + 1u, nwin = nwx * nwy;
    CUdeviceptr da, db, dr, dw;
    int rc = begin(g, &t0);
    if (!rc) rc = put(g, 0, a, n, &da);
    if (!rc) rc = put(g, 1, b, n, &db);
    if (!rc) rc = dbuf(g, 2, (size_t) h * 4u, &dr);
    if (!rc) rc = dbuf(g, 3, (size_t) nwin * 20u, &dw);
    void *a1[] = {&da, &db, &w, &h, &dr};
    void *a2[] = {&da, &db, &w, &wx, &wy, &step, &nwx, &nwin, &dw};
    if (!rc) rc = launch(g, K_SSE, h, a1);
    if (!rc) rc = launch(g, K_WIN, nwin, a2);
    if (!rc) rc = get(g, row_sse, dr, (size_t) h * 4u);
    if (!rc) rc = get(g, win, dw, (size_t) nwin * 20u);
    return finish(g, t0, rc);
}

static int cu_diff(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                   uint32_t thr, uint8_t *mask, uint32_t *row_count)
{
    zvt_cuda_t *g = (zvt_cuda_t *) ctx;
    uint64_t t0;
    size_t n = (size_t) w * h * 4u;
    CUdeviceptr da, db, dm, dr;
    int rc = begin(g, &t0);
    if (!rc) rc = put(g, 0, a, n, &da);
    if (!rc) rc = put(g, 1, b, n, &db);
    if (!rc) rc = dbuf(g, 2, (size_t) w * h, &dm);
    if (!rc) rc = dbuf(g, 3, (size_t) h * 4u, &dr);
    void *args[] = {&da, &db, &w, &h, &thr, &dm, &dr};
    if (!rc) rc = launch(g, K_DIFF, h, args);
    if (!rc) rc = get(g, mask, dm, (size_t) w * h);
    if (!rc) rc = get(g, row_count, dr, (size_t) h * 4u);
    return finish(g, t0, rc);
}

void zvt_cuda_backend(zvt_cuda_t *g, zvt_backend_t *out)
{
    zvt_backend_t b = {"cuda", g, cu_smooth, cu_quant, cu_edges, cu_metric, cu_diff};
    *out = b;
}
