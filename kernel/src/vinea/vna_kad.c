/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_kad.c — Kademlia routing table and lookup. See vna_kad.h. */
#include "vna_kad.h"

void vna_rt_init(vna_rt_t *rt, const vna_id_t *self, vna_contact_t *pool, uint32_t cap)
{
    rt->self = *self;
    rt->pool = pool;
    rt->cap = cap;
    rt->used = 0;
    rt->touch = 0;
    for (uint32_t b = 0; b < VNA_KAD_BUCKETS; b++) {
        rt->count[b] = 0;
        rt->bucket_activity_ms[b] = 0;
    }
    for (uint32_t i = 0; i < cap; i++) pool[i].in_use = 0;
}

vna_contact_t *vna_rt_find(vna_rt_t *rt, const vna_id_t *id)
{
    for (uint32_t i = 0; i < rt->cap; i++)
        if (rt->pool[i].in_use && vna_id_eq(&rt->pool[i].id, id)) return &rt->pool[i];
    return 0;
}

vna_rt_result_t vna_rt_seen(vna_rt_t *rt, const vna_id_t *id, const uint8_t *addr,
                            uint32_t addr_len, uint8_t flags, uint64_t now, vna_contact_t *lrs)
{
    int b = vna_bucket_index(&rt->self, id);
    if (b < 0 || addr_len > VNA_ADDR_MAX) return VNA_RT_IGNORED;
    rt->bucket_activity_ms[b] = now;
    vna_contact_t *c = vna_rt_find(rt, id);
    if (c) {
        if (addr && addr_len) {
            vna_copy(c->addr, addr, addr_len);
            c->addr_len = (uint8_t) addr_len;
        }
        c->flags = flags;
        c->last_seen_ms = now;
        c->touch = ++rt->touch;
        return VNA_RT_UPDATED;
    }
    if (rt->count[b] < VNA_KAD_K) {
        for (uint32_t i = 0; i < rt->cap; i++) {
            if (rt->pool[i].in_use) continue;
            c = &rt->pool[i];
            c->id = *id;
            vna_zero(c->addr, VNA_ADDR_MAX);
            if (addr) vna_copy(c->addr, addr, addr_len);
            c->addr_len = (uint8_t) addr_len;
            c->bucket = (uint8_t) b;
            c->in_use = 1;
            c->flags = flags;
            c->last_seen_ms = now;
            c->touch = ++rt->touch;
            rt->count[b]++;
            rt->used++;
            return VNA_RT_ADDED;
        }
        return VNA_RT_IGNORED; /* pool exhausted: fail closed, keep the old table */
    }
    vna_contact_t *old = 0;
    for (uint32_t i = 0; i < rt->cap; i++) {
        vna_contact_t *x = &rt->pool[i];
        if (x->in_use && x->bucket == (uint8_t) b && (!old || x->touch < old->touch)) old = x;
    }
    if (old && lrs) *lrs = *old;
    return VNA_RT_FULL;
}

bool vna_rt_remove(vna_rt_t *rt, const vna_id_t *id)
{
    vna_contact_t *c = vna_rt_find(rt, id);
    if (!c) return false;
    c->in_use = 0;
    rt->count[c->bucket]--;
    rt->used--;
    return true;
}

uint32_t vna_rt_closest(const vna_rt_t *rt, const vna_id_t *target, uint32_t k,
                        const vna_id_t *exclude, vna_contact_t *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < rt->cap; i++) {
        const vna_contact_t *c = &rt->pool[i];
        if (!c->in_use || (exclude && vna_id_eq(&c->id, exclude))) continue;
        uint32_t pos = n;
        while (pos > 0 && vna_id_closer(target, &c->id, &out[pos - 1].id) < 0) pos--;
        if (pos >= k) continue;
        uint32_t last = n < k ? n : k - 1;
        for (uint32_t j = last; j > pos; j--) out[j] = out[j - 1];
        out[pos] = *c;
        if (n < k) n++;
    }
    return n;
}

uint32_t vna_rt_idle_buckets(const vna_rt_t *rt, uint64_t now, uint64_t idle_ms, uint8_t *out,
                             uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t b = 0; b < VNA_KAD_BUCKETS && n < max; b++)
        if (rt->count[b] && now - rt->bucket_activity_ms[b] >= idle_ms) out[n++] = (uint8_t) b;
    return n;
}

void vna_rt_id_in_bucket(const vna_id_t *self, uint32_t b, const uint8_t rnd[32], vna_id_t *out)
{
    uint32_t p = 255u - (b & 255u); /* bit position from the most significant end */
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t lo = i * 8u, hi = lo + 7u;
        uint8_t keep; /* mask of bits (from the top) equal to self */
        if (hi < p)
            keep = 0xFF;
        else if (lo > p)
            keep = 0x00;
        else
            keep = (uint8_t) (0xFFu << (8u - (p - lo)));
        out->b[i] = (uint8_t) ((self->b[i] & keep) | (rnd[i] & (uint8_t) ~keep));
    }
    /* force bit p to differ from self, whatever rnd held there */
    uint8_t bit = (uint8_t) (0x80u >> (p & 7u));
    out->b[p >> 3] =
        (uint8_t) ((out->b[p >> 3] & (uint8_t) ~bit) | ((self->b[p >> 3] ^ bit) & bit));
}

/* ---- lookup ---- */
static bool lk_has(const vna_lookup_t *lk, const vna_id_t *id)
{
    for (uint32_t i = 0; i < lk->n; i++)
        if (vna_id_eq(&lk->c[i].id, id)) return true;
    return false;
}

static void lk_insert(vna_lookup_t *lk, const vna_id_t *id, const uint8_t *addr, uint32_t addr_len,
                      uint8_t flags, uint8_t path)
{
    if (vna_id_eq(id, &lk->self) || lk_has(lk, id) || addr_len > VNA_ADDR_MAX) return;
    uint32_t pos = lk->n;
    while (pos > 0 && vna_id_closer(&lk->target, id, &lk->c[pos - 1].id) < 0) pos--;
    if (lk->n == VNA_LOOKUP_MAX) { /* drop the farthest candidate not in flight */
        int32_t victim = -1;
        for (int32_t i = (int32_t) lk->n - 1; i >= 0; i--)
            if (lk->c[i].state != VNA_LK_INFLIGHT) {
                victim = i;
                break;
            }
        if (victim < 0 || (uint32_t) victim < pos) return; /* newcomer is farther */
        for (uint32_t j = (uint32_t) victim; j + 1 < lk->n; j++) lk->c[j] = lk->c[j + 1];
        lk->n--;
    }
    for (uint32_t j = lk->n; j > pos; j--) lk->c[j] = lk->c[j - 1];
    vna_cand_t *c = &lk->c[pos];
    vna_zero(c, sizeof *c);
    c->id = *id;
    if (addr) vna_copy(c->addr, addr, addr_len);
    c->addr_len = (uint8_t) addr_len;
    c->flags = flags;
    c->state = VNA_LK_NEW;
    c->path = path;
    lk->n++;
}

void vna_lookup_init(vna_lookup_t *lk, const vna_rt_t *rt, const vna_id_t *target,
                     vna_lk_mode_t mode, uint32_t paths)
{
    static vna_contact_t seed[VNA_LOOKUP_MAX]; /* single-threaded scratch */
    vna_zero(lk, sizeof *lk);
    if (paths == 0) paths = 1;
    if (paths > VNA_KAD_PATHS) paths = VNA_KAD_PATHS;
    lk->active = true;
    lk->mode = (uint8_t) mode;
    lk->paths = (uint8_t) paths;
    lk->alpha = VNA_KAD_ALPHA;
    lk->k = VNA_KAD_K;
    lk->target = *target;
    lk->self = rt->self;
    uint32_t want = VNA_KAD_K * paths;
    if (want > VNA_LOOKUP_MAX) want = VNA_LOOKUP_MAX;
    uint32_t n = vna_rt_closest(rt, target, want, 0, seed);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = i;
        while (p >= paths) p -= paths; /* round-robin: i mod paths */
        lk_insert(lk, &seed[i].id, seed[i].addr, seed[i].addr_len, seed[i].flags, (uint8_t) p);
    }
}

/* First NEW candidate among the k closest live ones of path p, or -1;
 * *pending is set if the path still has something to wait for or ask. */
static int32_t path_next(const vna_lookup_t *lk, uint32_t p, bool *pending)
{
    uint32_t live = 0;
    int32_t pick = -1;
    *pending = lk->inflight[p] > 0;
    for (uint32_t i = 0; i < lk->n && live < lk->k; i++) {
        const vna_cand_t *c = &lk->c[i];
        if (c->path != p || c->state == VNA_LK_FAILED) continue;
        live++;
        if (c->state == VNA_LK_NEW && pick < 0) pick = (int32_t) i;
    }
    if (pick >= 0) *pending = true;
    return pick;
}

int32_t vna_lookup_next(vna_lookup_t *lk)
{
    if (!lk->active || lk->finished || lk->found_value) return -1;
    for (uint32_t p = 0; p < lk->paths; p++) {
        if (lk->inflight[p] >= lk->alpha) continue;
        bool pending;
        int32_t i = path_next(lk, p, &pending);
        if (i >= 0) return i;
    }
    return -1;
}

void vna_lookup_sent(vna_lookup_t *lk, int32_t idx, uint64_t rpc, uint64_t deadline_ms)
{
    if (idx < 0 || (uint32_t) idx >= lk->n) return;
    vna_cand_t *c = &lk->c[idx];
    if (c->state != VNA_LK_NEW) return;
    c->state = VNA_LK_INFLIGHT;
    c->rpc = rpc;
    c->deadline_ms = deadline_ms;
    lk->inflight[c->path]++;
    lk->queries++;
}

static vna_cand_t *lk_match(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc)
{
    for (uint32_t i = 0; i < lk->n; i++) {
        vna_cand_t *c = &lk->c[i];
        if (c->state == VNA_LK_INFLIGHT && c->rpc == rpc && vna_id_eq(&c->id, from)) return c;
    }
    return 0;
}

vna_status_t vna_lookup_on_nodes(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc,
                                 const vna_contact_t *contacts, uint32_t n)
{
    vna_cand_t *c = lk_match(lk, from, rpc);
    if (!c) return VNA_ERR_UNEXPECTED;
    uint8_t path = c->path;
    c->state = VNA_LK_DONE;
    lk->inflight[path]--;
    lk->answers++;
    for (uint32_t i = 0; i < n; i++)
        lk_insert(lk, &contacts[i].id, contacts[i].addr, contacts[i].addr_len, contacts[i].flags,
                  path);
    return VNA_OK;
}

vna_status_t vna_lookup_on_value(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc)
{
    vna_cand_t *c = lk_match(lk, from, rpc);
    if (!c || lk->mode != VNA_LK_FIND_VALUE) return VNA_ERR_UNEXPECTED;
    c->state = VNA_LK_DONE;
    lk->inflight[c->path]--;
    lk->answers++;
    lk->found_value = true;
    lk->value_from = *from;
    return VNA_OK;
}

vna_status_t vna_lookup_on_fail(vna_lookup_t *lk, const vna_id_t *from, uint64_t rpc)
{
    vna_cand_t *c = lk_match(lk, from, rpc);
    if (!c) return VNA_ERR_UNEXPECTED;
    c->state = VNA_LK_FAILED;
    lk->inflight[c->path]--;
    lk->failures++;
    return VNA_OK;
}

uint32_t vna_lookup_expire(vna_lookup_t *lk, uint64_t now)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < lk->n; i++) {
        vna_cand_t *c = &lk->c[i];
        if (c->state == VNA_LK_INFLIGHT && now >= c->deadline_ms) {
            c->state = VNA_LK_FAILED;
            lk->inflight[c->path]--;
            lk->failures++;
            n++;
        }
    }
    return n;
}

bool vna_lookup_done(vna_lookup_t *lk)
{
    if (!lk->active) return true;
    if (lk->found_value) return true;
    for (uint32_t p = 0; p < lk->paths; p++) {
        bool pending;
        (void) path_next(lk, p, &pending);
        if (pending) return false;
    }
    lk->finished = true;
    return true;
}

uint32_t vna_lookup_result(const vna_lookup_t *lk, uint32_t k, vna_cand_t *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < lk->n && n < k; i++)
        if (lk->c[i].state == VNA_LK_DONE) out[n++] = lk->c[i];
    return n;
}
