/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_space.c — public spaces and the watch hall on Vinea. See vna_space.h
 * (P1-P7). */
#include "vna_space.h"
#include "vna_delta.h"

#define EV_TTL_MS    (30ull * 24u * 3600u * 1000u) /* an event record's lifetime */
#define RATE_MAX_Q16 (4u * VNA_SPACE_Q16)

/* ---- schemas ---- */
#define G vna_space_genesis_t
static const vna_field_t gen_fields[] = {
    VNA_FU8(G, version, 1, VNA_AXIS_NONE),
    VNA_FU8(G, kind, VNA_SPACE_HALL, VNA_AXIS_NONE),
    VNA_FU8(G, gov, VNA_GOV_QUORUM, VNA_AXIS_NONE),
    VNA_FU8(G, quorum_pct, 100, VNA_AXIS_NONE),
    VNA_FFIX(G, creator, VNA_AXIS_PROVENANCE),
    VNA_FU64(G, created, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(G, nonce, VNA_AXIS_NONE),
    VNA_FVAR(G, name, name_len, VNA_AXIS_NONE),
    VNA_FVAR(G, topic, topic_len, VNA_AXIS_NONE),
};
#undef G
const vna_schema_t vna_space_genesis_schema = {"space_genesis", 0x0060, 1, gen_fields, 9};

#define H vna_space_host_t
static const vna_field_t host_fields[] = {
    VNA_FVAR(H, gen, gen_len, VNA_AXIS_NONE),
    VNA_FVAR(H, addr, addr_len, VNA_AXIS_NONE),
    VNA_FU16(H, members, VNA_SPACE_MEMBERS, VNA_AXIS_NONE),
};
#undef H
const vna_schema_t vna_space_host_schema = {"space_host", 0x0061, 1, host_fields, 3};

#define E vna_space_ev_t
static const vna_field_t ev_fields[] = {
    VNA_FU8(E, kind, VNA_SPACE_EV_DELTA, VNA_AXIS_NONE),
    VNA_FU64(E, lamport, 0, VNA_AXIS_PROVENANCE),
    VNA_FVAR(E, data, len, VNA_AXIS_NONE),
};
#undef E
const vna_schema_t vna_space_ev_schema = {"space_event", 0x0062, 1, ev_fields, 3};

#define L vna_hall_t
static const vna_field_t hall_fields[] = {
    VNA_FU8(L, state, VNA_HALL_PLAY, VNA_AXIS_NONE),
    VNA_FU64(L, pos_ms, 0, VNA_AXIS_NONE),
    VNA_FU64(L, at_ms, 0, VNA_AXIS_PROVENANCE),
    VNA_FU32(L, rate_q16, RATE_MAX_Q16, VNA_AXIS_NONE),
    VNA_FVAR(L, media, media_len, VNA_AXIS_NONE),
};
#undef L
const vna_schema_t vna_hall_schema = {"hall_state", 0x0063, 1, hall_fields, 5};

/* WELCOME: the genesis block, then members with their addresses */
typedef struct {
    vna_id_t id;
    uint16_t addr_len;
    uint8_t addr[VNA_ADDR_MAX];
} wmember_t;
typedef struct {
    uint16_t gen_len;
    uint8_t gen[VNA_SPACE_GEN_MAX];
    uint16_t n;
    wmember_t m[VNA_SPACE_WELCOME_MAX];
} welcome_t;
static const vna_field_t wm_fields[] = {
    VNA_FFIX(wmember_t, id, VNA_AXIS_PROVENANCE),
    VNA_FVAR(wmember_t, addr, addr_len, VNA_AXIS_NONE),
};
static const vna_schema_t wm_schema = {"space_member", 0x0064, 1, wm_fields, 2};
static const vna_field_t welcome_fields[] = {
    VNA_FVAR(welcome_t, gen, gen_len, VNA_AXIS_NONE),
    VNA_FARR(welcome_t, m, n, &wm_schema, wmember_t, VNA_AXIS_NONE),
};
static const vna_schema_t welcome_schema = {"space_welcome", 0x0065, 1, welcome_fields, 2};

/* single-threaded scratch (the module is not reentrant, like vna_node) */
static vna_rec_t g_rec;
static vna_space_ev_t g_ev;
static vna_space_host_t g_host;
static vna_space_genesis_t g_gen;
static welcome_t g_wel;
static uint8_t g_buf[VNA_REC_MAX];

/* ---- encoding and keys ---- */
int32_t vna_space_genesis_encode(const vna_space_genesis_t *g, uint8_t *out, uint32_t cap)
{
    if (!g || !out) return -1;
    return vna_schema_pack(&vna_space_genesis_schema, g, out, cap, true);
}

static bool topic_ok(const uint8_t *t, uint32_t len)
{
    if (len > VNA_SPACE_TOPIC_MAX) return false;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

int vna_space_genesis_decode(const uint8_t *in, uint32_t len, vna_space_genesis_t *g)
{
    if (!in || !g || vna_schema_unpack(&vna_space_genesis_schema, in, len, g, 0) < 0)
        return VNA_ERR_PARSE;
    if (g->version != 1 || g->kind < VNA_SPACE_ROOM || g->gov < VNA_GOV_OWNER || g->name_len == 0 ||
        !topic_ok(g->topic, g->topic_len) || vna_id_is_zero(&g->creator))
        return VNA_ERR_PARSE;
    if (g->gov == VNA_GOV_QUORUM ? g->quorum_pct < 51 : g->quorum_pct != 0) return VNA_ERR_PARSE;
    return VNA_OK;
}

void vna_space_host_key(const uint8_t cid[VNA_CID_RAW_LEN], uint32_t slot, vna_id_t *key)
{
    uint8_t b[VNA_CID_RAW_LEN + 1];
    vna_copy(b, cid, VNA_CID_RAW_LEN);
    b[VNA_CID_RAW_LEN] = (uint8_t) slot;
    vna_htag("vinea/v3/space-host", b, sizeof b, key->b);
}

void vna_space_dir_key(const uint8_t *topic, uint32_t topic_len, uint32_t slot, vna_id_t *key)
{
    uint8_t b[VNA_SPACE_TOPIC_MAX + 1];
    if (topic_len > VNA_SPACE_TOPIC_MAX) topic_len = VNA_SPACE_TOPIC_MAX;
    vna_copy(b, topic, topic_len);
    b[topic_len] = (uint8_t) slot;
    vna_htag("vinea/v3/space-dir", b, topic_len + 1, key->b);
}

/* The slot a node hosts under, or lists a space under. */
static uint32_t slot_of(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen,
                        uint32_t n)
{
    uint8_t buf[2 * VNA_CID_RAW_LEN + 32], h[32];
    uint32_t k = 0;
    for (uint32_t i = 0; i < alen && k < sizeof buf; i++) buf[k++] = a[i];
    for (uint32_t i = 0; i < blen && k < sizeof buf; i++) buf[k++] = b[i];
    vna_sha3(buf, k, h);
    return h[0] % n;
}

static void sid_of(const uint8_t cid[VNA_CID_RAW_LEN], vna_id_t *sid)
{
    vna_copy(sid->b, cid + 4, 32); /* the sha2-256 digest after the 4-byte prefix */
}

vna_status_t vna_space_rec_check(const vna_rec_t *r)
{
    if (r->rtype == VNA_REC_SPACE) {
        uint8_t cid[VNA_CID_RAW_LEN];
        vna_id_t k;
        if (vna_schema_unpack(&vna_space_host_schema, r->payload, r->payload_len, &g_host, 0) < 0 ||
            vna_space_genesis_decode(g_host.gen, g_host.gen_len, &g_gen) != VNA_OK)
            return VNA_ERR_PARSE;
        vna_cid_raw(g_host.gen, g_host.gen_len, cid);
        for (uint32_t i = 0; i < VNA_SPACE_HOST_SLOTS; i++) {
            vna_space_host_key(cid, i, &k);
            if (vna_id_eq(&k, &r->key)) return VNA_OK;
        }
        if (g_gen.topic_len)
            for (uint32_t i = 0; i < VNA_SPACE_DIR_SLOTS; i++) {
                vna_space_dir_key(g_gen.topic, g_gen.topic_len, i, &k);
                if (vna_id_eq(&k, &r->key)) return VNA_OK;
            }
        return VNA_ERR_PARSE; /* a key its genesis does not give */
    }
    if (r->rtype == VNA_REC_SPACE_EV) {
        if (vna_schema_unpack(&vna_space_ev_schema, r->payload, r->payload_len, &g_ev, 0) < 0 ||
            g_ev.kind < VNA_SPACE_EV_POST)
            return VNA_ERR_PARSE;
        if (g_ev.kind == VNA_SPACE_EV_POST) return g_ev.len > 0 ? VNA_OK : VNA_ERR_PARSE;
        if (g_ev.kind == VNA_SPACE_EV_DELTA) {
            vna_delta_t dt;
            return vna_delta_parse(g_ev.data, g_ev.len, &dt) == VNA_OK ? VNA_OK : VNA_ERR_PARSE;
        }
        vna_hall_t h;
        return vna_schema_unpack(&vna_hall_schema, g_ev.data, g_ev.len, &h, 0) >= 0 ? VNA_OK
                                                                                    : VNA_ERR_PARSE;
    }
    return VNA_ERR_PARSE;
}

/* ---- tables ---- */
static vna_space_t *space_by_cid(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN])
{
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++)
        if (sp->s[i].used && vna_ct_eq(sp->s[i].cid, cid, VNA_CID_RAW_LEN)) return &sp->s[i];
    return 0;
}

static vna_space_t *space_by_sid(vna_spaces_t *sp, const vna_id_t *sid)
{
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++)
        if (sp->s[i].used && vna_id_eq(&sp->s[i].sid, sid)) return &sp->s[i];
    return 0;
}

const vna_space_t *vna_space_get(const vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN])
{
    return sp && cid ? space_by_cid((vna_spaces_t *) sp, cid) : 0;
}

static vna_space_t *space_new(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN])
{
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++) {
        vna_space_t *s = &sp->s[i];
        if (s->used) continue;
        vna_zero(s, sizeof *s);
        s->used = true;
        vna_copy(s->cid, cid, VNA_CID_RAW_LEN);
        sid_of(cid, &s->sid);
        s->lookup = -1;
        return s;
    }
    return 0;
}

static void space_free(vna_spaces_t *sp, vna_space_t *s)
{
    if (s->lookup >= 0) vna_node_lookup_release(sp->node, s->lookup);
    s->used = false;
    s->lookup = -1;
}

static bool any_in(const vna_spaces_t *sp)
{
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++)
        if (sp->s[i].used && sp->s[i].phase == VNA_SPACE_IN) return true;
    return false;
}

static vna_space_member_t *member_find(vna_space_t *s, const vna_id_t *id)
{
    for (uint32_t i = 0; i < s->nm; i++)
        if (vna_id_eq(&s->m[i].id, id)) return &s->m[i];
    return 0;
}

/* Add or refresh a member (the oldest silent one gives way when full). */
static void member_seen(vna_spaces_t *sp, vna_space_t *s, const vna_id_t *id, const uint8_t *addr,
                        uint32_t alen)
{
    if (vna_id_eq(id, &sp->node->idn->id)) return;
    vna_space_member_t *m = member_find(s, id);
    if (!m) {
        if (s->nm < VNA_SPACE_MEMBERS) {
            m = &s->m[s->nm++];
        } else {
            m = &s->m[0];
            for (uint32_t i = 1; i < s->nm; i++)
                if (s->m[i].last_seen < m->last_seen) m = &s->m[i];
        }
        vna_zero(m, sizeof *m);
        m->id = *id;
    }
    if (addr && alen && alen <= VNA_ADDR_MAX) {
        vna_copy(m->addr, addr, alen);
        m->addr_len = (uint8_t) alen;
    }
    m->last_seen = sp->now;
}

static void member_drop(vna_space_t *s, const vna_id_t *id)
{
    for (uint32_t i = 0; i < s->nm; i++)
        if (vna_id_eq(&s->m[i].id, id)) {
            s->m[i] = s->m[--s->nm];
            return;
        }
}

static vna_status_t send_to(vna_spaces_t *sp, const vna_space_t *s, const vna_space_member_t *m,
                            uint8_t kind, const uint8_t *data, uint32_t len)
{
    return vna_node_send_space_at(sp->node, &m->id, m->addr, m->addr_len, kind, &s->sid, data, len,
                                  sp->now, sp->ob);
}

/* ---- dedupe ---- */
static bool seen(const vna_spaces_t *sp, const uint8_t h[32])
{
    for (uint32_t i = 0; i < sp->seen_n; i++)
        if (vna_ct_eq(sp->seen[i], h, 32)) return true;
    return false;
}

static void remember(vna_spaces_t *sp, const uint8_t h[32])
{
    vna_copy(sp->seen[sp->seen_head], h, 32);
    sp->seen_head = (sp->seen_head + 1) % VNA_SPACE_SEEN;
    if (sp->seen_n < VNA_SPACE_SEEN) sp->seen_n++;
}

/* ---- the watch hall ---- */
uint64_t vna_space_hall_pos(const vna_hall_t *h, uint64_t now)
{
    if (!h) return 0;
    if (h->state != VNA_HALL_PLAY || now <= h->at_ms) return h->pos_ms;
    uint64_t dt = now - h->at_ms;
    if (dt > (1ull << 40)) dt = 1ull << 40; /* 35 years: no overflow below */
    return vna_sat_add(h->pos_ms, (dt * h->rate_q16) >> 16);
}

bool vna_space_may_control(const vna_space_t *s, const vna_id_t *who)
{
    if (!s || !who || !s->have_gen) return false;
    if (vna_id_eq(who, &s->gen.creator)) return true;
    /* horizontal: anyone in the space (anyone with the CID may join, so
     * membership is not checked: a member we have not heard of yet counts) */
    return s->gen.gov == VNA_GOV_QUORUM;
}

/* Newer playback state: by Lamport time, then author id. */
static bool hall_newer(const vna_space_t *s, uint64_t lamport, const vna_id_t *author)
{
    if (!s->hall_set || lamport > s->hall_lamport) return true;
    if (lamport < s->hall_lamport) return false;
    for (uint32_t i = 0; i < 32; i++)
        if (author->b[i] != s->hall_author.b[i]) return author->b[i] > s->hall_author.b[i];
    return false;
}

/* ---- events ---- */
static void log_add(vna_spaces_t *sp, vna_space_t *s, const vna_rec_t *r, const vna_space_ev_t *e,
                    const uint8_t *rec, uint32_t rec_len)
{
    vna_space_logent_t *l = &s->log[s->log_head];
    s->log_head = (s->log_head + 1) % VNA_SPACE_LOG;
    if (s->log_n < VNA_SPACE_LOG) s->log_n++;
    l->index = ++s->log_index;
    l->kind = e->kind;
    l->author = r->publisher;
    l->lamport = e->lamport;
    l->created = r->created;
    l->len = e->len;
    vna_copy(l->data, e->data, e->len);
    l->rec_len = (uint16_t) rec_len;
    vna_copy(l->rec, rec, rec_len);
    if (sp->on_event) sp->on_event(sp->on_event_ctx, s, l);
}

/* Apply a verified event record (r, e parsed from rec). false: refused. */
static bool apply(vna_spaces_t *sp, vna_space_t *s, const vna_rec_t *r, const vna_space_ev_t *e,
                  const uint8_t *rec, uint32_t rec_len)
{
    if (e->lamport > s->lamport) s->lamport = e->lamport;
    if (e->kind == VNA_SPACE_EV_HALL) {
        vna_hall_t h;
        if (s->gen.kind != VNA_SPACE_HALL || !vna_space_may_control(s, &r->publisher) ||
            vna_schema_unpack(&vna_hall_schema, e->data, e->len, &h, 0) < 0)
            return false;
        if (!hall_newer(s, e->lamport, &r->publisher)) return true; /* older: kept in the log */
        s->hall = h;
        s->hall_set = true;
        s->hall_author = r->publisher;
        s->hall_lamport = e->lamport;
        s->hall_rec_len = (uint16_t) rec_len;
        vna_copy(s->hall_rec, rec, rec_len);
    }
    log_add(sp, s, r, e, rec, rec_len);
    return true;
}

/* Pass a new event on to up to VNA_SPACE_FANOUT members (all of them when
 * it is our own), never back to `from` or its author. */
static void spread(vna_spaces_t *sp, vna_space_t *s, const uint8_t *rec, uint32_t len,
                   const vna_id_t *from, const vna_id_t *author, bool own)
{
    uint32_t n = s->nm, sent = 0;
    if (n == 0) return;
    uint32_t start = (uint32_t) vna_drbg_u64(&sp->rng) % n;
    for (uint32_t k = 0; k < n && (own || sent < VNA_SPACE_FANOUT); k++) {
        const vna_space_member_t *m = &s->m[(start + k) % n];
        if ((from && vna_id_eq(&m->id, from)) || vna_id_eq(&m->id, author)) continue;
        if (send_to(sp, s, m, VNA_SPACE_M_EVENT, rec, len) == VNA_OK) {
            sent++;
            if (own)
                sp->events_out++;
            else
                sp->forwards++;
        }
    }
}

/* Seal and send our own event. */
static int emit(vna_spaces_t *sp, vna_space_t *s, uint8_t kind, const uint8_t *data, uint32_t len)
{
    vna_rec_t *r = &g_rec;
    vna_space_ev_t *e = &g_ev;
    vna_zero(e, sizeof *e);
    e->kind = kind;
    e->lamport = ++s->lamport;
    e->len = (uint16_t) len;
    vna_copy(e->data, data, len);
    r->rtype = VNA_REC_SPACE_EV;
    r->key = s->sid;
    r->created = sp->now;
    r->expires = sp->now + EV_TTL_MS;
    int32_t pl = vna_schema_pack(&vna_space_ev_schema, e, r->payload, VNA_PAYLOAD_MAX, true);
    if (pl < 0) return VNA_ERR_ARG;
    r->payload_len = (uint16_t) pl;
    uint8_t rnd[32];
    vna_drbg_gen(&sp->rng, rnd, 32);
    int32_t n = vna_rec_seal(r, sp->node->idn, rnd, g_buf, sizeof g_buf);
    if (n < 0) return VNA_ERR_ARG;
    uint8_t h[32];
    vna_sha3(g_buf, (uint32_t) n, h);
    remember(sp, h);
    if (!apply(sp, s, r, e, g_buf, (uint32_t) n)) return VNA_ERR_DENIED;
    spread(sp, s, g_buf, (uint32_t) n, 0, &sp->node->idn->id, true);
    return VNA_OK;
}

/* ---- hosting ---- */
static void publish(vna_spaces_t *sp, vna_space_t *s)
{
    vna_rec_t *r = &g_rec;
    vna_space_host_t *h = &g_host;
    vna_zero(h, sizeof *h);
    h->gen_len = s->gen_len;
    vna_copy(h->gen, s->gen_bytes, s->gen_len);
    h->addr_len = sp->addr_len;
    vna_copy(h->addr, sp->addr, sp->addr_len);
    h->members = (uint16_t) s->nm;
    const vna_id_t *self = &sp->node->idn->id;
    for (uint32_t pass = 0; pass < 2; pass++) {
        if (pass == 1 && s->gen.topic_len == 0) break;
        r->rtype = VNA_REC_SPACE;
        if (pass == 0)
            vna_space_host_key(s->cid,
                               slot_of(self->b, 32, s->cid, VNA_CID_RAW_LEN, VNA_SPACE_HOST_SLOTS),
                               &r->key);
        else
            vna_space_dir_key(s->gen.topic, s->gen.topic_len,
                              slot_of(s->cid, VNA_CID_RAW_LEN, 0, 0, VNA_SPACE_DIR_SLOTS), &r->key);
        r->created = sp->now;
        r->expires = sp->now + sp->record_ttl_ms;
        int32_t pl = vna_schema_pack(&vna_space_host_schema, h, r->payload, VNA_PAYLOAD_MAX, true);
        uint8_t rnd[32];
        vna_drbg_gen(&sp->rng, rnd, 32);
        int32_t n = pl < 0 ? -1 : (r->payload_len = (uint16_t) pl, 0);
        if (n == 0) n = vna_rec_seal(r, sp->node->idn, rnd, g_buf, sizeof g_buf);
        int32_t slot = -1;
        for (uint32_t i = 0; i < 2u * VNA_SPACE_MAX && n > 0; i++)
            if (sp->pubs[i] < 0) {
                slot = vna_node_publish(sp->node, g_buf, (uint32_t) n, sp->now, sp->ob);
                if (slot >= 0) sp->pubs[i] = slot;
                break;
            }
        if (slot >= 0)
            sp->published++;
        else
            sp->publish_failed++;
    }
    s->next_publish = sp->now + sp->publish_ms;
}

static void become_member(vna_spaces_t *sp, vna_space_t *s)
{
    s->phase = VNA_SPACE_IN;
    s->next_presence = sp->now; /* say hello to whoever the welcome named */
    vna_node_space_store(sp->node, sp->store_base > VNA_SPACE_MEMBER_STORE
                                       ? sp->store_base
                                       : VNA_SPACE_MEMBER_STORE);
    publish(sp, s);
}

/* ---- the node's space handler ---- */
static vna_status_t on_join(vna_spaces_t *sp, vna_space_t *s, const vna_id_t *from,
                            const uint8_t *addr, uint32_t alen)
{
    if (s->phase != VNA_SPACE_IN) return VNA_ERR_DENIED;
    sp->joins_in++;
    /* WELCOME: genesis and the members the joiner can reach */
    welcome_t *w = &g_wel;
    vna_zero(w, sizeof *w);
    w->gen_len = s->gen_len;
    vna_copy(w->gen, s->gen_bytes, s->gen_len);
    for (uint32_t i = 0; i < s->nm && w->n < VNA_SPACE_WELCOME_MAX; i++) {
        if (vna_id_eq(&s->m[i].id, from) || s->m[i].addr_len == 0) continue;
        w->m[w->n].id = s->m[i].id;
        w->m[w->n].addr_len = s->m[i].addr_len;
        vna_copy(w->m[w->n].addr, s->m[i].addr, s->m[i].addr_len);
        w->n++;
    }
    int32_t len = vna_schema_pack(&welcome_schema, w, g_buf, sizeof g_buf, true);
    member_seen(sp, s, from, addr, alen);
    const vna_space_member_t *m = member_find(s, from);
    if (len < 0 || !m || send_to(sp, s, m, VNA_SPACE_M_WELCOME, g_buf, (uint32_t) len) != VNA_OK)
        return VNA_ERR_ARG;
    /* catch-up: the playback state, then the latest posts, oldest first */
    if (s->hall_set) (void) send_to(sp, s, m, VNA_SPACE_M_EVENT, s->hall_rec, s->hall_rec_len);
    uint32_t posts = 0, start = s->log_n;
    while (start > 0 && posts < VNA_SPACE_CATCHUP) {
        const vna_space_logent_t *l =
            &s->log[(s->log_head + VNA_SPACE_LOG - s->log_n + start - 1) % VNA_SPACE_LOG];
        if (l->kind == VNA_SPACE_EV_POST) posts++;
        start--;
    }
    for (uint32_t k = start; k < s->log_n; k++) {
        const vna_space_logent_t *l =
            &s->log[(s->log_head + VNA_SPACE_LOG - s->log_n + k) % VNA_SPACE_LOG];
        if (l->kind == VNA_SPACE_EV_POST)
            (void) send_to(sp, s, m, VNA_SPACE_M_EVENT, l->rec, l->rec_len);
    }
    return VNA_OK;
}

static vna_status_t on_welcome(vna_spaces_t *sp, vna_space_t *s, const vna_id_t *from,
                               const uint8_t *addr, uint32_t alen, const vna_b_space_t *b)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    welcome_t *w = &g_wel;
    /* only the host we asked, for a space we are joining, with the genesis
     * block its CID names */
    if (s->phase != VNA_SPACE_JOINING || !vna_id_eq(from, &s->via) ||
        vna_schema_unpack(&welcome_schema, b->data, b->len, w, 0) < 0)
        return VNA_ERR_DENIED;
    vna_cid_raw(w->gen, w->gen_len, cid);
    if (!vna_ct_eq(cid, s->cid, VNA_CID_RAW_LEN) ||
        vna_space_genesis_decode(w->gen, w->gen_len, &s->gen) != VNA_OK)
        return VNA_ERR_PARSE;
    s->have_gen = true;
    s->gen_len = w->gen_len;
    vna_copy(s->gen_bytes, w->gen, w->gen_len);
    sp->welcomes_in++;
    member_seen(sp, s, from, addr, alen);
    for (uint16_t i = 0; i < w->n; i++) {
        member_seen(sp, s, &w->m[i].id, w->m[i].addr, w->m[i].addr_len);
        vna_space_member_t *m = member_find(s, &w->m[i].id);
        if (m) m->last_seen = sp->now; /* named by the host: a chance to answer PRESENCE */
    }
    become_member(sp, s);
    return VNA_OK;
}

static vna_status_t on_event(vna_spaces_t *sp, vna_space_t *s, const vna_id_t *from,
                             const vna_b_space_t *b)
{
    uint8_t h[32];
    vna_rec_t *r = &g_rec;
    if (s->phase != VNA_SPACE_IN && s->phase != VNA_SPACE_JOINING) return VNA_ERR_DENIED;
    vna_sha3(b->data, b->len, h);
    if (seen(sp, h)) return VNA_OK; /* applied once */
    if (vna_rec_verify(&sp->node->v, b->data, b->len, sp->now, sp->node->cfg.ts_window_ms, r) !=
            VNA_OK ||
        r->rtype != VNA_REC_SPACE_EV || !vna_id_eq(&r->key, &s->sid) ||
        vna_schema_unpack(&vna_space_ev_schema, r->payload, r->payload_len, &g_ev, 0) < 0 ||
        !s->have_gen) {
        sp->events_refused++;
        return VNA_ERR_DENIED;
    }
    remember(sp, h);
    vna_id_t author = r->publisher;
    if (!apply(sp, s, r, &g_ev, b->data, b->len)) {
        sp->events_refused++;
        return VNA_ERR_DENIED;
    }
    sp->events_in++;
    spread(sp, s, b->data, b->len, from, &author, false);
    return VNA_OK;
}

static vna_status_t on_space(void *ctx, const vna_id_t *from, const uint8_t *addr, uint32_t alen,
                             const vna_b_space_t *b, uint64_t now, vna_outbox_t *ob)
{
    vna_spaces_t *sp = (vna_spaces_t *) ctx;
    (void) ob; /* answers go to sp->ob, the same outbox the host passed */
    if (now > sp->now) sp->now = now;
    vna_space_t *s = space_by_sid(sp, &b->space);
    vna_status_t st = VNA_ERR_DENIED;
    if (s) switch (b->kind) {
        case VNA_SPACE_M_JOIN:
            st = on_join(sp, s, from, addr, alen);
            break;
        case VNA_SPACE_M_WELCOME:
            st = on_welcome(sp, s, from, addr, alen, b);
            break;
        case VNA_SPACE_M_PRESENCE:
            if (s->phase == VNA_SPACE_IN) {
                sp->presence_in++;
                member_seen(sp, s, from, addr, alen);
                st = VNA_OK;
            }
            break;
        case VNA_SPACE_M_LEAVE:
            sp->leaves_in++;
            member_drop(s, from);
            st = VNA_OK;
            break;
        case VNA_SPACE_M_EVENT:
            st = on_event(sp, s, from, b);
            if (st == VNA_OK && s->phase == VNA_SPACE_IN) member_seen(sp, s, from, addr, alen);
            break;
        default:
            break;
        }
    if (st != VNA_OK) sp->refused++;
    return st;
}

/* ---- API ---- */
int vna_spaces_init(vna_spaces_t *sp, vna_node_t *node, vna_outbox_t *ob, const uint8_t seed[32])
{
    if (!sp || !node || !ob || !node->idn || !seed) return VNA_ERR_ARG;
    vna_zero(sp, sizeof *sp);
    sp->node = node;
    sp->ob = ob;
    vna_drbg_seed(&sp->rng, seed, 32);
    sp->presence_ms = 30000;
    sp->member_ttl_ms = 100000;
    sp->join_wait_ms = 4000;
    sp->publish_ms = 300000;
    sp->record_ttl_ms = 900000;
    sp->find_lookup = -1;
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++) sp->s[i].lookup = -1;
    for (uint32_t i = 0; i < 2u * VNA_SPACE_MAX; i++) sp->pubs[i] = -1;
    vna_node_set_space_handler(node, on_space, sp);
    return VNA_OK;
}

void vna_spaces_set_addr(vna_spaces_t *sp, const uint8_t *addr, uint32_t len)
{
    if (!sp || len > VNA_ADDR_MAX || (len && !addr)) return;
    if (len) vna_copy(sp->addr, addr, len);
    sp->addr_len = (uint8_t) len;
}

void vna_spaces_store_base(vna_spaces_t *sp, uint32_t cap)
{
    if (!sp) return;
    sp->store_base = cap;
    if (!any_in(sp)) vna_node_space_store(sp->node, cap);
}

void vna_spaces_on_event(vna_spaces_t *sp, vna_space_event_fn fn, void *ctx)
{
    if (!sp) return;
    sp->on_event = fn;
    sp->on_event_ctx = fn ? ctx : 0;
}

static uint32_t cstr_len(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (s && s[n] && n <= max) n++;
    return n;
}

static int create_gen(vna_spaces_t *sp, uint8_t kind, uint8_t gov, uint8_t quorum_pct,
                      const vna_id_t *creator, const char *name, const char *topic,
                      uint64_t created, const uint8_t nonce[16], uint64_t now,
                      uint8_t cid[VNA_CID_RAW_LEN])
{
    vna_space_genesis_t *g = &g_gen;
    uint8_t buf[VNA_SPACE_GEN_MAX], c[VNA_CID_RAW_LEN];
    if (!sp || !name || !cid) return VNA_ERR_ARG;
    uint32_t nl = cstr_len(name, VNA_SPACE_NAME_MAX), tl = cstr_len(topic, VNA_SPACE_TOPIC_MAX);
    if (nl == 0 || nl > VNA_SPACE_NAME_MAX || tl > VNA_SPACE_TOPIC_MAX) return VNA_ERR_ARG;
    sp->now = now;
    vna_zero(g, sizeof *g);
    g->version = 1;
    g->kind = kind;
    g->gov = gov;
    g->quorum_pct = gov == VNA_GOV_QUORUM ? (quorum_pct ? quorum_pct : 51) : 0;
    g->creator = *creator;
    g->created = created;
    vna_copy(g->nonce, nonce, sizeof g->nonce);
    g->name_len = (uint16_t) nl;
    vna_copy(g->name, name, nl);
    g->topic_len = (uint16_t) tl;
    if (tl) vna_copy(g->topic, topic, tl);
    int32_t len = vna_space_genesis_encode(g, buf, sizeof buf);
    if (len < 0 || vna_space_genesis_decode(buf, (uint32_t) len, g) != VNA_OK) return VNA_ERR_ARG;
    vna_cid_raw(buf, (uint32_t) len, c);
    vna_space_t *s = space_new(sp, c);
    if (!s) return VNA_ERR_SPACE;
    s->gen = *g;
    s->have_gen = true;
    s->gen_len = (uint16_t) len;
    vna_copy(s->gen_bytes, buf, (uint32_t) len);
    sp->created++;
    become_member(sp, s);
    vna_copy(cid, c, VNA_CID_RAW_LEN);
    return VNA_OK;
}

int vna_space_create(vna_spaces_t *sp, uint8_t kind, uint8_t gov, uint8_t quorum_pct,
                     const char *name, const char *topic, uint64_t now,
                     uint8_t cid[VNA_CID_RAW_LEN])
{
    uint8_t nonce[16];
    if (!sp) return VNA_ERR_ARG;
    vna_drbg_gen(&sp->rng, nonce, sizeof nonce);
    return create_gen(sp, kind, gov, quorum_pct, &sp->node->idn->id, name, topic, now, nonce, now,
                      cid);
}

/* The deterministic create: a space whose genesis is fully given, so every
 * node that runs it lands on the SAME CID — a well-known bus needs no join
 * dance and no published CID. `creator` is a canonical id (the bus's own,
 * derived from its tag), `created` and `nonce` are constants of the bus. */
int vna_space_create_at(vna_spaces_t *sp, uint8_t kind, uint8_t gov, uint8_t quorum_pct,
                        const vna_id_t *creator, const char *name, const char *topic,
                        uint64_t created, const uint8_t nonce[16], uint64_t now,
                        uint8_t cid[VNA_CID_RAW_LEN])
{
    if (!creator || !nonce) return VNA_ERR_ARG;
    return create_gen(sp, kind, gov, quorum_pct, creator, name, topic, created, nonce, now, cid);
}

/* Look up the next host slot (none left: not found). */
static void search_next(vna_spaces_t *sp, vna_space_t *s)
{
    if (s->lookup >= 0) vna_node_lookup_release(sp->node, s->lookup);
    s->lookup = -1;
    while (s->slots_tried < VNA_SPACE_HOST_SLOTS) {
        vna_id_t k;
        vna_space_host_key(s->cid, s->slot_next, &k);
        s->slot_next = (uint8_t) ((s->slot_next + 1) % VNA_SPACE_HOST_SLOTS);
        s->slots_tried++;
        int32_t slot = vna_node_lookup(sp->node, &k, VNA_LK_FIND_VALUE, sp->now, sp->ob);
        if (slot >= 0) {
            s->lookup = slot;
            s->phase = VNA_SPACE_SEARCHING;
            return;
        }
        if (slot != VNA_ERR_SPACE) continue; /* no contacts: try the next (fails fast) */
        s->slots_tried--;                    /* every lookup slot busy: retry on the next tick */
        s->slot_next = (uint8_t) ((s->slot_next + VNA_SPACE_HOST_SLOTS - 1) % VNA_SPACE_HOST_SLOTS);
        s->phase = VNA_SPACE_SEARCHING;
        return;
    }
    s->phase = VNA_SPACE_NOT_FOUND;
}

int vna_space_join(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now)
{
    if (!sp || !cid || !vna_cid_is_raw(cid)) return VNA_ERR_ARG;
    sp->now = now;
    vna_space_t *s = space_by_cid(sp, cid);
    if (s && s->phase != VNA_SPACE_NOT_FOUND) return VNA_OK;
    if (!s && !(s = space_new(sp, cid))) return VNA_ERR_SPACE;
    s->slots_tried = 0;
    s->slot_next = (uint8_t) ((uint32_t) vna_drbg_u64(&sp->rng) % VNA_SPACE_HOST_SLOTS);
    search_next(sp, s);
    return VNA_OK;
}

int vna_space_leave(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint64_t now)
{
    vna_space_t *s = sp && cid ? space_by_cid(sp, cid) : 0;
    if (!s) return VNA_ERR_ARG;
    sp->now = now;
    if (s->phase == VNA_SPACE_IN)
        for (uint32_t i = 0; i < s->nm; i++)
            (void) send_to(sp, s, &s->m[i], VNA_SPACE_M_LEAVE, 0, 0);
    space_free(sp, s);
    if (!any_in(sp)) vna_node_space_store(sp->node, sp->store_base);
    return VNA_OK;
}

int vna_space_post(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], const uint8_t *text,
                   uint32_t len, uint64_t now)
{
    vna_space_t *s = sp && cid ? space_by_cid(sp, cid) : 0;
    if (!s || !text || len == 0 || len > VNA_SPACE_TEXT_MAX) return VNA_ERR_ARG;
    if (s->phase != VNA_SPACE_IN) return VNA_ERR_DENIED;
    sp->now = now;
    return emit(sp, s, VNA_SPACE_EV_POST, text, len);
}

/* Post a packed delta (vna_delta.h) to the space's state bus. The bytes are
 * checked as canonical before they are signed: a malformed delta is refused
 * here rather than gossiped. */
int vna_space_delta(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], const uint8_t *packed,
                    uint32_t len, uint64_t now)
{
    vna_space_t *s = sp && cid ? space_by_cid(sp, cid) : 0;
    vna_delta_t dt;
    if (!s || !packed || vna_delta_parse(packed, len, &dt) != VNA_OK) return VNA_ERR_ARG;
    if (s->phase != VNA_SPACE_IN) return VNA_ERR_DENIED;
    sp->now = now;
    return emit(sp, s, VNA_SPACE_EV_DELTA, packed, len);
}

int vna_space_hall_set(vna_spaces_t *sp, const uint8_t cid[VNA_CID_RAW_LEN], uint8_t state,
                       const uint8_t *media, uint32_t media_len, uint64_t pos_ms, uint32_t rate_q16,
                       uint64_t now)
{
    vna_space_t *s = sp && cid ? space_by_cid(sp, cid) : 0;
    vna_hall_t h;
    uint8_t buf[64 + VNA_SPACE_MEDIA_MAX];
    if (!s || state > VNA_HALL_PLAY || media_len > VNA_SPACE_MEDIA_MAX || (media_len && !media) ||
        rate_q16 == 0 || rate_q16 > RATE_MAX_Q16)
        return VNA_ERR_ARG;
    if (s->phase != VNA_SPACE_IN || s->gen.kind != VNA_SPACE_HALL ||
        !vna_space_may_control(s, &sp->node->idn->id))
        return VNA_ERR_DENIED;
    sp->now = now;
    vna_zero(&h, sizeof h);
    h.state = state;
    h.pos_ms = pos_ms;
    h.at_ms = now;
    h.rate_q16 = rate_q16;
    h.media_len = (uint16_t) media_len;
    if (media_len) vna_copy(h.media, media, media_len);
    int32_t len = vna_schema_pack(&vna_hall_schema, &h, buf, sizeof buf, true);
    if (len < 0) return VNA_ERR_ARG;
    return emit(sp, s, VNA_SPACE_EV_HALL, buf, (uint32_t) len);
}

int vna_space_find(vna_spaces_t *sp, const char *topic, uint64_t now)
{
    uint32_t tl = cstr_len(topic, VNA_SPACE_TOPIC_MAX);
    if (!sp || tl == 0 || !topic_ok((const uint8_t *) topic, tl)) return VNA_ERR_ARG;
    if (sp->find_lookup >= 0) vna_node_lookup_release(sp->node, sp->find_lookup);
    sp->now = now;
    sp->find_on = true;
    sp->find_lookup = -1;
    sp->find_topic_len = (uint8_t) tl;
    vna_copy(sp->find_topic, topic, tl);
    sp->find_next = 0;
    sp->nfound = 0;
    return VNA_OK;
}

bool vna_space_find_done(const vna_spaces_t *sp)
{
    return sp && !sp->find_on;
}

/* A finished directory lookup: keep what it found, start the next slot. */
static void find_step(vna_spaces_t *sp)
{
    if (sp->find_lookup >= 0) {
        if (!vna_node_lookup_done(sp->node, sp->find_lookup)) return;
        const vna_node_lookup_t *L = vna_node_lookup_get(sp->node, sp->find_lookup);
        if (L && L->value_len &&
            vna_schema_unpack(&vna_rec_schema, L->value, L->value_len, &g_rec, 0) >= 0 &&
            g_rec.rtype == VNA_REC_SPACE &&
            vna_schema_unpack(&vna_space_host_schema, g_rec.payload, g_rec.payload_len, &g_host,
                              0) >= 0 &&
            vna_space_genesis_decode(g_host.gen, g_host.gen_len, &g_gen) == VNA_OK &&
            sp->nfound < VNA_SPACE_FOUND_MAX) {
            vna_space_found_t *f = &sp->found[sp->nfound];
            vna_cid_raw(g_host.gen, g_host.gen_len, f->cid);
            bool dup = false;
            for (uint32_t i = 0; i < sp->nfound; i++)
                dup |= vna_ct_eq(sp->found[i].cid, f->cid, VNA_CID_RAW_LEN);
            if (!dup) {
                f->gen = g_gen;
                f->host = g_rec.publisher;
                f->members = g_host.members;
                sp->nfound++;
            }
        }
        vna_node_lookup_release(sp->node, sp->find_lookup);
        sp->find_lookup = -1;
    }
    while (sp->find_next < VNA_SPACE_DIR_SLOTS) {
        vna_id_t k;
        vna_space_dir_key(sp->find_topic, sp->find_topic_len, sp->find_next, &k);
        int32_t slot = vna_node_lookup(sp->node, &k, VNA_LK_FIND_VALUE, sp->now, sp->ob);
        if (slot == VNA_ERR_SPACE) return; /* all lookup slots busy: next tick */
        sp->find_next++;
        if (slot >= 0) {
            sp->find_lookup = slot;
            return;
        }
    }
    sp->find_on = false;
}

/* A finished host lookup: JOIN through the host it found, else the next slot. */
static void search_step(vna_spaces_t *sp, vna_space_t *s)
{
    if (s->lookup < 0) {
        search_next(sp, s);
        return;
    }
    if (!vna_node_lookup_done(sp->node, s->lookup)) return;
    const vna_node_lookup_t *L = vna_node_lookup_get(sp->node, s->lookup);
    bool asked = false;
    if (L && L->value_len &&
        vna_schema_unpack(&vna_rec_schema, L->value, L->value_len, &g_rec, 0) >= 0 &&
        g_rec.rtype == VNA_REC_SPACE &&
        vna_schema_unpack(&vna_space_host_schema, g_rec.payload, g_rec.payload_len, &g_host, 0) >=
            0 &&
        !vna_id_eq(&g_rec.publisher, &sp->node->idn->id)) {
        vna_id_t host = g_rec.publisher;
        sp->hosts_found++;
        if (vna_node_send_space_at(sp->node, &host, g_host.addr, g_host.addr_len, VNA_SPACE_M_JOIN,
                                   &s->sid, 0, 0, sp->now, sp->ob) == VNA_OK) {
            s->via = host;
            s->phase = VNA_SPACE_JOINING;
            s->wait_until = sp->now + sp->join_wait_ms;
            asked = true;
        }
    }
    vna_node_lookup_release(sp->node, s->lookup);
    s->lookup = -1;
    if (!asked) search_next(sp, s);
}

void vna_spaces_tick(vna_spaces_t *sp, uint64_t now)
{
    if (!sp) return;
    if (now > sp->now) sp->now = now;
    for (uint32_t i = 0; i < 2u * VNA_SPACE_MAX; i++)
        if (sp->pubs[i] >= 0 && vna_node_lookup_done(sp->node, sp->pubs[i])) {
            vna_node_lookup_release(sp->node, sp->pubs[i]);
            sp->pubs[i] = -1;
        }
    if (sp->find_on) find_step(sp);
    for (uint32_t i = 0; i < VNA_SPACE_MAX; i++) {
        vna_space_t *s = &sp->s[i];
        if (!s->used) continue;
        switch (s->phase) {
        case VNA_SPACE_SEARCHING:
            search_step(sp, s);
            break;
        case VNA_SPACE_JOINING:
            if (sp->now >= s->wait_until) search_next(sp, s); /* that host is gone: another */
            break;
        case VNA_SPACE_IN:
            for (uint32_t k = 0; k < s->nm;)
                if (sp->now - s->m[k].last_seen > sp->member_ttl_ms)
                    s->m[k] = s->m[--s->nm];
                else
                    k++;
            if (sp->now >= s->next_presence) {
                for (uint32_t k = 0; k < s->nm; k++)
                    (void) send_to(sp, s, &s->m[k], VNA_SPACE_M_PRESENCE, 0, 0);
                s->next_presence = sp->now + sp->presence_ms;
            }
            if (sp->now >= s->next_publish) publish(sp, s);
            break;
        default:
            break;
        }
    }
}

uint32_t vna_space_events(const vna_space_t *s, uint64_t since, const vna_space_logent_t **out,
                          uint32_t cap)
{
    uint32_t k = 0;
    if (!s || !out) return 0;
    for (uint32_t i = 0; i < s->log_n && k < cap; i++) {
        const vna_space_logent_t *l =
            &s->log[(s->log_head + VNA_SPACE_LOG - s->log_n + i) % VNA_SPACE_LOG];
        if (l->index > since) out[k++] = l;
    }
    return k;
}
