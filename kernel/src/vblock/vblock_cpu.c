/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vblock_cpu.c — the CPU reference backend: host and pinned arenas on
 * device 0, memcpy copies, every fence complete on return. It is the
 * behaviour every accelerated backend must match byte for byte. */
#include "vblock.h"

void vb_cpu_init(vb_cpu_t *c)
{
    uint8_t *p = (uint8_t *) c;
    for (uint32_t i = 0; i < sizeof(*c); i++) p[i] = 0;
}

int vb_cpu_add_arena(vb_cpu_t *c, uint32_t tier, uint8_t *mem, uint64_t cap)
{
    if (!c || !mem || !cap || c->n >= VB_CPU_ARENAS ||
        (tier != VB_TIER_HOST && tier != VB_TIER_PINNED))
        return VB_ERR_ARG;
    c->mem[c->n] = mem;
    c->cap[c->n] = cap;
    c->tier[c->n] = (uint8_t) tier;
    c->taken[c->n] = 0;
    return (int) c->n++;
}

static int cpu_alloc(void *ctx, uint32_t dev, uint32_t tier, uint64_t bytes, uint32_t *pool)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    if (dev != 0) return VB_ERR_UNSUPP;
    for (uint32_t i = 0; i < c->n; i++)
        if (!c->taken[i] && c->tier[i] == tier && c->cap[i] >= bytes) {
            c->taken[i] = 1;
            *pool = i;
            return VB_OK;
        }
    return VB_ERR_NOMEM;
}

static void cpu_free(void *ctx, uint32_t pool)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    if (pool < c->n) c->taken[pool] = 0;
}

static int cpu_copy(void *ctx, const vb_xfer_t *x, vb_fence_t *done)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    if (x->src >= c->n || x->dst >= c->n || x->src_off + x->len > c->cap[x->src] ||
        x->dst_off + x->len > c->cap[x->dst])
        return VB_ERR_ARG;
    const uint8_t *s = c->mem[x->src] + x->src_off;
    uint8_t *d = c->mem[x->dst] + x->dst_off;
    for (uint64_t i = 0; i < x->len; i++) d[i] = s[i];
    *done = 0;
    return VB_OK;
}

static int cpu_fence_wait(void *ctx, vb_fence_t f)
{
    (void) ctx;
    (void) f;
    return VB_OK;
}

static bool cpu_fence_done(void *ctx, vb_fence_t f)
{
    (void) ctx;
    (void) f;
    return true;
}

static vb_fence_t cpu_fence_join(void *ctx, vb_fence_t a, vb_fence_t b)
{
    (void) ctx;
    (void) a;
    (void) b;
    return 0;
}

static uint8_t *cpu_map(void *ctx, uint32_t pool)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    return pool < c->n ? c->mem[pool] : 0;
}

static int cpu_link(void *ctx, uint32_t a, uint32_t b, vb_link_t *out)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    if (a >= c->n || b >= c->n) return VB_ERR_NOROUTE;
    out->mbps = 10000; /* nominal memcpy rate; only used to rank routes */
    out->lat_ns = 100;
    return VB_OK;
}

static bool cpu_access(void *ctx, uint32_t pool, uint32_t dev)
{
    vb_cpu_t *c = (vb_cpu_t *) ctx;
    return dev == 0 && pool < c->n;
}

static uint64_t cpu_now(void *ctx)
{
    (void) ctx;
    return 0;
}

void vb_cpu_backend(vb_cpu_t *c, vb_backend_t *be)
{
    be->ctx = c;
    be->name = "cpu";
    be->alloc = cpu_alloc;
    be->free = cpu_free;
    be->copy_h2h = cpu_copy;
    be->copy_h2d = 0; /* there is no device */
    be->copy_d2h = 0;
    be->copy_d2d = 0;
    be->copy_peer = 0;
    be->hash = 0;
    be->fence_wait = cpu_fence_wait;
    be->fence_done = cpu_fence_done;
    be->fence_join = cpu_fence_join;
    be->map = cpu_map;
    be->link = cpu_link;
    be->access = cpu_access;
    be->rdma_export = 0;
    be->now_ns = cpu_now;
}
