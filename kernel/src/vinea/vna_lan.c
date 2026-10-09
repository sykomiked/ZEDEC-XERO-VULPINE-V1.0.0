/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_lan.c — LAN discovery state machine. See vna_lan.h. */
#include "vna_lan.h"

static const vna_field_t known_fields[] = {VNA_FFIX(vna_lan_known_t, id, VNA_AXIS_NONE)};
static const vna_schema_t known_schema = {"lan_known", 0x0051, 1, known_fields, 1};

#define L_ vna_lan_msg_t
static const vna_field_t lan_fields[] = {
    VNA_FCONST(L_, magic, VNA_LAN_MAGIC),
    VNA_FU8(L_, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU8(L_, kind, VNA_LAN_ANNOUNCE, VNA_AXIS_NONE),
    VNA_FVAR(L_, svc, svc_len, VNA_AXIS_NONE),
    VNA_FU64(L_, qid, 0, VNA_AXIS_NONE),
    VNA_FARR(L_, known, nknown, &known_schema, vna_lan_known_t, VNA_AXIS_NONE),
    VNA_FVAR(L_, rec, rec_len, VNA_AXIS_PROVENANCE),
};
#undef L_
/* UBH class 1 = CAUSAL_EVENT */
const vna_schema_t vna_lan_msg_schema = {"lan_msg", 0x0050, 1, lan_fields,
                                         sizeof lan_fields / sizeof lan_fields[0]};

static const char SVC[] = VNA_LAN_SERVICE;
#define SVC_LEN (sizeof SVC - 1u)

void vna_lan_cfg_default(vna_lan_cfg_t *c)
{
    c->ttl_ms = 120000;
    c->steady_ms = 60000;
    c->burst = 3;
    c->query_burst = 3;
    c->skew_ms = 30000;
    c->answer_min_ms = 1000;
    c->flags = 0;
}

vna_status_t vna_lan_init(vna_lan_t *l, vna_node_t *node, const vna_lan_cfg_t *cfg,
                          const uint8_t *group, uint32_t group_len, const vna_addr_el_t *addrs,
                          uint32_t naddr, vna_lan_peer_t *peers, uint32_t peer_cap,
                          const uint8_t seed[32])
{
    if (!l || !node || !cfg || !group || group_len == 0 || group_len > VNA_ADDR_MAX || !addrs ||
        naddr == 0 || naddr > VNA_NR_ADDRS || !peers || !seed || cfg->ttl_ms < 2u)
        return VNA_ERR_ARG;
    for (uint32_t i = 0; i < naddr; i++)
        if (addrs[i].len == 0 || addrs[i].len > VNA_ADDR_MAX) return VNA_ERR_ARG;
    vna_zero(l, sizeof *l);
    l->node = node;
    l->cfg = *cfg;
    vna_copy(l->group, group, group_len);
    l->group_len = (uint8_t) group_len;
    for (uint32_t i = 0; i < naddr; i++) l->self_addr[i] = addrs[i];
    l->naddr = (uint16_t) naddr;
    l->peers = peers;
    l->peer_cap = peer_cap;
    for (uint32_t i = 0; i < peer_cap; i++) peers[i].used = false;
    vna_drbg_seed(&l->rng, seed, 32);
    return VNA_OK;
}

void vna_lan_set_xform(vna_lan_t *l, const vna_xform_t *x)
{
    l->xform = x;
}

/* Sign a fresh record (seq + 1). */
static vna_status_t make_record(vna_lan_t *l, uint64_t now, bool goodbye)
{
    vna_noderec_t *r = &l->nr;
    vna_zero(r, sizeof *r);
    r->features = l->node->cfg.ubh ? VNA_FEAT_UBH168 : 0;
    r->flags = (uint8_t) ((l->cfg.flags & VNA_NR_HIGHCAP) | (goodbye ? VNA_NR_GOODBYE : 0));
    r->seq = ++l->rec_seq;
    r->created = now;
    r->expires = goodbye ? now : now + l->cfg.ttl_ms;
    r->naddr = l->naddr;
    for (uint16_t i = 0; i < l->naddr; i++) r->addr[i] = l->self_addr[i];
    uint8_t rnd[32];
    vna_drbg_gen(&l->rng, rnd, 32);
    int32_t n = vna_noderec_seal(r, l->node->idn, rnd, l->rec, sizeof l->rec);
    if (n < 0) return VNA_ERR_ARG;
    l->rec_len = (uint16_t) n;
    l->rec_created = r->created;
    l->rec_expires = r->expires;
    return VNA_OK;
}

static vna_status_t emit(vna_lan_t *l, const vna_lan_msg_t *m, vna_outbox_t *ob)
{
    uint32_t xo = l->xform ? l->xform->overhead : 0;
    if (!ob || ob->n >= VNA_OUTBOX_MAX || ob->cap - ob->used < VNA_LAN_MSG_MAX + xo) {
        if (ob) ob->dropped++;
        return VNA_ERR_SPACE;
    }
    int32_t len =
        vna_schema_pack(&vna_lan_msg_schema, m, ob->buf + ob->used, ob->cap - ob->used, true);
    if (len < 0) return VNA_ERR_ARG;
    len = vna_xform_apply(l->xform, true, l->group, l->group_len, ob->buf + ob->used,
                          (uint32_t) len, ob->cap - ob->used, l->xbuf, sizeof l->xbuf);
    if (len < 0) return VNA_ERR_CRYPTO;
    vna_out_ent_t *e = &ob->e[ob->n++];
    e->off = ob->used;
    e->len = (uint32_t) len;
    vna_copy(e->addr, l->group, l->group_len);
    e->addr_len = l->group_len;
    ob->used += (uint32_t) len;
    return VNA_OK;
}

static void msg_base(vna_lan_msg_t *m, uint8_t kind, uint64_t qid)
{
    m->magic = VNA_LAN_MAGIC;
    m->version = VNA_VERSION;
    m->kind = kind;
    m->svc_len = (uint16_t) SVC_LEN;
    vna_copy(m->svc, SVC, SVC_LEN);
    m->qid = qid;
    m->nknown = 0;
    m->rec_len = 0;
}

static vna_status_t send_announce(vna_lan_t *l, uint64_t qid, vna_outbox_t *ob)
{
    vna_lan_msg_t *m = &l->msg;
    msg_base(m, VNA_LAN_ANNOUNCE, qid);
    vna_copy(m->rec, l->rec, l->rec_len);
    m->rec_len = l->rec_len;
    vna_status_t st = emit(l, m, ob);
    if (st == VNA_OK) l->sent_announce++;
    return st;
}

static vna_status_t send_query(vna_lan_t *l, uint64_t now, vna_outbox_t *ob)
{
    vna_lan_msg_t *m = &l->msg;
    uint64_t qid;
    do qid = vna_drbg_u64(&l->rng);
    while (qid == 0);
    msg_base(m, VNA_LAN_QUERY, qid);
    for (uint32_t i = 0; i < l->peer_cap && m->nknown < VNA_LAN_KNOWN; i++)
        if (l->peers[i].used && l->peers[i].expires > now)
            m->known[m->nknown++].id = l->peers[i].id;
    vna_status_t st = emit(l, m, ob);
    if (st == VNA_OK) l->sent_query++;
    return st;
}

vna_status_t vna_lan_start(vna_lan_t *l, uint64_t now, vna_outbox_t *ob)
{
    if (!l || !ob) return VNA_ERR_ARG;
    vna_status_t st = make_record(l, now, false);
    if (st != VNA_OK) return st;
    l->running = true;
    (void) send_query(l, now, ob);
    (void) send_announce(l, 0, ob);
    l->announces_left = l->cfg.burst > 0 ? l->cfg.burst - 1u : 0;
    l->announce_gap = 1000;
    l->next_announce = now + (l->announces_left ? l->announce_gap : l->cfg.steady_ms);
    l->queries_left = l->cfg.query_burst > 0 ? l->cfg.query_burst - 1u : 0;
    l->query_gap = 1000;
    l->next_query = now + l->query_gap;
    l->answer_due = false;
    l->last_answer = 0;
    return VNA_OK;
}

vna_status_t vna_lan_stop(vna_lan_t *l, uint64_t now, vna_outbox_t *ob)
{
    if (!l || !l->running) return VNA_ERR_STATE;
    l->running = false;
    vna_status_t st = make_record(l, now, true);
    if (st != VNA_OK) return st;
    return send_announce(l, 0, ob);
}

void vna_lan_tick(vna_lan_t *l, uint64_t now, vna_outbox_t *ob)
{
    if (!l || !l->running) return;
    for (uint32_t i = 0; i < l->peer_cap; i++)
        if (l->peers[i].used && l->peers[i].expires <= now) l->peers[i].used = false;
    bool resigned = false;
    if (now >= l->rec_expires - (l->cfg.ttl_ms >> 1)) { /* half its life gone: re-sign */
        if (make_record(l, now, false) == VNA_OK) resigned = true;
    }
    if (l->answer_due && now >= l->answer_at) {
        l->answer_due = false;
        l->last_answer = now;
        (void) send_announce(l, l->answer_qid, ob);
    }
    if (now >= l->next_announce || resigned) {
        (void) send_announce(l, 0, ob);
        if (l->announces_left) {
            l->announces_left--;
            l->announce_gap <<= 1;
            l->next_announce = now + (l->announces_left ? l->announce_gap : l->cfg.steady_ms);
        } else {
            l->next_announce = now + l->cfg.steady_ms;
        }
    }
    if (l->queries_left && now >= l->next_query) {
        (void) send_query(l, now, ob);
        l->queries_left--;
        l->query_gap <<= 1;
        l->next_query = now + l->query_gap;
    }
}

static vna_lan_peer_t *peer_find(vna_lan_t *l, const vna_id_t *id)
{
    for (uint32_t i = 0; i < l->peer_cap; i++)
        if (l->peers[i].used && vna_id_eq(&l->peers[i].id, id)) return &l->peers[i];
    return 0;
}

vna_status_t vna_lan_handle(vna_lan_t *l, const uint8_t *buf, uint32_t len, uint64_t now,
                            vna_outbox_t *ob)
{
    if (!l || !buf) return VNA_ERR_ARG;
    if (l->xform && l->xform->open) {
        int32_t r = l->xform->open(l->xform->ctx, l->group, l->group_len, buf, len, l->xbuf,
                                   sizeof l->xbuf);
        if (r < 0 || (uint32_t) r > sizeof l->xbuf) {
            l->rej_xform++;
            return VNA_ERR_CRYPTO;
        }
        buf = l->xbuf;
        len = (uint32_t) r;
    }
    vna_lan_msg_t *m = &l->msg;
    if (vna_schema_unpack(&vna_lan_msg_schema, buf, len, m, 0) < 0 || m->version != VNA_VERSION ||
        m->kind == 0 || m->svc_len != SVC_LEN || !vna_eq(m->svc, SVC, SVC_LEN)) {
        l->rej_parse++;
        return VNA_ERR_PARSE;
    }
    if (m->kind == VNA_LAN_QUERY) {
        if (m->rec_len != 0) {
            l->rej_parse++;
            return VNA_ERR_PARSE;
        }
        if (!l->running) return VNA_OK;
        for (uint16_t i = 0; i < m->nknown; i++)
            if (vna_id_eq(&m->known[i].id, &l->node->idn->id)) { /* known answer: stay quiet */
                l->suppressed++;
                return VNA_OK;
            }
        if (l->answer_due) return VNA_OK; /* one answer already scheduled */
        uint64_t at = now + 20u + (vna_drbg_u64(&l->rng) & 63u) + (vna_drbg_u64(&l->rng) & 31u) +
                      (vna_drbg_u64(&l->rng) & 7u);
        if (l->last_answer && at < l->last_answer + l->cfg.answer_min_ms) {
            at = l->last_answer + l->cfg.answer_min_ms;
            l->rate_limited++;
        }
        l->answer_due = true;
        l->answer_at = at;
        l->answer_qid = m->qid;
        return VNA_OK;
    }
    /* ANNOUNCE */
    if (m->nknown != 0 || m->rec_len == 0) {
        l->rej_parse++;
        return VNA_ERR_PARSE;
    }
    vna_noderec_t *r = &l->nr;
    /* Cheap refusals first (exact: they never accept anything, so skipping
     * the signature check cannot let a forgery in): our own record, or one
     * whose seq is not newer than what we hold. Most repeated announcements
     * on a busy LAN end here, without an ML-DSA verification. */
    if (vna_schema_unpack(&vna_noderec_schema, m->rec, m->rec_len, r, 0) < 0) {
        l->rej_record++;
        return VNA_ERR_PARSE;
    }
    if (vna_id_eq(&r->id, &l->node->idn->id)) {
        l->rej_self++;
        return VNA_ERR_DST;
    }
    vna_lan_peer_t *p0 = peer_find(l, &r->id);
    if (p0 && r->seq <= p0->seq) {
        l->rej_old++;
        return VNA_ERR_REPLAY;
    }
    vna_status_t st = vna_noderec_verify(&l->node->v, m->rec, m->rec_len, now, l->cfg.skew_ms, r);
    if (st != VNA_OK) {
        l->rej_record++;
        return st;
    }
    vna_lan_peer_t *p = p0; /* replayed or reordered records were refused above */
    if (r->flags & VNA_NR_GOODBYE) {
        if (p) p->used = false;
        l->goodbyes++;
        return vna_node_seed_verified(l->node, r, now, ob); /* removes the contact */
    }
    if (!p) { /* a free slot, else replace the entry soonest to expire */
        for (uint32_t i = 0; i < l->peer_cap && !p; i++)
            if (!l->peers[i].used) p = &l->peers[i];
        for (uint32_t i = 0; i < l->peer_cap && (!p || p->used); i++)
            if (!p || l->peers[i].expires < p->expires) p = &l->peers[i];
    }
    if (p) {
        p->used = true;
        p->id = r->id;
        p->seq = r->seq;
        p->expires = r->expires;
        p->flags = r->flags;
    }
    vna_copy(l->last_rec, m->rec, m->rec_len);
    l->last_len = m->rec_len;
    l->accepted++;
    return vna_node_seed_verified(l->node, r, now, ob);
}

uint32_t vna_lan_peer_count(const vna_lan_t *l, uint64_t now)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < l->peer_cap; i++) k += l->peers[i].used && l->peers[i].expires > now;
    return k;
}
