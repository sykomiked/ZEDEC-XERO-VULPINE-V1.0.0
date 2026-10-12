/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_vna.c — AIPI over Vinea. See aipi_vna.h (V1-V5). */
#include "aipi_vna.h"

static void id_of(const uint8_t b[32], vna_id_t *id)
{
    vna_copy(id->b, b, 32);
}

static int32_t pending_job(const aipi_ctx_t *ai, const ipfsn_cid_t *call)
{
    for (uint32_t i = 0; i < ai->job_cap; i++)
        if (ai->jobs[i].status == AIPI_JOB_PENDING && ipfsn_cid_equal(&ai->jobs[i].call_cid, call))
            return (int32_t) i;
    return -1;
}

static aipi_vna_call_t *call_find(aipi_vna_t *v, const ipfsn_cid_t *call)
{
    for (uint32_t i = 0; i < AIPI_VNA_CALLS; i++)
        if (v->calls[i].used && ipfsn_cid_equal(&v->calls[i].call, call)) return &v->calls[i];
    return 0;
}

static bool asked(const aipi_vna_call_t *c, const vna_id_t *id)
{
    for (uint32_t i = 0; i < c->nasked; i++)
        if (vna_id_eq(&c->asked[i], id)) return true;
    return false;
}

/* Send the call to `id` (at addr when it is not a contact yet). */
static bool ask(aipi_vna_t *v, aipi_vna_call_t *c, const vna_id_t *id, const uint8_t *addr,
                uint32_t alen)
{
    if (c->nasked >= c->want || c->nasked >= AIPI_CLAIMS_MAX || asked(c, id) ||
        vna_id_eq(id, &v->node->idn->id))
        return false;
    if (vna_node_send_aipi_at(v->node, id, addr, alen, VNA_AIPI_CALL, c->bounty, c->rec,
                              c->rec_len, v->now, v->ob) != VNA_OK)
        return false;
    c->asked[c->nasked++] = *id;
    v->calls_out++;
    return true;
}

/* ---- AIPI mesh hooks (aipi_mesh_ops_t) ---- */
static int hk_broadcast(void *ctx, const ipfsn_cid_t *call_cid, const uint8_t *rec, uint32_t len,
                        uint64_t bounty, uint32_t want)
{
    aipi_vna_t *v = (aipi_vna_t *) ctx;
    ipfsn_cid_t fn_cid;
    const uint8_t *in;
    uint32_t in_len;
    if (len > AIPI_CALL_ENC_MAX || want == 0 ||
        aipi_call_decode(rec, len, &fn_cid, &in, &in_len) != AIPI_OK)
        return AIPI_ERR_ARG;
    aipi_vna_call_t *c = call_find(v, call_cid);
    for (uint32_t i = 0; i < AIPI_VNA_CALLS && !c; i++)
        if (!v->calls[i].used) c = &v->calls[i];
    if (!c) return AIPI_ERR_FULL;
    vna_zero(c, sizeof *c);
    c->used = true;
    c->call = *call_cid;
    c->lookup = -1;
    c->bounty = bounty;
    c->want = want > AIPI_CLAIMS_MAX ? AIPI_CLAIMS_MAX : want;
    c->deadline = v->now + v->call_ttl;
    c->rec_len = (uint16_t) len;
    vna_copy(c->rec, rec, len);
    /* V1: the owner's executors first */
    for (uint32_t i = 0; i < v->nexec; i++)
        if (vna_rt_find(&v->node->rt, &v->exec[i])) (void) ask(v, c, &v->exec[i], 0, 0);
    /* then the provider the DHT knows */
    if (c->nasked < c->want && v->node->rt.used > 0) { /* nobody to ask: no lookup */
        uint8_t key[32];
        vna_id_t k;
        if (aipi_provider_key(&fn_cid, key) == AIPI_OK) {
            id_of(key, &k);
            int32_t slot = vna_node_lookup(v->node, &k, VNA_LK_FIND_VALUE, v->now, v->ob);
            if (slot >= 0) {
                c->lookup = slot;
                v->lookups++;
            }
        }
    }
    if (c->nasked == 0 && c->lookup < 0) {
        c->used = false;
        return AIPI_ERR_TRANSPORT;
    }
    return 0;
}

static int hk_reserve(void *ctx, uint64_t amount)
{
    aipi_vna_t *v = (aipi_vna_t *) ctx;
    if (v->econ.reserve) return v->econ.reserve(v->econ.ctx, amount);
    return amount == 0 ? 0 : -1; /* V3: fail closed */
}

static void hk_refund(void *ctx, uint64_t amount)
{
    aipi_vna_t *v = (aipi_vna_t *) ctx;
    if (v->econ.refund) v->econ.refund(v->econ.ctx, amount);
}

static void hk_pay(void *ctx, const uint8_t executor[32], uint64_t amount)
{
    aipi_vna_t *v = (aipi_vna_t *) ctx;
    vna_id_t ex;
    id_of(executor, &ex);
    if (!v->cur || !v->econ.pay || v->econ.pay(v->econ.ctx, &ex, amount, &v->cur->call) != 0) {
        v->pay_failed++;
        return;
    }
    uint8_t cid[IPFSN_CID_BIN_MAX];
    int cl = ipfsn_cid_encode(&v->cur->call, cid, sizeof cid);
    if (cl <= 0 || vna_node_send_aipi(v->node, &ex, VNA_AIPI_PAID, amount, cid, (uint32_t) cl,
                                      v->now, v->ob) != VNA_OK) {
        v->pay_failed++;
        return;
    }
    v->paid_out++;
}

/* ---- the node's AIPI handler ---- */
static vna_status_t on_call(aipi_vna_t *v, const vna_id_t *from, const vna_b_aipi_t *b)
{
    ipfsn_cid_t fn_cid, call;
    const uint8_t *in;
    uint32_t in_len;
    int r = AIPI_ERR_STATE;
    if (aipi_call_decode(b->data, b->len, &fn_cid, &in, &in_len) != AIPI_OK ||
        aipi_call_cid(&fn_cid, in, in_len, &call) != AIPI_OK)
        return VNA_ERR_PARSE;
    if (v->serve && b->amount >= v->min_bounty)
        r = aipi_serve(v->ai, b->data, b->len, v->claim, sizeof v->claim);
    if (r > 0 && vna_node_send_aipi(v->node, from, VNA_AIPI_CLAIM, 0, v->claim, (uint32_t) r,
                                    v->now, v->ob) == VNA_OK) {
        v->served++;
        return VNA_OK;
    }
    v->serve_refused++;
    uint8_t cid[IPFSN_CID_BIN_MAX];
    int cl = ipfsn_cid_encode(&call, cid, sizeof cid);
    if (cl > 0)
        (void) vna_node_send_aipi(v->node, from, VNA_AIPI_BUSY, 0, cid, (uint32_t) cl, v->now,
                                  v->ob);
    return VNA_ERR_DENIED;
}

static vna_status_t on_claim(aipi_vna_t *v, const vna_id_t *from, const vna_b_aipi_t *b)
{
    aipi_claim_t cl;
    vna_id_t ex;
    if (aipi_claim_decode(b->data, b->len, &cl) != AIPI_OK) goto refuse;
    id_of(cl.executor, &ex);
    aipi_vna_call_t *c = call_find(v, &cl.call);
    int32_t job = pending_job(v->ai, &cl.call);
    /* V2: signed by the executor it names, who was asked, for a live job */
    if (!vna_id_eq(&ex, from) || !c || !asked(c, from) || job < 0) goto refuse;
    uint8_t verdict = 0;
    v->cur = c;
    int r = aipi_submit(v->ai, (uint32_t) job, b->data, b->len, &verdict);
    v->cur = 0;
    if (r != AIPI_OK) goto refuse;
    v->claims_in++;
    return VNA_OK;
refuse:
    v->claims_refused++;
    return VNA_ERR_DENIED;
}

static vna_status_t on_aipi(void *ctx, const vna_id_t *from, const vna_b_aipi_t *b, uint64_t now,
                            vna_outbox_t *ob)
{
    aipi_vna_t *v = (aipi_vna_t *) ctx;
    (void) ob; /* replies go to v->ob, which is the same outbox the host passed */
    if (now > v->now) v->now = now;
    switch (b->kind) {
    case VNA_AIPI_CALL:
        return on_call(v, from, b);
    case VNA_AIPI_CLAIM:
        return on_claim(v, from, b);
    case VNA_AIPI_PAID: {
        ipfsn_cid_t call;
        if (ipfsn_cid_decode_exact(b->data, b->len, &call) != IPFSN_OK) return VNA_ERR_PARSE;
        v->paid_in++;
        if (v->econ.paid) v->econ.paid(v->econ.ctx, from, b->amount, &call);
        return VNA_OK;
    }
    case VNA_AIPI_BUSY:
        v->busy_in++;
        return VNA_OK;
    default:
        return VNA_ERR_PARSE;
    }
}

int aipi_vna_init(aipi_vna_t *v, aipi_ctx_t *ai, vna_node_t *node, vna_outbox_t *ob,
                  const aipi_vna_econ_t *econ)
{
    if (!v || !ai || !node || !ob || !node->idn) return AIPI_ERR_ARG;
    vna_zero(v, sizeof *v);
    v->ai = ai;
    v->node = node;
    v->ob = ob;
    if (econ) v->econ = *econ;
    v->call_ttl = 60000;
    for (uint32_t i = 0; i < AIPI_VNA_CALLS; i++) v->calls[i].lookup = -1;
    ai->mesh.ctx = v;
    ai->mesh.broadcast = hk_broadcast;
    ai->mesh.reserve = hk_reserve;
    ai->mesh.pay = hk_pay;
    ai->mesh.refund = hk_refund;
    vna_copy(ai->self, node->idn->id.b, 32);
    vna_node_set_aipi_handler(node, on_aipi, v);
    return AIPI_OK;
}

int aipi_vna_add_executor(aipi_vna_t *v, const vna_id_t *id)
{
    if (!v || !id || vna_id_is_zero(id)) return AIPI_ERR_ARG;
    for (uint32_t i = 0; i < v->nexec; i++)
        if (vna_id_eq(&v->exec[i], id)) return AIPI_OK;
    if (v->nexec >= AIPI_VNA_EXECUTORS) return AIPI_ERR_FULL;
    v->exec[v->nexec++] = *id;
    return AIPI_OK;
}

void aipi_vna_clear_executors(aipi_vna_t *v)
{
    if (v) v->nexec = 0;
}

void aipi_vna_serve(aipi_vna_t *v, bool on, uint64_t min_bounty)
{
    if (!v) return;
    v->serve = on;
    v->min_bounty = min_bounty;
}

int32_t aipi_vna_announce(aipi_vna_t *v, const ipfsn_cid_t *fn_cid, const uint8_t *addr,
                          uint32_t addr_len, uint64_t now, uint64_t ttl_ms)
{
    uint8_t key[32];
    vna_id_t k;
    vna_provider_t p;
    if (!v || !fn_cid || addr_len > VNA_ADDR_MAX || (addr_len && !addr) ||
        aipi_provider_key(fn_cid, key) != AIPI_OK)
        return VNA_ERR_ARG;
    id_of(key, &k);
    vna_zero(&p, sizeof p);
    p.visibility = VNA_VIS_PUBLIC;
    p.addr_len = (uint16_t) addr_len;
    if (addr_len) vna_copy(p.addr, addr, addr_len);
    v->now = now;
    return vna_node_announce(v->node, &k, &p, now, ttl_ms, v->ob);
}

int aipi_vna_execute(aipi_vna_t *v, const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len,
                     bool input_private, uint64_t bounty, uint64_t now, uint64_t ttl,
                     uint32_t *job)
{
    if (!v) return AIPI_ERR_ARG;
    v->now = now;
    return aipi_execute(v->ai, fn_cid, in, in_len, input_private, bounty, now, ttl, job);
}

/* A finished provider lookup: ask the provider it found. */
static void lookup_done(aipi_vna_t *v, aipi_vna_call_t *c)
{
    static vna_rec_t rec;
    static vna_provider_t p;
    const vna_node_lookup_t *L = vna_node_lookup_get(v->node, c->lookup);
    if (L && L->value_len > 0 &&
        vna_schema_unpack(&vna_rec_schema, L->value, L->value_len, &rec, 0) >= 0 &&
        rec.rtype == VNA_REC_PROVIDER &&
        vna_schema_unpack(&vna_provider_schema, rec.payload, rec.payload_len, &p, 0) >= 0 &&
        ask(v, c, &rec.publisher, p.addr, p.addr_len))
        v->provider_sends++;
    vna_node_lookup_release(v->node, c->lookup);
    c->lookup = -1;
}

void aipi_vna_tick(aipi_vna_t *v, uint64_t now)
{
    if (!v) return;
    if (now > v->now) v->now = now;
    (void) aipi_tick(v->ai, now); /* expiry refunds through hk_refund */
    for (uint32_t i = 0; i < AIPI_VNA_CALLS; i++) {
        aipi_vna_call_t *c = &v->calls[i];
        if (!c->used) continue;
        if (c->lookup >= 0 && vna_node_lookup_done(v->node, c->lookup)) lookup_done(v, c);
        if (pending_job(v->ai, &c->call) < 0 || now >= c->deadline) {
            if (c->lookup >= 0) vna_node_lookup_release(v->node, c->lookup);
            c->used = false;
            c->lookup = -1;
        }
    }
}

uint32_t aipi_vna_pending(const aipi_vna_t *v)
{
    uint32_t k = 0;
    if (!v) return 0;
    for (uint32_t i = 0; i < AIPI_VNA_CALLS; i++) k += v->calls[i].used;
    return k;
}
