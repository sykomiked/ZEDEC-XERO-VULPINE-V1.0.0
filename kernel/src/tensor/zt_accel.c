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
    int (*map)(void *g, const void *base, uint64_t len);
    int (*map_state)(void *g, void *base, uint64_t len);
    void (*set_min_tokens)(void *g, uint32_t n);
    void (*kern)(zt_kern_t *out, const zt_kern_t *base, void *g, bool resident);
    const char *(*name)(const void *g);
    void (*stats)(const void *g, zt_accel_stats_t *out);
    void (*free)(void *g);
} za_ops_t;

struct zt_accel {
    const za_ops_t *ops;
    void *g;
};

#define ZA_STATS(dst, src)                                                                         \
    ((dst)->calls = (src).calls, (dst)->products = (src).products,                                 \
     (dst)->declined = (src).declined, (dst)->gpu_ns = (src).gpu_ns,                               \
     (dst)->wall_ns = (src).wall_ns, (dst)->passes = (src).passes,                                 \
     (dst)->passes_declined = (src).passes_declined, (dst)->pass_ops = (src).pass_ops)

/* One backend's table: P its prefix (zt_metal, zt_cuda, zt_hip, zt_vk), K its
 * zt_kern_ prefix, M its state-sharing map. */
#define ZA_BACKEND(tag, api, P, K, M)                                                              \
    static int tag##_map(void *g, const void *b, uint64_t n)                                       \
    {                                                                                              \
        return P##_map(g, b, n);                                                                   \
    }                                                                                              \
    static int tag##_map_state(void *g, void *b, uint64_t n)                                       \
    {                                                                                              \
        return M(g, b, n);                                                                         \
    }                                                                                              \
    static void tag##_min(void *g, uint32_t n)                                                     \
    {                                                                                              \
        P##_set_min_tokens(g, n);                                                                  \
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
    static const za_ops_t tag##_ops = {api,        tag##_map,  tag##_map_state, tag##_min,         \
                                       tag##_kern, tag##_name, tag##_stats,     tag##_free};

#if defined(ZT_HAVE_METAL)
ZA_BACKEND(mt, "metal", zt_metal, zt_kern_metal, zt_metal_map) /* unified memory */
#endif
#if defined(ZA_DL)
ZA_BACKEND(cu, "cuda", zt_cuda, zt_kern_cuda, zt_cuda_map_shared)
ZA_BACKEND(hp, "hip", zt_hip, zt_kern_hip, zt_hip_map_shared)
ZA_BACKEND(vk, "vulkan", zt_vk, zt_kern_vk, zt_vk_map_shared)
#endif

static zt_accel_t *wrap(const za_ops_t *ops, void *g)
{
    zt_accel_t *a = calloc(1, sizeof *a);
    if (!a) {
        ops->free(g);
        return NULL;
    }
    a->ops = ops;
    a->g = g;
    return a;
}

/* "api: why" after what err already holds */
static void add_why(char *err, uint32_t cap, const char *api, const char *why)
{
    if (!err || !cap) return;
    size_t l = strlen(err);
    if (l + 1 < cap) snprintf(err + l, cap - l, "%s%s: %s", l ? "; " : "", api, why);
}

#if defined(ZA_DL)
static int dev_index(const char *env)
{
    const char *d = getenv(env);
    return d ? atoi(d) : 0;
}
#endif

zt_accel_t *zt_accel_open(char *err, uint32_t errcap)
{
    const char *want = getenv("ZT_GPU");
    char why[256];
    if (err && errcap) err[0] = 0;
    if (want && (!strcmp(want, "cpu") || !strcmp(want, "off") || !strcmp(want, "none"))) {
        add_why(err, errcap, "ZT_GPU", want);
        return NULL;
    }
    bool tried = false;
    (void) why;
#if defined(ZT_HAVE_METAL)
    if (!want || !strcmp(want, "metal")) {
        tried = true;
        why[0] = 0;
        void *g = zt_metal_new(why, sizeof why);
        if (g) return wrap(&mt_ops, g);
        add_why(err, errcap, "metal", why[0] ? why : "no Metal GPU");
    }
#endif
#if defined(ZA_DL)
    if (!want || !strcmp(want, "cuda")) {
        tried = true;
        why[0] = 0;
        void *g = zt_cuda_new(dev_index("ZT_CUDA_DEVICE"), why, sizeof why);
        if (g) return wrap(&cu_ops, g);
        add_why(err, errcap, "cuda", why);
    }
    if (!want || !strcmp(want, "hip")) {
        tried = true;
        why[0] = 0;
        void *g = zt_hip_new(dev_index("ZT_HIP_DEVICE"), why, sizeof why);
        if (g) return wrap(&hp_ops, g);
        add_why(err, errcap, "hip", why);
    }
    if (!want || !strcmp(want, "vulkan")) {
        tried = true;
        why[0] = 0;
        void *g = zt_vk_new(-1, why, sizeof why); /* ZT_VK_DEVICE picks the GPU */
        if (g) return wrap(&vk_ops, g);
        add_why(err, errcap, "vulkan", why);
    }
#endif
    if (!tried) {
        if (want)
            add_why(err, errcap, "ZT_GPU", "that backend is not built in");
        else
            add_why(err, errcap, "gpu", "this build has no GPU backend");
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
    if (a) a->ops->set_min_tokens(a->g, n);
}

void zt_accel_kern(zt_accel_t *a, zt_kern_t *out, const zt_kern_t *base, bool resident)
{
    if (!a) {
        *out = *base;
        return;
    }
    a->ops->kern(out, base, a->g, resident);
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
