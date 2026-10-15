/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_accel.c — zt_accel.h: the first GPU backend that finds a GPU. */
#include "zt_accel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ZT_HAVE_METAL)
#    include "zt_metal.h"
#endif
#if !defined(__APPLE__)
#    define ZA_DL 1 /* CUDA, HIP and Vulkan: drivers opened at run time */
#    include "zt_cuda.h"
#    include "zt_hip.h"
#    include "zt_vk.h"
#endif

typedef struct {
    const char *api;
    void *(*open)(char *why, uint32_t cap);
    int (*map)(void *g, const void *base, uint64_t len);
    int (*map_state)(void *g, void *base, uint64_t len);
    void (*kern)(zt_kern_t *out, const zt_kern_t *base, void *g, bool resident);
    const char *(*name)(const void *g);
    void (*stats)(const void *g, zt_accel_stats_t *out);
    void (*free)(void *g);
} za_ops_t;

struct zt_accel {
    const za_ops_t *ops;
    void *g;
    zt_kern_t inner; /* the backend's kernels, which out's hooks call */
    uint32_t lo, hi; /* the GPU takes products and passes of lo..hi tokens */
};

#define ZA_STATS(dst, src)                                                                         \
    ((dst)->calls = (src).calls, (dst)->products = (src).products,                                 \
     (dst)->declined = (src).declined, (dst)->gpu_ns = (src).gpu_ns,                               \
     (dst)->wall_ns = (src).wall_ns, (dst)->passes = (src).passes,                                 \
     (dst)->passes_declined = (src).passes_declined, (dst)->pass_ops = (src).pass_ops)

/* One backend's table: P its prefix (zt_metal, zt_cuda, zt_hip, zt_vk), K its
 * zt_kern_ prefix, M its state-sharing map, O an expression opening it with
 * why and cap in scope. */
#define ZA_BACKEND(tag, api, P, K, M, O)                                                           \
    static void *tag##_open(char *why, uint32_t cap)                                               \
    {                                                                                              \
        return O;                                                                                  \
    }                                                                                              \
    static int tag##_map(void *g, const void *b, uint64_t n)                                       \
    {                                                                                              \
        return P##_map(g, b, n);                                                                   \
    }                                                                                              \
    static int tag##_map_state(void *g, void *b, uint64_t n)                                       \
    {                                                                                              \
        return M(g, b, n);                                                                         \
    }                                                                                              \
    static void tag##_kern(zt_kern_t *o, const zt_kern_t *b, void *g, bool res)                    \
    {                                                                                              \
        if (res)                                                                                   \
            K##_resident(o, b, g);                                                                 \
        else                                                                                       \
            K(o, b, g);                                                                            \
    }                                                                                              \
    static const char *tag##_name(const void *g)                                                   \
    {                                                                                              \
        return P##_name(g);                                                                        \
    }                                                                                              \
    static void tag##_stats(const void *g, zt_accel_stats_t *o)                                    \
    {                                                                                              \
        P##_stats_t s;                                                                             \
        P##_stats(g, &s);                                                                          \
        ZA_STATS(o, s);                                                                            \
    }                                                                                              \
    static void tag##_free(void *g)                                                                \
    {                                                                                              \
        P##_free(g);                                                                               \
    }                                                                                              \
    static const za_ops_t tag##_ops = {api,        tag##_open, tag##_map,   tag##_map_state,       \
                                       tag##_kern, tag##_name, tag##_stats, tag##_free};

#if defined(ZA_DL)
static int dev_index(const char *env)
{
    const char *d = getenv(env);
    return d ? atoi(d) : 0;
}
#endif

#if defined(ZT_HAVE_METAL)
ZA_BACKEND(mt, "metal", zt_metal, zt_kern_metal, zt_metal_map, zt_metal_new(why, cap)) /* unified */
#endif
#if defined(ZA_DL)
ZA_BACKEND(cu, "cuda", zt_cuda, zt_kern_cuda, zt_cuda_map_shared,
           zt_cuda_new(dev_index("ZT_CUDA_DEVICE"), why, cap))
ZA_BACKEND(hp, "hip", zt_hip, zt_kern_hip, zt_hip_map_shared,
           zt_hip_new(dev_index("ZT_HIP_DEVICE"), why, cap))
ZA_BACKEND(vk, "vulkan", zt_vk, zt_kern_vk, zt_vk_map_shared,
           zt_vk_new(-1, why, cap)) /* ZT_VK_DEVICE picks the GPU */
#endif

/* every backend built in, in the default order */
static const za_ops_t *const za_all[] = {
#if defined(ZT_HAVE_METAL)
    &mt_ops,
#endif
#if defined(ZA_DL)
    &cu_ops, &hp_ops, &vk_ops,
#endif
    NULL};

uint32_t zt_accel_apis(const char **out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; za_all[i]; i++)
        if (n < cap) out[n++] = za_all[i]->api;
    return n;
}

/* "api: why" after what err already holds */
static void add_why(char *err, uint32_t cap, const char *api, const char *why)
{
    if (!err || !cap) return;
    size_t l = strlen(err);
    if (l + 1 < cap) snprintf(err + l, cap - l, "%s%s: %s", l ? "; " : "", api, why);
}

zt_accel_t *zt_accel_open_api(const char *api, char *err, uint32_t errcap)
{
    if (err && errcap) err[0] = 0;
    for (uint32_t i = 0; za_all[i]; i++) {
        if (strcmp(za_all[i]->api, api)) continue;
        char why[256] = "";
        void *g = za_all[i]->open(why, sizeof why);
        if (!g) {
            add_why(err, errcap, api, why[0] ? why : "no GPU");
            return NULL;
        }
        zt_accel_t *a = calloc(1, sizeof *a);
        if (!a) {
            za_all[i]->free(g);
            add_why(err, errcap, api, "out of memory");
            return NULL;
        }
        a->ops = za_all[i];
        a->g = g;
        a->hi = UINT32_MAX;
        return a;
    }
    add_why(err, errcap, api, "not built in");
    return NULL;
}

zt_accel_t *zt_accel_open(char *err, uint32_t errcap)
{
    const char *want = getenv("ZT_GPU");
    if (err && errcap) err[0] = 0;
    if (want && (!strcmp(want, "cpu") || !strcmp(want, "off") || !strcmp(want, "none"))) {
        add_why(err, errcap, "ZT_GPU", want);
        return NULL;
    }
    if (want) return zt_accel_open_api(want, err, errcap);
    if (!za_all[0]) {
        add_why(err, errcap, "gpu", "this build has no GPU backend");
        return NULL;
    }
    for (uint32_t i = 0; za_all[i]; i++) {
        char why[256] = "";
        zt_accel_t *a = zt_accel_open_api(za_all[i]->api, why, sizeof why);
        if (a) {
            if (err && errcap) err[0] = 0;
            return a;
        }
        if (err && errcap) {
            size_t l = strlen(err);
            if (l + 1 < errcap) snprintf(err + l, errcap - l, "%s%s", l ? "; " : "", why);
        }
    }
    return NULL;
}

void zt_accel_free(zt_accel_t *a)
{
    if (!a) return;
    a->ops->free(a->g);
    free(a);
}

const char *zt_accel_api(const zt_accel_t *a)
{
    return a ? a->ops->api : "";
}

const char *zt_accel_name(const zt_accel_t *a)
{
    return a ? a->ops->name(a->g) : "";
}

int zt_accel_map(zt_accel_t *a, const void *base, uint64_t len)
{
    return a ? a->ops->map(a->g, base, len) : -1;
}

int zt_accel_map_state(zt_accel_t *a, void *base, uint64_t len)
{
    return a ? a->ops->map_state(a->g, base, len) : -1;
}

void zt_accel_set_min_tokens(zt_accel_t *a, uint32_t n)
{
    zt_accel_set_tokens(a, n, UINT32_MAX);
}

void zt_accel_set_tokens(zt_accel_t *a, uint32_t lo, uint32_t hi)
{
    if (!a) return;
    a->lo = lo;
    a->hi = hi;
}

/* out's hooks: the backend's, for lo..hi tokens; the CPU's otherwise */
static int za_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    const zt_accel_t *a = fctx;
    if (!n || jobs[0].B < a->lo || jobs[0].B > a->hi || !a->inner.mm_full) return 1;
    return a->inner.mm_full(a->inner.fctx, jobs, n);
}

static int za_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                  uint32_t *n_sat)
{
    const zt_accel_t *a = fctx;
    if (B < a->lo || B > a->hi || !a->inner.fwd_full) return 1;
    return a->inner.fwd_full(a->inner.fctx, m, s, B, logits, first, n_sat);
}

void zt_accel_kern(zt_accel_t *a, zt_kern_t *out, const zt_kern_t *base, bool resident)
{
    if (!a) {
        *out = *base;
        return;
    }
    a->ops->kern(&a->inner, base, a->g, resident);
    *out = a->inner;
    out->mm_full = a->inner.mm_full ? za_mm : NULL;
    out->fwd_full = a->inner.fwd_full ? za_fwd : NULL;
    out->fctx = a;
}

uint64_t zt_accel_state_align(void)
{
    return 65536u;
}

void zt_accel_stats(const zt_accel_t *a, zt_accel_stats_t *out)
{
    memset(out, 0, sizeof *out);
    if (a) a->ops->stats(a->g, out);
}
