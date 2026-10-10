/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_node.c — the DHT node state machine. See vna_node.h. */
#include "vna_node.h"
#include "../swarm/swarm_hk.h"

void vna_outbox_init(vna_outbox_t *ob, uint8_t *buf, uint32_t cap)
{
    ob->buf = buf;
    ob->cap = cap;
    ob->used = ob->n = ob->dropped = 0;
}

void vna_outbox_clear(vna_outbox_t *ob)
{
    ob->used = ob->n = 0;
}

void vna_node_cfg_default(vna_node_cfg_t *c)
{
    c->degree = VNA_DEG_ROUTE;
    c->pow_bits = 0;
    c->ts_window_ms = 30000;
    c->rpc_timeout_ms = 2000;
    c->rec_max_ttl_ms = 24ull * 3600u * 1000u;
    c->republish_ms = 3600ull * 1000u;
    c->paths = 1;
    c->max_records_per_peer = 8;
    c->ubh = true;
}

vna_status_t vna_node_init(vna_node_t *n, const vna_identity_t *idn, const vna_node_cfg_t *cfg,
                           const vna_node_mem_t *mem, const uint8_t seed[32])
{
    if (!n || !idn || !cfg || !mem || !seed || !mem->pool || !mem->store) return VNA_ERR_ARG;
    n->idn = idn;
    n->cfg = *cfg;
    if (n->cfg.paths == 0 || n->cfg.paths > VNA_KAD_PATHS) return VNA_ERR_ARG;
    vna_rt_init(&n->rt, &idn->id, mem->pool, mem->pool_cap);
    vna_keycache_init(&n->kc, mem->kc, mem->kc ? mem->kc_cap : 0);
    vna_replay_init(&n->rp, mem->rp, mem->rp_cap, cfg->ts_window_ms);
    vna_dedupe_init(&n->dd, mem->dd, mem->dd ? mem->dd_cap : 0);
    vna_zero(&n->v, sizeof n->v);
    n->v.self = idn->id;
    n->v.pow_bits = cfg->pow_bits;
    n->v.kc = mem->kc ? &n->kc : 0;
    n->v.rp = &n->rp;
    n->v.dd = &n->dd;
    n->store = mem->store;
    n->store_cap = mem->store_cap;
    for (uint32_t i = 0; i < n->store_cap; i++) n->store[i].used = false;
    n->agr = 0;
    n->usage = 0;
    n->trust = 0;
    n->trust_ctx = 0;
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++) n->pend[i].kind = VNA_P_FREE;
    for (uint32_t i = 0; i < VNA_NODE_LOOKUPS; i++) n->lks[i].used = false;
    n->ev_head = n->ev_n = 0;
    vna_drbg_seed(&n->rng, seed, 32);
    n->seq = 0;
    n->evictions = n->evict_kept = n->stores_ok = n->stores_refused = 0;
    n->unexpected = n->denied = n->hk_refused = n->sent = n->received = n->dropped_out = 0;
    n->rcpt = 0;
    n->rcpt_ctx = 0;
    n->rcpt_in = n->rcpt_refused = n->rcpt_out = 0;
    n->xform_refused = n->seeded = 0;
    n->spool = 0;
    n->xform = 0;
    n->joined = false;
    return VNA_OK;
}

void vna_node_set_agreement(vna_node_t *n, const vna_agreement_t *a, vna_usage_t *us,
                            vna_trust_fn trust, void *ctx)
{
    n->agr = a;
    n->usage = us;
    n->trust = trust;
    n->trust_ctx = ctx;
}

static uint8_t eff_degree(const vna_node_t *n)
{
    uint8_t d = n->cfg.degree;
    if (n->agr && n->agr->degree < d) d = n->agr->degree;
    return d;
}

static uint64_t trust_of(const vna_node_t *n, const vna_id_t *peer)
{
    return n->trust ? n->trust(n->trust_ctx, peer) : 0;
}

static uint64_t new_rpc(vna_node_t *n)
{
    uint64_t r;
    do r = vna_drbg_u64(&n->rng);
    while (r == 0);
    return r;
}

static vna_pending_t *pend_alloc(vna_node_t *n)
{
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++)
        if (n->pend[i].kind == VNA_P_FREE) return &n->pend[i];
    return 0;
}

static vna_pending_t *pend_find(vna_node_t *n, uint64_t rpc)
{
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++)
        if (n->pend[i].kind != VNA_P_FREE && n->pend[i].rpc == rpc) return &n->pend[i];
    return 0;
}

static void spool_on_ack(vna_node_t *n, const vna_id_t *peer, uint64_t sseq, uint8_t status,
                         uint64_t now);
static void spool_on_timeout(vna_node_t *n, const vna_id_t *peer, uint64_t sseq, uint64_t now);
static void spool_pump(vna_node_t *n, uint64_t now, vna_outbox_t *ob);
static void spool_refuse(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                         uint64_t now, vna_outbox_t *ob);
static void handle_spool(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                         uint64_t now, vna_outbox_t *ob);

/* Sign and queue one message. body_schema/body may be NULL for an empty body. */
static vna_status_t send_msg(vna_node_t *n, const vna_id_t *dst, const uint8_t *addr,
                             uint32_t addr_len, uint8_t type, uint16_t flags, uint64_t rpc,
                             const vna_schema_t *body_schema, const void *body, uint64_t now,
                             vna_outbox_t *ob)
{
    if (!ob || addr_len > VNA_ADDR_MAX) return VNA_ERR_ARG;
    uint32_t xo = n->xform ? n->xform->overhead : 0;
    if (ob->n >= VNA_OUTBOX_MAX || ob->cap - ob->used < VNA_MSG_WIRE_MAX + xo) {
        ob->dropped++;
        n->dropped_out++;
        return VNA_ERR_SPACE;
    }
    vna_msg_t *m = &n->out;
    m->type = type;
    m->flags = flags;
    m->features = n->cfg.ubh ? VNA_FEAT_UBH168 : 0;
    if (dst)
        m->dst = *dst;
    else
        vna_zero(&m->dst, sizeof m->dst);
    m->seq = ++n->seq;
    m->ts = now;
    m->rpc = rpc;
    m->body_len = 0;
    if (body_schema) {
        int32_t bl = vna_schema_pack(body_schema, body, m->body, VNA_BODY_MAX, true);
        if (bl < 0) return VNA_ERR_ARG;
        m->body_len = (uint16_t) bl;
    }
    bool ubh = false;
    if (n->cfg.ubh && dst) { /* UBH-168 once the peer has said it speaks it; plain otherwise */
        const vna_contact_t *c = vna_rt_find(&n->rt, dst);
        ubh = c && (c->flags & VNA_CF_UBH);
    }
    uint8_t rnd[32];
    vna_drbg_gen(&n->rng, rnd, 32);
    int32_t len = vna_msg_seal(m, n->idn, rnd, ubh, ob->buf + ob->used, ob->cap - ob->used);
    if (len < 0) return VNA_ERR_SPACE;
    /* per-frame transform hook, outermost layer (identity when none) */
    len = vna_xform_apply(n->xform, true, addr, addr_len, ob->buf + ob->used, (uint32_t) len,
                          ob->cap - ob->used, n->xbuf, sizeof n->xbuf);
    if (len < 0) return VNA_ERR_CRYPTO;
    vna_out_ent_t *e = &ob->e[ob->n++];
    e->off = ob->used;
    e->len = (uint32_t) len;
    vna_copy(e->addr, addr, addr_len);
    e->addr_len = (uint8_t) addr_len;
    ob->used += (uint32_t) len;
    n->sent++;
    return VNA_OK;
}

static vna_status_t send_to_contact(vna_node_t *n, const vna_id_t *id, uint8_t type, uint16_t flags,
                                    uint64_t rpc, const vna_schema_t *s, const void *body,
                                    uint64_t now, vna_outbox_t *ob)
{
    const vna_contact_t *c = vna_rt_find(&n->rt, id);
    if (!c) return VNA_ERR_ARG;
    return send_msg(n, id, c->addr, c->addr_len, type, flags, rpc, s, body, now, ob);
}

/* ---- record store ---- */
static vna_store_slot_t *store_find(const vna_node_t *n, const vna_id_t *key, uint64_t now)
{
    for (uint32_t i = 0; i < n->store_cap; i++) {
        vna_store_slot_t *s = &n->store[i];
        if (s->used && s->expires > now && vna_id_eq(&s->key, key)) return s;
    }
    return 0;
}

const vna_store_slot_t *vna_node_store_find(const vna_node_t *n, const vna_id_t *key)
{
    for (uint32_t i = 0; i < n->store_cap; i++)
        if (n->store[i].used && vna_id_eq(&n->store[i].key, key)) return &n->store[i];
    return 0;
}

uint32_t vna_node_store_count(const vna_node_t *n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n->store_cap; i++) c += n->store[i].used ? 1u : 0u;
    return c;
}

/* Keep an already verified record (n->rec holds its parse). */
static vna_status_t store_put(vna_node_t *n, const uint8_t *bytes, uint32_t len, uint64_t now)
{
    const vna_rec_t *r = &n->rec;
    vna_store_slot_t *slot = 0;
    for (uint32_t i = 0; i < n->store_cap; i++) {
        vna_store_slot_t *s = &n->store[i];
        if (s->used && vna_id_eq(&s->key, &r->key) && vna_id_eq(&s->publisher, &r->publisher)) {
            if (s->created > r->created) return VNA_ERR_REPLAY; /* never roll back */
            slot = s;
            break;
        }
    }
    if (!slot)
        for (uint32_t i = 0; i < n->store_cap; i++)
            if (!n->store[i].used || n->store[i].expires <= now) {
                slot = &n->store[i];
                break;
            }
    if (!slot || len > VNA_REC_MAX) return VNA_ERR_SPACE;
    vna_copy(slot->rec, bytes, len);
    slot->len = (uint16_t) len;
    slot->key = r->key;
    slot->publisher = r->publisher;
    slot->created = r->created;
    slot->expires = r->expires;
    slot->republish_at = n->cfg.republish_ms ? now + n->cfg.republish_ms : UINT64_MAX;
    slot->used = true;
    return VNA_OK;
}

static uint32_t records_from(const vna_node_t *n, const vna_id_t *pub)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n->store_cap; i++)
        if (n->store[i].used && vna_id_eq(&n->store[i].publisher, pub)) c++;
    return c;
}

/* ---- lookups ---- */
static void finalize_lookup(vna_node_t *n, uint32_t li, uint64_t now, vna_outbox_t *ob);

static void pump_lookup(vna_node_t *n, uint32_t li, uint64_t now, vna_outbox_t *ob)
{
    vna_node_lookup_t *L = &n->lks[li];
    if (!L->used) return;
    int32_t idx;
    while ((idx = vna_lookup_next(&L->lk)) >= 0) {
        vna_pending_t *p = pend_alloc(n);
        if (!p) break;
        const vna_cand_t *c = &L->lk.c[idx];
        vna_b_key_t body;
        body.key = L->lk.target;
        uint64_t rpc = new_rpc(n);
        uint8_t type = L->lk.mode == VNA_LK_FIND_VALUE ? VNA_MSG_FIND_VALUE : VNA_MSG_FIND_NODE;
        if (send_msg(n, &c->id, c->addr, c->addr_len, type, 0, rpc, &vna_b_key_schema, &body, now,
                     ob) != VNA_OK)
            break;
        p->kind = VNA_P_LOOKUP;
        p->expect = type == VNA_MSG_FIND_VALUE ? VNA_MSG_VALUE : VNA_MSG_NODES;
        p->lookup = (uint8_t) li;
        p->rpc = rpc;
        p->peer = c->id;
        p->deadline_ms = now + n->cfg.rpc_timeout_ms;
        vna_lookup_sent(&L->lk, idx, rpc, p->deadline_ms);
    }
    finalize_lookup(n, li, now, ob);
}

static void finalize_lookup(vna_node_t *n, uint32_t li, uint64_t now, vna_outbox_t *ob)
{
    static vna_cand_t res[VNA_KAD_K];
    vna_node_lookup_t *L = &n->lks[li];
    if (!L->used || L->reported || !vna_lookup_done(&L->lk)) return;
    L->reported = true;
    if (!L->store_after) return;
    uint32_t k = vna_lookup_result(&L->lk, VNA_KAD_K, res);
    for (uint32_t i = 0; i < k; i++) {
        vna_pending_t *p = pend_alloc(n);
        if (!p) break;
        uint64_t rpc = new_rpc(n);
        vna_b_rec_t *b = &n->brec;
        vna_copy(b->rec, L->value, L->value_len);
        b->len = L->value_len;
        if (send_msg(n, &res[i].id, res[i].addr, res[i].addr_len, VNA_MSG_STORE, 0, rpc,
                     &vna_b_rec_schema, b, now, ob) != VNA_OK)
            break;
        p->kind = VNA_P_STORE;
        p->expect = VNA_MSG_STORE_ACK;
        p->lookup = (uint8_t) li;
        p->rpc = rpc;
        p->peer = res[i].id;
        p->deadline_ms = now + n->cfg.rpc_timeout_ms;
        L->stores_sent++;
    }
}

static int32_t lookup_start(vna_node_t *n, const vna_id_t *target, vna_lk_mode_t mode,
                            bool store_after, const uint8_t *value, uint32_t vlen, uint64_t now,
                            vna_outbox_t *ob)
{
    for (uint32_t i = 0; i < VNA_NODE_LOOKUPS; i++) {
        vna_node_lookup_t *L = &n->lks[i];
        if (L->used) continue;
        L->used = true;
        L->store_after = store_after;
        L->reported = false;
        L->stores_sent = L->stores_acked = 0;
        L->value_len = 0;
        if (value && vlen <= VNA_REC_MAX) {
            vna_copy(L->value, value, vlen);
            L->value_len = (uint16_t) vlen;
        }
        vna_lookup_init(&L->lk, &n->rt, target, mode, n->cfg.paths);
        pump_lookup(n, i, now, ob);
        return (int32_t) i;
    }
    return VNA_ERR_SPACE;
}

int32_t vna_node_lookup(vna_node_t *n, const vna_id_t *target, vna_lk_mode_t mode, uint64_t now,
                        vna_outbox_t *ob)
{
    if (!n || !target) return VNA_ERR_ARG;
    return lookup_start(n, target, mode, false, 0, 0, now, ob);
}

const vna_node_lookup_t *vna_node_lookup_get(const vna_node_t *n, int32_t slot)
{
    if (slot < 0 || (uint32_t) slot >= VNA_NODE_LOOKUPS || !n->lks[slot].used) return 0;
    return &n->lks[slot];
}

bool vna_node_lookup_done(vna_node_t *n, int32_t slot)
{
    if (slot < 0 || (uint32_t) slot >= VNA_NODE_LOOKUPS || !n->lks[slot].used) return true;
    return vna_lookup_done(&n->lks[slot].lk);
}

void vna_node_lookup_release(vna_node_t *n, int32_t slot)
{
    if (slot < 0 || (uint32_t) slot >= VNA_NODE_LOOKUPS) return;
    n->lks[slot].used = false;
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++) /* late answers become unexpected */
        if ((n->pend[i].kind == VNA_P_LOOKUP || n->pend[i].kind == VNA_P_STORE) &&
            n->pend[i].lookup == (uint8_t) slot)
            n->pend[i].kind = VNA_P_FREE;
}

int32_t vna_node_publish(vna_node_t *n, const uint8_t *rec, uint32_t len, uint64_t now,
                         vna_outbox_t *ob)
{
    if (!n || !rec || len > VNA_REC_MAX) return VNA_ERR_ARG;
    vna_status_t st = vna_rec_verify(&n->v, rec, len, now, n->cfg.ts_window_ms, &n->rec);
    if (st != VNA_OK) return st;
    if (!vna_id_eq(&n->rec.publisher, &n->idn->id)) return VNA_ERR_BINDING;
    vna_id_t key = n->rec.key;
    (void) store_put(n, rec, len, now); /* we hold our own record too */
    return lookup_start(n, &key, VNA_LK_FIND_NODE, true, rec, len, now, ob);
}

int32_t vna_node_announce(vna_node_t *n, const vna_id_t *root, const vna_provider_t *p,
                          uint64_t now, uint64_t ttl_ms, vna_outbox_t *ob)
{
    static uint8_t buf[VNA_REC_MAX];
    if (!n || !root || !p) return VNA_ERR_ARG;
    if (p->visibility != VNA_VIS_PUBLIC) return VNA_ERR_DENIED; /* private: never announced */
    vna_rec_t *r = &n->rec;
    r->rtype = VNA_REC_PROVIDER;
    r->key = *root;
    r->created = now;
    r->expires = now + ttl_ms;
    int32_t pl = vna_schema_pack(&vna_provider_schema, p, r->payload, VNA_PAYLOAD_MAX, true);
    if (pl < 0) return VNA_ERR_ARG;
    r->payload_len = (uint16_t) pl;
    uint8_t rnd[32];
    vna_drbg_gen(&n->rng, rnd, 32);
    int32_t len = vna_rec_seal(r, n->idn, rnd, buf, sizeof buf);
    if (len < 0) return VNA_ERR_ARG;
    return vna_node_publish(n, buf, (uint32_t) len, now, ob);
}

int32_t vna_node_publish_agreement(vna_node_t *n, const vna_agreement_t *a, uint64_t now,
                                   uint64_t ttl_ms, vna_outbox_t *ob)
{
    static uint8_t buf[VNA_REC_MAX];
    if (!n || !a || !vna_id_eq(&a->owner, &n->idn->id)) return VNA_ERR_ARG;
    vna_rec_t *r = &n->rec;
    r->rtype = VNA_REC_AGREEMENT;
    vna_agree_key(&n->idn->id, &r->key);
    r->created = now;
    r->expires = now + ttl_ms;
    int32_t pl = vna_agree_encode(a, r->payload, VNA_PAYLOAD_MAX);
    if (pl < 0) return VNA_ERR_ARG;
    r->payload_len = (uint16_t) pl;
    uint8_t rnd[32];
    vna_drbg_gen(&n->rng, rnd, 32);
    int32_t len = vna_rec_seal(r, n->idn, rnd, buf, sizeof buf);
    if (len < 0) return VNA_ERR_ARG;
    return vna_node_publish(n, buf, (uint32_t) len, now, ob);
}

/* ---- contacts, eviction ---- */
static void learn(vna_node_t *n, const vna_id_t *id, const uint8_t *addr, uint32_t addr_len,
                  uint8_t flags, uint64_t now, vna_outbox_t *ob)
{
    vna_contact_t lrs;
    vna_rt_result_t r = vna_rt_seen(&n->rt, id, addr, addr_len, flags, now, &lrs);
    if (r == VNA_RT_ADDED && n->spool) /* a destination became reachable: retry now */
        for (uint32_t i = 0; i < n->spool->cap; i++)
            if (n->spool->e[i].used && vna_id_eq(&n->spool->e[i].dst, id))
                n->spool->e[i].next_try = now;
    if (r != VNA_RT_FULL) return;
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++) /* one eviction probe per contact */
        if (n->pend[i].kind == VNA_P_EVICT && vna_id_eq(&n->pend[i].peer, &lrs.id)) return;
    vna_pending_t *p = pend_alloc(n);
    if (!p) return;
    uint64_t rpc = new_rpc(n);
    if (send_msg(n, &lrs.id, lrs.addr, lrs.addr_len, VNA_MSG_PING, 0, rpc, 0, 0, now, ob) != VNA_OK)
        return;
    p->kind = VNA_P_EVICT;
    p->expect = VNA_MSG_PONG;
    p->rpc = rpc;
    p->peer = lrs.id;
    p->deadline_ms = now + n->cfg.rpc_timeout_ms;
    vna_zero(&p->newcomer, sizeof p->newcomer);
    p->newcomer.id = *id;
    vna_copy(p->newcomer.addr, addr, addr_len);
    p->newcomer.addr_len = (uint8_t) addr_len;
    p->newcomer.flags = flags;
}

vna_status_t vna_node_bootstrap(vna_node_t *n, const uint8_t *addr, uint32_t addr_len, uint64_t now,
                                vna_outbox_t *ob)
{
    vna_pending_t *p = pend_alloc(n);
    if (!p) return VNA_ERR_SPACE;
    uint64_t rpc = new_rpc(n);
    vna_status_t st = send_msg(n, 0, addr, addr_len, VNA_MSG_PING, 0, rpc, 0, 0, now, ob);
    if (st != VNA_OK) return st;
    p->kind = VNA_P_BOOT;
    p->expect = VNA_MSG_PONG;
    p->rpc = rpc;
    vna_zero(&p->peer, sizeof p->peer);
    p->deadline_ms = now + n->cfg.rpc_timeout_ms;
    return VNA_OK;
}

vna_status_t vna_node_ping(vna_node_t *n, const vna_id_t *id, uint64_t now, vna_outbox_t *ob)
{
    vna_pending_t *p = pend_alloc(n);
    if (!p) return VNA_ERR_SPACE;
    uint64_t rpc = new_rpc(n);
    vna_status_t st = send_to_contact(n, id, VNA_MSG_PING, 0, rpc, 0, 0, now, ob);
    if (st != VNA_OK) return st;
    p->kind = VNA_P_PING;
    p->expect = VNA_MSG_PONG;
    p->rpc = rpc;
    p->peer = *id;
    p->deadline_ms = now + n->cfg.rpc_timeout_ms;
    return VNA_OK;
}

uint32_t vna_node_refresh(vna_node_t *n, uint64_t now, uint64_t idle_ms, uint32_t max,
                          vna_outbox_t *ob)
{
    uint8_t idle[VNA_KAD_BUCKETS];
    uint32_t k = vna_rt_idle_buckets(&n->rt, now, idle_ms, idle, max);
    uint32_t started = 0;
    for (uint32_t i = 0; i < k; i++) {
        uint8_t rnd[32];
        vna_id_t t;
        vna_drbg_gen(&n->rng, rnd, 32);
        vna_rt_id_in_bucket(&n->idn->id, idle[i], rnd, &t);
        if (vna_node_lookup(n, &t, VNA_LK_FIND_NODE, now, ob) < 0) break;
        n->rt.bucket_activity_ms[idle[i]] = now;
        started++;
    }
    return started;
}

/* ---- Hackronomicon ---- */
static void push_event(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                       const vna_cmd_t *cmd, const vna_b_hk_t *hk, bool resp)
{
    if (n->ev_n >= VNA_NODE_EVENTS) return; /* queue full: drop (the sender will retry) */
    uint32_t i = n->ev_head + n->ev_n;
    if (i >= VNA_NODE_EVENTS) i -= VNA_NODE_EVENTS;
    vna_event_t *e = &n->ev[i];
    e->from = m->src;
    vna_copy(e->addr, addr, alen);
    e->addr_len = (uint8_t) alen;
    e->rpc = m->rpc;
    e->is_response = resp;
    e->cmd = *cmd;
    e->hk = *hk;
    e->spooled = false;
    e->sseq = e->created = 0;
    n->ev_n++;
}

bool vna_node_poll_event(vna_node_t *n, vna_event_t *ev)
{
    if (n->ev_n == 0) return false;
    *ev = n->ev[n->ev_head];
    n->ev_head = n->ev_head + 1u == VNA_NODE_EVENTS ? 0 : n->ev_head + 1u;
    n->ev_n--;
    return true;
}

static vna_status_t send_hk_raw(vna_node_t *n, const vna_id_t *dst, const uint8_t *addr,
                                uint32_t alen, const char *text, uint8_t truth, uint32_t ordinal,
                                bool is_response, uint64_t rpc, uint64_t now, vna_outbox_t *ob)
{
    vna_b_hk_t b;
    int32_t l = vna_hk_canonicalize(text, b.text, VNA_HK_MAX);
    if (l < 0) return VNA_ERR_HK;
    b.len = (uint16_t) l;
    b.truth = truth;
    b.ordinal = ordinal;
    return send_msg(n, dst, addr, alen, VNA_MSG_HK, is_response ? VNA_FL_RESPONSE : 0, rpc,
                    &vna_b_hk_schema, &b, now, ob);
}

vna_status_t vna_node_send_hk(vna_node_t *n, const vna_id_t *dst, const char *text, uint8_t truth,
                              uint32_t ordinal, bool is_response, uint64_t rpc, uint64_t now,
                              vna_outbox_t *ob)
{
    const vna_contact_t *c = vna_rt_find(&n->rt, dst);
    if (!c || !text || truth > SWARM_HK_UNKNOWN) return VNA_ERR_ARG;
    if (is_response)
        return send_hk_raw(n, dst, c->addr, c->addr_len, text, truth, ordinal, true, rpc, now, ob);
    vna_pending_t *p = pend_alloc(n);
    if (!p) return VNA_ERR_SPACE;
    uint64_t r = new_rpc(n);
    vna_status_t st =
        send_hk_raw(n, dst, c->addr, c->addr_len, text, truth, ordinal, false, r, now, ob);
    if (st != VNA_OK) return st;
    p->kind = VNA_P_HK;
    p->expect = VNA_MSG_HK;
    p->rpc = r;
    p->peer = *dst;
    p->deadline_ms = now + n->cfg.rpc_timeout_ms;
    return VNA_OK;
}

static void handle_hk_request(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                              uint64_t now, vna_outbox_t *ob)
{
    vna_b_hk_t hk;
    vna_cmd_t cmd;
    if (vna_schema_unpack(&vna_b_hk_schema, m->body, m->body_len, &hk, 0) < 0 ||
        vna_cmd_decode(hk.text, hk.len, &cmd) != VNA_OK) {
        n->hk_refused++; /* unknown verb or key: nothing outside the vocabulary is executed */
        (void) send_hk_raw(n, &m->src, addr, alen, "reject(route)", SWARM_HK_FALSE, hk.ordinal,
                           true, m->rpc, now, ob);
        return;
    }
    if (vna_cmd_requests_resource(&cmd)) {
        uint64_t amount = cmd.resource == VNA_RES_FILE ? 1u : cmd.units;
        vna_status_t st = vna_agree_check(
            n->agr, n->usage, &m->src, trust_of(n, &m->src), (vna_resource_t) cmd.resource,
            (cmd.has & VNA_CMD_ROOT) ? &cmd.root : 0, amount, (uint32_t) cmd.cycles, now, false);
        if (st != VNA_OK) {
            n->denied++;
            vna_cmd_t rej;
            uint8_t txt[VNA_HK_MAX + 1];
            vna_zero(&rej, sizeof rej);
            rej.verb = VNA_V_REJECT;
            rej.resource = cmd.resource;
            int32_t l = vna_cmd_encode(&rej, txt, VNA_HK_MAX);
            if (l > 0) {
                txt[l] = 0;
                (void) send_hk_raw(n, &m->src, addr, alen, (const char *) txt, SWARM_HK_FALSE,
                                   hk.ordinal, true, m->rpc, now, ob);
            }
            return;
        }
    }
    push_event(n, m, addr, alen, &cmd, &hk, false);
}

/* ---- trade receipts ---- */
void vna_node_set_rcpt_handler(vna_node_t *n, vna_rcpt_fn fn, void *ctx)
{
    if (!n) return;
    n->rcpt = fn;
    n->rcpt_ctx = ctx;
}

vna_status_t vna_node_send_rcpt(vna_node_t *n, const vna_id_t *dst, const uint8_t *rcpt,
                                uint32_t len, uint64_t now, vna_outbox_t *ob)
{
    if (!n || !dst || !rcpt || len == 0 || len > VNA_RCPT_WIRE_MAX) return VNA_ERR_ARG;
    const vna_contact_t *c = vna_rt_find(&n->rt, dst);
    if (!c) return VNA_ERR_ARG;
    vna_b_rec_t *b = &n->brec;
    vna_copy(b->rec, rcpt, len);
    b->len = (uint16_t) len;
    vna_status_t st = send_msg(n, dst, c->addr, c->addr_len, VNA_MSG_RCPT, 0, new_rpc(n),
                               &vna_b_rec_schema, b, now, ob);
    if (st == VNA_OK) n->rcpt_out++;
    return st;
}

static void handle_rcpt(vna_node_t *n, const vna_msg_t *m, uint64_t now)
{
    vna_b_rec_t *b = &n->brec;
    if (!n->rcpt || vna_schema_unpack(&vna_b_rec_schema, m->body, m->body_len, b, 0) < 0 ||
        b->len > VNA_RCPT_WIRE_MAX || n->rcpt(n->rcpt_ctx, &m->src, b->rec, b->len, now) != VNA_OK)
        n->rcpt_refused++;
    else
        n->rcpt_in++;
}

/* ---- receive ---- */
static bool is_response_type(const vna_msg_t *m)
{
    return m->type == VNA_MSG_PONG || m->type == VNA_MSG_NODES || m->type == VNA_MSG_VALUE ||
           m->type == VNA_MSG_STORE_ACK || m->type == VNA_MSG_SPOOL_ACK ||
           (m->type == VNA_MSG_HK && (m->flags & VNA_FL_RESPONSE));
}

static vna_status_t handle_response(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr,
                                    uint32_t alen, uint8_t cflags, uint64_t now, vna_outbox_t *ob)
{
    vna_pending_t *p = pend_find(n, m->rpc);
    bool type_ok =
        p && (p->expect == m->type || (p->expect == VNA_MSG_VALUE && m->type == VNA_MSG_NODES));
    if (!p || !type_ok || (!vna_id_is_zero(&p->peer) && !vna_id_eq(&p->peer, &m->src))) {
        n->unexpected++; /* unsolicited, wrong type, or signed by someone we did not ask */
        return VNA_ERR_UNEXPECTED;
    }
    learn(n, &m->src, addr, alen, cflags, now, ob);
    vna_pending_t P = *p;
    p->kind = VNA_P_FREE;
    switch (P.kind) {
    case VNA_P_BOOT: {
        vna_id_t self = n->idn->id;
        n->joined = true;
        (void) lookup_start(n, &self, VNA_LK_FIND_NODE, false, 0, 0, now, ob);
        return VNA_OK;
    }
    case VNA_P_SPOOL: {
        vna_b_spool_ack_t a;
        if (vna_schema_unpack(&vna_b_spool_ack_schema, m->body, m->body_len, &a, 0) < 0 ||
            a.sseq != P.sseq)
            return VNA_ERR_PARSE;
        spool_on_ack(n, &P.peer, a.sseq, a.status, now);
        return VNA_OK;
    }
    case VNA_P_EVICT:
        n->evict_kept++;
        return VNA_OK; /* the old contact answered: it stays */
    case VNA_P_PING:
        return VNA_OK;
    case VNA_P_STORE: {
        vna_b_ack_t a;
        if (vna_schema_unpack(&vna_b_ack_schema, m->body, m->body_len, &a, 0) < 0)
            return VNA_ERR_PARSE;
        if (a.status == 0 && P.lookup < VNA_NODE_LOOKUPS && n->lks[P.lookup].used)
            n->lks[P.lookup].stores_acked++;
        return VNA_OK;
    }
    case VNA_P_HK: {
        vna_b_hk_t hk;
        vna_cmd_t cmd;
        if (vna_schema_unpack(&vna_b_hk_schema, m->body, m->body_len, &hk, 0) < 0 ||
            vna_cmd_decode(hk.text, hk.len, &cmd) != VNA_OK)
            return VNA_ERR_HK;
        push_event(n, m, addr, alen, &cmd, &hk, true);
        return VNA_OK;
    }
    case VNA_P_LOOKUP: {
        if (P.lookup >= VNA_NODE_LOOKUPS || !n->lks[P.lookup].used) return VNA_ERR_UNEXPECTED;
        vna_node_lookup_t *L = &n->lks[P.lookup];
        if (m->type == VNA_MSG_NODES) {
            static vna_contact_t cs[VNA_K];
            vna_b_nodes_t *b = &n->nodes;
            if (vna_schema_unpack(&vna_b_nodes_schema, m->body, m->body_len, b, 0) < 0) {
                (void) vna_lookup_on_fail(&L->lk, &m->src, m->rpc);
                pump_lookup(n, P.lookup, now, ob);
                return VNA_ERR_PARSE;
            }
            for (uint16_t i = 0; i < b->n; i++) {
                vna_zero(&cs[i], sizeof cs[i]);
                cs[i].id = b->c[i].id;
                vna_copy(cs[i].addr, b->c[i].addr, b->c[i].addr_len);
                cs[i].addr_len = (uint8_t) b->c[i].addr_len;
            }
            (void) vna_lookup_on_nodes(&L->lk, &m->src, m->rpc, cs, b->n);
        } else { /* VALUE: accept only a record that verifies and matches the key */
            vna_b_rec_t *b = &n->brec;
            bool ok = vna_schema_unpack(&vna_b_rec_schema, m->body, m->body_len, b, 0) >= 0 &&
                      vna_rec_verify(&n->v, b->rec, b->len, now, n->cfg.ts_window_ms, &n->rec) ==
                          VNA_OK &&
                      vna_id_eq(&n->rec.key, &L->lk.target);
            if (ok) {
                vna_copy(L->value, b->rec, b->len);
                L->value_len = b->len;
                (void) vna_lookup_on_value(&L->lk, &m->src, m->rpc);
            } else {
                (void) vna_lookup_on_fail(&L->lk, &m->src, m->rpc);
            }
        }
        pump_lookup(n, P.lookup, now, ob);
        return VNA_OK;
    }
    default:
        return VNA_ERR_UNEXPECTED;
    }
}

static void reply_nodes(vna_node_t *n, const vna_msg_t *m, const vna_id_t *target,
                        const uint8_t *addr, uint32_t alen, uint64_t now, vna_outbox_t *ob)
{
    static vna_contact_t cs[VNA_K];
    vna_b_nodes_t *b = &n->nodes;
    uint32_t k = vna_rt_closest(&n->rt, target, VNA_K, &m->src, cs);
    b->n = (uint16_t) k;
    for (uint32_t i = 0; i < k; i++) {
        b->c[i].id = cs[i].id;
        b->c[i].addr_len = cs[i].addr_len;
        vna_copy(b->c[i].addr, cs[i].addr, cs[i].addr_len);
    }
    (void) send_msg(n, &m->src, addr, alen, VNA_MSG_NODES, 0, m->rpc, &vna_b_nodes_schema, b, now,
                    ob);
}

static void handle_store(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                         uint64_t now, vna_outbox_t *ob)
{
    vna_b_ack_t ack;
    ack.status = 1;
    vna_b_rec_t *b = &n->brec;
    if (eff_degree(n) >= VNA_DEG_STORE &&
        vna_schema_unpack(&vna_b_rec_schema, m->body, m->body_len, b, 0) >= 0 &&
        vna_rec_verify(&n->v, b->rec, b->len, now, n->cfg.ts_window_ms, &n->rec) == VNA_OK &&
        n->rec.expires - now <= n->cfg.rec_max_ttl_ms) {
        bool quota;
        if (n->agr)
            quota = vna_agree_check(n->agr, n->usage, &m->src, trust_of(n, &m->src), VNA_RES_RECORD,
                                    0, 1, 0, now, true) == VNA_OK;
        else
            quota = records_from(n, &n->rec.publisher) < n->cfg.max_records_per_peer ||
                    vna_node_store_find(n, &n->rec.key) != 0;
        if (quota && store_put(n, b->rec, b->len, now) == VNA_OK) ack.status = 0;
    }
    if (ack.status == 0)
        n->stores_ok++;
    else
        n->stores_refused++;
    (void) send_msg(n, &m->src, addr, alen, VNA_MSG_STORE_ACK, 0, m->rpc, &vna_b_ack_schema, &ack,
                    now, ob);
}

vna_status_t vna_node_handle(vna_node_t *n, const uint8_t *buf, uint32_t len, const uint8_t *addr,
                             uint32_t addr_len, uint64_t now, vna_outbox_t *ob)
{
    if (!n || !buf || !addr || addr_len > VNA_ADDR_MAX) return VNA_ERR_ARG;
    vna_msg_t *m = &n->in;
    bool ubh = false;
    if (n->xform && n->xform->open) { /* per-frame transform hook: undo it first */
        int32_t r =
            n->xform->open(n->xform->ctx, addr, addr_len, buf, len, n->xbuf, sizeof n->xbuf);
        if (r < 0 || (uint32_t) r > sizeof n->xbuf) {
            n->xform_refused++;
            return VNA_ERR_CRYPTO;
        }
        buf = n->xbuf;
        len = (uint32_t) r;
    }
    vna_status_t st = vna_msg_open(&n->v, buf, len, now, m, &ubh);
    if (st != VNA_OK) return st;
    n->received++;
    uint8_t cflags = (m->features & VNA_FEAT_UBH168) ? VNA_CF_UBH : 0;
    if (is_response_type(m)) return handle_response(n, m, addr, addr_len, cflags, now, ob);

    /* a request */
    if (eff_degree(n) < VNA_DEG_ROUTE) {
        n->denied++;
        return VNA_ERR_DENIED; /* degree OFF: share nothing, answer nothing */
    }
    if (n->agr && vna_agree_check(n->agr, n->usage, &m->src, trust_of(n, &m->src), VNA_RES_ROUTE, 0,
                                  0, 0, now, false) != VNA_OK) {
        n->denied++;
        /* An HK request gets one signed reject(route) so a negotiating agent
         * stops; DHT requests are dropped silently. The sender's signature
         * and sequence were verified, so this cannot be used to reflect. */
        if (m->type == VNA_MSG_HK && !(m->flags & VNA_FL_RESPONSE)) {
            vna_b_hk_t hk;
            uint32_t ord = vna_schema_unpack(&vna_b_hk_schema, m->body, m->body_len, &hk, 0) >= 0
                               ? hk.ordinal
                               : 0;
            (void) send_hk_raw(n, &m->src, addr, addr_len, "reject(route)", SWARM_HK_FALSE, ord,
                               true, m->rpc, now, ob);
        }
        if (m->type == VNA_MSG_SPOOL) spool_refuse(n, m, addr, addr_len, now, ob);
        return VNA_ERR_DENIED; /* blocklisted or outside the audience */
    }
    learn(n, &m->src, addr, addr_len, cflags, now, ob);
    switch (m->type) {
    case VNA_MSG_PING:
        (void) send_msg(n, &m->src, addr, addr_len, VNA_MSG_PONG, 0, m->rpc, 0, 0, now, ob);
        return VNA_OK;
    case VNA_MSG_FIND_NODE:
    case VNA_MSG_FIND_VALUE: {
        vna_b_key_t k;
        if (vna_schema_unpack(&vna_b_key_schema, m->body, m->body_len, &k, 0) < 0)
            return VNA_ERR_PARSE;
        const vna_store_slot_t *s = m->type == VNA_MSG_FIND_VALUE ? store_find(n, &k.key, now) : 0;
        if (s) {
            vna_b_rec_t *b = &n->brec;
            vna_copy(b->rec, s->rec, s->len);
            b->len = s->len;
            (void) send_msg(n, &m->src, addr, addr_len, VNA_MSG_VALUE, 0, m->rpc, &vna_b_rec_schema,
                            b, now, ob);
        } else {
            reply_nodes(n, m, &k.key, addr, addr_len, now, ob);
        }
        return VNA_OK;
    }
    case VNA_MSG_STORE:
        handle_store(n, m, addr, addr_len, now, ob);
        return VNA_OK;
    case VNA_MSG_HK:
        handle_hk_request(n, m, addr, addr_len, now, ob);
        return VNA_OK;
    case VNA_MSG_SPOOL:
        handle_spool(n, m, addr, addr_len, now, ob);
        return VNA_OK;
    case VNA_MSG_RCPT:
        handle_rcpt(n, m, now);
        return VNA_OK;
    default:
        return VNA_ERR_PARSE;
    }
}

void vna_node_tick(vna_node_t *n, uint64_t now, vna_outbox_t *ob)
{
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++) {
        vna_pending_t *p = &n->pend[i];
        if (p->kind == VNA_P_FREE || p->deadline_ms > now) continue;
        vna_pending_t P = *p;
        p->kind = VNA_P_FREE;
        if (P.kind == VNA_P_LOOKUP && P.lookup < VNA_NODE_LOOKUPS && n->lks[P.lookup].used) {
            (void) vna_lookup_on_fail(&n->lks[P.lookup].lk, &P.peer, P.rpc);
        } else if (P.kind == VNA_P_SPOOL) {
            spool_on_timeout(n, &P.peer, P.sseq, now);
        } else if (P.kind == VNA_P_EVICT) { /* silent: evict it, admit the newcomer */
            if (vna_rt_remove(&n->rt, &P.peer)) n->evictions++;
            vna_contact_t dummy;
            (void) vna_rt_seen(&n->rt, &P.newcomer.id, P.newcomer.addr, P.newcomer.addr_len,
                               P.newcomer.flags, now, &dummy);
        }
    }
    for (uint32_t li = 0; li < VNA_NODE_LOOKUPS; li++)
        if (n->lks[li].used) pump_lookup(n, li, now, ob);
    spool_pump(n, now, ob);
    for (uint32_t i = 0; i < n->store_cap; i++) {
        vna_store_slot_t *s = &n->store[i];
        if (!s->used) continue;
        if (s->expires <= now) {
            s->used = false; /* expired records are dropped, never served */
            continue;
        }
        if (s->republish_at <= now) {
            static vna_contact_t cs[VNA_K];
            uint32_t k = vna_rt_closest(&n->rt, &s->key, VNA_K, 0, cs);
            for (uint32_t j = 0; j < k; j++) {
                vna_b_rec_t *b = &n->brec;
                vna_copy(b->rec, s->rec, s->len);
                b->len = s->len;
                vna_pending_t *p = pend_alloc(n);
                if (!p) break;
                uint64_t rpc = new_rpc(n);
                if (send_msg(n, &cs[j].id, cs[j].addr, cs[j].addr_len, VNA_MSG_STORE, 0, rpc,
                             &vna_b_rec_schema, b, now, ob) != VNA_OK)
                    break;
                p->kind = VNA_P_STORE;
                p->expect = VNA_MSG_STORE_ACK;
                p->lookup = 0xFF; /* republish: not tied to a lookup */
                p->rpc = rpc;
                p->peer = cs[j].id;
                p->deadline_ms = now + n->cfg.rpc_timeout_ms;
            }
            s->republish_at = now + n->cfg.republish_ms;
        }
    }
}

/* ===================================================================== */
/* seeding: LAN discovery and cached peers                                */
/* ===================================================================== */
vna_status_t vna_node_seed_verified(vna_node_t *n, const vna_noderec_t *r, uint64_t now,
                                    vna_outbox_t *ob)
{
    if (!n || !r || r->naddr == 0) return VNA_ERR_ARG;
    if (vna_id_eq(&r->id, &n->idn->id)) return VNA_ERR_DST;
    if (r->flags & VNA_NR_GOODBYE) {
        (void) vna_rt_remove(&n->rt, &r->id);
        return VNA_OK;
    }
    uint8_t cf = (r->features & VNA_FEAT_UBH168) ? VNA_CF_UBH : 0;
    learn(n, &r->id, r->addr[0].a, r->addr[0].len, cf, now, ob);
    n->seeded++;
    if (!n->joined) { /* first contact: join exactly as after a bootstrap PONG */
        vna_id_t self = n->idn->id;
        if (lookup_start(n, &self, VNA_LK_FIND_NODE, false, 0, 0, now, ob) >= 0) n->joined = true;
    }
    return VNA_OK;
}

vna_status_t vna_node_seed_record(vna_node_t *n, const uint8_t *rec, uint32_t len, uint64_t now,
                                  vna_outbox_t *ob)
{
    static vna_noderec_t r; /* single-threaded scratch */
    if (!n || !rec) return VNA_ERR_ARG;
    vna_status_t st = vna_noderec_verify(&n->v, rec, len, now, n->cfg.ts_window_ms, &r);
    if (st != VNA_OK) return st;
    return vna_node_seed_verified(n, &r, now, ob);
}

void vna_node_set_xform(vna_node_t *n, const vna_xform_t *x)
{
    n->xform = x;
}

uint32_t vna_node_contacts(const vna_node_t *n, vna_contact_t *out, uint32_t max)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n->rt.cap && k < max; i++)
        if (n->rt.pool[i].in_use) out[k++] = n->rt.pool[i];
    return k;
}

/* ===================================================================== */
/* offline outbox                                                         */
/* ===================================================================== */
void vna_spool_init(vna_spool_t *s, vna_spool_ent_t *e, uint32_t cap, vna_spool_rx_t *rx,
                    uint32_t rx_cap, uint64_t next_sseq)
{
    vna_zero(s, sizeof *s);
    s->e = e;
    s->cap = e ? cap : 0;
    s->rx = rx;
    s->rx_cap = rx ? rx_cap : 0;
    s->next_sseq = next_sseq ? next_sseq : 1u;
    s->lookup_slot = -1;
    for (uint32_t i = 0; i < s->cap; i++) e[i].used = false;
    for (uint32_t i = 0; i < s->rx_cap; i++) rx[i].used = false;
}

uint32_t vna_spool_pending(const vna_spool_t *s, const vna_id_t *dst)
{
    uint32_t k = 0;
    if (!s) return 0;
    for (uint32_t i = 0; i < s->cap; i++)
        if (s->e[i].used && (!dst || vna_id_eq(&s->e[i].dst, dst))) k++;
    return k;
}

void vna_node_set_spool(vna_node_t *n, vna_spool_t *s)
{
    n->spool = s;
}

static uint64_t spool_backoff(const vna_node_t *n, uint8_t tries)
{
    uint8_t t = tries > 6 ? 6 : tries;
    return n->cfg.rpc_timeout_ms << t;
}

static vna_spool_ent_t *spool_find(vna_spool_t *S, const vna_id_t *dst, uint64_t sseq)
{
    for (uint32_t i = 0; i < S->cap; i++)
        if (S->e[i].used && S->e[i].sseq == sseq && vna_id_eq(&S->e[i].dst, dst)) return &S->e[i];
    return 0;
}

vna_status_t vna_node_spool_hk(vna_node_t *n, const vna_id_t *dst, const char *text, uint8_t truth,
                               uint32_t ordinal, uint64_t ttl_ms, uint64_t now, vna_outbox_t *ob,
                               uint64_t *sseq_out)
{
    if (!n || !dst || !text || truth > SWARM_HK_UNKNOWN || ttl_ms == 0) return VNA_ERR_ARG;
    vna_spool_t *S = n->spool;
    if (!S) return VNA_ERR_STATE;
    if (vna_id_eq(dst, &n->idn->id)) return VNA_ERR_DST;
    vna_spool_ent_t *e = 0;
    for (uint32_t i = 0; i < S->cap && !e; i++)
        if (!S->e[i].used) e = &S->e[i];
    if (!e) return VNA_ERR_SPACE;
    vna_spool_item_t *it = &n->spi;
    vna_zero(it, sizeof *it);
    int32_t l = vna_hk_canonicalize(text, it->text, VNA_HK_MAX);
    if (l < 0) return VNA_ERR_HK;
    it->len = (uint16_t) l;
    it->magic = VNA_SPOOL_MAGIC;
    it->version = VNA_VERSION;
    it->src = n->idn->id;
    it->dst = *dst;
    it->sseq = S->next_sseq;
    it->created = now;
    it->expires = now + ttl_ms;
    it->truth = truth;
    it->ordinal = ordinal;
    int32_t sl = vna_schema_pack(&vna_spool_item_schema, it, e->item, VNA_SPOOL_ITEM_MAX, false);
    if (sl < 0) return VNA_ERR_ARG;
    uint8_t rnd[32];
    vna_drbg_gen(&n->rng, rnd, 32);
    vna_sign(n->idn->sk, VNA_CTX_SPOOL, e->item, (uint32_t) sl, rnd, it->sig);
    int32_t tl = vna_schema_pack(&vna_spool_item_schema, it, e->item, VNA_SPOOL_ITEM_MAX, true);
    if (tl < 0) return VNA_ERR_ARG;
    e->used = true;
    e->inflight = false;
    e->tries = 0;
    e->dst = *dst;
    e->sseq = S->next_sseq++;
    e->expires = it->expires;
    e->next_try = now;
    e->len = (uint16_t) tl;
    S->queued++;
    if (sseq_out) *sseq_out = e->sseq;
    if (ob) spool_pump(n, now, ob);
    return VNA_OK;
}

static void spool_on_ack(vna_node_t *n, const vna_id_t *peer, uint64_t sseq, uint8_t status,
                         uint64_t now)
{
    vna_spool_t *S = n->spool;
    if (!S) return;
    vna_spool_ent_t *e = spool_find(S, peer, sseq);
    if (!e || !e->inflight) return;
    e->inflight = false;
    if (status == VNA_SPOOL_ACK_OK || status == VNA_SPOOL_ACK_DUP) {
        e->used = false;
        S->delivered++;
    } else if (status == VNA_SPOOL_ACK_REFUSED) {
        e->used = false;
        S->refused++;
    } else { /* BUSY: try again shortly */
        e->next_try = now + n->cfg.rpc_timeout_ms;
    }
}

static void spool_on_timeout(vna_node_t *n, const vna_id_t *peer, uint64_t sseq, uint64_t now)
{
    vna_spool_t *S = n->spool;
    if (!S) return;
    vna_spool_ent_t *e = spool_find(S, peer, sseq);
    if (!e || !e->inflight) return;
    e->inflight = false;
    S->retries++;
    e->next_try = now + spool_backoff(n, e->tries);
}

static bool spool_is_head(const vna_spool_t *S, const vna_spool_ent_t *e)
{
    for (uint32_t i = 0; i < S->cap; i++) {
        const vna_spool_ent_t *o = &S->e[i];
        if (o != e && o->used && vna_id_eq(&o->dst, &e->dst) && o->sseq < e->sseq) return false;
    }
    return true;
}

static void spool_pump(vna_node_t *n, uint64_t now, vna_outbox_t *ob)
{
    vna_spool_t *S = n->spool;
    if (!S) return;
    if (S->lookup_slot >= 0 && vna_node_lookup_done(n, S->lookup_slot)) {
        vna_node_lookup_release(n, S->lookup_slot);
        S->lookup_slot = -1;
    }
    for (uint32_t i = 0; i < S->cap; i++) {
        vna_spool_ent_t *e = &S->e[i];
        if (e->used && !e->inflight && e->expires <= now) {
            e->used = false; /* past its lifetime: never delivered late */
            S->expired++;
        }
    }
    for (uint32_t i = 0; i < S->cap; i++) {
        vna_spool_ent_t *e = &S->e[i];
        if (!e->used || e->inflight || e->next_try > now || !spool_is_head(S, e)) continue;
        bool busy = false; /* one item in flight per destination */
        for (uint32_t j = 0; j < S->cap && !busy; j++)
            busy = S->e[j].used && S->e[j].inflight && vna_id_eq(&S->e[j].dst, &e->dst);
        if (busy) continue;
        const vna_contact_t *c = vna_rt_find(&n->rt, &e->dst);
        if (!c) { /* no route: look the destination up if we know anyone, else wait */
            if (n->rt.used > 0 && S->lookup_slot < 0) {
                int32_t slot = vna_node_lookup(n, &e->dst, VNA_LK_FIND_NODE, now, ob);
                if (slot >= 0) {
                    S->lookup_slot = slot;
                    if (e->tries < 255) e->tries++; /* back off: do not hammer the DHT */
                }
            }
            e->next_try = now + spool_backoff(n, e->tries);
            continue;
        }
        vna_pending_t *p = pend_alloc(n);
        if (!p) return;
        vna_b_rec_t *b = &n->brec;
        vna_copy(b->rec, e->item, e->len);
        b->len = e->len;
        uint64_t rpc = new_rpc(n);
        if (send_msg(n, &e->dst, c->addr, c->addr_len, VNA_MSG_SPOOL, 0, rpc, &vna_b_rec_schema, b,
                     now, ob) != VNA_OK)
            return;
        p->kind = VNA_P_SPOOL;
        p->expect = VNA_MSG_SPOOL_ACK;
        p->rpc = rpc;
        p->peer = e->dst;
        p->sseq = e->sseq;
        p->deadline_ms = now + n->cfg.rpc_timeout_ms;
        e->inflight = true;
        if (e->tries < 255) e->tries++;
    }
}

static void spool_ack(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                      uint64_t sseq, uint8_t status, uint64_t now, vna_outbox_t *ob)
{
    vna_b_spool_ack_t a;
    a.sseq = sseq;
    a.status = status;
    (void) send_msg(n, &m->src, addr, alen, VNA_MSG_SPOOL_ACK, 0, m->rpc, &vna_b_spool_ack_schema,
                    &a, now, ob);
}

/* Parse and authenticate the item inside a SPOOL message into n->spi. */
static vna_status_t spool_parse(vna_node_t *n, const vna_msg_t *m, uint64_t now)
{
    vna_b_rec_t *b = &n->brec;
    vna_spool_item_t *it = &n->spi;
    uint32_t sig_off = 0;
    if (vna_schema_unpack(&vna_b_rec_schema, m->body, m->body_len, b, 0) < 0 ||
        vna_schema_unpack(&vna_spool_item_schema, b->rec, b->len, it, &sig_off) < 0 ||
        it->version != VNA_VERSION)
        return VNA_ERR_PARSE;
    if (!vna_id_eq(&it->src, &m->src) || !vna_id_eq(&it->dst, &n->idn->id)) return VNA_ERR_DST;
    /* m->pk already binds to m->src (vna_msg_open), and the item names the same origin */
    if (!vna_verify(m->pk, VNA_CTX_SPOOL, b->rec, sig_off, it->sig)) return VNA_ERR_SIG;
    if (it->expires <= now) return VNA_ERR_EXPIRED;
    if (it->created > now + n->cfg.ts_window_ms) return VNA_ERR_STALE;
    return VNA_OK;
}

static void spool_refuse(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                         uint64_t now, vna_outbox_t *ob)
{
    if (spool_parse(n, m, now) != VNA_OK) return;
    if (n->spool) n->spool->rx_refused++;
    spool_ack(n, m, addr, alen, n->spi.sseq, VNA_SPOOL_ACK_REFUSED, now, ob);
}

static void handle_spool(vna_node_t *n, const vna_msg_t *m, const uint8_t *addr, uint32_t alen,
                         uint64_t now, vna_outbox_t *ob)
{
    vna_spool_t *S = n->spool;
    vna_status_t st = spool_parse(n, m, now);
    if (st == VNA_ERR_PARSE || st == VNA_ERR_SIG || st == VNA_ERR_DST) return; /* not an item */
    const vna_spool_item_t *it = &n->spi;
    uint64_t sseq = it->sseq;
    if (st != VNA_OK || !S || S->rx_cap == 0) { /* expired, or nowhere to dedupe: refuse */
        if (S) S->rx_refused++;
        spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_REFUSED, now, ob);
        return;
    }
    vna_spool_rx_t *rx = 0, *oldest = 0;
    for (uint32_t i = 0; i < S->rx_cap && !rx; i++)
        if (S->rx[i].used && vna_id_eq(&S->rx[i].peer, &m->src)) rx = &S->rx[i];
    for (uint32_t i = 0; i < S->rx_cap && !rx; i++) { /* a free slot, else least recently seen */
        vna_spool_rx_t *r = &S->rx[i];
        if (!r->used) {
            oldest = r;
            break;
        }
        if (!oldest || r->seen_ms < oldest->seen_ms) oldest = r;
    }
    if (rx && sseq <= rx->last) { /* already surfaced: acknowledge so the sender stops */
        S->rx_dup++;
        rx->seen_ms = now;
        spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_DUP, now, ob);
        return;
    }
    vna_cmd_t cmd;
    vna_b_hk_t hk;
    if (vna_cmd_decode(it->text, it->len, &cmd) != VNA_OK) {
        n->hk_refused++;
        S->rx_refused++;
        spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_REFUSED, now, ob);
        return;
    }
    if (vna_cmd_requests_resource(&cmd)) {
        uint64_t amount = cmd.resource == VNA_RES_FILE ? 1u : cmd.units;
        if (vna_agree_check(n->agr, n->usage, &m->src, trust_of(n, &m->src),
                            (vna_resource_t) cmd.resource, (cmd.has & VNA_CMD_ROOT) ? &cmd.root : 0,
                            amount, (uint32_t) cmd.cycles, now, false) != VNA_OK) {
            n->denied++;
            S->rx_refused++;
            spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_REFUSED, now, ob);
            return;
        }
    }
    if (n->ev_n >= VNA_NODE_EVENTS) { /* cannot surface it now: do not record it */
        S->rx_busy++;
        spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_BUSY, now, ob);
        return;
    }
    hk.truth = it->truth;
    hk.ordinal = it->ordinal;
    hk.len = it->len;
    vna_copy(hk.text, it->text, it->len);
    uint64_t created = it->created;
    push_event(n, m, addr, alen, &cmd, &hk, false);
    uint32_t last = n->ev_head + n->ev_n - 1u;
    if (last >= VNA_NODE_EVENTS) last -= VNA_NODE_EVENTS;
    n->ev[last].spooled = true;
    n->ev[last].sseq = sseq;
    n->ev[last].created = created;
    if (!rx) {
        rx = oldest; /* table full: forget the least recently seen origin */
        rx->used = true;
        rx->peer = m->src;
    }
    rx->last = sseq;
    rx->seen_ms = now;
    S->rx_accepted++;
    spool_ack(n, m, addr, alen, sseq, VNA_SPOOL_ACK_OK, now, ob);
}
