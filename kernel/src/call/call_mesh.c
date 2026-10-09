/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_mesh.c — deterministic relay forest for decentralized group calls. */
#include "call_mesh.h"
#include "call_common.h"
#include "call_rtp.h"

#define NOT_IN_TREE 0xffu
#define MAX_TABLE   4096u

void call_mesh_init(call_mesh_t *m, uint32_t group_id, call_mesh_member_t *table, uint32_t cap,
                    call_mesh_verify_fn verify, void *vctx)
{
    m->group_id = group_id;
    m->members = table;
    m->cap = cap > MAX_TABLE ? MAX_TABLE : cap;
    m->n = 0;
    m->max_depth = 6;
    m->verify = verify;
    m->vctx = vctx;
    m->changes = 0;
}

/* ---- wire format ---- */

static uint32_t member_body_len(const call_mesh_member_t *s)
{
    uint32_t len = CALL_MESH_HDR_LEN + 6;
    for (uint32_t i = 0; i < s->nstreams; i++) len += 6u + 2u * s->streams[i].nlayers;
    return len + 2u + 5u * s->nsubs;
}

static void put_hdr(uint8_t *o, uint8_t type, uint32_t group, const call_mesh_member_t *s)
{
    call_put16(o, CALL_MESH_MAGIC);
    o[2] = CALL_MESH_VERSION;
    o[3] = type;
    call_put32(o + 4, group);
    call_put32(o + 8, s->id);
    call_put32(o + 12, s->version);
    call_copy(o + 16, s->pubkey, CALL_MESH_KEY_LEN);
}

static bool member_valid(const call_mesh_member_t *s)
{
    if (s->id == 0 || s->nstreams > CALL_MESH_MAX_STREAMS || s->nsubs > CALL_MESH_MAX_SUBS)
        return false;
    for (uint32_t i = 0; i < s->nstreams; i++) {
        const call_mesh_stream_t *st = &s->streams[i];
        if (st->id == 0 || st->nlayers == 0 || st->nlayers > CALL_MESH_MAX_LAYERS) return false;
        for (uint32_t l = 0; l < st->nlayers; l++) {
            if (st->kbps[l] == 0) return false;
            if (l && st->kbps[l] <= st->kbps[l - 1]) return false;
        }
        for (uint32_t j = 0; j < i; j++)
            if (s->streams[j].id == st->id) return false;
    }
    for (uint32_t i = 0; i < s->nsubs; i++) {
        if (s->subs[i].stream_id == 0 || s->subs[i].layer >= CALL_MESH_MAX_LAYERS) return false;
        for (uint32_t j = 0; j < i; j++)
            if (s->subs[j].stream_id == s->subs[i].stream_id) return false;
    }
    return true;
}

int call_mesh_encode_member(const call_mesh_member_t *s, uint32_t group_id, uint8_t *out,
                            uint32_t cap, call_mesh_sign_fn sign, void *sctx)
{
    if (!s || !out || !sign || !member_valid(s)) return CALL_ERR_ARG;
    uint32_t body = member_body_len(s);
    if (cap < body + CALL_MESH_SIG_LEN) return CALL_ERR_SPACE;
    put_hdr(out, CALL_MESH_MSG_MEMBER, group_id, s);
    uint8_t *p = out + CALL_MESH_HDR_LEN;
    call_put32(p, s->uplink_kbps);
    p[4] = s->fanout;
    p[5] = s->nstreams;
    p += 6;
    for (uint32_t i = 0; i < s->nstreams; i++) {
        const call_mesh_stream_t *st = &s->streams[i];
        call_put32(p, st->id);
        p[4] = st->kind;
        p[5] = st->nlayers;
        p += 6;
        for (uint32_t l = 0; l < st->nlayers; l++, p += 2) call_put16(p, st->kbps[l]);
    }
    call_put16(p, s->nsubs);
    p += 2;
    for (uint32_t i = 0; i < s->nsubs; i++, p += 5) {
        call_put32(p, s->subs[i].stream_id);
        p[4] = s->subs[i].layer;
    }
    if (!sign(sctx, out, body, out + body)) return CALL_ERR_AUTH;
    return (int) (body + CALL_MESH_SIG_LEN);
}

int call_mesh_encode_leave(const call_mesh_member_t *s, uint32_t group_id, uint8_t *out,
                           uint32_t cap, call_mesh_sign_fn sign, void *sctx)
{
    if (!s || !out || !sign || s->id == 0) return CALL_ERR_ARG;
    if (cap < CALL_MESH_HDR_LEN + CALL_MESH_SIG_LEN) return CALL_ERR_SPACE;
    put_hdr(out, CALL_MESH_MSG_LEAVE, group_id, s);
    if (!sign(sctx, out, CALL_MESH_HDR_LEN, out + CALL_MESH_HDR_LEN)) return CALL_ERR_AUTH;
    return CALL_MESH_HDR_LEN + CALL_MESH_SIG_LEN;
}

int call_mesh_parse(const uint8_t *msg, uint32_t len, uint32_t *group_id, uint8_t *type,
                    call_mesh_member_t *out)
{
    if (!msg || !out || !type || !group_id) return CALL_ERR_ARG;
    if (len < CALL_MESH_HDR_LEN + CALL_MESH_SIG_LEN) return CALL_ERR_SHORT;
    if (call_get16(msg) != CALL_MESH_MAGIC || msg[2] != CALL_MESH_VERSION) return CALL_ERR_FORMAT;
    call_fill(out, 0, sizeof(*out));
    *type = msg[3];
    *group_id = call_get32(msg + 4);
    out->id = call_get32(msg + 8);
    out->version = call_get32(msg + 12);
    call_copy(out->pubkey, msg + 16, CALL_MESH_KEY_LEN);
    if (out->id == 0) return CALL_ERR_FORMAT;
    if (*type == CALL_MESH_MSG_LEAVE)
        return len == CALL_MESH_HDR_LEN + CALL_MESH_SIG_LEN ? CALL_OK : CALL_ERR_FORMAT;
    if (*type != CALL_MESH_MSG_MEMBER) return CALL_ERR_FORMAT;
    uint32_t body = len - CALL_MESH_SIG_LEN, o = CALL_MESH_HDR_LEN;
    if (o + 6 > body) return CALL_ERR_SHORT;
    out->uplink_kbps = call_get32(msg + o);
    out->fanout = msg[o + 4];
    out->nstreams = msg[o + 5];
    o += 6;
    if (out->nstreams > CALL_MESH_MAX_STREAMS) return CALL_ERR_FORMAT;
    for (uint32_t i = 0; i < out->nstreams; i++) {
        if (o + 6 > body) return CALL_ERR_SHORT;
        call_mesh_stream_t *st = &out->streams[i];
        st->id = call_get32(msg + o);
        st->kind = msg[o + 4];
        st->nlayers = msg[o + 5];
        o += 6;
        if (st->nlayers == 0 || st->nlayers > CALL_MESH_MAX_LAYERS) return CALL_ERR_FORMAT;
        if (o + 2u * st->nlayers > body) return CALL_ERR_SHORT;
        for (uint32_t l = 0; l < st->nlayers; l++, o += 2) st->kbps[l] = call_get16(msg + o);
    }
    if (o + 2 > body) return CALL_ERR_SHORT;
    out->nsubs = call_get16(msg + o);
    o += 2;
    if (out->nsubs > CALL_MESH_MAX_SUBS) return CALL_ERR_FORMAT;
    if (o + 5u * out->nsubs != body) return CALL_ERR_FORMAT;
    for (uint32_t i = 0; i < out->nsubs; i++, o += 5) {
        out->subs[i].stream_id = call_get32(msg + o);
        out->subs[i].layer = msg[o + 4];
    }
    return member_valid(out) ? CALL_OK : CALL_ERR_FORMAT;
}

/* ---- membership ---- */

static int32_t find_idx(const call_mesh_t *m, uint32_t id)
{
    uint32_t lo = 0, hi = m->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (m->members[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (lo < m->n && m->members[lo].id == id) ? (int32_t) lo : -(int32_t) lo - 1;
}

int call_mesh_on_message(call_mesh_t *m, const uint8_t *msg, uint32_t len)
{
    if (!m || !msg) return CALL_ERR_ARG;
    call_mesh_member_t in;
    uint32_t group;
    uint8_t type;
    int r = call_mesh_parse(msg, len, &group, &type, &in);
    if (r < 0) return r;
    if (group != m->group_id) return CALL_ERR_ARG;
    if (!m->verify ||
        !m->verify(m->vctx, in.pubkey, msg, len - CALL_MESH_SIG_LEN, msg + len - CALL_MESH_SIG_LEN))
        return CALL_ERR_AUTH;
    int32_t at = find_idx(m, in.id);
    if (at >= 0) {
        call_mesh_member_t *cur = &m->members[at];
        if (call_cmp(cur->pubkey, in.pubkey, CALL_MESH_KEY_LEN)) return CALL_ERR_AUTH;
        if (in.version <= cur->version) return 0;
        if (type == CALL_MESH_MSG_LEAVE) {
            cur->version = in.version;
            if (!cur->alive) return 0;
            cur->alive = 0;
        } else {
            CALL_SET(*cur, in);
            cur->alive = 1;
        }
        m->changes++;
        return 1;
    }
    if (m->n >= m->cap) return CALL_ERR_FULL;
    uint32_t pos = (uint32_t) (-at - 1);
    for (uint32_t i = m->n; i > pos; i--) CALL_SET(m->members[i], m->members[i - 1]);
    CALL_SET(m->members[pos], in);
    m->members[pos].alive = (type == CALL_MESH_MSG_MEMBER) ? 1 : 0; /* leave = tombstone */
    m->n++;
    if (!m->members[pos].alive) return 0;
    m->changes++;
    return 1;
}

int call_mesh_mark_failed(call_mesh_t *m, uint32_t member_id)
{
    int32_t at = find_idx(m, member_id);
    if (at < 0 || !m->members[at].alive) return 0;
    m->members[at].alive = 0;
    m->changes++;
    return 1;
}

uint32_t call_mesh_alive(const call_mesh_t *m)
{
    uint32_t a = 0;
    for (uint32_t i = 0; i < m->n; i++) a += m->members[i].alive;
    return a;
}

/* ---- builder ---- */

uint32_t call_mesh_scratch_words(const call_mesh_t *m)
{
    uint32_t d = 0;
    for (uint32_t i = 0; i < m->n; i++) d += m->members[i].nsubs;
    return 3u * m->n + 2u * d;
}

/* demand words: w0 = pub_idx << 16 | stream_idx, w1 = sub_idx << 16 | layer << 8 | state */
#define D_PENDING 1u
#define D_DONE    2u

typedef struct key {
    uint32_t cls; /* audio 0, data 1, video and anything else 2 */
    uint32_t kbps;
    uint32_t stream;
    uint32_t layer;
} mesh_key_t;

static bool key_before(const mesh_key_t *a, const mesh_key_t *b)
{
    if (a->cls != b->cls) return a->cls < b->cls;
    if (a->kbps != b->kbps) return a->kbps > b->kbps;
    if (a->stream != b->stream) return a->stream < b->stream;
    return a->layer < b->layer;
}

static void fnv(uint32_t *h, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        *h ^= (v >> (8 * i)) & 0xff;
        *h *= 16777619u;
    }
}

static mesh_key_t demand_key(const call_mesh_t *m, const uint32_t *dm, uint32_t i)
{
    uint32_t pub = dm[2 * i] >> 16, si = dm[2 * i] & 0xffff, layer = (dm[2 * i + 1] >> 8) & 0xff;
    const call_mesh_stream_t *st = &m->members[pub].streams[si];
    uint32_t cls = st->kind == CALL_STREAM_AUDIO ? 0u : (st->kind == CALL_STREAM_DATA ? 1u : 2u);
    mesh_key_t k = {cls, st->kbps[layer], st->id, layer};
    return k;
}

int call_mesh_build(const call_mesh_t *m, call_mesh_plan_t *p, uint32_t *scratch,
                    uint32_t scratch_words)
{
    if (!m || !p || !scratch) return CALL_ERR_ARG;
    if (scratch_words < call_mesh_scratch_words(m) || p->up_cap < m->n) return CALL_ERR_SPACE;
    uint32_t n = m->n;
    uint32_t *resid = scratch, *fan = scratch + n, *depth = scratch + 2 * n;
    uint32_t *dm = scratch + 3 * n;
    p->nedges = p->ndl = 0;
    p->served = p->degraded = p->unserved = 0;
    p->max_depth_seen = 0;
    p->hash = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        resid[i] = m->members[i].alive ? m->members[i].uplink_kbps : 0;
        fan[i] = 0;
    }

    /* 1. demands, in member order */
    uint32_t nd = 0;
    for (uint32_t s = 0; s < n; s++) {
        const call_mesh_member_t *sub = &m->members[s];
        if (!sub->alive) continue;
        for (uint32_t k = 0; k < sub->nsubs; k++) {
            uint32_t sid = sub->subs[k].stream_id;
            int32_t pub = -1, si = -1;
            for (uint32_t q = 0; q < n && pub < 0; q++) {
                if (!m->members[q].alive) continue;
                for (uint32_t t = 0; t < m->members[q].nstreams; t++)
                    if (m->members[q].streams[t].id == sid) {
                        pub = (int32_t) q;
                        si = (int32_t) t;
                        break;
                    }
            }
            if (pub < 0 || (uint32_t) pub == s) continue;
            const call_mesh_stream_t *st = &m->members[pub].streams[si];
            uint32_t want = sub->subs[k].layer;
            if (want >= st->nlayers) want = st->nlayers - 1u;
            if (p->ndl >= p->dl_cap) return CALL_ERR_SPACE;
            call_mesh_delivery_t *d = &p->dl[p->ndl++];
            d->member = sub->id;
            d->stream_id = sid;
            d->publisher = m->members[pub].id;
            d->want = (uint8_t) want;
            d->got = CALL_MESH_NO_LAYER;
            dm[2 * nd] = ((uint32_t) pub << 16) | (uint32_t) si;
            dm[2 * nd + 1] = (s << 16) | (want << 8) | D_PENDING;
            nd++;
        }
    }

    /* 2. trees, highest bitrate first */
    for (;;) {
        bool any = false;
        mesh_key_t best = {0, 0, 0, 0};
        for (uint32_t i = 0; i < nd; i++) {
            if (!(dm[2 * i + 1] & D_PENDING)) continue;
            mesh_key_t k = demand_key(m, dm, i);
            if (!any || key_before(&k, &best)) {
                best = k;
                any = true;
            }
        }
        if (!any) break;
        for (uint32_t i = 0; i < n; i++) {
            depth[i] = NOT_IN_TREE;
            fan[i] = 0; /* fanout is a per-stream bound */
        }
        int32_t root = -1;
        for (uint32_t i = 0; i < nd && root < 0; i++) {
            mesh_key_t k = demand_key(m, dm, i);
            if ((dm[2 * i + 1] & D_PENDING) && k.stream == best.stream && k.layer == best.layer)
                root = (int32_t) (dm[2 * i] >> 16);
        }
        depth[root] = 0;
        /* 3. attach subscribers, largest residual uplink first */
        for (;;) {
            int32_t pick = -1;
            for (uint32_t i = 0; i < nd; i++) {
                if (!(dm[2 * i + 1] & D_PENDING)) continue;
                mesh_key_t k = demand_key(m, dm, i);
                if (k.stream != best.stream || k.layer != best.layer) continue;
                if (pick < 0) {
                    pick = (int32_t) i;
                    continue;
                }
                uint32_t a = dm[2 * i + 1] >> 16, b = dm[2 * (uint32_t) pick + 1] >> 16;
                if (resid[a] > resid[b] || (resid[a] == resid[b] && a < b)) pick = (int32_t) i;
            }
            if (pick < 0) break;
            uint32_t pi = (uint32_t) pick;
            uint32_t sub = dm[2 * pi + 1] >> 16;
            uint32_t layer = (dm[2 * pi + 1] >> 8) & 0xff;
            if (depth[sub] != NOT_IN_TREE) {
                dm[2 * pi + 1] = (dm[2 * pi + 1] & ~0xffu) | D_DONE; /* duplicate demand */
                continue;
            }
            /* candidate parent */
            int32_t par = -1;
            {
                for (uint32_t c = 0; c < n; c++) {
                    if (depth[c] == NOT_IN_TREE || !m->members[c].alive) continue;
                    if (resid[c] < best.kbps || fan[c] >= m->members[c].fanout) continue;
                    if (depth[c] + 1u > m->max_depth) continue;
                    if (par < 0) {
                        par = (int32_t) c;
                        continue;
                    }
                    uint32_t pc = (uint32_t) par;
                    if (depth[c] < depth[pc] || (depth[c] == depth[pc] && resid[c] > resid[pc]) ||
                        (depth[c] == depth[pc] && resid[c] == resid[pc] && c < pc))
                        par = (int32_t) c;
                }
            }
            if (par >= 0) {
                uint32_t pc = (uint32_t) par;
                if (p->nedges >= p->edge_cap) return CALL_ERR_SPACE;
                call_mesh_edge_t *e = &p->edges[p->nedges++];
                e->stream_id = best.stream;
                e->from = m->members[pc].id;
                e->to = m->members[sub].id;
                e->kbps = (uint16_t) best.kbps;
                e->layer = (uint8_t) layer;
                e->depth = (uint8_t) (depth[pc] + 1u);
                if (e->depth > p->max_depth_seen) p->max_depth_seen = e->depth;
                resid[pc] -= best.kbps;
                fan[pc]++;
                depth[sub] = depth[pc] + 1u;
                fnv(&p->hash, e->stream_id);
                fnv(&p->hash, e->from);
                fnv(&p->hash, e->to);
                fnv(&p->hash, e->layer);
                p->dl[pi].got = (uint8_t) layer;
                dm[2 * pi + 1] = (dm[2 * pi + 1] & ~0xffu) | D_DONE;
            } else if (layer > 0) {
                /* 4. simulcast fallback: try the next lower layer later */
                dm[2 * pi + 1] = (sub << 16) | ((layer - 1u) << 8) | D_PENDING;
            } else {
                dm[2 * pi + 1] = (dm[2 * pi + 1] & ~0xffu) | D_DONE;
            }
        }
    }
    for (uint32_t i = 0; i < p->ndl; i++) {
        if (p->dl[i].got == CALL_MESH_NO_LAYER)
            p->unserved++;
        else if (p->dl[i].got < p->dl[i].want)
            p->degraded++;
        else
            p->served++;
    }
    for (uint32_t i = 0; i < n; i++)
        p->up_used_kbps[i] = m->members[i].alive ? m->members[i].uplink_kbps - resid[i] : 0;
    return CALL_OK;
}

uint32_t call_mesh_parent(const call_mesh_plan_t *p, uint32_t member, uint32_t stream_id,
                          uint8_t layer)
{
    for (uint32_t i = 0; i < p->nedges; i++) {
        const call_mesh_edge_t *e = &p->edges[i];
        if (e->to == member && e->stream_id == stream_id && e->layer == layer) return e->from;
    }
    return 0;
}

uint32_t call_mesh_children(const call_mesh_plan_t *p, uint32_t member, uint32_t stream_id,
                            uint8_t layer, uint32_t *out, uint32_t max)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < p->nedges; i++) {
        const call_mesh_edge_t *e = &p->edges[i];
        if (e->from == member && e->stream_id == stream_id && e->layer == layer) {
            if (out && c < max) out[c] = e->to;
            c++;
        }
    }
    return c;
}
