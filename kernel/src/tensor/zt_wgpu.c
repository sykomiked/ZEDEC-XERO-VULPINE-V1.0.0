/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_wgpu.c — zt_wgpu.h: zt_wgpu.wgsl's kernels through webgpu.h. Natively
 * the WebGPU library is opened at run time (no link to it); under Emscripten
 * the functions are the browser bindings. The WGSL is included as
 * ZT_WGPU_WGSL_H (gen_zt_wgpu_wgsl.sh). A build without <webgpu/webgpu.h> or
 * the WGSL compiles to stubs that return null, so every hosted build links.
 *
 * Native and browser differ in two places only, both behind __EMSCRIPTEN__:
 * how the functions are found (dlsym, or the bindings), and how the CPU
 * waits for the GPU (native: wgpuDevicePoll where the library has it, as
 * wgpu-native does, else wgpuInstanceProcessEvents in a loop; browser:
 * wgpuInstanceWaitAny with the TimedWaitAny instance feature, which needs
 * -sASYNCIFY or -sJSPI). Everything else is the standard API. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_gpu.h"
#include "zt_wgpu.h"

#if defined(__has_include)
#    if __has_include(<webgpu/webgpu.h>) && defined(ZT_WGPU_WGSL_H)
#        define ZT_WGPU_BUILD 1
#    endif
#endif

#ifndef ZT_WGPU_BUILD

/* ---- stubs: built without webgpu.h or the WGSL ---- */

zt_wgpu_t *zt_wgpu_new(char *err, uint32_t errcap)
{
    if (err && errcap)
        snprintf(err, errcap,
                 "built without WebGPU (needs <webgpu/webgpu.h> and the WGSL header at build "
                 "time)");
    return NULL;
}
void zt_wgpu_free(zt_wgpu_t *g)
{
    (void) g;
}
const char *zt_wgpu_name(const zt_wgpu_t *g)
{
    (void) g;
    return "";
}
void zt_wgpu_sizes(const zt_wgpu_t *g, zt_wgpu_sizes_t *out)
{
    (void) g;
    if (out) memset(out, 0, sizeof *out);
}
int zt_wgpu_map(zt_wgpu_t *g, const void *base, uint64_t len)
{
    (void) g, (void) base, (void) len;
    return -1;
}
int zt_wgpu_map_shared(zt_wgpu_t *g, void *base, uint64_t len)
{
    (void) g, (void) base, (void) len;
    return -1;
}
int zt_wgpu_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    (void) fctx, (void) jobs, (void) n;
    return 1;
}
void zt_kern_wgpu(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g)
{
    (void) g;
    *out = *base;
}
void zt_kern_wgpu_resident(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g)
{
    (void) g;
    *out = *base;
}
int zt_wgpu_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                uint32_t *n_sat)
{
    (void) fctx, (void) m, (void) s, (void) B, (void) logits, (void) first, (void) n_sat;
    return 1;
}
void zt_wgpu_set_min_tokens(zt_wgpu_t *g, uint32_t n)
{
    (void) g, (void) n;
}
void zt_wgpu_stats(const zt_wgpu_t *g, zt_wgpu_stats_t *out)
{
    (void) g;
    if (out) memset(out, 0, sizeof *out);
}

#else /* ZT_WGPU_BUILD */

#    include <webgpu/webgpu.h>
#    if defined(__EMSCRIPTEN__)
/* the bindings are linked: nothing to open */
#    elif defined(_WIN32)
#        include <windows.h>
#    else
#        include <dlfcn.h>
#    endif

#    include ZT_WGPU_WGSL_H /* zw_wgsl: zt_wgpu.wgsl without its size constants */

#    define ZW_REGIONS 16u
#    define ZW_PW      32u /* uniform words per dispatch: u[16], off[6], sel[6], gx, pad */
#    define ZW_UNIFORM 128u
#    define ZW_UP      ((uint64_t) 64 << 20) /* bytes per wgpuQueueWriteBuffer */

/* ---- the API's functions ---- */

#    define ZW_FNS(X)                                                                              \
        X(CreateInstance)                                                                          \
        X(InstanceRequestAdapter)                                                                  \
        X(InstanceProcessEvents)                                                                   \
        X(InstanceWaitAny)                                                                         \
        X(InstanceRelease)                                                                         \
        X(AdapterGetLimits)                                                                        \
        X(AdapterGetInfo)                                                                          \
        X(AdapterInfoFreeMembers)                                                                  \
        X(AdapterRequestDevice)                                                                    \
        X(AdapterRelease)                                                                          \
        X(DeviceGetLimits)                                                                         \
        X(DeviceGetQueue)                                                                          \
        X(DeviceCreateBuffer)                                                                      \
        X(DeviceCreateShaderModule)                                                                \
        X(DeviceCreateBindGroupLayout)                                                             \
        X(DeviceCreatePipelineLayout)                                                              \
        X(DeviceCreateComputePipeline)                                                             \
        X(DeviceCreateBindGroup)                                                                   \
        X(DeviceCreateCommandEncoder)                                                              \
        X(DevicePushErrorScope)                                                                    \
        X(DevicePopErrorScope)                                                                     \
        X(DeviceDestroy)                                                                           \
        X(DeviceRelease)                                                                           \
        X(BufferMapAsync)                                                                          \
        X(BufferGetConstMappedRange)                                                               \
        X(BufferUnmap)                                                                             \
        X(BufferDestroy)                                                                           \
        X(BufferRelease)                                                                           \
        X(ShaderModuleRelease)                                                                     \
        X(BindGroupLayoutRelease)                                                                  \
        X(PipelineLayoutRelease)                                                                   \
        X(ComputePipelineRelease)                                                                  \
        X(BindGroupRelease)                                                                        \
        X(CommandEncoderBeginComputePass)                                                          \
        X(CommandEncoderCopyBufferToBuffer)                                                        \
        X(CommandEncoderFinish)                                                                    \
        X(CommandEncoderRelease)                                                                   \
        X(ComputePassEncoderSetPipeline)                                                           \
        X(ComputePassEncoderSetBindGroup)                                                          \
        X(ComputePassEncoderDispatchWorkgroups)                                                    \
        X(ComputePassEncoderEnd)                                                                   \
        X(ComputePassEncoderRelease)                                                               \
        X(CommandBufferRelease)                                                                    \
        X(QueueSubmit)                                                                             \
        X(QueueWriteBuffer)                                                                        \
        X(QueueRelease)

#    define ZW_DECL(n) WGPUProc##n n;
typedef struct {
    ZW_FNS(ZW_DECL)
} zw_api_t;

/* wgpu-native's wgpuDevicePoll (wgpu.h): block until the queue is idle */
typedef WGPUBool (*zw_poll_fn)(WGPUDevice, WGPUBool, const void *);

/* a GPU buffer: cap bytes, used of them in use */
typedef struct {
    WGPUBuffer b;
    uint64_t cap, used;
} wbuf_t;

/* a mapped region: host [base, base + len) in nch buffers, chunk k holding
 * [k * stride, k * stride + clen) (clipped to len); neighbours overlap by
 * clen - stride bytes, so every range that short lies inside one chunk */
typedef struct {
    const uint8_t *base;
    uint64_t len;
    bool shared; /* zt_wgpu_map_shared: the GPU writes it (one chunk) */
    uint32_t nch;
    WGPUBuffer *ch;
    uint64_t stride, clen;
    uint64_t up_lo, up_hi, dn_lo, dn_hi; /* this call's spans in and out (shared) */
} region_t;

/* an argument of a dispatch: buffer b (null: unused), byte offset, bytes the
 * kernel may touch from there, and the buffer's size */
typedef struct {
    WGPUBuffer b;
    uint64_t off, len, cap;
} wref_t;

/* one planned dispatch */
typedef struct {
    WGPUComputePipeline pipe;
    WGPUBuffer bind[5]; /* r0..r2, w0, w1 (null: the dummy) */
    uint64_t boff[5], bsize[5];
    uint32_t P[ZW_PW];
    uint32_t gx, gy;
} plan_t;

struct zt_wgpu {
    void *lib;
    zw_api_t api;
    zw_poll_fn poll; /* may be null */
    WGPUInstance inst;
    WGPUAdapter adapter;
    WGPUDevice dev;
    WGPUQueue q;
    WGPULimits lim;
    uint64_t max_binding, max_buffer, chunk_max;
    uint32_t align, ualign, max_groups;
    uint32_t R, L, T, WG; /* product rows, threads per row, tile; layer workgroup */
    WGPUBindGroupLayout bgl;
    WGPUPipelineLayout pl;
    WGPUComputePipeline zk[ZG_NK]; /* ZG_K_MM unused: products use mm */
    WGPUComputePipeline mm[2][3];  /* [tiled][q4k, q6k, q80] */
    WGPUBuffer dummy[3];           /* unused read-only, w0 and w1 bindings */
    wbuf_t work, cst, in, out, ctr, prm, rb;
    uint8_t *work_h, *cst_h, *in_h, *prm_h;
    uint64_t work_hcap, cst_hcap, in_hcap, prm_hcap;
    region_t reg[ZW_REGIONS];
    uint32_t n_reg;
    plan_t *plan;
    uint32_t n_plan, plan_cap;
    bool plan_fail;
    WGPUBindGroup *bgs;
    uint32_t bgs_cap;
    int lost;
    char name[256];
    char emsg[256];
    zt_wgpu_stats_t st;
    uint32_t min_tokens;
    uint64_t timeout_ns;
};

/* the bytes each kernel writes: argument j written when bit j is set (the
 * WGSL's st* calls); a buffer holding one is bound writable */
static const uint8_t wmask[ZG_NK] = {
    [ZG_K_RMS_INV] = 1u << 1, [ZG_K_RMS_APPLY] = 1u << 3, [ZG_K_ACT] = 7u << 1,
    [ZG_K_MM] = 1u << 4,      [ZG_K_BIAS] = 1u << 0,      [ZG_K_ROPE] = 1u << 0,
    [ZG_K_KV] = 3u << 2,      [ZG_K_ATT_Q] = 3u << 1,     [ZG_K_ATT_SCORE] = 1u << 2,
    [ZG_K_ATT_MAX] = 1u << 1, [ZG_K_ATT_EXP] = 1u << 0,   [ZG_K_ATT_SUM] = 1u << 1,
    [ZG_K_ATT_WV] = 1u << 2,  [ZG_K_RESID] = 1u << 0,     [ZG_K_SILU] = 1u << 0,
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void say(char *err, uint32_t cap, const char *what, const char *detail)
{
    if (!err || !cap) return;
    if (detail && *detail)
        snprintf(err, cap, "%s (%s)", what, detail);
    else
        snprintf(err, cap, "%s", what);
}

static void sv_copy(char *dst, size_t cap, WGPUStringView s)
{
    size_t n = !s.data ? 0 : s.length == WGPU_STRLEN ? strlen(s.data) : s.length;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, s.data, n);
    dst[n] = 0;
}

static WGPUStringView sv(const char *s)
{
    WGPUStringView v = {s, WGPU_STRLEN};
    return v;
}

static uint64_t env_u64(const char *name)
{
    const char *v = getenv(name);
    return v && *v ? strtoull(v, NULL, 10) : 0;
}

/* ---- the library ---- */

#    if !defined(__EMSCRIPTEN__)
static void *lib_open(void)
{
    const char *env = getenv("ZT_WGPU_LIB");
#        ifdef _WIN32
    const char *names[] = {env, "wgpu_native.dll", "webgpu_dawn.dll"};
#        else
    const char *names[] = {env,
                           "libwgpu_native.so",
                           "libwgpu_native-release.so",
                           "libwebgpu_dawn.so",
                           "libwgpu_native.dylib",
                           "libwebgpu_dawn.dylib"};
#        endif
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (!names[i] || !*names[i]) continue;
#        ifdef _WIN32
        void *h = (void *) LoadLibraryA(names[i]);
#        else
        void *h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
#        endif
        if (h) return h;
    }
    return NULL;
}

static void lib_close(void *h)
{
    if (!h) return;
#        ifdef _WIN32
    FreeLibrary((HMODULE) h);
#        else
    dlclose(h);
#        endif
}

static void *lib_sym(void *h, const char *name)
{
#        ifdef _WIN32
    FARPROC s = GetProcAddress((HMODULE) h, name);
    void *p;
    memcpy(&p, &s, sizeof p);
    return p;
#        else
    return dlsym(h, name);
#        endif
}
#    endif

/* ---- waiting for the GPU ---- */

/* until *done (set by a callback of future f), or the timeout: 0 when done */
static int zw_wait(zt_wgpu_t *g, WGPUFuture f, const int *done)
{
#    if defined(__EMSCRIPTEN__)
    if (*done) return 0;
    WGPUFutureWaitInfo wi = {f, 0};
    WGPUWaitStatus s = g->api.InstanceWaitAny(g->inst, 1, &wi, UINT64_MAX);
    return s == WGPUWaitStatus_Success && *done ? 0 : -1;
#    else
    (void) f;
    uint64_t t0 = now_ns();
    while (!*done) {
        if (g->poll && g->dev) g->poll(g->dev, 1, NULL);
        g->api.InstanceProcessEvents(g->inst);
        if (*done) break;
        if (now_ns() - t0 > g->timeout_ns) return -1;
        if (!g->poll || !g->dev) {
            struct timespec ts = {0, 20000};
            nanosleep(&ts, NULL);
        }
    }
    return 0;
#    endif
}

typedef struct {
    int done;
    int err;
    char *msg;
    size_t cap;
} wres_t;

static void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView m, void *u1,
                       void *u2)
{
    zt_wgpu_t *g = u1;
    wres_t *r = u2;
    if (st == WGPURequestAdapterStatus_Success)
        g->adapter = a;
    else
        sv_copy(g->emsg, sizeof g->emsg, m);
    r->done = 1;
}

static void on_device(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView m, void *u1,
                      void *u2)
{
    zt_wgpu_t *g = u1;
    wres_t *r = u2;
    if (st == WGPURequestDeviceStatus_Success)
        g->dev = d;
    else
        sv_copy(g->emsg, sizeof g->emsg, m);
    r->done = 1;
}

static void on_lost(WGPUDevice const *d, WGPUDeviceLostReason why, WGPUStringView m, void *u1,
                    void *u2)
{
    zt_wgpu_t *g = u1;
    (void) d, (void) u2;
    if (why == WGPUDeviceLostReason_Destroyed) return;
    g->lost = 1;
    sv_copy(g->emsg, sizeof g->emsg, m);
}

static void on_error(WGPUDevice const *d, WGPUErrorType t, WGPUStringView m, void *u1, void *u2)
{
    zt_wgpu_t *g = u1;
    (void) d, (void) t, (void) u2;
    sv_copy(g->emsg, sizeof g->emsg, m);
}

static void on_pop(WGPUPopErrorScopeStatus st, WGPUErrorType t, WGPUStringView m, void *u1,
                   void *u2)
{
    wres_t *r = u1;
    (void) u2;
    if (st != WGPUPopErrorScopeStatus_Success || t != WGPUErrorType_NoError) {
        r->err = 1;
        if (r->msg && r->cap && !r->msg[0]) sv_copy(r->msg, r->cap, m);
    }
    r->done = 1;
}

static void on_map(WGPUMapAsyncStatus st, WGPUStringView m, void *u1, void *u2)
{
    wres_t *r = u1;
    (void) m, (void) u2;
    r->err = st != WGPUMapAsyncStatus_Success;
    r->done = 1;
}

/* errors from here to scope_end are caught (validation and out of memory) */
static void scope_begin(zt_wgpu_t *g)
{
    g->api.DevicePushErrorScope(g->dev, WGPUErrorFilter_OutOfMemory);
    g->api.DevicePushErrorScope(g->dev, WGPUErrorFilter_Validation);
}

/* 0 when nothing failed since scope_begin */
static int scope_end(zt_wgpu_t *g)
{
    int bad = 0;
    g->emsg[0] = 0;
    for (int i = 0; i < 2; i++) {
        wres_t r = {0, 0, g->emsg, sizeof g->emsg};
        WGPUPopErrorScopeCallbackInfo ci = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        ci.mode = WGPUCallbackMode_AllowProcessEvents;
        ci.callback = on_pop;
        ci.userdata1 = &r;
        WGPUFuture f = g->api.DevicePopErrorScope(g->dev, ci);
        if (zw_wait(g, f, &r.done) || r.err) bad = 1;
    }
    return bad || g->lost ? -1 : 0;
}

/* ---- buffers ---- */

static void buf_free(zt_wgpu_t *g, WGPUBuffer *b)
{
    if (!*b) return;
    g->api.BufferDestroy(*b);
    g->api.BufferRelease(*b);
    *b = NULL;
}

static WGPUBuffer buf_make(zt_wgpu_t *g, uint64_t size, WGPUBufferUsage usage)
{
    if (size > g->lim.maxBufferSize) return NULL;
    WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
    d.size = (size + 255) & ~(uint64_t) 255;
    if (d.size > g->lim.maxBufferSize) d.size = size + 3 - (size + 3) % 4;
    d.usage = usage;
    scope_begin(g);
    WGPUBuffer b = g->api.DeviceCreateBuffer(g->dev, &d);
    if (scope_end(g)) buf_free(g, &b);
    return b;
}

static const WGPUBufferUsage U_STORE =
    WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;

/* make w a GPU buffer of at least need bytes (contents not kept) */
static int grow(zt_wgpu_t *g, wbuf_t *w, uint64_t need, WGPUBufferUsage usage)
{
    need = (need + 3) & ~(uint64_t) 3;
    if (need < 16) need = 16;
    w->used = need;
    if (w->b && w->cap >= need) return 0;
    buf_free(g, &w->b);
    uint64_t want = need + need / 4 + 4096;
    if (want > g->lim.maxBufferSize) want = need;
    if (!(w->b = buf_make(g, want, usage))) {
        w->cap = w->used = 0;
        return -1;
    }
    w->cap = (want + 255) & ~(uint64_t) 255;
    if (w->cap > g->lim.maxBufferSize) w->cap = (want + 3) & ~(uint64_t) 3;
    w->used = need;
    return 0;
}

/* host memory of at least need bytes in *p (contents not kept) */
static int hgrow(uint8_t **p, uint64_t *cap, uint64_t need)
{
    need = (need + 255) & ~(uint64_t) 255;
    if (*p && *cap >= need) return 0;
    free(*p);
    *p = malloc((size_t) need);
    *cap = *p ? need : 0;
    return *p ? 0 : -1;
}

/* write n bytes of src at off (a multiple of 4) in b; a last partial word
 * is padded with zeros */
static void put(zt_wgpu_t *g, WGPUBuffer b, uint64_t off, const void *src, uint64_t n)
{
    const uint8_t *s = src;
    uint64_t whole = n & ~(uint64_t) 3;
    for (uint64_t o = 0; o < whole; o += ZW_UP) {
        uint64_t k = whole - o < ZW_UP ? whole - o : ZW_UP;
        g->api.QueueWriteBuffer(g->q, b, off + o, s + o, (size_t) k);
    }
    if (n > whole) {
        uint8_t t[4] = {0, 0, 0, 0};
        memcpy(t, s + whole, (size_t) (n - whole));
        g->api.QueueWriteBuffer(g->q, b, off + whole, t, 4);
    }
}

/* ---- creating and freeing ---- */

void zt_wgpu_free(zt_wgpu_t *g)
{
    if (!g) return;
    if (g->dev) {
        for (uint32_t i = 0; i < g->n_reg; i++) {
            for (uint32_t k = 0; k < g->reg[i].nch; k++) buf_free(g, &g->reg[i].ch[k]);
            free(g->reg[i].ch);
        }
        wbuf_t *w[] = {&g->work, &g->cst, &g->in, &g->out, &g->ctr, &g->prm, &g->rb};
        for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) buf_free(g, &w[i]->b);
        for (int i = 0; i < 3; i++) buf_free(g, &g->dummy[i]);
        for (uint32_t k = 0; k < ZG_NK; k++)
            if (g->zk[k]) g->api.ComputePipelineRelease(g->zk[k]);
        for (uint32_t t = 0; t < 2; t++)
            for (uint32_t q = 0; q < 3; q++)
                if (g->mm[t][q]) g->api.ComputePipelineRelease(g->mm[t][q]);
        if (g->pl) g->api.PipelineLayoutRelease(g->pl);
        if (g->bgl) g->api.BindGroupLayoutRelease(g->bgl);
        if (g->q) g->api.QueueRelease(g->q);
        g->api.DeviceDestroy(g->dev);
        g->api.DeviceRelease(g->dev);
    }
    if (g->adapter) g->api.AdapterRelease(g->adapter);
    if (g->inst) g->api.InstanceRelease(g->inst);
    free(g->work_h), free(g->cst_h), free(g->in_h), free(g->prm_h);
    free(g->plan), free(g->bgs);
#    if !defined(__EMSCRIPTEN__)
    lib_close(g->lib);
#    endif
    free(g);
}

/* the products' sizes from the limits (see zt_wgpu.h), then ZT_WGPU_TILE
 * where the limits allow it; every choice keeps T <= L (thread t of a row
 * finishes token t) and the workgroup memory T * R * L * 8 bytes */
static void pick_sizes(zt_wgpu_t *g)
{
    uint32_t inv = g->lim.maxComputeInvocationsPerWorkgroup < g->lim.maxComputeWorkgroupSizeX
                       ? g->lim.maxComputeInvocationsPerWorkgroup
                       : g->lim.maxComputeWorkgroupSizeX;
    uint64_t smem = g->lim.maxComputeWorkgroupStorageSize;
    bool desktop = g->max_binding >= ((uint64_t) 1 << 30) && inv >= 128;
    g->WG = 256;
    while (g->WG > inv) g->WG /= 2;
    g->L = 32;
    while (g->L > inv) g->L /= 2;
    g->R = desktop ? 4 : 2;
    while (g->R > 1 && g->R * g->L > inv) g->R /= 2;
    g->T = desktop ? 8 : 4;
    if (g->T > g->L) g->T = g->L;
    while (g->T > 1 && (uint64_t) g->T * g->R * g->L * 8 > smem) g->T /= 2;
    const char *t = getenv("ZT_WGPU_TILE");
    unsigned r, l, tt;
    if (t && sscanf(t, "%u,%u,%u", &r, &l, &tt) == 3 && r && l && tt && tt <= l &&
        (uint64_t) r * l <= inv && (uint64_t) tt * r * l * 8 <= smem)
        g->R = r, g->L = l, g->T = tt;
}

/* the kernels' module with the size constants for a tile of T tokens */
static WGPUShaderModule make_module(zt_wgpu_t *g, uint32_t T)
{
    char pre[256];
    int np = snprintf(pre, sizeof pre,
                      "const ZW_WG: u32 = %uu;\nconst ZW_R: u32 = %uu;\nconst ZW_L: u32 = "
                      "%uu;\nconst ZW_T: u32 = %uu;\n",
                      g->WG, g->R, g->L, T);
    size_t n = (size_t) np + sizeof zw_wgsl;
    char *src = malloc(n);
    if (!src) return NULL;
    memcpy(src, pre, (size_t) np);
    memcpy(src + np, zw_wgsl, sizeof zw_wgsl);
    WGPUShaderSourceWGSL w = WGPU_SHADER_SOURCE_WGSL_INIT;
    w.code.data = src;
    w.code.length = n - 1;
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    md.nextInChain = &w.chain;
    WGPUShaderModule m = g->api.DeviceCreateShaderModule(g->dev, &md);
    free(src);
    return m;
}

static WGPUComputePipeline make_pipeline(zt_wgpu_t *g, WGPUShaderModule m, const char *entry)
{
    WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pd.layout = g->pl;
    pd.compute.module = m;
    pd.compute.entryPoint = sv(entry);
    return g->api.DeviceCreateComputePipeline(g->dev, &pd);
}

static int make_kernels(zt_wgpu_t *g)
{
    WGPUBindGroupLayoutEntry e[7];
    for (uint32_t i = 0; i < 7; i++) {
        e[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        e[i].binding = i;
        e[i].visibility = WGPUShaderStage_Compute;
        e[i].buffer.type = i == 0   ? WGPUBufferBindingType_Uniform
                           : i <= 3 ? WGPUBufferBindingType_ReadOnlyStorage
                                    : WGPUBufferBindingType_Storage;
    }
    WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    ld.entryCount = 7;
    ld.entries = e;
    scope_begin(g);
    g->bgl = g->api.DeviceCreateBindGroupLayout(g->dev, &ld);
    WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &g->bgl;
    g->pl = g->api.DeviceCreatePipelineLayout(g->dev, &pld);
    WGPUShaderModule m1 = make_module(g, 1), mt = make_module(g, g->T);
    static const char *const mmn[3] = {"mm_q4k", "mm_q6k", "mm_q80"};
    if (m1 && mt) {
        for (uint32_t k = 0; k < ZG_NK; k++) {
            if (k == ZG_K_MM) continue;
            char en[8];
            snprintf(en, sizeof en, "k%u", k);
            g->zk[k] = make_pipeline(g, m1, en);
        }
        for (uint32_t q = 0; q < 3; q++) {
            g->mm[0][q] = make_pipeline(g, m1, mmn[q]);
            g->mm[1][q] = make_pipeline(g, mt, mmn[q]);
        }
    }
    if (m1) g->api.ShaderModuleRelease(m1);
    if (mt) g->api.ShaderModuleRelease(mt);
    if (scope_end(g) || !m1 || !mt) return -1;
    for (uint32_t k = 0; k < ZG_NK; k++)
        if (k != ZG_K_MM && !g->zk[k]) return -1;
    for (uint32_t q = 0; q < 3; q++)
        if (!g->mm[0][q] || !g->mm[1][q]) return -1;
    return 0;
}

zt_wgpu_t *zt_wgpu_new(char *err, uint32_t errcap)
{
    zt_wgpu_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    uint64_t to = env_u64("ZT_WGPU_TIMEOUT_S");
    g->timeout_ns = (to ? to : 600) * 1000000000ull;
#    if defined(__EMSCRIPTEN__)
#        define ZW_BIND(n) g->api.n = wgpu##n;
    ZW_FNS(ZW_BIND)
#    else
    g->lib = lib_open();
    if (!g->lib) {
        say(err, errcap,
            "no WebGPU library (libwgpu_native, libwebgpu_dawn; ZT_WGPU_LIB names one)", NULL);
        goto fail;
    }
#        define ZW_LOAD(n)                                                                         \
            {                                                                                      \
                void *s_ = lib_sym(g->lib, "wgpu" #n);                                             \
                if (!s_) {                                                                         \
                    say(err, errcap, "the WebGPU library lacks wgpu" #n, NULL);                    \
                    goto fail;                                                                     \
                }                                                                                  \
                memcpy(&g->api.n, &s_, sizeof s_);                                                 \
            }
    ZW_FNS(ZW_LOAD)
    {
        void *p = lib_sym(g->lib, "wgpuDevicePoll");
        memcpy(&g->poll, &p, sizeof p);
    }
#    endif

    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
#    if defined(__EMSCRIPTEN__)
    WGPUInstanceFeatureName tw = WGPUInstanceFeatureName_TimedWaitAny;
    id.requiredFeatureCount = 1;
    id.requiredFeatures = &tw;
#    endif
    if (!(g->inst = g->api.CreateInstance(&id))) {
        say(err, errcap, "cannot create a WebGPU instance", NULL);
        goto fail;
    }
    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.powerPreference = WGPUPowerPreference_HighPerformance;
    wres_t ra = {0, 0, NULL, 0};
    WGPURequestAdapterCallbackInfo aci = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    aci.mode = WGPUCallbackMode_AllowProcessEvents;
    aci.callback = on_adapter;
    aci.userdata1 = g;
    aci.userdata2 = &ra;
    WGPUFuture fa = g->api.InstanceRequestAdapter(g->inst, &ao, aci);
    if (zw_wait(g, fa, &ra.done) || !g->adapter) {
        say(err, errcap, "no WebGPU adapter", g->emsg);
        goto fail;
    }
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (g->api.AdapterGetInfo(g->adapter, &info) == WGPUStatus_Success) {
        sv_copy(g->name, sizeof g->name, info.device);
        if (!g->name[0]) sv_copy(g->name, sizeof g->name, info.description);
        g->api.AdapterInfoFreeMembers(info);
    }
    WGPULimits al = WGPU_LIMITS_INIT;
    if (g->api.AdapterGetLimits(g->adapter, &al) != WGPUStatus_Success) {
        say(err, errcap, "cannot read the WebGPU adapter's limits", NULL);
        goto fail;
    }
    /* ask for the adapter's best in what the kernels use; the rest default */
    WGPULimits want = WGPU_LIMITS_INIT;
    want.maxStorageBufferBindingSize = al.maxStorageBufferBindingSize;
    want.maxBufferSize = al.maxBufferSize;
    want.maxComputeWorkgroupStorageSize = al.maxComputeWorkgroupStorageSize;
    want.maxComputeInvocationsPerWorkgroup = al.maxComputeInvocationsPerWorkgroup;
    want.maxComputeWorkgroupSizeX = al.maxComputeWorkgroupSizeX;
    want.maxComputeWorkgroupsPerDimension = al.maxComputeWorkgroupsPerDimension;
    want.maxStorageBuffersPerShaderStage = al.maxStorageBuffersPerShaderStage;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.requiredLimits = &want;
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    dd.deviceLostCallbackInfo.callback = on_lost;
    dd.deviceLostCallbackInfo.userdata1 = g;
    dd.uncapturedErrorCallbackInfo.callback = on_error;
    dd.uncapturedErrorCallbackInfo.userdata1 = g;
    wres_t rd = {0, 0, NULL, 0};
    WGPURequestDeviceCallbackInfo dci = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dci.mode = WGPUCallbackMode_AllowProcessEvents;
    dci.callback = on_device;
    dci.userdata1 = g;
    dci.userdata2 = &rd;
    WGPUFuture fd = g->api.AdapterRequestDevice(g->adapter, &dd, dci);
    if (zw_wait(g, fd, &rd.done) || !g->dev) {
        say(err, errcap, "cannot create the WebGPU device", g->emsg);
        goto fail;
    }
    g->lim = WGPU_LIMITS_INIT;
    if (g->api.DeviceGetLimits(g->dev, &g->lim) != WGPUStatus_Success) {
        say(err, errcap, "cannot read the WebGPU device's limits", NULL);
        goto fail;
    }
    if (g->lim.maxStorageBuffersPerShaderStage < 6 ||
        g->lim.maxComputeInvocationsPerWorkgroup < 32 || g->lim.maxComputeWorkgroupSizeX < 32) {
        say(err, errcap, "the WebGPU device's limits are below the kernels' needs", NULL);
        goto fail;
    }
    g->q = g->api.DeviceGetQueue(g->dev);
    g->align =
        g->lim.minStorageBufferOffsetAlignment ? g->lim.minStorageBufferOffsetAlignment : 256;
    g->ualign =
        g->lim.minUniformBufferOffsetAlignment ? g->lim.minUniformBufferOffsetAlignment : 256;
    if (g->ualign < ZW_UNIFORM) g->ualign = ZW_UNIFORM;
    g->max_groups = g->lim.maxComputeWorkgroupsPerDimension;
    /* in-shader offsets are 32-bit: no binding past 2 GiB */
    g->max_binding = g->lim.maxStorageBufferBindingSize;
    if (g->max_binding > ((uint64_t) 1 << 31)) g->max_binding = (uint64_t) 1 << 31;
    g->max_binding &= ~(uint64_t) 3;
    uint64_t kb = env_u64("ZT_WGPU_MAX_BINDING");
    if (kb >= 4096 && kb < g->max_binding) g->max_binding = kb & ~(uint64_t) 3;
    g->max_buffer = g->lim.maxBufferSize;
    if (g->max_buffer > ((uint64_t) 1 << 31)) g->max_buffer = (uint64_t) 1 << 31;
    g->max_buffer &= ~(uint64_t) 255;
    g->chunk_max = g->max_buffer;
    uint64_t kf = env_u64("ZT_WGPU_MAX_BUFFER");
    if (kf >= 4096 && kf < g->chunk_max) g->chunk_max = kf & ~(uint64_t) 255;
    pick_sizes(g);
    if (make_kernels(g)) {
        say(err, errcap, "the WebGPU kernels did not build", g->emsg);
        goto fail;
    }
    for (int i = 0; i < 3; i++)
        if (!(g->dummy[i] = buf_make(g, 256, U_STORE))) {
            say(err, errcap, "cannot make a WebGPU buffer", g->emsg);
            goto fail;
        }
    if (grow(g, &g->ctr, 4, U_STORE)) {
        say(err, errcap, "cannot make a WebGPU buffer", g->emsg);
        goto fail;
    }
    return g;
fail:
    zt_wgpu_free(g);
    return NULL;
}

const char *zt_wgpu_name(const zt_wgpu_t *g)
{
    return g ? g->name : "";
}

void zt_wgpu_sizes(const zt_wgpu_t *g, zt_wgpu_sizes_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!g) return;
    out->rows = g->R, out->lanes = g->L, out->tile = g->T, out->wg = g->WG;
    out->max_binding = g->max_binding, out->max_buffer = g->chunk_max;
}

/* ---- regions ---- */

static int region_add(zt_wgpu_t *g, const void *base, uint64_t len, bool shared)
{
    if (!g || !base || !len || g->n_reg >= ZW_REGIONS) return -1;
    region_t *rg = &g->reg[g->n_reg];
    memset(rg, 0, sizeof *rg);
    uint64_t C = shared ? g->max_buffer : g->chunk_max;
    if (shared && len > C) return -1; /* one buffer: the GPU writes it */
    uint64_t O = C / 2 < ((uint64_t) 16 << 20) ? C / 2 : ((uint64_t) 16 << 20);
    O &= ~(uint64_t) 255;
    rg->clen = len <= C ? len : C;
    rg->stride = len <= C ? C : C - O;
    rg->nch = len <= C ? 1 : (uint32_t) (1 + (len - C + rg->stride - 1) / rg->stride);
    if (!(rg->ch = calloc(rg->nch, sizeof *rg->ch))) return -1;
    rg->base = base;
    rg->len = len;
    rg->shared = shared;
    int rc = 0;
    for (uint32_t k = 0; k < rg->nch && !rc; k++) {
        uint64_t o = (uint64_t) k * rg->stride, n = len - o < rg->clen ? len - o : rg->clen;
        if (!(rg->ch[k] = buf_make(g, n, U_STORE)))
            rc = -1;
        else if (!shared)
            put(g, rg->ch[k], 0, rg->base + o, n);
    }
    if (rc) {
        for (uint32_t k = 0; k < rg->nch; k++) buf_free(g, &rg->ch[k]);
        free(rg->ch);
        memset(rg, 0, sizeof *rg);
        return -1;
    }
    g->n_reg++;
    return 0;
}

int zt_wgpu_map(zt_wgpu_t *g, const void *base, uint64_t len)
{
    return region_add(g, base, len, false);
}

int zt_wgpu_map_shared(zt_wgpu_t *g, void *base, uint64_t len)
{
    return region_add(g, base, len, true);
}

static int find_region(const zt_wgpu_t *g, const uint8_t *p, uint64_t len, uint64_t *off)
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

/* the chunk of region rg that holds [off, off + len): its argument; 1 if none */
static int region_ref(const region_t *rg, uint64_t off, uint64_t len, wref_t *out)
{
    uint32_t k = (uint32_t) (off / rg->stride);
    if (k >= rg->nch) k = rg->nch - 1;
    uint64_t co = (uint64_t) k * rg->stride;
    uint64_t cl = rg->len - co < rg->clen ? rg->len - co : rg->clen;
    if (off < co || off + len > co + cl) return 1;
    *out = (wref_t){rg->ch[k], off - co, len, (cl + 3) & ~(uint64_t) 3};
    return 0;
}

static void span(uint64_t *lo, uint64_t *hi, uint64_t a, uint64_t n)
{
    if (*hi <= *lo) {
        *lo = a, *hi = a + n;
        return;
    }
    if (a < *lo) *lo = a;
    if (a + n > *hi) *hi = a + n;
}

static void spans_clear(zt_wgpu_t *g)
{
    for (uint32_t i = 0; i < g->n_reg; i++)
        g->reg[i].up_lo = g->reg[i].up_hi = g->reg[i].dn_lo = g->reg[i].dn_hi = 0;
}

/* copy the shared regions' spans the GPU will read in (the host has the
 * latest bytes) */
static void spans_in(zt_wgpu_t *g)
{
    for (uint32_t i = 0; i < g->n_reg; i++) {
        region_t *r = &g->reg[i];
        if (!r->shared || r->up_hi <= r->up_lo) continue;
        uint64_t a = r->up_lo & ~(uint64_t) 3, e = (r->up_hi + 3) & ~(uint64_t) 3;
        if (e > r->len) e = r->len; /* the last word: zero padded by put */
        put(g, r->ch[0], a, r->base + a, e - a);
    }
}

/* ---- planning and running dispatches ---- */

static plan_t *plan_add(zt_wgpu_t *g)
{
    if (g->n_plan == g->plan_cap) {
        uint32_t cap = g->plan_cap ? g->plan_cap * 2 : 256;
        plan_t *p = realloc(g->plan, (size_t) cap * sizeof *p);
        if (!p) return NULL;
        g->plan = p;
        g->plan_cap = cap;
    }
    plan_t *p = &g->plan[g->n_plan++];
    memset(p, 0, sizeof *p);
    return p;
}

/* plan one dispatch of pipe over gx x gy workgroups with arguments r[0..6)
 * (bit j of wm: the kernel writes argument j): each distinct buffer bound
 * once, read-only or (when written) writable. 0, or 1 when the arguments
 * cannot be bound on this device. */
static int plan_dispatch(zt_wgpu_t *g, WGPUComputePipeline pipe, uint32_t wm, const wref_t *r,
                         const uint32_t *u, uint32_t gx, uint32_t gy)
{
    WGPUBuffer bufs[6];
    uint64_t lo[6], hi[6], cap[6];
    bool wr[6];
    uint32_t nb = 0, at[6];
    for (uint32_t j = 0; j < 6; j++) {
        at[j] = UINT32_MAX;
        if (!r[j].b) continue;
        if (r[j].off > r[j].cap || r[j].len > r[j].cap - r[j].off) return 1;
        uint32_t k = 0;
        while (k < nb && bufs[k] != r[j].b) k++;
        if (k == nb) {
            bufs[nb] = r[j].b, lo[nb] = r[j].off, hi[nb] = r[j].off + r[j].len, cap[nb] = r[j].cap;
            wr[nb++] = false;
        }
        span(&lo[k], &hi[k], r[j].off, r[j].len);
        if (wm & (1u << j)) wr[k] = true;
        at[j] = k;
    }
    plan_t *p = plan_add(g);
    if (!p) return 1;
    uint32_t slot[6], nr = 0, nw = 0;
    for (uint32_t k = 0; k < nb; k++) {
        uint64_t base = lo[k] - lo[k] % g->align;
        uint64_t end = (hi[k] + 3) & ~(uint64_t) 3;
        if (end > cap[k]) end = cap[k];
        if (end <= base) end = base + 4 <= cap[k] ? base + 4 : cap[k];
        if (end - base > g->max_binding || end <= base) return 1;
        if (wr[k] ? nw >= 2 : nr >= 3) return 1;
        slot[k] = wr[k] ? 3 + nw++ : nr++;
        p->bind[slot[k]] = bufs[k];
        p->boff[slot[k]] = base;
        p->bsize[slot[k]] = end - base;
    }
    p->pipe = pipe;
    memcpy(p->P, u, 16 * sizeof *u);
    for (uint32_t j = 0; j < 6; j++)
        if (at[j] != UINT32_MAX) {
            uint32_t k = at[j];
            p->P[16 + j] = (uint32_t) (r[j].off - p->boff[slot[k]]);
            p->P[22 + j] = slot[k];
        }
    p->P[28] = gx;
    p->gx = gx;
    p->gy = gy;
    return 0;
}

/* a layer kernel over u[0] threads (a 2-D grid past max_groups workgroups) */
static int plan_zk(zt_wgpu_t *g, uint32_t k, const wref_t *r, const uint32_t *u)
{
    uint32_t groups = (u[0] + g->WG - 1) / g->WG;
    uint32_t gx = groups < g->max_groups ? groups : g->max_groups;
    uint32_t gy = (groups + gx - 1) / gx;
    if (gy > g->max_groups) return 1;
    return plan_dispatch(g, g->zk[k], wmask[k], r, u, gx, gy);
}

/* a product y[B x rows] = weights (region ri from byte woff; q type 0 Q4_K,
 * 1 Q6_K, 2 Q8_0) times the activations a / shifts / sums: sliced by rows so
 * each slice lies in one chunk and one binding */
static int plan_mm(zt_wgpu_t *g, int q, uint32_t rows, uint32_t nb, uint32_t astride, uint32_t B,
                   uint32_t rowbytes, int ri, uint64_t woff, wref_t a, wref_t ash, wref_t asum,
                   wref_t y)
{
    uint32_t tiled = B > 1, T = tiled ? g->T : 1, sb = astride / 32;
    const region_t *rg = &g->reg[ri];
    if ((B + T - 1) / T > g->max_groups) return 1;
    a.len = (uint64_t) B * astride * 2;
    ash.len = (uint64_t) B * sb;
    asum.len = (uint64_t) B * sb * 4;
    y.len = (uint64_t) B * rows * 4;
    for (uint32_t r0 = 0; r0 < rows;) {
        uint64_t p = woff + (uint64_t) r0 * rowbytes;
        uint32_t k = (uint32_t) (p / rg->stride);
        if (k >= rg->nch) k = rg->nch - 1;
        uint64_t co = (uint64_t) k * rg->stride;
        uint64_t cl = rg->len - co < rg->clen ? rg->len - co : rg->clen, rel = p - co;
        uint64_t room = cl - rel, bind = g->max_binding - rel % g->align;
        if (bind < room) room = bind;
        uint64_t n = room / rowbytes, grid = (uint64_t) g->max_groups * g->R;
        if (n > rows - r0) n = rows - r0;
        if (n > grid) n = grid;
        if (!n) return 1;
        wref_t ref[6] = {
            {rg->ch[k], rel, n * rowbytes, (cl + 3) & ~(uint64_t) 3}, a, ash, asum, y, {0}};
        uint32_t u[16] = {(uint32_t) n, nb, astride, B, rowbytes, rows, r0};
        if (plan_dispatch(g, g->mm[tiled][q], wmask[ZG_K_MM], ref, u,
                          (uint32_t) ((n + g->R - 1) / g->R), (B + T - 1) / T))
            return 1;
        r0 += (uint32_t) n;
    }
    return 0;
}

/* one copy out of the GPU after the dispatches */
typedef struct {
    WGPUBuffer src;
    uint64_t off, n; /* multiples of 4 */
} cpy_t;

/* run the planned dispatches, then copy each of out[0..nout) into the
 * readback buffer, back to back; *rb points at its mapped bytes until
 * run_done. 0 on success. */
static int run_plan(zt_wgpu_t *g, const cpy_t *out, uint32_t nout, const uint8_t **rb)
{
    uint32_t n = g->n_plan;
    uint64_t total = 0;
    for (uint32_t i = 0; i < nout; i++) total += out[i].n;
    if (grow(g, &g->rb, total, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst) ||
        grow(g, &g->prm, (uint64_t) (n ? n : 1) * g->ualign,
             WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst) ||
        hgrow(&g->prm_h, &g->prm_hcap, (uint64_t) (n ? n : 1) * g->ualign))
        return -1;
    if (n > g->bgs_cap) {
        WGPUBindGroup *b = realloc(g->bgs, (size_t) n * sizeof *b);
        if (!b) return -1;
        g->bgs = b;
        g->bgs_cap = n;
    }
    for (uint32_t i = 0; i < n; i++)
        memcpy(g->prm_h + (uint64_t) i * g->ualign, g->plan[i].P, sizeof g->plan[i].P);
    if (n) put(g, g->prm.b, 0, g->prm_h, (uint64_t) n * g->ualign);
    scope_begin(g);
    WGPUCommandEncoderDescriptor ed = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder enc = g->api.DeviceCreateCommandEncoder(g->dev, &ed);
    WGPUComputePassDescriptor cd = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = g->api.CommandEncoderBeginComputePass(enc, &cd);
    for (uint32_t i = 0; i < n; i++) {
        const plan_t *p = &g->plan[i];
        WGPUBindGroupEntry e[7];
        for (uint32_t j = 0; j < 7; j++) {
            e[j] = WGPU_BIND_GROUP_ENTRY_INIT;
            e[j].binding = j;
        }
        e[0].buffer = g->prm.b, e[0].offset = (uint64_t) i * g->ualign, e[0].size = ZW_UNIFORM;
        for (uint32_t s = 0; s < 5; s++) {
            WGPUBindGroupEntry *x = &e[1 + s];
            if (p->bind[s])
                x->buffer = p->bind[s], x->offset = p->boff[s], x->size = p->bsize[s];
            else
                x->buffer = g->dummy[s < 3 ? 0 : s - 2], x->offset = 0, x->size = 256;
        }
        e[6].buffer = g->ctr.b, e[6].offset = 0, e[6].size = 4;
        WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bd.layout = g->bgl;
        bd.entryCount = 7;
        bd.entries = e;
        g->bgs[i] = g->api.DeviceCreateBindGroup(g->dev, &bd);
        g->api.ComputePassEncoderSetPipeline(pass, p->pipe);
        g->api.ComputePassEncoderSetBindGroup(pass, 0, g->bgs[i], 0, NULL);
        g->api.ComputePassEncoderDispatchWorkgroups(pass, p->gx, p->gy, 1);
    }
    g->api.ComputePassEncoderEnd(pass);
    g->api.ComputePassEncoderRelease(pass);
    uint64_t at = 0;
    for (uint32_t i = 0; i < nout; i++) {
        g->api.CommandEncoderCopyBufferToBuffer(enc, out[i].src, out[i].off, g->rb.b, at, out[i].n);
        at += out[i].n;
    }
    WGPUCommandBufferDescriptor cbd = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer cb = g->api.CommandEncoderFinish(enc, &cbd);
    g->api.CommandEncoderRelease(enc);
    /* nothing invalid is submitted (some libraries abort on that) */
    if (scope_end(g)) {
        if (cb) g->api.CommandBufferRelease(cb);
        for (uint32_t i = 0; i < n; i++)
            if (g->bgs[i]) g->api.BindGroupRelease(g->bgs[i]);
        return -1;
    }
    scope_begin(g);
    g->api.QueueSubmit(g->q, 1, &cb);
    g->api.CommandBufferRelease(cb);
    wres_t rm = {0, 0, NULL, 0};
    WGPUBufferMapCallbackInfo mi = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mi.mode = WGPUCallbackMode_AllowProcessEvents;
    mi.callback = on_map;
    mi.userdata1 = &rm;
    WGPUFuture fm = g->api.BufferMapAsync(g->rb.b, WGPUMapMode_Read, 0, (size_t) g->rb.used, mi);
    int bad = zw_wait(g, fm, &rm.done) || rm.err;
    if (scope_end(g)) bad = 1;
    for (uint32_t i = 0; i < n; i++)
        if (g->bgs[i]) g->api.BindGroupRelease(g->bgs[i]);
    if (bad) {
        if (rm.done && !rm.err) g->api.BufferUnmap(g->rb.b);
        return -1;
    }
    *rb = g->api.BufferGetConstMappedRange(g->rb.b, 0, (size_t) g->rb.used);
    if (!*rb) {
        g->api.BufferUnmap(g->rb.b);
        return -1;
    }
    return 0;
}

static void run_done(zt_wgpu_t *g)
{
    g->api.BufferUnmap(g->rb.b);
}

/* ---- the per-product path (zt_kern_t.mm_full) ---- */

#    define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

static int qindex(uint32_t qtype)
{
    return qtype == 12 ? 0 : qtype == 14 ? 1 : qtype == 8 ? 2 : -1;
}

static uint64_t row_bytes(uint32_t qtype, uint32_t nb)
{
    return qtype == 12   ? (uint64_t) (nb / 8) * 144
           : qtype == 14 ? (uint64_t) (nb / 8) * 210
                         : (uint64_t) nb * 34;
}

int zt_wgpu_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    zt_wgpu_t *g = fctx;
    if (!g || n == 0 || n > 8) return 1;
    uint64_t t_in = now_ns();
    g->st.calls++;
    if (jobs[0].B < g->min_tokens || g->lost) {
        g->st.declined++;
        return 1;
    }
    int ri[8];
    uint64_t woff[8], aoff[8], soff[8], hoff[8], yoff[8];
    uint64_t in_bytes = 0, out_bytes = 0;
    spans_clear(g);
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        bool ok = j->raw && j->B && j->cols % 32 == 0 && j->astride >= j->cols &&
                  j->astride % 32 == 0 && qindex(j->qtype) >= 0 &&
                  (j->qtype == 8 || j->cols % 256 == 0);
        uint64_t rb = row_bytes(j->qtype, nb);
        if (ok) ri[i] = find_region(g, j->raw, rb * j->rows, &woff[i]);
        if (!ok || ri[i] < 0) {
            g->st.declined++;
            return 1;
        }
        region_t *r = &g->reg[ri[i]];
        if (r->shared) span(&r->up_lo, &r->up_hi, woff[i], rb * j->rows);
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
    if (grow(g, &g->in, in_bytes, U_STORE) || grow(g, &g->out, out_bytes, U_STORE) ||
        hgrow(&g->in_h, &g->in_hcap, in_bytes)) {
        g->st.declined++;
        return 1;
    }
    g->n_plan = 0;
    int rc = 0;
    for (uint32_t i = 0; i < n && !rc; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        wref_t a = {g->in.b, aoff[i], 0, g->in.cap}, ash = {g->in.b, hoff[i], 0, g->in.cap};
        wref_t asum = {g->in.b, soff[i], 0, g->in.cap}, y = {g->out.b, yoff[i], 0, g->out.cap};
        rc = plan_mm(g, qindex(j->qtype), j->rows, nb, j->astride, j->B,
                     (uint32_t) row_bytes(j->qtype, nb), ri[i], woff[i], a, ash, asum, y);
    }
    if (rc) {
        g->st.declined++;
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        if (i && aoff[i] == aoff[i - 1]) continue;
        uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
        memcpy(g->in_h + aoff[i], j->a, (size_t) j->B * j->astride * 2);
        memcpy(g->in_h + soff[i], j->asum, (size_t) nbk * 4);
        memcpy(g->in_h + hoff[i], j->ash, (size_t) nbk);
    }
    spans_in(g);
    put(g, g->in.b, 0, g->in_h, in_bytes);
    cpy_t c = {g->out.b, 0, out_bytes};
    const uint8_t *res;
    if (run_plan(g, &c, 1, &res)) {
        g->st.declined++;
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        const uint8_t *src = res + yoff[i];
        for (uint32_t t = 0; t < j->B; t++)
            memcpy(j->y + (uint64_t) t * j->ystride, src + (uint64_t) t * j->rows * 4,
                   (size_t) j->rows * 4);
    }
    run_done(g);
    g->st.products += n;
    g->st.wall_ns += now_ns() - t_in;
    return 0;
}

void zt_kern_wgpu(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g)
{
    *out = *base;
    out->mm_full = g ? zt_wgpu_mm : 0;
    out->fwd_full = 0;
    out->fctx = g;
}

/* ---- the whole forward pass (zt_gpu.h) ---- */

static void *be_work(void *ctx, uint64_t bytes)
{
    zt_wgpu_t *g = ctx;
    if (grow(g, &g->work, bytes, U_STORE) || hgrow(&g->work_h, &g->work_hcap, g->work.used))
        return NULL;
    return g->work_h;
}

static void *be_cst(void *ctx, uint64_t bytes)
{
    zt_wgpu_t *g = ctx;
    if (grow(g, &g->cst, bytes ? bytes : 4, U_STORE) || hgrow(&g->cst_h, &g->cst_hcap, g->cst.used))
        return NULL;
    return g->cst_h;
}

/* an op's buffer argument (not a product's weights); 1 if it cannot be bound */
static int be_ref(zt_wgpu_t *g, const zg_buf_t *b, bool written, wref_t *out)
{
    switch (b->kind) {
    case ZG_B_WORK:
        if (b->off < 4 || b->off >= g->work.used) return 1; /* 0..3: the counter's copy */
        *out = (wref_t){g->work.b, b->off, g->work.used - b->off, g->work.cap};
        return 0;
    case ZG_B_CONST:
        if (b->off >= g->cst.used) return 1;
        *out = (wref_t){g->cst.b, b->off, g->cst.used - b->off, g->cst.cap};
        return 0;
    case ZG_B_HOST: {
        uint64_t off;
        int ri = find_region(g, b->host, b->len, &off);
        if (ri < 0) return 1;
        region_t *r = &g->reg[ri];
        if ((b->inplace || written) && !r->shared) return 1;
        if (region_ref(r, off, b->len, out)) return 1;
        if (r->shared) span(&r->up_lo, &r->up_hi, off, b->len);
        if (r->shared && written) span(&r->dn_lo, &r->dn_hi, off, b->len);
        return 0;
    }
    default:
        *out = (wref_t){0};
        return 0;
    }
}

static int be_run(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst)
{
    zt_wgpu_t *g = ctx;
    (void) work, (void) cst;
    if (g->lost) return 1;
    /* plan every op before anything runs: a pass is all or nothing */
    spans_clear(g);
    g->n_plan = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zg_op_t *o = &ops[i];
        if (o->k >= ZG_NK) return 1;
        if (!o->u[0]) continue;
        wref_t r[6];
        if (o->k == ZG_K_MM) {
            uint64_t woff;
            int ri =
                o->b[0].kind == ZG_B_HOST ? find_region(g, o->b[0].host, o->b[0].len, &woff) : -1;
            if (ri < 0 || qindex(o->u[4]) < 0) return 1;
            region_t *rg = &g->reg[ri];
            if (rg->shared) span(&rg->up_lo, &rg->up_hi, woff, o->b[0].len);
            for (int j = 1; j < 5; j++)
                if (be_ref(g, &o->b[j], j == 4, &r[j])) return 1;
            if (plan_mm(g, qindex(o->u[4]), o->u[1], o->u[2], o->u[3], o->u[6], o->u[5], ri, woff,
                        r[1], r[2], r[3], r[4]))
                return 1;
        } else {
            for (int j = 0; j < 6; j++)
                if (be_ref(g, &o->b[j], (wmask[o->k] >> j) & 1u, &r[j])) return 1;
            if (plan_zk(g, o->k, r, o->u)) return 1;
        }
    }
    /* in: the work and constants as zg_forward wrote them, the counter's 0,
     * the shared regions' spans the pass reads */
    put(g, g->work.b, 0, g->work_h, g->work.used);
    put(g, g->cst.b, 0, g->cst_h, g->cst.used);
    put(g, g->ctr.b, 0, g->work_h, 4);
    spans_in(g);
    /* out: the work, the counter, the spans the pass writes in place */
    cpy_t c[2 + ZW_REGIONS];
    uint32_t nc = 0;
    c[nc++] = (cpy_t){g->work.b, 0, g->work.used};
    c[nc++] = (cpy_t){g->ctr.b, 0, 4};
    for (uint32_t i = 0; i < g->n_reg; i++) {
        region_t *r = &g->reg[i];
        if (!r->shared || r->dn_hi <= r->dn_lo) continue;
        uint64_t a = r->dn_lo & ~(uint64_t) 3, e = (r->dn_hi + 3) & ~(uint64_t) 3;
        c[nc++] = (cpy_t){r->ch[0], a, e - a};
    }
    const uint8_t *res;
    if (run_plan(g, c, nc, &res)) return 1;
    memcpy(g->work_h, res, (size_t) g->work.used);
    memcpy(g->work_h, res + g->work.used, 4); /* the saturation counter */
    uint64_t at = g->work.used + 4;
    for (uint32_t i = 0; i < g->n_reg; i++) {
        region_t *r = &g->reg[i];
        if (!r->shared || r->dn_hi <= r->dn_lo) continue;
        uint64_t a = r->dn_lo & ~(uint64_t) 3, e = (r->dn_hi + 3) & ~(uint64_t) 3;
        memcpy((uint8_t *) r->base + r->dn_lo, res + at + (r->dn_lo - a),
               (size_t) (r->dn_hi - r->dn_lo));
        at += e - a;
    }
    run_done(g);
    return 0;
}

int zt_wgpu_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                uint32_t *n_sat)
{
    zt_wgpu_t *g = fctx;
    if (!g) return 1;
    uint64_t t0 = now_ns();
    zg_backend_t be = {g, be_work, be_cst, be_run};
    int r = zg_forward(&be, m, s, B, logits, first, n_sat);
    if (r) {
        g->st.passes_declined++;
        return r;
    }
    g->st.passes++;
    g->st.pass_ops += zg_last_ops();
    g->st.wall_ns += now_ns() - t0;
    return 0;
}

void zt_kern_wgpu_resident(zt_kern_t *out, const zt_kern_t *base, zt_wgpu_t *g)
{
    zt_kern_wgpu(out, base, g);
    out->fwd_full = g ? zt_wgpu_fwd : 0;
}

void zt_wgpu_set_min_tokens(zt_wgpu_t *g, uint32_t n)
{
    if (g) g->min_tokens = n;
}

void zt_wgpu_stats(const zt_wgpu_t *g, zt_wgpu_stats_t *out)
{
    if (g && out) *out = g->st;
}

#endif /* ZT_WGPU_BUILD */
