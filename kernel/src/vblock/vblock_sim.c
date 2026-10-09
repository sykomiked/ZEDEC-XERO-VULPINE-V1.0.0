/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vblock_sim.c — a simulated GPU backend for tests.
 *
 * Device memory is caller RAM and copies really move the bytes, so every
 * integrity check runs on real data. Time is a model: each transfer occupies
 * a LANE (one PCIe direction, one NVLink direction, the host memcpy engine)
 * from max(now, its wait fence, the lane's previous transfer) for
 * latency + bytes / bandwidth. Compute and device hashing have lanes of their
 * own, so transfers and kernels overlap exactly when the fences allow it.
 * A fence is the completion time in nanoseconds.
 *
 * Hazards: every read and write is remembered until it has finished. Two
 * accesses to the same bytes, at least one of them a write, whose time
 * intervals overlap are a hazard (a missing fence). Tests require zero.
 *
 * The bandwidths and latencies are model parameters set by the test or the
 * host; they are not measurements of any particular machine. */
#include "vblock.h"
#include "../robin_debanks/sha256.h"

void vb_sim_init(vb_sim_t *m)
{
    uint8_t *p = (uint8_t *) m;
    for (uint64_t i = 0; i < sizeof(*m); i++) p[i] = 0;
}

void vb_sim_dev(vb_sim_t *m, uint32_t dev, uint32_t kind)
{
    if (dev < VB_MAX_DEVS) m->devkind[dev] = (uint8_t) kind;
}

int vb_sim_region(vb_sim_t *m, uint32_t dev, uint32_t tier, uint8_t *mem, uint64_t cap)
{
    if (!m || !mem || !cap || dev >= VB_MAX_DEVS || tier >= VB_TIERS || m->n >= VB_MAX_POOLS)
        return VB_ERR_ARG;
    uint32_t i = m->n++;
    m->mem[i] = mem;
    m->cap[i] = cap;
    m->dev[i] = (uint8_t) dev;
    m->tier[i] = (uint8_t) tier;
    m->acc[i][dev] = 1; /* a device reads its own memory in place */
    return (int) i;
}

void vb_sim_link(vb_sim_t *m, uint32_t a, uint32_t b, uint32_t mbps, uint32_t lat_ns, uint32_t lane)
{
    if (a >= m->n || b >= m->n || lane >= VB_SIM_LANE_COMP) return;
    m->lok[a][b] = 1;
    m->lnk[a][b].mbps = mbps;
    m->lnk[a][b].lat_ns = lat_ns;
    m->lane[a][b] = (uint8_t) lane;
}

void vb_sim_access(vb_sim_t *m, uint32_t region, uint32_t dev, bool yes)
{
    if (region < m->n && dev < VB_MAX_DEVS) m->acc[region][dev] = yes;
}

void vb_sim_rdma(vb_sim_t *m, uint32_t region, bool yes)
{
    if (region < m->n) m->rdma[region] = yes;
}

void vb_sim_hash_rate(vb_sim_t *m, uint32_t dev, uint32_t mbps, uint32_t lat_ns)
{
    if (dev < VB_MAX_DEVS) {
        m->hash_rate[dev].mbps = mbps;
        m->hash_rate[dev].lat_ns = lat_ns;
    }
}

/* ---- the clock, lanes and hazard log ---- */

static uint64_t maxu(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

static void ev_copy(vb_sim_ev_t *d, const vb_sim_ev_t *s)
{
    d->off = s->off;
    d->len = s->len;
    d->t0 = s->t0;
    d->t1 = s->t1;
    d->pool = s->pool;
    d->write = s->write;
}

static void ev_prune(vb_sim_t *m)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < m->nev; i++) {
        if (m->ev[i].t1 <= m->now) continue;
        if (w != i) ev_copy(&m->ev[w], &m->ev[i]);
        w++;
    }
    m->nev = w;
}

static void ev_access(vb_sim_t *m, uint32_t pool, uint64_t off, uint64_t len, uint64_t t0,
                      uint64_t t1, bool write)
{
    for (uint32_t i = 0; i < m->nev; i++) {
        const vb_sim_ev_t *e = &m->ev[i];
        if (e->pool != pool || !(write || e->write)) continue;
        if (e->off < off + len && off < e->off + e->len && e->t0 < t1 && t0 < e->t1) m->hazards++;
    }
    if (m->nev == VB_SIM_TRACK) ev_prune(m);
    if (m->nev == VB_SIM_TRACK) { /* drop the oldest */
        for (uint32_t i = 1; i < m->nev; i++) ev_copy(&m->ev[i - 1], &m->ev[i]);
        m->nev--;
    }
    vb_sim_ev_t *e = &m->ev[m->nev++];
    e->pool = (uint8_t) pool;
    e->off = off;
    e->len = len;
    e->t0 = t0;
    e->t1 = t1;
    e->write = write;
}

static uint64_t lane_run(vb_sim_t *m, uint32_t lane, vb_fence_t wait, uint64_t dur, uint64_t *start)
{
    uint64_t t0 = maxu(maxu(m->now, wait), m->lane_free[lane]);
    uint64_t t1 = t0 + (dur ? dur : 1u);
    m->lane_free[lane] = t1;
    m->lane_busy[lane] += t1 - t0;
    *start = t0;
    return t1;
}

static uint64_t ns_for(vb_link_t l, uint64_t len)
{
    return vb_link_ns(l, len);
}

/* ---- backend ops ---- */

static int sim_alloc(void *ctx, uint32_t dev, uint32_t tier, uint64_t bytes, uint32_t *pool)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    for (uint32_t i = 0; i < m->n; i++)
        if (!m->taken[i] && m->dev[i] == dev && m->tier[i] == tier && m->cap[i] >= bytes) {
            m->taken[i] = 1;
            *pool = i;
            return VB_OK;
        }
    return VB_ERR_NOMEM;
}

static void sim_free(void *ctx, uint32_t pool)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (pool < m->n) m->taken[pool] = 0;
}

static int sim_copy(void *ctx, const vb_xfer_t *x, vb_fence_t *done)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (x->src >= m->n || x->dst >= m->n || !m->lok[x->src][x->dst] ||
        x->src_off + x->len > m->cap[x->src] || x->dst_off + x->len > m->cap[x->dst])
        return VB_ERR_ARG;
    uint64_t t0;
    uint64_t t1 =
        lane_run(m, m->lane[x->src][x->dst], x->wait, ns_for(m->lnk[x->src][x->dst], x->len), &t0);
    ev_access(m, x->src, x->src_off, x->len, t0, t1, false);
    ev_access(m, x->dst, x->dst_off, x->len, t0, t1, true);
    const uint8_t *s = m->mem[x->src] + x->src_off;
    uint8_t *d = m->mem[x->dst] + x->dst_off;
    for (uint64_t i = 0; i < x->len; i++) d[i] = s[i];
    m->copies++;
    *done = t1;
    return VB_OK;
}

static int sim_hash(void *ctx, uint32_t pool, uint64_t off, uint64_t len, vb_fence_t wait,
                    uint8_t digest[32], vb_fence_t *done)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (pool >= m->n || off + len > m->cap[pool]) return VB_ERR_ARG;
    uint32_t dev = m->dev[pool];
    if (m->hash_rate[dev].mbps == 0) return VB_ERR_UNSUPP;
    uint64_t t0;
    uint64_t t1 = lane_run(m, VB_SIM_LANE_HASH + dev, wait, ns_for(m->hash_rate[dev], len), &t0);
    ev_access(m, pool, off, len, t0, t1, false);
    sha256(m->mem[pool] + off, (size_t) len, digest);
    *done = t1;
    return VB_OK;
}

static int sim_fence_wait(void *ctx, vb_fence_t f)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (f > m->now) m->now = f;
    return VB_OK;
}

static bool sim_fence_done(void *ctx, vb_fence_t f)
{
    return f <= ((vb_sim_t *) ctx)->now;
}

static vb_fence_t sim_fence_join(void *ctx, vb_fence_t a, vb_fence_t b)
{
    (void) ctx;
    return maxu(a, b);
}

static uint8_t *sim_map(void *ctx, uint32_t pool)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (pool >= m->n || m->tier[pool] == VB_TIER_VRAM) return 0;
    return m->mem[pool];
}

static int sim_link(void *ctx, uint32_t a, uint32_t b, vb_link_t *out)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (a >= m->n || b >= m->n || !m->lok[a][b]) return VB_ERR_NOROUTE;
    out->mbps = m->lnk[a][b].mbps;
    out->lat_ns = m->lnk[a][b].lat_ns;
    return VB_OK;
}

static bool sim_access(void *ctx, uint32_t pool, uint32_t dev)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    return pool < m->n && dev < VB_MAX_DEVS && m->acc[pool][dev];
}

static int sim_rdma_export(void *ctx, uint32_t pool, uint64_t off, uint64_t len, vb_rdma_t *out)
{
    vb_sim_t *m = (vb_sim_t *) ctx;
    if (pool >= m->n || !m->rdma[pool] || off + len > m->cap[pool]) return VB_ERR_UNSUPP;
    out->addr = ((uint64_t) (pool + 1u) << 40) + off; /* a made-up IOVA */
    out->key = 0x5A580000u + pool;
    out->pool = pool;
    out->off = off;
    out->len = len;
    return VB_OK;
}

static uint64_t sim_now(void *ctx)
{
    return ((vb_sim_t *) ctx)->now;
}

void vb_sim_backend(vb_sim_t *m, vb_backend_t *be)
{
    be->ctx = m;
    be->name = "sim";
    be->alloc = sim_alloc;
    be->free = sim_free;
    be->copy_h2h = sim_copy;
    be->copy_h2d = sim_copy;
    be->copy_d2h = sim_copy;
    be->copy_d2d = sim_copy;
    be->copy_peer = sim_copy;
    be->hash = sim_hash;
    be->fence_wait = sim_fence_wait;
    be->fence_done = sim_fence_done;
    be->fence_join = sim_fence_join;
    be->map = sim_map;
    be->link = sim_link;
    be->access = sim_access;
    be->rdma_export = sim_rdma_export;
    be->now_ns = sim_now;
}

int vb_sim_compute(vb_sim_t *m, uint32_t dev, vb_fence_t wait, uint64_t cost_ns,
                   const vb_ref_t *refs, uint32_t nrefs, vb_fence_t *done)
{
    if (!m || dev >= VB_MAX_DEVS || (!refs && nrefs) || !done) return VB_ERR_ARG;
    uint64_t t0;
    uint64_t t1 = lane_run(m, VB_SIM_LANE_COMP + dev, wait, cost_ns, &t0);
    for (uint32_t i = 0; i < nrefs; i++)
        ev_access(m, refs[i].backend_pool, refs[i].off, refs[i].len, t0, t1, false);
    *done = t1;
    return VB_OK;
}

vb_fence_t vb_sim_nic(vb_sim_t *m, uint32_t region, uint64_t off, uint64_t len, bool write,
                      uint32_t mbps, uint32_t lat_ns, uint32_t lane)
{
    vb_link_t l;
    l.mbps = mbps;
    l.lat_ns = lat_ns;
    uint64_t t0;
    uint64_t t1 = lane_run(m, lane < VB_SIM_LANE_COMP ? lane : 0u, 0, ns_for(l, len), &t0);
    if (region < m->n) ev_access(m, region, off, len, t0, t1, write);
    return t1;
}
