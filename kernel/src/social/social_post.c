/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_post.c — signed social records. See social_post.h P1-P9.
 * Freestanding: no libc, no allocation, no floating point. This file never
 * prints anything, so a DM body (P9) cannot reach a log from here. */
#include "social_post.h"
#include "../robin_debanks/sha256.h"

#define F_WALL     0x01u
#define F_PARENT   0x02u
#define F_AUDIENCE 0x04u
#define F_KNOWN    (F_WALL | F_PARENT | F_AUDIENCE)
#define E_PK       0x01u

static const uint8_t MAGIC[4] = {'Z', 'X', 'S', '1'};

/* ---------------- bytes ---------------- */

bool sp_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

int32_t sp_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

void sp_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static bool all_zero(const uint8_t *p, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= p[i];
    return d == 0;
}

/* ---------------- clock ---------------- */

uint64_t sp_clock_next(sp_clock_t *c)
{
    if (c->t != UINT64_MAX) c->t++;
    return c->t;
}

void sp_clock_observe(sp_clock_t *c, uint64_t remote)
{
    if (remote > c->t) c->t = remote;
}

/* ---------------- fields ---------------- */

void sp_node_id(const uint8_t pk[SP_PK_LEN], uint8_t out[SP_NODEID_LEN])
{
    sha256(pk, SP_PK_LEN, out);
}

void sp_init(sp_record_t *r, sp_kind_t kind, sp_vis_t vis, const uint8_t author[SP_NODEID_LEN],
             uint64_t lamport)
{
    zero(r, (uint32_t) sizeof *r);
    r->kind = (uint8_t) kind;
    r->vis = (uint8_t) vis;
    if (author) sp_copy(r->author, author, SP_NODEID_LEN);
    r->lamport = lamport;
}

bool sp_set_body(sp_record_t *r, const uint8_t *body, uint32_t len)
{
    if (len > SP_BODY_MAX || (!body && len)) return false;
    sp_copy(r->body, body, len);
    r->body_len = (uint16_t) len;
    return true;
}

bool sp_set_parent(sp_record_t *r, const uint8_t parent[SP_ID_LEN])
{
    if (!parent) return false;
    sp_copy(r->parent, parent, SP_ID_LEN);
    r->has_parent = true;
    return true;
}

bool sp_set_audience(sp_record_t *r, const uint8_t audience[SP_NODEID_LEN])
{
    if (!audience) return false;
    sp_copy(r->audience, audience, SP_NODEID_LEN);
    r->has_audience = true;
    return true;
}

bool sp_add_media(sp_record_t *r, const uint8_t *cid, uint32_t len)
{
    if (!cid || len == 0 || len > SP_CID_MAX || r->n_media >= SP_MEDIA_MAX) return false;
    sp_copy(r->media[r->n_media], cid, len);
    r->media_len[r->n_media] = (uint8_t) len;
    r->n_media++;
    return true;
}

void sp_set_wall(sp_record_t *r, uint64_t wall_ms)
{
    r->wall_ms = wall_ms;
    r->has_wall = true;
}

void sp_attach_pk(sp_record_t *r, const uint8_t pk[SP_PK_LEN])
{
    sp_copy(r->pk, pk, SP_PK_LEN);
    r->has_pk = true;
}

/* ---------------- P7 ---------------- */

int32_t sp_validate(const sp_record_t *r)
{
    if (!r) return SP_ERR_ARG;
    if (r->kind < SP_KIND_POST || r->kind > SP_KIND_LAST) return SP_ERR_FORMAT;
    if (r->vis > SP_VIS_GROUP) return SP_ERR_FORMAT;
    if (r->body_len > SP_BODY_MAX || r->n_media > SP_MEDIA_MAX) return SP_ERR_RANGE;
    for (uint32_t i = 0; i < r->n_media; i++)
        if (r->media_len[i] == 0 || r->media_len[i] > SP_CID_MAX) return SP_ERR_RANGE;
    if ((r->vis == SP_VIS_LIST || r->vis == SP_VIS_PRIVATE || r->vis == SP_VIS_GROUP) &&
        !r->has_audience)
        return SP_ERR_RULE;
    bool content = r->body_len > 0 || r->n_media > 0;
    switch (r->kind) {
    case SP_KIND_POST:
        if (r->has_parent || !content) return SP_ERR_RULE;
        break;
    case SP_KIND_REPLY:
        if (!r->has_parent || !content) return SP_ERR_RULE;
        break;
    case SP_KIND_REACTION:
        if (!r->has_parent || r->body_len == 0 || r->body_len > SP_REACT_MAX || r->n_media)
            return SP_ERR_RULE;
        break;
    case SP_KIND_DM:
        if (r->vis != SP_VIS_PRIVATE || !r->has_audience || r->body_len == 0 || r->n_media ||
            r->has_parent)
            return SP_ERR_RULE;
        break;
    case SP_KIND_PRESENCE:
        if (r->has_parent || r->body_len > SP_STATUS_MAX || r->n_media) return SP_ERR_RULE;
        break;
    case SP_KIND_FOLLOW:
        if (!r->has_audience || r->has_parent || r->body_len != 1 || r->body[0] > 1 || r->n_media)
            return SP_ERR_RULE;
        break;
    case SP_KIND_BUCKET_LISTING:
        if (r->has_parent || r->n_media) return SP_ERR_RULE;
        return sb_check_listing_body(r->body, r->body_len);
    case SP_KIND_RATING:
        if (!r->has_parent || r->n_media) return SP_ERR_RULE;
        return sb_check_rating_body(r->body, r->body_len);
    case SP_KIND_GROUP_CREATE:
    case SP_KIND_GROUP_INVITE:
    case SP_KIND_GROUP_JOIN:
    case SP_KIND_GROUP_LEAVE:
    case SP_KIND_GROUP_ROLE:
    case SP_KIND_GROUP_CHARTER:
        return sg_check_record(r);
    default:
        return SP_ERR_FORMAT;
    }
    return SP_OK;
}

/* ---------------- P3 writer: into a buffer, or straight into a hash -------- */

typedef struct {
    uint8_t *buf; /* NULL = hash only */
    uint32_t cap, len;
    sha256_ctx_t *h;
    bool over;
} wr_t;

static void w_put(wr_t *w, const uint8_t *p, uint32_t n)
{
    if (w->over) return;
    if (w->h) sha256_update(w->h, p, n);
    if (w->buf) {
        if (n > w->cap - w->len) {
            w->over = true;
            return;
        }
        sp_copy(w->buf + w->len, p, n);
    }
    w->len += n;
}

static void w_u8(wr_t *w, uint8_t v)
{
    w_put(w, &v, 1);
}

static void w_u16(wr_t *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t) (v >> 8), (uint8_t) v};
    w_put(w, b, 2);
}

static void w_u64(wr_t *w, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (56 - 8 * i));
    w_put(w, b, 8);
}

static void write_signed(wr_t *w, const sp_record_t *r)
{
    uint8_t flags = (uint8_t) ((r->has_wall ? F_WALL : 0) | (r->has_parent ? F_PARENT : 0) |
                               (r->has_audience ? F_AUDIENCE : 0));
    w_put(w, MAGIC, 4);
    w_u8(w, SP_VERSION);
    w_u8(w, r->kind);
    w_u8(w, r->vis);
    w_u8(w, flags);
    w_put(w, r->author, SP_NODEID_LEN);
    w_u64(w, r->lamport);
    if (r->has_wall) w_u64(w, r->wall_ms);
    if (r->has_parent) w_put(w, r->parent, SP_ID_LEN);
    if (r->has_audience) w_put(w, r->audience, SP_NODEID_LEN);
    w_u16(w, r->body_len);
    w_put(w, r->body, r->body_len);
    w_u8(w, r->n_media);
    for (uint32_t i = 0; i < r->n_media; i++) {
        w_u8(w, r->media_len[i]);
        w_put(w, r->media[i], r->media_len[i]);
    }
}

static void id_of(const sp_record_t *r, uint8_t id[SP_ID_LEN])
{
    sha256_ctx_t h;
    wr_t w = {0, 0, 0, &h, false};
    sha256_init(&h);
    write_signed(&w, r);
    sha256_final(&h, id);
}

int32_t sp_compute_id(sp_record_t *r)
{
    int32_t v = sp_validate(r);
    if (v != SP_OK) return v;
    id_of(r, r->id);
    return SP_OK;
}

int32_t sp_sign(sp_record_t *r, const sp_signer_t *s)
{
    if (!r || !s || !s->sign) return SP_ERR_ARG;
    int32_t v = sp_compute_id(r);
    if (v != SP_OK) return v;
    if (!s->sign(s->ctx, r->id, r->sig) || all_zero(r->sig, SP_SIG_LEN)) {
        zero(r->sig, SP_SIG_LEN);
        return SP_ERR_SIG;
    }
    return SP_OK;
}

int32_t sp_verify(const sp_record_t *r, const sp_verifier_t *v)
{
    if (!r || !v || !v->verify) return SP_ERR_ARG;
    int32_t st = sp_validate(r);
    if (st != SP_OK) return st;
    uint8_t id[SP_ID_LEN];
    id_of(r, id);
    if (!sp_eq(id, r->id, SP_ID_LEN)) return SP_ERR_SIG;
    const uint8_t *pk;
    if (r->has_pk) {
        uint8_t nid[SP_NODEID_LEN];
        sp_node_id(r->pk, nid);
        if (!sp_eq(nid, r->author, SP_NODEID_LEN)) return SP_ERR_KEY;
        pk = r->pk;
    } else {
        pk = v->lookup ? v->lookup(v->lookup_ctx, r->author) : 0;
        if (!pk) return SP_ERR_KEY;
        uint8_t nid[SP_NODEID_LEN];
        sp_node_id(pk, nid);
        if (!sp_eq(nid, r->author, SP_NODEID_LEN)) return SP_ERR_KEY;
    }
    return v->verify(v->verify_ctx, pk, id, r->sig) ? SP_OK : SP_ERR_SIG;
}

int32_t sp_encode(const sp_record_t *r, uint8_t *buf, uint32_t cap)
{
    if (!r || !buf) return SP_ERR_ARG;
    int32_t v = sp_validate(r);
    if (v != SP_OK) return v;
    wr_t w = {buf, cap, 0, 0, false};
    write_signed(&w, r);
    w_u8(&w, r->has_pk ? E_PK : 0);
    if (r->has_pk) w_put(&w, r->pk, SP_PK_LEN);
    w_put(&w, r->sig, SP_SIG_LEN);
    if (w.over) return SP_ERR_SPACE;
    return (int32_t) w.len;
}

/* ---------------- P6 reader ---------------- */

typedef struct {
    const uint8_t *p;
    uint32_t len, off;
    bool bad;
} rd_t;

static const uint8_t *r_take(rd_t *r, uint32_t n)
{
    if (r->bad || n > r->len - r->off) {
        r->bad = true;
        return 0;
    }
    const uint8_t *p = r->p + r->off;
    r->off += n;
    return p;
}

static uint8_t r_u8(rd_t *r)
{
    const uint8_t *p = r_take(r, 1);
    return p ? p[0] : 0;
}

static uint16_t r_u16(rd_t *r)
{
    const uint8_t *p = r_take(r, 2);
    return p ? (uint16_t) ((p[0] << 8) | p[1]) : 0;
}

static uint64_t r_u64(rd_t *r)
{
    const uint8_t *p = r_take(r, 8);
    uint64_t v = 0;
    if (p)
        for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static bool r_copy(rd_t *r, uint8_t *dst, uint32_t n)
{
    const uint8_t *p = r_take(r, n);
    if (!p) return false;
    sp_copy(dst, p, n);
    return true;
}

static int32_t decode_inner(const uint8_t *buf, uint32_t len, sp_record_t *o,
                            const sp_verifier_t *v)
{
    rd_t r = {buf, len, 0, false};
    const uint8_t *m = r_take(&r, 4);
    if (!m || !sp_eq(m, MAGIC, 4)) return SP_ERR_FORMAT;
    if (r_u8(&r) != SP_VERSION) return SP_ERR_FORMAT;
    o->kind = r_u8(&r);
    o->vis = r_u8(&r);
    uint8_t flags = r_u8(&r);
    if (r.bad) return SP_ERR_FORMAT;
    if (o->kind < SP_KIND_POST || o->kind > SP_KIND_LAST || o->vis > SP_VIS_GROUP ||
        (flags & ~F_KNOWN))
        return SP_ERR_FORMAT;
    o->has_wall = (flags & F_WALL) != 0;
    o->has_parent = (flags & F_PARENT) != 0;
    o->has_audience = (flags & F_AUDIENCE) != 0;
    r_copy(&r, o->author, SP_NODEID_LEN);
    o->lamport = r_u64(&r);
    if (o->has_wall) o->wall_ms = r_u64(&r);
    if (o->has_parent) r_copy(&r, o->parent, SP_ID_LEN);
    if (o->has_audience) r_copy(&r, o->audience, SP_NODEID_LEN);
    uint16_t bl = r_u16(&r);
    if (r.bad) return SP_ERR_FORMAT;
    if (bl > SP_BODY_MAX) return SP_ERR_RANGE;
    o->body_len = bl;
    if (!r_copy(&r, o->body, bl)) return SP_ERR_FORMAT;
    uint8_t nm = r_u8(&r);
    if (r.bad) return SP_ERR_FORMAT;
    if (nm > SP_MEDIA_MAX) return SP_ERR_RANGE;
    o->n_media = nm;
    for (uint32_t i = 0; i < nm; i++) {
        uint8_t ml = r_u8(&r);
        if (r.bad) return SP_ERR_FORMAT;
        if (ml == 0 || ml > SP_CID_MAX) return SP_ERR_RANGE;
        o->media_len[i] = ml;
        if (!r_copy(&r, o->media[i], ml)) return SP_ERR_FORMAT;
    }
    uint32_t signed_len = r.off;
    uint8_t env = r_u8(&r);
    if (r.bad) return SP_ERR_FORMAT;
    if (env & ~E_PK) return SP_ERR_FORMAT;
    o->has_pk = (env & E_PK) != 0;
    if (o->has_pk && !r_copy(&r, o->pk, SP_PK_LEN)) return SP_ERR_FORMAT;
    if (!r_copy(&r, o->sig, SP_SIG_LEN)) return SP_ERR_FORMAT;
    if (r.off != len) return SP_ERR_TRAILING;
    int32_t st = sp_validate(o);
    if (st != SP_OK) return st;
    sha256(buf, signed_len, o->id);
    return sp_verify(o, v);
}

int32_t sp_decode(const uint8_t *buf, uint32_t len, sp_record_t *out, const sp_verifier_t *v)
{
    if (!out) return SP_ERR_ARG;
    zero(out, (uint32_t) sizeof *out);
    if (!buf || !v || !v->verify) return SP_ERR_ARG;
    int32_t st = decode_inner(buf, len, out, v);
    if (st != SP_OK) zero(out, (uint32_t) sizeof *out);
    return st;
}

/* ---------------- P8 ---------------- */

bool sp_may_share(const sp_record_t *r, const uint8_t self[SP_NODEID_LEN],
                  const uint8_t peer[SP_NODEID_LEN], const sp_policy_t *p)
{
    if (!r || !self || !peer || !p) return false;
    if (sp_validate(r) != SP_OK) return false;
    /* Handing someone their own record back discloses nothing. */
    if (sp_eq(peer, r->author, SP_NODEID_LEN)) return true;
    bool authored = sp_eq(self, r->author, SP_NODEID_LEN);
    bool base;
    switch (r->vis) {
    case SP_VIS_PUBLIC:
        base = true;
        break;
    case SP_VIS_FOLLOWERS:
        base = authored && p->is_follower && p->is_follower(p->ctx, r->author, peer);
        break;
    case SP_VIS_LIST:
        base = authored && r->has_audience && p->in_list &&
               p->in_list(p->ctx, r->author, r->audience, peer);
        break;
    case SP_VIS_PRIVATE:
        base = r->has_audience && sp_eq(peer, r->audience, SP_NODEID_LEN);
        break;
    case SP_VIS_GROUP:
        base = r->has_audience && p->in_group && p->in_group(p->ctx, r->audience, self) &&
               p->in_group(p->ctx, r->audience, peer);
        break;
    default:
        base = false;
    }
    if (!base) return false;
    return p->agree ? p->agree(p->ctx, r, peer) : true;
}
