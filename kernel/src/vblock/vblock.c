/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vblock.c — the device-memory block store: index, pools, routing, dedup,
 * zero-copy, batching, eviction, pipeline, placement, network landing and
 * serving. See vblock.h for the rules V1-V9. */
#include "vblock.h"
#include "../robin_debanks/sha256.h"
#include "../tensor/zt.h"

#define RAW_OWNER 0xFFFFFFFFu /* a range not (yet) owned by a block copy */
#define INF_NS    0xFFFFFFFFFFFFFFFFull

/* ---- small helpers (no libc) --------------------------------------------- */

static void vcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static void vset(void *dst, uint8_t v, uint64_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint64_t i = 0; i < n; i++) d[i] = v;
}

static bool veq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static uint64_t align_up(uint64_t x)
{
    return (x + (VB_ALIGN - 1u)) & ~(uint64_t) (VB_ALIGN - 1u);
}

static bool host_side(uint32_t tier)
{
    return tier == VB_TIER_HOST || tier == VB_TIER_PINNED;
}

static uint32_t cid_hash(const ipfsn_cid_t *c)
{
    uint32_t h = 2166136261u;
    h = (h ^ (c->codec & 0xFFu)) * 16777619u;
    h = (h ^ (c->mh_code & 0xFFu)) * 16777619u;
    for (uint32_t i = 0; i < c->digest_len; i++) h = (h ^ c->digest[i]) * 16777619u;
    return h;
}

uint64_t vb_link_ns(vb_link_t l, uint64_t len)
{
    if (l.mbps == 0) return l.lat_ns;
    return (uint64_t) l.lat_ns + zt_udiv64(len * 1000u, l.mbps, 0);
}

/* ---- fences --------------------------------------------------------------- */

static bool f_done(vb_store_t *s, vb_fence_t f)
{
    if (f == 0) return true;
    return s->be->fence_done ? s->be->fence_done(s->be->ctx, f) : false;
}

static void f_wait(vb_store_t *s, vb_fence_t f)
{
    if (f == 0 || f_done(s, f)) return;
    s->st.host_waits++;
    if (s->be->fence_wait) s->be->fence_wait(s->be->ctx, f);
}

static vb_fence_t f_join(vb_store_t *s, vb_fence_t a, vb_fence_t b)
{
    if (a == 0 || f_done(s, a)) return b;
    if (b == 0 || f_done(s, b)) return a;
    if (s->be->fence_join) return s->be->fence_join(s->be->ctx, a, b);
    f_wait(s, a); /* no join: order on the host */
    return b;
}

/* ---- index ---------------------------------------------------------------- */

static void entry_clear(vb_entry_t *e)
{
    vset(e, 0, sizeof(*e));
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) e->c[i].pool = VB_NONE;
}

/* Returns the entry index, or -1 (not found / table full when creating). */
static int find(vb_store_t *s, const ipfsn_cid_t *cid_in, bool create, uint32_t len)
{
    ipfsn_cid_t v1;
    ipfsn_cid_to_v1(cid_in, &v1);
    uint32_t mask = s->cap - 1u, h = cid_hash(&v1) & mask;
    int tomb = -1;
    for (uint32_t i = 0; i < s->cap; i++) {
        uint32_t k = (h + i) & mask;
        vb_entry_t *e = &s->tab[k];
        if (e->state == 0) {
            if (tomb < 0) tomb = (int) k;
            break;
        }
        if (e->state == 2) {
            if (tomb < 0) tomb = (int) k;
            continue;
        }
        if (ipfsn_cid_equal(&e->cid, &v1)) return (int) k;
    }
    if (!create || tomb < 0 || s->count + 1u > s->cap - s->cap / 8u) return -1;
    vb_entry_t *e = &s->tab[tomb];
    entry_clear(e);
    vcpy(&e->cid, &v1, sizeof(v1));
    e->len = len;
    e->state = 1;
    s->count++;
    return tomb;
}

static int slot_new(const vb_entry_t *e)
{
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
        if (e->c[i].pool == VB_NONE) return (int) i;
    return -1;
}

static bool usable(const vb_copy_t *c)
{
    return c->pool != VB_NONE && (c->flags & VB_C_VERIFIED);
}

static uint32_t ncopies(const vb_entry_t *e)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) n += e->c[i].pool != VB_NONE;
    return n;
}

/* ---- pool allocator -------------------------------------------------------- */

static void retire_prune(vb_store_t *s, vb_pool_t *p)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < p->nret; i++) {
        if (f_done(s, p->ret[i].fence)) continue;
        if (w != i) vcpy(&p->ret[w], &p->ret[i], sizeof(p->ret[i]));
        w++;
    }
    p->nret = w;
}

static void retire_add(vb_store_t *s, vb_pool_t *p, uint64_t off, uint64_t len, vb_fence_t f)
{
    if (f_done(s, f)) return;
    retire_prune(s, p);
    if (p->nret == VB_RETIRED) {
        f_wait(s, p->ret[0].fence);
        retire_prune(s, p);
        if (p->nret == VB_RETIRED) p->nret--; /* backend without polling: dropped after wait */
    }
    p->ret[p->nret].off = off;
    p->ret[p->nret].len = len;
    p->ret[p->nret].fence = f;
    p->nret++;
}

static vb_fence_t retire_wait(vb_store_t *s, vb_pool_t *p, uint64_t off, uint64_t len)
{
    vb_fence_t w = 0;
    for (uint32_t i = 0; i < p->nret; i++) {
        vb_retired_t *r = &p->ret[i];
        if (r->off < off + len && off < r->off + r->len) w = f_join(s, w, r->fence);
    }
    return w;
}

static bool gap_find(const vb_pool_t *p, uint64_t len, uint64_t *off)
{
    if (p->nr >= p->rcap) return false;
    uint64_t prev = 0;
    for (uint32_t i = 0; i < p->nr; i++) {
        if (p->r[i].off - prev >= len) {
            *off = prev;
            return true;
        }
        prev = p->r[i].off + p->r[i].len;
    }
    if (p->cap - prev >= len) {
        *off = prev;
        return true;
    }
    return false;
}

static int range_add(vb_pool_t *p, uint64_t off, uint64_t len, uint32_t entry, uint32_t slot)
{
    if (p->nr >= p->rcap) return VB_ERR_FULL;
    uint32_t i = p->nr;
    while (i > 0 && p->r[i - 1].off > off) {
        vcpy(&p->r[i], &p->r[i - 1], sizeof(p->r[i]));
        i--;
    }
    p->r[i].off = off;
    p->r[i].len = len;
    p->r[i].entry = entry;
    p->r[i].slot = (uint8_t) slot;
    p->nr++;
    p->used += len;
    return VB_OK;
}

static vb_range_t *range_at(vb_pool_t *p, uint64_t off)
{
    for (uint32_t i = 0; i < p->nr; i++)
        if (p->r[i].off == off) return &p->r[i];
    return 0;
}

static void range_del(vb_store_t *s, vb_pool_t *p, uint64_t off, vb_fence_t f)
{
    for (uint32_t i = 0; i < p->nr; i++) {
        if (p->r[i].off != off) continue;
        uint64_t len = p->r[i].len;
        for (uint32_t j = i; j + 1u < p->nr; j++) vcpy(&p->r[j], &p->r[j + 1u], sizeof(p->r[j]));
        p->nr--;
        p->used -= len;
        retire_add(s, p, off, len, f);
        return;
    }
}

static void copy_free(vb_store_t *s, uint32_t ei, uint32_t slot)
{
    vb_entry_t *e = &s->tab[ei];
    vb_copy_t *c = &e->c[slot];
    if (c->pool == VB_NONE) return;
    range_del(s, &s->pool[c->pool], c->off, f_join(s, c->busy, c->ready));
    vset(c, 0, sizeof(*c));
    c->pool = VB_NONE;
    if (ncopies(e) == 0) {
        e->state = 2;
        s->count--;
    }
}

/* ---- routing (V3) ----------------------------------------------------------- */

static uint32_t xkind(const vb_store_t *s, uint32_t a, uint32_t b)
{
    const vb_pool_t *pa = &s->pool[a], *pb = &s->pool[b];
    bool ha = host_side(pa->tier), hb = host_side(pb->tier);
    if (ha && hb) return VB_K_H2H;
    if (ha) return VB_K_H2D;
    if (hb) return VB_K_D2H;
    return pa->dev == pb->dev ? VB_K_D2D : VB_K_PEER;
}

static bool edge_ok(const vb_store_t *s, uint32_t a, uint32_t b)
{
    if (!s->lok[a][b]) return false;
    if (!(s->opts & VB_OPT_P2P) && xkind(s, a, b) == VB_K_PEER) return false;
    return true;
}

int vb_route(const vb_store_t *s, uint32_t a, uint32_t b, uint64_t len, uint8_t *hops,
             uint32_t *nhops, uint64_t *cost_ns)
{
    if (!s || a >= s->npools || b >= s->npools) return VB_ERR_ARG;
    uint64_t dist[VB_MAX_POOLS];
    uint8_t prev[VB_MAX_POOLS], done[VB_MAX_POOLS];
    for (uint32_t i = 0; i < s->npools; i++) {
        dist[i] = INF_NS;
        prev[i] = VB_NONE;
        done[i] = 0;
    }
    dist[a] = 0;
    for (uint32_t it = 0; it < s->npools; it++) {
        uint32_t u = VB_NONE;
        for (uint32_t i = 0; i < s->npools; i++)
            if (!done[i] && dist[i] != INF_NS && (u == VB_NONE || dist[i] < dist[u])) u = i;
        if (u == VB_NONE) break;
        done[u] = 1;
        for (uint32_t v = 0; v < s->npools; v++) {
            if (done[v] || !edge_ok(s, u, v)) continue;
            uint64_t c = dist[u] + vb_link_ns(s->lnk[u][v], len);
            if (c < dist[v]) {
                dist[v] = c;
                prev[v] = (uint8_t) u;
            }
        }
    }
    if (dist[b] == INF_NS) return VB_ERR_NOROUTE;
    uint8_t rev[VB_MAX_POOLS];
    uint32_t n = 0;
    for (uint32_t v = b; v != VB_NONE && n < VB_MAX_POOLS; v = prev[v]) {
        rev[n++] = (uint8_t) v;
        if (v == a) break;
    }
    if (hops)
        for (uint32_t i = 0; i < n; i++) hops[i] = rev[n - 1u - i];
    if (nhops) *nhops = n;
    if (cost_ns) *cost_ns = dist[b];
    return VB_OK;
}

static uint64_t route_ns(const vb_store_t *s, uint32_t a, uint32_t b, uint64_t len)
{
    uint64_t c;
    if (a == b) return 0;
    return vb_route(s, a, b, len, 0, 0, &c) == VB_OK ? c : INF_NS;
}

/* Time to bring e back into pool `pi` without copy `skip` (eviction cost). */
static uint64_t refetch_ns(const vb_store_t *s, const vb_entry_t *e, uint32_t skip, uint32_t pi)
{
    uint64_t best = INF_NS;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        if (i == skip || !usable(&e->c[i])) continue;
        uint64_t c = route_ns(s, e->c[i].pool, pi, e->len);
        if (c < best) best = c;
    }
    if (best == INF_NS && s->backing && s->stage != VB_NONE) {
        uint64_t c = route_ns(s, s->stage, pi, e->len);
        if (c != INF_NS) best = vb_link_ns(s->backing_cost, e->len) + c;
    }
    return best;
}

/* ---- eviction (V6) ---------------------------------------------------------- */

static void touch(vb_store_t *s, uint32_t ei, uint32_t slot)
{
    vb_entry_t *e = &s->tab[ei];
    vb_copy_t *c = &e->c[slot];
    s->tick++;
    if (!(s->opts & VB_OPT_COSTEVICT)) {
        c->prio = s->tick;
        return;
    }
    uint64_t cost = refetch_ns(s, e, slot, c->pool);
    if (cost == INF_NS) cost = 0xFFFFFFFFull; /* last copy: very expensive */
    /* GreedyDual-Size: H = L + cost / size, in ns per KiB. */
    uint64_t per = zt_udiv64(cost << 10, e->len ? e->len : 1u, 0);
    c->prio = s->pool[c->pool].L + per;
}

static bool evictable(const vb_store_t *s, const vb_entry_t *e, uint32_t slot)
{
    const vb_copy_t *c = &e->c[slot];
    if (c->refs || (c->flags & VB_C_PINNED)) return false;
    if (s->backing) return true;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
        if (i != slot && usable(&e->c[i])) return true;
    return false;
}

static bool evict_one(vb_store_t *s, uint32_t pi)
{
    vb_pool_t *p = &s->pool[pi];
    uint32_t vi = RAW_OWNER, vs = 0;
    uint64_t vp = 0;
    for (uint32_t i = 0; i < p->nr; i++) {
        uint32_t ei = p->r[i].entry;
        if (ei == RAW_OWNER) continue;
        vb_entry_t *e = &s->tab[ei];
        uint32_t sl = p->r[i].slot;
        if (!evictable(s, e, sl)) continue;
        if (vi == RAW_OWNER || e->c[sl].prio < vp) {
            vi = ei;
            vs = sl;
            vp = e->c[sl].prio;
        }
    }
    if (vi == RAW_OWNER) return false;
    if (s->opts & VB_OPT_COSTEVICT) p->L = vp;
    s->st.evictions++;
    s->st.evicted_bytes += s->tab[vi].len;
    copy_free(s, vi, vs);
    return true;
}

/* Find room for len bytes; *wait = fence of whatever last used the range. */
static int pool_alloc(vb_store_t *s, uint32_t pi, uint64_t len, uint64_t *off, vb_fence_t *wait)
{
    vb_pool_t *p = &s->pool[pi];
    uint64_t a = align_up(len ? len : 1u);
    if (a > p->cap) return VB_ERR_NOMEM;
    retire_prune(s, p);
    while (!gap_find(p, a, off))
        if (!evict_one(s, pi)) return p->nr >= p->rcap ? VB_ERR_FULL : VB_ERR_NOMEM;
    *wait = retire_wait(s, p, *off, a);
    return VB_OK;
}

/* ---- transfers ---------------------------------------------------------------- */

static int xfer(vb_store_t *s, uint32_t a, uint64_t aoff, uint32_t b, uint64_t boff, uint64_t len,
                vb_fence_t wait, vb_fence_t *done)
{
    const vb_backend_t *be = s->be;
    uint32_t k = xkind(s, a, b);
    vb_copy_fn fn = k == VB_K_H2H   ? be->copy_h2h
                    : k == VB_K_H2D ? be->copy_h2d
                    : k == VB_K_D2H ? be->copy_d2h
                    : k == VB_K_D2D ? be->copy_d2d
                                    : be->copy_peer;
    if (!fn) return VB_ERR_UNSUPP;
    vb_xfer_t x;
    x.src = s->pool[a].h;
    x.dst = s->pool[b].h;
    x.src_off = aoff;
    x.dst_off = boff;
    x.len = len;
    x.wait = wait;
    *done = 0;
    if (fn(be->ctx, &x, done) != 0) return VB_ERR_BACKEND;
    s->st.bytes[k] += len;
    s->st.xfers[k]++;
    if (!(s->opts & VB_OPT_ASYNC)) f_wait(s, *done);
    return VB_OK;
}

/* Give a copy to entry ei in pool pi at off (range already reserved). */
static void copy_set(vb_store_t *s, uint32_t ei, uint32_t slot, uint32_t pi, uint64_t off,
                     uint32_t flags, vb_fence_t ready)
{
    vb_copy_t *c = &s->tab[ei].c[slot];
    vset(c, 0, sizeof(*c));
    c->pool = (uint8_t) pi;
    c->off = off;
    c->flags = (uint8_t) flags;
    c->ready = ready;
    touch(s, ei, slot);
}

/* Move copy `src` of entry ei to pool dst along the cheapest route. Hops
 * become resident copies (staged once); with temp they are freed after use. */
static int move_block(vb_store_t *s, uint32_t ei, uint32_t src, uint32_t dst, bool temp,
                      uint32_t *out)
{
    vb_entry_t *e = &s->tab[ei];
    uint8_t hops[VB_MAX_POOLS];
    uint32_t n = 0;
    int rc = vb_route(s, e->c[src].pool, dst, e->len, hops, &n, 0);
    if (rc != VB_OK) return rc;
    if (n > 2) s->st.multi_hop++;
    uint32_t cur = src;
    e->c[cur].refs++;
    for (uint32_t h = 1; h < n; h++) {
        uint32_t pi = hops[h];
        int sl = -1;
        if (!temp && h + 1u < n)
            for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
                if (usable(&e->c[i]) && e->c[i].pool == pi) sl = (int) i;
        if (sl < 0) {
            sl = slot_new(e);
            uint64_t off = 0;
            vb_fence_t w = 0, done = 0;
            if (sl < 0) rc = VB_ERR_FULL;
            if (rc == VB_OK) rc = pool_alloc(s, pi, e->len, &off, &w);
            if (rc == VB_OK) rc = range_add(&s->pool[pi], off, align_up(e->len), ei, (uint32_t) sl);
            if (rc != VB_OK) {
                e->c[cur].refs--;
                return rc;
            }
            copy_set(s, ei, (uint32_t) sl, pi, off, VB_C_VERIFIED | (temp ? VB_C_TEMP : 0u), 0);
            e->c[sl].refs++; /* protect the new copy while it is being written */
            rc = xfer(s, e->c[cur].pool, e->c[cur].off, pi, off, e->len,
                      f_join(s, e->c[cur].ready, w), &done);
            e->c[sl].refs--;
            if (rc != VB_OK) {
                copy_free(s, ei, (uint32_t) sl);
                e->c[cur].refs--;
                return rc;
            }
            e->c[sl].ready = done;
            e->c[cur].busy = f_join(s, e->c[cur].busy, done);
        }
        e->c[cur].refs--;
        if (cur != src && (e->c[cur].flags & VB_C_TEMP) && e->c[cur].refs == 0)
            copy_free(s, ei, cur);
        cur = (uint32_t) sl;
        e->c[cur].refs++;
    }
    e->c[cur].refs--;
    *out = cur;
    return VB_OK;
}

/* ---- init -------------------------------------------------------------------- */

int vb_init(vb_store_t *s, const vb_cfg_t *cfg)
{
    if (!s || !cfg || !cfg->be || !cfg->table || cfg->table_cap < 2u ||
        (cfg->table_cap & (cfg->table_cap - 1u)) || !cfg->pools || cfg->npools == 0 ||
        cfg->npools > VB_MAX_POOLS || !cfg->be->alloc || !cfg->be->link)
        return VB_ERR_ARG;
    vset(s, 0, sizeof(*s));
    s->be = cfg->be;
    s->tab = cfg->table;
    s->cap = cfg->table_cap;
    for (uint32_t i = 0; i < s->cap; i++) entry_clear(&s->tab[i]);
    s->opts = cfg->opts;
    s->batch_max_block = cfg->batch_max_block ? cfg->batch_max_block : 65536u;
    s->extent_bytes = cfg->extent_bytes ? cfg->extent_bytes : (4ull << 20);
    s->scratch = cfg->scratch;
    s->scratch_cap = cfg->scratch_cap;
    s->stage = VB_NONE;
    for (uint32_t d = 0; d < VB_MAX_DEVS; d++) s->home[d] = VB_NONE;
    for (uint32_t i = 0; i < cfg->npools; i++) {
        const vb_pool_cfg_t *pc = &cfg->pools[i];
        vb_pool_t *p = &s->pool[i];
        if (pc->dev >= VB_MAX_DEVS || pc->tier >= VB_TIERS || !pc->ranges || !pc->nranges ||
            !pc->bytes)
            goto fail;
        if (s->be->alloc(s->be->ctx, pc->dev, pc->tier, pc->bytes, &p->h) != 0) goto fail;
        s->npools = i + 1u;
        p->dev = (uint8_t) pc->dev;
        p->tier = (uint8_t) pc->tier;
        p->cap = pc->bytes;
        p->r = pc->ranges;
        p->rcap = pc->nranges;
        p->cpu = s->be->map ? s->be->map(s->be->ctx, p->h) : 0;
        if (pc->dev + 1u > s->ndevs) s->ndevs = (uint8_t) (pc->dev + 1u);
        if (pc->tier == VB_TIER_PINNED && p->cpu && s->stage == VB_NONE) s->stage = (uint8_t) i;
    }
    /* A device's working copies go to its fastest local memory. */
    static const uint8_t order[VB_TIERS] = {VB_TIER_VRAM, VB_TIER_UNIFIED, VB_TIER_PINNED,
                                            VB_TIER_HOST};
    for (uint32_t d = 0; d < s->ndevs; d++)
        for (uint32_t o = 0; o < VB_TIERS && s->home[d] == VB_NONE; o++) {
            int pi = vb_pool_index(s, d, order[o]);
            if (pi >= 0) s->home[d] = (uint8_t) pi;
        }
    for (uint32_t a = 0; a < s->npools; a++) {
        for (uint32_t b = 0; b < s->npools; b++) {
            vb_link_t l;
            l.mbps = 0;
            l.lat_ns = 0;
            if (a != b && s->be->link(s->be->ctx, s->pool[a].h, s->pool[b].h, &l) == 0) {
                s->lok[a][b] = 1;
                s->lnk[a][b] = l;
            }
        }
        for (uint32_t d = 0; d < s->ndevs; d++)
            s->acc[a][d] =
                s->be->access ? (uint8_t) s->be->access(s->be->ctx, s->pool[a].h, d) : 0u;
    }
    return VB_OK;
fail:
    vb_fini(s);
    return VB_ERR_BACKEND;
}

void vb_fini(vb_store_t *s)
{
    if (!s || !s->be) return;
    for (uint32_t i = 0; i < s->npools; i++)
        if (s->be->free) s->be->free(s->be->ctx, s->pool[i].h);
    s->npools = 0;
}

int vb_pool_index(const vb_store_t *s, uint32_t dev, uint32_t tier)
{
    for (uint32_t i = 0; i < s->npools; i++)
        if (s->pool[i].dev == dev && s->pool[i].tier == tier) return (int) i;
    return -1;
}

void vb_set_backing(vb_store_t *s, ipfsn_get_fn get, void *ctx, vb_link_t cost)
{
    s->backing = get;
    s->backing_ctx = ctx;
    s->backing_cost = cost;
}

/* ---- put / has ------------------------------------------------------------------ */

int vb_put(vb_store_t *s, const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len, uint32_t pool,
           uint32_t flags)
{
    if (!s || !cid || (!data && len) || pool >= s->npools || !s->pool[pool].cpu) return VB_ERR_ARG;
    if (ipfsn_cid_verify(cid, data, len) != IPFSN_OK) {
        s->st.rejected++;
        return VB_ERR_HASH;
    }
    s->st.hashed_cpu_bytes += len;
    int ei = find(s, cid, true, len);
    if (ei < 0) return VB_ERR_FULL;
    vb_entry_t *e = &s->tab[ei];
    if (e->len != len) return VB_ERR_ARG;
    e->flags |= (uint8_t) (flags & VB_F_PRIVATE);
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
        if (usable(&e->c[i]) && e->c[i].pool == pool) {
            s->st.dedup_hits++;
            return VB_OK;
        }
    int sl = slot_new(e);
    uint64_t off = 0;
    vb_fence_t w = 0;
    int rc = sl < 0 ? VB_ERR_FULL : pool_alloc(s, pool, len, &off, &w);
    if (rc == VB_OK)
        rc = range_add(&s->pool[pool], off, align_up(len), (uint32_t) ei, (uint32_t) sl);
    if (rc != VB_OK) {
        if (ncopies(e) == 0) {
            e->state = 2;
            s->count--;
        }
        return rc;
    }
    f_wait(s, w);
    vcpy(s->pool[pool].cpu + off, data, len);
    copy_set(s, (uint32_t) ei, (uint32_t) sl, pool, off, VB_C_VERIFIED, 0);
    return VB_OK;
}

bool vb_has(vb_store_t *s, const ipfsn_cid_t *cid)
{
    int ei = find(s, cid, false, 0);
    if (ei < 0) return false;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
        if (usable(&s->tab[ei].c[i])) return true;
    return false;
}

uint32_t vb_copies(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t pool, bool *in_pool)
{
    if (in_pool) *in_pool = false;
    int ei = find(s, cid, false, 0);
    if (ei < 0) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        const vb_copy_t *c = &s->tab[ei].c[i];
        if (!usable(c) || (c->flags & VB_C_TEMP)) continue;
        n++;
        if (in_pool && c->pool == pool) *in_pool = true;
    }
    return n;
}

/* ---- acquire ---------------------------------------------------------------------- */

static int fetch_backing(vb_store_t *s, const ipfsn_cid_t *cid)
{
    uint32_t len = 0;
    if (!s->backing || !s->scratch || s->stage == VB_NONE) return VB_ERR_NOTFOUND;
    if (s->backing(s->backing_ctx, cid, s->scratch, s->scratch_cap, &len) != 0)
        return VB_ERR_NOTFOUND;
    int rc = vb_put(s, cid, s->scratch, len, s->stage, 0);
    if (rc != VB_OK) return rc;
    s->st.backing_fetches++;
    return find(s, cid, false, 0);
}

static void ref_fill(vb_store_t *s, uint32_t ei, uint32_t slot, vb_ref_t *r)
{
    vb_copy_t *c = &s->tab[ei].c[slot];
    c->refs++;
    touch(s, ei, slot);
    r->entry = ei;
    r->slot = (uint8_t) slot;
    r->pool = c->pool;
    r->backend_pool = s->pool[c->pool].h;
    r->off = c->off;
    r->len = s->tab[ei].len;
    r->ready = c->ready;
}

/* Where can dev read e without a transfer? Returns a slot or -1. */
static int resident(vb_store_t *s, uint32_t ei, uint32_t dev)
{
    vb_entry_t *e = &s->tab[ei];
    uint32_t tp = s->home[dev];
    if (s->opts & VB_OPT_DEDUP)
        for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
            if (usable(&e->c[i]) && !(e->c[i].flags & VB_C_TEMP) && e->c[i].pool == tp) {
                s->st.dedup_hits++;
                return (int) i;
            }
    if (s->opts & VB_OPT_ZEROCOPY)
        for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
            if (usable(&e->c[i]) && !(e->c[i].flags & VB_C_TEMP) && s->acc[e->c[i].pool][dev]) {
                s->st.zero_copy_hits++;
                return (int) i;
            }
    return -1;
}

/* Cheapest source copy for pool tp, or -1. */
static int best_source(vb_store_t *s, uint32_t ei, uint32_t tp, uint64_t *cost)
{
    vb_entry_t *e = &s->tab[ei];
    int best = -1;
    uint64_t bc = INF_NS;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        if (!usable(&e->c[i]) || (e->c[i].flags & VB_C_TEMP)) continue;
        uint64_t c = route_ns(s, e->c[i].pool, tp, e->len);
        if (c < bc) {
            bc = c;
            best = (int) i;
        }
    }
    if (cost) *cost = bc;
    return bc == INF_NS ? -1 : best;
}

static int lookup_or_fetch(vb_store_t *s, const ipfsn_cid_t *cid)
{
    int ei = find(s, cid, false, 0);
    if (ei >= 0) {
        for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
            if (usable(&s->tab[ei].c[i])) return ei;
        if (ncopies(&s->tab[ei])) return VB_ERR_UNVERIFIED;
    }
    return fetch_backing(s, cid);
}

int vb_acquire(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev, vb_ref_t *ref)
{
    if (!s || !cid || !ref || dev >= s->ndevs || s->home[dev] == VB_NONE) return VB_ERR_ARG;
    ref->pool = VB_NONE;
    int ei = lookup_or_fetch(s, cid);
    if (ei < 0) return ei;
    int sl = resident(s, (uint32_t) ei, dev);
    if (sl < 0) {
        int src = best_source(s, (uint32_t) ei, s->home[dev], 0);
        if (src < 0) return VB_ERR_NOROUTE;
        uint32_t out = 0;
        int rc = move_block(s, (uint32_t) ei, (uint32_t) src, s->home[dev],
                            !(s->opts & VB_OPT_DEDUP), &out);
        if (rc != VB_OK) return rc;
        sl = (int) out;
    }
    ref_fill(s, (uint32_t) ei, (uint32_t) sl, ref);
    return VB_OK;
}

void vb_release(vb_store_t *s, vb_ref_t *ref, vb_fence_t busy)
{
    if (!s || !ref || ref->pool == VB_NONE) return;
    vb_entry_t *e = &s->tab[ref->entry];
    vb_copy_t *c = &e->c[ref->slot];
    c->busy = f_join(s, c->busy, busy);
    if (c->refs) c->refs--;
    if ((c->flags & VB_C_TEMP) && c->refs == 0) copy_free(s, ref->entry, ref->slot);
    ref->pool = VB_NONE;
}

/* V4: gather small blocks from host memory into one pinned extent and move
 * it with one DMA. q[] lists the refs to fill, src[] their source slots
 * (already protected by the caller). */
static int batch_move(vb_store_t *s, vb_ref_t *refs, const uint32_t *q, const uint8_t *src,
                      uint32_t m, uint32_t tp)
{
    uint32_t sp = s->stage;
    uint64_t total = 0, soff = 0, doff = 0, o = 0;
    vb_fence_t ws = 0, wd = 0, gather = 0, done = 0;
    for (uint32_t j = 0; j < m; j++) total += align_up(s->tab[refs[q[j]].entry].len);
    int rc = pool_alloc(s, sp, total, &soff, &ws);
    if (rc == VB_OK) rc = range_add(&s->pool[sp], soff, total, RAW_OWNER, 0);
    if (rc != VB_OK) return rc;
    for (uint32_t j = 0; j < m && rc == VB_OK; j++) {
        vb_entry_t *e = &s->tab[refs[q[j]].entry];
        vb_copy_t *c = &e->c[src[j]];
        vb_fence_t g = 0;
        rc = xfer(s, c->pool, c->off, sp, soff + o, e->len, f_join(s, c->ready, ws), &g);
        c->busy = f_join(s, c->busy, g);
        gather = f_join(s, gather, g);
        o += align_up(e->len);
    }
    if (rc == VB_OK) rc = pool_alloc(s, tp, total, &doff, &wd);
    if (rc == VB_OK) rc = xfer(s, sp, soff, tp, doff, total, f_join(s, gather, wd), &done);
    range_del(s, &s->pool[sp], soff, f_join(s, gather, done));
    if (rc != VB_OK) return rc;
    o = 0;
    for (uint32_t j = 0; j < m; j++) {
        uint32_t ei = refs[q[j]].entry;
        vb_entry_t *e = &s->tab[ei];
        int sl = slot_new(e); /* checked free when queued */
        range_add(&s->pool[tp], doff + o, align_up(e->len), ei, (uint32_t) sl);
        copy_set(s, ei, (uint32_t) sl, tp, doff + o, VB_C_VERIFIED, done);
        refs[q[j]].slot = (uint8_t) sl;
        o += align_up(e->len);
    }
    s->st.batches++;
    s->st.batched_blocks += m;
    return VB_OK;
}

typedef struct {
    uint32_t q[VB_EXTENT_BLOCKS];
    uint8_t src[VB_EXTENT_BLOCKS];
    uint32_t m;
    uint64_t bytes;
} vb_batch_t;

static int batch_flush(vb_store_t *s, vb_batch_t *b, vb_ref_t *refs, uint32_t tp)
{
    int rc = VB_OK;
    if (b->m == 1) {
        uint32_t out = 0;
        rc = move_block(s, refs[b->q[0]].entry, b->src[0], tp, false, &out);
        refs[b->q[0]].slot = (uint8_t) out;
    } else if (b->m > 1) {
        rc = batch_move(s, refs, b->q, b->src, b->m, tp);
        if (rc == VB_ERR_NOMEM || rc == VB_ERR_FULL) {
            /* No room for a staging extent: move the blocks one by one. */
            rc = VB_OK;
            for (uint32_t j = 0; j < b->m && rc == VB_OK; j++) {
                uint32_t out = 0;
                rc = move_block(s, refs[b->q[j]].entry, b->src[j], tp, false, &out);
                refs[b->q[j]].slot = (uint8_t) out;
            }
        }
    }
    for (uint32_t j = 0; j < b->m; j++) {
        vb_ref_t *r = &refs[b->q[j]];
        s->tab[r->entry].c[b->src[j]].refs--;
        if (rc == VB_OK) ref_fill(s, r->entry, r->slot, r);
    }
    b->m = 0;
    b->bytes = 0;
    return rc;
}

int vb_acquire_many(vb_store_t *s, const ipfsn_cid_t *cids, uint32_t n, uint32_t dev,
                    vb_ref_t *refs)
{
    if (!s || (!cids && n) || (!refs && n) || dev >= s->ndevs || s->home[dev] == VB_NONE)
        return VB_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        refs[i].pool = VB_NONE;
        refs[i].slot = 0;
    }
    uint32_t tp = s->home[dev];
    bool batch = (s->opts & VB_OPT_BATCH) && (s->opts & VB_OPT_DEDUP) && s->stage != VB_NONE &&
                 !host_side(s->pool[tp].tier) && s->lok[s->stage][tp];
    vb_batch_t b;
    b.m = 0;
    b.bytes = 0;
    int rc = VB_OK;
    bool dups = false;
    for (uint32_t i = 0; i < n && rc == VB_OK; i++) {
        int ei = lookup_or_fetch(s, &cids[i]);
        if (ei < 0) {
            rc = ei;
            break;
        }
        vb_entry_t *e = &s->tab[ei];
        bool dup = false;
        for (uint32_t j = 0; j < b.m; j++) dup |= refs[b.q[j]].entry == (uint32_t) ei;
        if (dup) { /* the same CID is already queued: resolve after the flush */
            refs[i].slot = VB_NONE;
            dups = true;
            continue;
        }
        int sl = resident(s, (uint32_t) ei, dev);
        if (sl >= 0) {
            ref_fill(s, (uint32_t) ei, (uint32_t) sl, &refs[i]);
            continue;
        }
        int from = best_source(s, (uint32_t) ei, tp, 0);
        if (from < 0) {
            rc = VB_ERR_NOROUTE;
            break;
        }
        uint64_t a = align_up(e->len);
        if (batch && e->len <= s->batch_max_block && host_side(s->pool[e->c[from].pool].tier) &&
            slot_new(e) >= 0) {
            if (b.m == VB_EXTENT_BLOCKS || b.bytes + a > s->extent_bytes)
                rc = batch_flush(s, &b, refs, tp);
            if (rc != VB_OK) break;
            refs[i].entry = (uint32_t) ei;
            e->c[from].refs++; /* keep the source until the flush */
            b.q[b.m] = i;
            b.src[b.m] = (uint8_t) from;
            b.m++;
            b.bytes += a;
            continue;
        }
        uint32_t out = 0;
        rc = move_block(s, (uint32_t) ei, (uint32_t) from, tp, !(s->opts & VB_OPT_DEDUP), &out);
        if (rc == VB_OK) ref_fill(s, (uint32_t) ei, out, &refs[i]);
    }
    int frc = batch_flush(s, &b, refs, tp);
    if (rc == VB_OK) rc = frc;
    for (uint32_t i = 0; i < n && rc == VB_OK && dups; i++)
        if (refs[i].pool == VB_NONE && refs[i].slot == VB_NONE)
            rc = vb_acquire(s, &cids[i], dev, &refs[i]);
    if (rc != VB_OK)
        for (uint32_t i = 0; i < n; i++) vb_release(s, &refs[i], 0);
    return rc;
}

/* ---- pins ------------------------------------------------------------------------- */

int vb_pin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev)
{
    vb_ref_t r;
    int rc = vb_acquire(s, cid, dev, &r);
    if (rc != VB_OK) return rc;
    s->tab[r.entry].c[r.slot].flags |= VB_C_PINNED;
    vb_release(s, &r, 0);
    return VB_OK;
}

int vb_unpin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev)
{
    if (!s || !cid || dev >= s->ndevs) return VB_ERR_ARG;
    int ei = find(s, cid, false, 0);
    if (ei < 0) return VB_ERR_NOTFOUND;
    int rc = VB_ERR_NOTFOUND;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        vb_copy_t *c = &s->tab[ei].c[i];
        if (c->pool != VB_NONE && (c->flags & VB_C_PINNED) &&
            (c->pool == s->home[dev] || s->pool[c->pool].dev == dev)) {
            c->flags &= (uint8_t) ~VB_C_PINNED;
            rc = VB_OK;
        }
    }
    return rc;
}

uint32_t vb_drop_pool(vb_store_t *s, uint32_t pi)
{
    if (!s || pi >= s->npools) return 0;
    vb_pool_t *p = &s->pool[pi];
    uint32_t n = 0;
    for (uint32_t i = p->nr; i-- > 0;) {
        uint32_t ei = p->r[i].entry;
        if (ei == RAW_OWNER || !evictable(s, &s->tab[ei], p->r[i].slot)) continue;
        copy_free(s, ei, p->r[i].slot);
        n++;
    }
    return n;
}

/* ---- pipeline (V5) ------------------------------------------------------------------ */

#define VB_DEPTH_MAX 8u

int vb_run(vb_store_t *s, const vb_op_t *ops, uint32_t nops, const uint8_t *devs, uint32_t depth,
           vb_ref_t *scratch, uint32_t max_in, vb_compute_fn fn, void *ctx, vb_fence_t *last)
{
    if (!s || (!ops && nops) || !scratch || !fn || depth == 0 || depth > VB_DEPTH_MAX)
        return VB_ERR_ARG;
    vb_fence_t done[VB_DEPTH_MAX], ready[VB_DEPTH_MAX];
    for (uint32_t k = 0; k < VB_DEPTH_MAX; k++) done[k] = ready[k] = 0;
    if (last) *last = 0;
    uint32_t ka = 0, kc = 0; /* ring slots of the next acquire and compute */
    for (uint32_t st = 0; st < nops + depth - 1u; st++) {
        if (st < nops) {
            uint32_t k = ka;
            ka = ka + 1u == depth ? 0u : ka + 1u;
            if (ops[st].nin > max_in) return VB_ERR_ARG;
            f_wait(s, done[k]); /* op st-depth: keep at most `depth` in flight */
            uint32_t dev = devs ? devs[st] : ops[st].dev;
            vb_ref_t *r = scratch + (uint64_t) k * max_in;
            int rc = vb_acquire_many(s, ops[st].in, ops[st].nin, dev, r);
            if (rc != VB_OK) return rc;
            ready[k] = 0;
            for (uint32_t i = 0; i < ops[st].nin; i++) ready[k] = f_join(s, ready[k], r[i].ready);
        }
        if (st + 1u >= depth && st + 1u - depth < nops) {
            uint32_t j = st + 1u - depth, k = kc;
            kc = kc + 1u == depth ? 0u : kc + 1u;
            uint32_t dev = devs ? devs[j] : ops[j].dev;
            vb_ref_t *r = scratch + (uint64_t) k * max_in;
            vb_fence_t d = 0;
            int rc = fn(ctx, j, dev, r, ops[j].nin, ready[k], &d);
            for (uint32_t i = 0; i < ops[j].nin; i++) vb_release(s, &r[i], d);
            if (rc != 0) return VB_ERR_BACKEND;
            done[k] = d;
            if (last) *last = d;
        }
    }
    return VB_OK;
}

/* ---- placement (V7) ------------------------------------------------------------------ */

/* Cost of making entry ei readable on dev under the plan so far. */
static uint64_t plan_in_ns(vb_store_t *s, uint32_t ei, uint32_t dev, uint64_t *bytes)
{
    vb_entry_t *e = &s->tab[ei];
    uint32_t tp = s->home[dev];
    if (e->plan & (1u << dev)) return 0;
    uint64_t best = INF_NS;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        const vb_copy_t *c = &e->c[i];
        if (!usable(c) || (c->flags & VB_C_TEMP)) continue;
        if (c->pool == tp || ((s->opts & VB_OPT_ZEROCOPY) && s->acc[c->pool][dev])) return 0;
        uint64_t r = route_ns(s, c->pool, tp, e->len);
        if (r < best) best = r;
    }
    if (best == INF_NS) best = refetch_ns(s, e, VB_NONE, tp);
    if (best != INF_NS) *bytes += e->len;
    return best == INF_NS ? 0 : best;
}

static int plan_impl(vb_store_t *s, const vb_op_t *ops, uint32_t nops, uint32_t mask,
                     const uint8_t *fixed, uint8_t *out, uint64_t *xb, uint64_t *xn)
{
    uint64_t fin[VB_PLAN_MAX];
    if (!s || (!ops && nops) || !out || nops > VB_PLAN_MAX) return VB_ERR_ARG;
    for (uint32_t i = 0; i < s->cap; i++) s->tab[i].plan = 0;
    uint64_t load[VB_MAX_DEVS];
    for (uint32_t d = 0; d < VB_MAX_DEVS; d++) load[d] = 0;
    uint64_t tb = 0, tn = 0;
    for (uint32_t i = 0; i < nops; i++) {
        const vb_op_t *op = &ops[i];
        uint32_t lo = 0, hi = s->ndevs;
        uint32_t want = fixed ? fixed[i] : op->dev;
        if (want != VB_DEV_ANY) {
            if (want >= s->ndevs || s->home[want] == VB_NONE) return VB_ERR_ARG;
            lo = want;
            hi = want + 1u;
        }
        uint32_t bd = VB_NONE;
        uint64_t bf = INF_NS, bb = 0, bx = 0;
        for (uint32_t d = lo; d < hi; d++) {
            if (s->home[d] == VB_NONE || (want == VB_DEV_ANY && !(mask & (1u << d)))) continue;
            uint64_t bytes = 0, x = 0, start = load[d];
            for (uint32_t k = 0; k < op->nin; k++) {
                int ei = find(s, &op->in[k], false, 0);
                if (ei >= 0) x += plan_in_ns(s, (uint32_t) ei, d, &bytes);
            }
            for (uint32_t k = 0; k < op->ndeps; k++) {
                uint32_t p = op->deps[k];
                if (p >= i) continue;
                uint64_t arrive = fin[p];
                if (out[p] != d) {
                    uint64_t r = route_ns(s, s->home[out[p]], s->home[d], ops[p].out_len);
                    if (r != INF_NS) {
                        x += r;
                        arrive += r;
                        bytes += ops[p].out_len;
                    }
                }
                if (arrive > start) start = arrive;
            }
            uint64_t f = start + x + op->cost_ns;
            if (f < bf) {
                bf = f;
                bd = d;
                bb = bytes;
                bx = x;
            }
        }
        if (bd == VB_NONE) return VB_ERR_ARG;
        out[i] = (uint8_t) bd;
        load[bd] = bf;
        fin[i] = bf;
        tb += bb;
        tn += bx;
        for (uint32_t k = 0; k < op->nin; k++) {
            int ei = find(s, &op->in[k], false, 0);
            if (ei >= 0) s->tab[ei].plan |= (uint8_t) (1u << bd);
        }
    }
    if (xb) *xb = tb;
    if (xn) *xn = tn;
    return VB_OK;
}

int vb_plan(vb_store_t *s, const vb_op_t *ops, uint32_t nops, uint32_t dev_mask, uint8_t *out,
            uint64_t *xfer_bytes, uint64_t *xfer_ns)
{
    return plan_impl(s, ops, nops, dev_mask, 0, out, xfer_bytes, xfer_ns);
}

int vb_plan_cost(vb_store_t *s, const vb_op_t *ops, uint32_t nops, const uint8_t *devs,
                 uint64_t *xfer_bytes, uint64_t *xfer_ns)
{
    if (!devs) return VB_ERR_ARG;
    uint8_t tmp[VB_PLAN_MAX]; /* plan_impl writes out[] as it goes */
    if (nops > VB_PLAN_MAX) return VB_ERR_ARG;
    return plan_impl(s, ops, nops, 0, devs, tmp, xfer_bytes, xfer_ns);
}

int vb_affinity(vb_store_t *s, const vb_access_t *trace, uint32_t n)
{
    if (!s || (!trace && n)) return VB_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if (trace[i].dev >= VB_MAX_DEVS) return VB_ERR_ARG;
        int ei = find(s, trace[i].cid, false, 0);
        if (ei < 0) continue;
        uint16_t *a = &s->tab[ei].acc[trace[i].dev];
        if (*a != 0xFFFFu) (*a)++;
    }
    return VB_OK;
}

int vb_place_affine(vb_store_t *s, bool pin)
{
    if (!s) return VB_ERR_ARG;
    int placed = 0;
    for (uint32_t i = 0; i < s->cap; i++) {
        vb_entry_t *e = &s->tab[i];
        if (e->state != 1) continue;
        uint32_t bd = VB_NONE, bc = 0;
        for (uint32_t d = 0; d < s->ndevs; d++)
            if (e->acc[d] > bc && s->home[d] != VB_NONE) {
                bc = e->acc[d];
                bd = d;
            }
        for (uint32_t d = 0; d < VB_MAX_DEVS; d++) e->acc[d] = 0;
        if (bd == VB_NONE) continue;
        ipfsn_cid_t cid;
        vcpy(&cid, &e->cid, sizeof(cid));
        int rc = pin ? vb_pin(s, &cid, bd) : VB_OK;
        if (!pin) {
            vb_ref_t r;
            rc = vb_acquire(s, &cid, bd, &r);
            if (rc == VB_OK) vb_release(s, &r, 0);
        }
        if (rc != VB_OK) return rc;
        placed++;
    }
    return placed;
}

/* ---- device outputs and network landing (V8, V9) ------------------------------------- */

static int land_alloc(vb_store_t *s, uint32_t pi, uint32_t len, vb_landing_t *l)
{
    uint64_t off = 0;
    vb_fence_t w = 0;
    int rc = pool_alloc(s, pi, len, &off, &w);
    if (rc == VB_OK) rc = range_add(&s->pool[pi], off, align_up(len), RAW_OWNER, 0);
    if (rc != VB_OK) return rc;
    l->pool = (uint8_t) pi;
    l->len = len;
    l->off = off;
    l->wait = w;
    l->ptr = s->pool[pi].cpu ? s->pool[pi].cpu + off : 0;
    vset(&l->rdma, 0, sizeof(l->rdma));
    l->rdma.pool = s->pool[pi].h;
    l->rdma.off = off;
    l->rdma.len = len;
    return VB_OK;
}

static void land_free(vb_store_t *s, vb_landing_t *l, vb_fence_t f)
{
    if (l->pool == VB_NONE) return;
    range_del(s, &s->pool[l->pool], l->off, f);
    l->pool = VB_NONE;
}

/* SHA-256 of a landing: CPU for host-visible memory, else the device hook,
 * else one staging copy to pinned memory. */
static int land_digest(vb_store_t *s, vb_landing_t *l, vb_fence_t written, uint8_t dig[32])
{
    vb_pool_t *p = &s->pool[l->pool];
    if (p->cpu) {
        f_wait(s, written);
        sha256(p->cpu + l->off, l->len, dig);
        s->st.hashed_cpu_bytes += l->len;
        return VB_OK;
    }
    if (s->be->hash) {
        vb_fence_t d = 0;
        if (s->be->hash(s->be->ctx, p->h, l->off, l->len, written, dig, &d) == 0) {
            f_wait(s, d);
            s->st.hashed_dev_bytes += l->len;
            return VB_OK;
        }
    }
    if (s->stage == VB_NONE) return VB_ERR_UNSUPP;
    vb_landing_t t;
    t.pool = VB_NONE;
    vb_fence_t d = 0;
    int rc = land_alloc(s, s->stage, l->len, &t);
    if (rc == VB_OK)
        rc = xfer(s, l->pool, l->off, t.pool, t.off, l->len, f_join(s, written, t.wait), &d);
    if (rc == VB_OK) {
        f_wait(s, d);
        sha256(t.ptr, t.len, dig);
        s->st.hashed_cpu_bytes += l->len;
    }
    land_free(s, &t, d);
    return rc;
}

/* Turn a verified landing into a resident copy of cid (or free it if the
 * block is already in that pool). */
static int land_adopt(vb_store_t *s, vb_landing_t *l, const ipfsn_cid_t *cid, vb_fence_t ready)
{
    int ei = find(s, cid, true, l->len);
    if (ei < 0) {
        land_free(s, l, ready);
        return VB_ERR_FULL;
    }
    vb_entry_t *e = &s->tab[ei];
    if (e->len != l->len) {
        land_free(s, l, ready);
        return VB_ERR_ARG;
    }
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++)
        if (usable(&e->c[i]) && e->c[i].pool == l->pool) {
            s->st.dedup_hits++;
            land_free(s, l, ready);
            return VB_OK;
        }
    int sl = slot_new(e);
    vb_range_t *r = range_at(&s->pool[l->pool], l->off);
    if (sl < 0 || !r) {
        land_free(s, l, ready);
        if (ncopies(e) == 0) {
            e->state = 2;
            s->count--;
        }
        return VB_ERR_FULL;
    }
    r->entry = (uint32_t) ei;
    r->slot = (uint8_t) sl;
    copy_set(s, (uint32_t) ei, (uint32_t) sl, l->pool, l->off, VB_C_VERIFIED, ready);
    l->pool = VB_NONE;
    return VB_OK;
}

int vb_output_alloc(vb_store_t *s, uint32_t dev, uint32_t len, vb_landing_t *l)
{
    if (!s || !l || dev >= s->ndevs || s->home[dev] == VB_NONE) return VB_ERR_ARG;
    vset(&l->cid, 0, sizeof(l->cid));
    l->pool = VB_NONE;
    l->kind = VB_LAND_DEV;
    return land_alloc(s, s->home[dev], len, l);
}

int vb_output_commit(vb_store_t *s, vb_landing_t *l, uint32_t codec, vb_fence_t written,
                     ipfsn_cid_t *cid)
{
    if (!s || !l || !cid || l->pool == VB_NONE) return VB_ERR_ARG;
    uint8_t dig[32];
    int rc = land_digest(s, l, written, dig);
    if (rc != VB_OK) {
        land_free(s, l, written);
        return rc;
    }
    vset(cid, 0, sizeof(*cid));
    cid->version = 1;
    cid->codec = codec;
    cid->mh_code = IPFSN_MH_SHA2_256;
    cid->digest_len = 32;
    vcpy(cid->digest, dig, 32);
    return land_adopt(s, l, cid, written);
}

int vb_recv_begin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t len, uint32_t dev,
                  vb_landing_t *l)
{
    if (!s || !cid || !l || dev >= s->ndevs || s->home[dev] == VB_NONE) return VB_ERR_ARG;
    l->pool = VB_NONE;
    if ((s->opts & VB_OPT_DEDUP) && vb_has(s, cid)) {
        s->st.dedup_hits++;
        return VB_HAVE;
    }
    ipfsn_cid_to_v1(cid, &l->cid);
    uint32_t tp = s->home[dev];
    if (s->be->rdma_export && !s->pool[tp].cpu && cid->mh_code == IPFSN_MH_SHA2_256) {
        int rc = land_alloc(s, tp, len, l);
        if (rc == VB_OK) {
            if (s->be->rdma_export(s->be->ctx, s->pool[tp].h, l->off, len, &l->rdma) == 0) {
                l->kind = VB_LAND_RDMA;
                s->st.rdma_landings++;
                return VB_OK;
            }
            land_free(s, l, 0);
        }
    }
    uint32_t hp = s->stage != VB_NONE ? s->stage : tp;
    if (!s->pool[hp].cpu) return VB_ERR_UNSUPP;
    int rc = land_alloc(s, hp, len, l);
    if (rc != VB_OK) return rc;
    l->kind = VB_LAND_HOST;
    if (s->be->rdma_export) s->be->rdma_export(s->be->ctx, s->pool[hp].h, l->off, len, &l->rdma);
    return VB_OK;
}

int vb_recv_end(vb_store_t *s, vb_landing_t *l, vb_fence_t arrived)
{
    if (!s || !l || l->pool == VB_NONE) return VB_ERR_ARG;
    int rc;
    if (l->cid.mh_code == IPFSN_MH_SHA2_256) {
        uint8_t dig[32];
        rc = land_digest(s, l, arrived, dig);
        if (rc == VB_OK && (l->cid.digest_len != 32 || !veq(dig, l->cid.digest, 32)))
            rc = VB_ERR_HASH;
    } else {
        f_wait(s, arrived);
        rc = l->ptr && ipfsn_cid_verify(&l->cid, l->ptr, l->len) == IPFSN_OK ? VB_OK : VB_ERR_HASH;
    }
    if (rc != VB_OK) {
        if (rc == VB_ERR_HASH) s->st.rejected++;
        land_free(s, l, arrived);
        return rc;
    }
    ipfsn_cid_t c;
    vcpy(&c, &l->cid, sizeof(c));
    return land_adopt(s, l, &c, arrived);
}

void vb_recv_abort(vb_store_t *s, vb_landing_t *l)
{
    if (s && l) land_free(s, l, 0);
}

static int serve_impl(vb_store_t *s, uint32_t ei, bool rdma, vb_serve_t *out)
{
    vb_entry_t *e = &s->tab[ei];
    int host = -1, dev = -1;
    out->ref.pool = VB_NONE;
    for (uint32_t i = 0; i < VB_MAX_COPIES; i++) {
        if (!usable(&e->c[i]) || (e->c[i].flags & VB_C_TEMP)) continue;
        if (s->pool[e->c[i].pool].cpu)
            host = (int) i;
        else if (dev < 0)
            dev = (int) i;
    }
    if (host < 0 && dev < 0) return VB_ERR_UNVERIFIED;
    vset(&out->rdma, 0, sizeof(out->rdma));
    out->len = e->len;
    if (host < 0 && rdma && s->be->rdma_export) {
        vb_copy_t *c = &e->c[dev];
        if (s->be->rdma_export(s->be->ctx, s->pool[c->pool].h, c->off, e->len, &out->rdma) == 0) {
            f_wait(s, c->ready); /* the NIC is not ordered by our fences */
            ref_fill(s, ei, (uint32_t) dev, &out->ref);
            out->kind = VB_SERVE_RDMA;
            out->ptr = 0;
            s->st.rdma_serves++;
            return VB_OK;
        }
    }
    if (host < 0) { /* stage once; the pinned copy stays resident */
        if (s->stage == VB_NONE) return VB_ERR_UNSUPP;
        uint32_t o = 0;
        int rc = move_block(s, ei, (uint32_t) dev, s->stage, false, &o);
        if (rc != VB_OK) return rc;
        host = (int) o;
        s->st.staged_serves++;
    } else {
        s->st.host_serves++;
    }
    vb_copy_t *c = &e->c[host];
    f_wait(s, c->ready);
    ref_fill(s, ei, (uint32_t) host, &out->ref);
    out->kind = VB_SERVE_HOST;
    out->ptr = s->pool[c->pool].cpu + c->off;
    return VB_OK;
}

int vb_serve(vb_store_t *s, const ipfsn_cid_t *cid, vb_serve_t *out)
{
    if (!s || !cid || !out) return VB_ERR_ARG;
    out->ref.pool = VB_NONE;
    int ei = find(s, cid, false, 0);
    if (ei < 0) return VB_ERR_NOTFOUND;
    if (s->tab[ei].flags & VB_F_PRIVATE) return VB_ERR_PRIVATE;
    return serve_impl(s, (uint32_t) ei, true, out);
}

void vb_serve_done(vb_store_t *s, vb_serve_t *sv, vb_fence_t sent)
{
    if (s && sv) vb_release(s, &sv->ref, sent);
}

int vb_ipfs_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    vb_store_t *s = (vb_store_t *) ctx;
    if (!s || !cid || !buf || !len) return IPFSN_ERR_ARG;
    int ei = lookup_or_fetch(s, cid);
    if (ei < 0) return IPFSN_ERR_NOTFOUND;
    if (s->tab[ei].len > cap) return IPFSN_ERR_SPACE;
    vb_serve_t sv; /* local read: no privacy gate, no RDMA */
    if (serve_impl(s, (uint32_t) ei, false, &sv) != VB_OK) return IPFSN_ERR_IO;
    vcpy(buf, sv.ptr, sv.len);
    *len = sv.len;
    vb_serve_done(s, &sv, 0);
    return IPFSN_OK;
}
