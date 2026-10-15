/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vfloat.c — Vino floating vouchers. See vfloat.h. */
#include "vfloat.h"

#define SCORE_MAX (1ull << 60)

static void put32(uint8_t *p, uint32_t v)
{
    for (uint32_t i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) v |= (uint32_t) p[i] << (8 * i);
    return v;
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * i);
    return v;
}

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

void vf_init(vf_t *f, const vna_identity_t *me, uint32_t epoch)
{
    zero(f, (uint32_t) sizeof *f);
    f->me = me;
    f->epoch = epoch;
}

void vf_set_letter_hook(vf_t *f, vf_letter_fn fn, void *ctx)
{
    f->letter = fn;
    f->letter_ctx = ctx;
}

int32_t vf_find_peer(const vf_t *f, const vna_id_t *id)
{
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++)
        if (f->peer[i].used && eq(f->peer[i].id.b, id->b, VNA_ID_LEN)) return (int32_t) i;
    return VF_E_PEER;
}

int32_t vf_add_peer(vf_t *f, const uint8_t pk[VNA_PK_LEN])
{
    vna_id_t id;
    vna_node_id(pk, &id);
    int32_t k = vf_find_peer(f, &id);
    if (k >= 0) return k;
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++) {
        vf_peer_t *p = &f->peer[i];
        if (p->used) continue;
        zero(p, (uint32_t) sizeof *p);
        p->used = true;
        p->id = id;
        cpy(p->pk, pk, VNA_PK_LEN);
        for (uint32_t r = 0; r < VF_RES_COUNT; r++) p->cap[r] = VF_DEFAULT_CAP;
        return (int32_t) i;
    }
    return VF_E_PEER;
}

static vf_peer_t *peer_at(vf_t *f, uint32_t peer)
{
    if (peer >= VF_MAX_PEERS || !f->peer[peer].used) return 0;
    return &f->peer[peer];
}

vf_status_t vf_set_cap(vf_t *f, uint32_t peer, uint8_t resource, uint64_t cap)
{
    vf_peer_t *p = peer_at(f, peer);
    if (!p) return VF_E_PEER;
    if (resource >= VF_RES_COUNT || cap > VF_CAP_LIMIT) return VF_E_ARG;
    p->cap[resource] = cap;
    return VF_OK;
}

void vf_encode_body(const vf_voucher_t *v, uint8_t out[VF_BODY_LEN])
{
    uint8_t *p = out;
    p[0] = 'V', p[1] = 'F', p[2] = 'L', p[3] = 'T';
    p[4] = VF_VERSION;
    p[5] = v->resource;
    p[6] = 0, p[7] = 0;
    p += 8;
    cpy(p, v->issuer.b, 32), p += 32;
    cpy(p, v->bearer.b, 32), p += 32;
    cpy(p, v->scope, VNA_CID_RAW_LEN), p += VNA_CID_RAW_LEN;
    cpy(p, v->letter, 32), p += 32;
    put64(p, v->units), p += 8;
    put64(p, v->pair_seq), p += 8;
    cpy(p, v->prev, 32), p += 32;
    put32(p, v->issued_epoch), p += 4;
    put32(p, v->expiry_epoch);
}

vf_status_t vf_decode(const uint8_t *in, uint32_t len, vf_voucher_t *v)
{
    if (!in || len != VF_VOUCHER_LEN) return VF_E_DECODE;
    if (in[0] != 'V' || in[1] != 'F' || in[2] != 'L' || in[3] != 'T') return VF_E_DECODE;
    if (in[4] != VF_VERSION || in[6] != 0 || in[7] != 0) return VF_E_DECODE;
    if (in[5] >= VF_RES_COUNT) return VF_E_DECODE;
    const uint8_t *p = in + 8;
    v->resource = in[5];
    cpy(v->issuer.b, p, 32), p += 32;
    cpy(v->bearer.b, p, 32), p += 32;
    cpy(v->scope, p, VNA_CID_RAW_LEN), p += VNA_CID_RAW_LEN;
    cpy(v->letter, p, 32), p += 32;
    v->units = get64(p), p += 8;
    v->pair_seq = get64(p), p += 8;
    cpy(v->prev, p, 32), p += 32;
    v->issued_epoch = get32(p), p += 4;
    v->expiry_epoch = get32(p);
    return VF_OK;
}

uint64_t vf_open_units(const vf_t *f, uint32_t peer, uint8_t resource, uint8_t dir)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        const vf_open_t *o = &f->open[i];
        if (o->used && o->peer == peer && o->resource == resource && o->dir == dir) s += o->units;
    }
    return s;
}

int64_t vf_net(const vf_t *f, uint32_t peer, uint8_t resource)
{
    /* Both sides are at most VF_CAP_LIMIT (2^62): the difference fits. */
    return (int64_t) vf_open_units(f, peer, resource, VF_DIR_HELD) -
           (int64_t) vf_open_units(f, peer, resource, VF_DIR_OWED);
}

static int32_t free_open(const vf_t *f)
{
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++)
        if (!f->open[i].used) return (int32_t) i;
    return -1;
}

static void record(vf_t *f, int32_t slot, uint32_t peer, uint8_t dir, const vf_voucher_t *v)
{
    vf_open_t *o = &f->open[slot];
    o->used = true;
    o->peer = (uint8_t) peer;
    o->dir = dir;
    o->resource = v->resource;
    o->expiry = v->expiry_epoch;
    o->seq = v->pair_seq;
    o->units = v->units;
}

vf_status_t vf_issue(vf_t *f, uint32_t peer, uint8_t resource, const uint8_t scope[VNA_CID_RAW_LEN],
                     const uint8_t letter[32], uint64_t units, uint32_t lifetime,
                     const uint8_t *rnd, uint8_t out[VF_VOUCHER_LEN])
{
    if (f->sealed) return VF_E_SEALED;
    vf_peer_t *p = peer_at(f, peer);
    if (!p) return VF_E_PEER;
    if (resource >= VF_RES_COUNT || !scope || !letter || !out || units == 0 || lifetime == 0)
        return VF_E_ARG;
    if (lifetime > UINT32_MAX - f->epoch) return VF_E_ARG;
    uint64_t owed = vf_open_units(f, peer, resource, VF_DIR_OWED);
    if (units > p->cap[resource] || owed > p->cap[resource] - units) return VF_E_CAP;
    int32_t slot = free_open(f);
    if (slot < 0) return VF_E_FULL;
    if (!f->letter || !f->letter(f->letter_ctx, letter, &p->id, resource, units, true))
        return VF_E_LETTER;

    vf_voucher_t v;
    v.resource = resource;
    v.issuer = f->me->id;
    v.bearer = p->id;
    cpy(v.scope, scope, VNA_CID_RAW_LEN);
    cpy(v.letter, letter, 32);
    v.units = units;
    v.pair_seq = p->out_seq + 1;
    cpy(v.prev, p->out_prev, 32);
    v.issued_epoch = f->epoch;
    v.expiry_epoch = f->epoch + lifetime;
    vf_encode_body(&v, out);
    vna_sign(f->me->sk, VF_CTX, out, VF_BODY_LEN, rnd, out + VF_BODY_LEN);

    record(f, slot, peer, VF_DIR_OWED, &v);
    p->out_seq = v.pair_seq;
    vna_sha3(out, VF_BODY_LEN, p->out_prev);
    p->score = (units >= SCORE_MAX - p->score) ? SCORE_MAX : p->score + units;
    return VF_OK;
}

vf_status_t vf_issue_chunk(vf_t *f, uint32_t peer, const uint8_t scope[VNA_CID_RAW_LEN],
                           const uint8_t letter[32], const uint8_t chunk_cid[VNA_CID_RAW_LEN],
                           const uint8_t *data, uint32_t len, uint32_t lifetime,
                           const uint8_t *rnd, uint8_t out[VF_VOUCHER_LEN])
{
    if (!chunk_cid || !data || len == 0) return VF_E_ARG;
    uint8_t cid[VNA_CID_RAW_LEN];
    vna_cid_raw(data, len, cid);
    if (!eq(cid, chunk_cid, VNA_CID_RAW_LEN)) return VF_E_CHUNK;
    uint64_t sectors = ((uint64_t) len + VF_SECTOR_BYTES - 1) / VF_SECTOR_BYTES;
    return vf_issue(f, peer, VF_RES_TRANSFER, scope, letter, sectors, lifetime, rnd, out);
}

vf_status_t vf_accept(vf_t *f, const uint8_t *in, uint32_t len)
{
    if (f->sealed) return VF_E_SEALED;
    vf_voucher_t v;
    vf_status_t st = vf_decode(in, len, &v);
    if (st != VF_OK) return st;
    if (!eq(v.bearer.b, f->me->id.b, VNA_ID_LEN)) return VF_E_BEARER;
    int32_t k = vf_find_peer(f, &v.issuer);
    if (k < 0) return VF_E_PEER;
    vf_peer_t *p = &f->peer[k];
    if (v.pair_seq != p->in_seq + 1 || !eq(v.prev, p->in_prev, 32)) return VF_E_SEQ;
    if (v.units == 0 || v.issued_epoch > v.expiry_epoch) return VF_E_DECODE;
    if (!vna_verify(p->pk, VF_CTX, in, VF_BODY_LEN, in + VF_BODY_LEN)) return VF_E_SIG;
    if (v.expiry_epoch <= f->epoch || (f->ever_sealed && v.issued_epoch <= f->seal_epoch)) {
        /* Signed, in sequence, but its window is gone: it takes its place in
         * the chain (so the next one can follow) and evaporates. */
        p->in_seq = v.pair_seq;
        vna_sha3(in, VF_BODY_LEN, p->in_prev);
        f->evaporated += v.units;
        return VF_E_EXPIRED;
    }
    uint64_t held = vf_open_units(f, (uint32_t) k, v.resource, VF_DIR_HELD);
    if (v.units > p->cap[v.resource] || held > p->cap[v.resource] - v.units) return VF_E_CAP;
    int32_t slot = free_open(f);
    if (slot < 0) return VF_E_FULL;
    if (!f->letter || !f->letter(f->letter_ctx, v.letter, &v.bearer, v.resource, v.units, false))
        return VF_E_LETTER;

    record(f, slot, (uint32_t) k, VF_DIR_HELD, &v);
    p->in_seq = v.pair_seq;
    vna_sha3(in, VF_BODY_LEN, p->in_prev);
    return VF_OK;
}

/* The open voucher of (peer, resource, dir) that goes first: lowest expiry,
 * then lowest seq. -1 when none. */
static int32_t oldest(const vf_t *f, uint32_t peer, uint8_t resource, uint8_t dir)
{
    int32_t best = -1;
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        const vf_open_t *o = &f->open[i];
        if (!o->used || o->peer != peer || o->resource != resource || o->dir != dir) continue;
        if (best < 0 || o->expiry < f->open[best].expiry ||
            (o->expiry == f->open[best].expiry && o->seq < f->open[best].seq))
            best = (int32_t) i;
    }
    return best;
}

static void cancel(vf_t *f, uint32_t peer, uint8_t resource, uint8_t dir, uint64_t m)
{
    while (m > 0) {
        int32_t i = oldest(f, peer, resource, dir);
        if (i < 0) return;
        vf_open_t *o = &f->open[i];
        uint64_t c = o->units < m ? o->units : m;
        o->units -= c;
        m -= c;
        if (o->units == 0) o->used = false;
    }
}

uint64_t vf_offset(vf_t *f, uint32_t peer, uint8_t resource)
{
    if (peer_at(f, peer) == 0 || resource >= VF_RES_COUNT) return 0;
    uint64_t h = vf_open_units(f, peer, resource, VF_DIR_HELD);
    uint64_t o = vf_open_units(f, peer, resource, VF_DIR_OWED);
    uint64_t m = h < o ? h : o;
    cancel(f, peer, resource, VF_DIR_HELD, m);
    cancel(f, peer, resource, VF_DIR_OWED, m);
    return m;
}

vf_status_t vf_tick(vf_t *f, uint32_t epoch)
{
    if (epoch < f->epoch) return VF_E_ARG;
    uint32_t d = epoch - f->epoch;
    f->epoch = epoch;
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++) {
        vf_peer_t *p = &f->peer[i];
        if (!p->used) continue;
        /* Stops at zero: 86 steps of 13/21 take even SCORE_MAX there. */
        for (uint32_t s = 0; s < d && p->score; s++)
            p->score = p->score * VF_DECAY_NUM / VF_DECAY_DEN;
    }
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        vf_open_t *o = &f->open[i];
        if (o->used && o->expiry <= epoch) {
            f->evaporated += o->units;
            o->used = false;
        }
    }
    return VF_OK;
}

uint32_t vf_weight(const vf_t *f, uint32_t peer)
{
    if (peer >= VF_MAX_PEERS || !f->peer[peer].used) return 0;
    uint64_t s = f->peer[peer].score;
    /* s <= 2^60, so 13 * s fits; the quotient is below 13 for any s. */
    return VF_PRIO_BASE + (uint32_t) ((VF_PRIO_BOOST * s) / (s + VF_PRIO_HALF));
}

vf_status_t vf_split(const vf_t *f, const uint32_t *idx, uint32_t n, uint32_t slots,
                     uint32_t *out)
{
    if (!idx || !out || n == 0 || n > VF_MAX_PEERS) return VF_E_ARG;
    uint64_t sum = 0;
    uint64_t rem[VF_MAX_PEERS];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w = vf_weight(f, idx[i]);
        if (w == 0) return VF_E_ARG;
        sum += w;
    }
    uint64_t given = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t x = (uint64_t) slots * vf_weight(f, idx[i]);
        out[i] = (uint32_t) (x / sum);
        rem[i] = x % sum;
        given += out[i];
    }
    /* Largest remainder; ties go to the earlier peer in idx. */
    while (given < slots) {
        uint32_t b = 0;
        for (uint32_t i = 1; i < n; i++)
            if (rem[i] > rem[b]) b = i;
        out[b]++;
        rem[b] = 0;
        given++;
    }
    return VF_OK;
}

uint64_t vf_seal(vf_t *f)
{
    uint64_t gone = 0;
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        if (f->open[i].used) gone += f->open[i].units;
        zero(&f->open[i], (uint32_t) sizeof f->open[i]);
    }
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++) f->peer[i].score = 0;
    f->evaporated += gone;
    f->sealed = true;
    f->ever_sealed = true;
    f->seal_epoch = f->epoch;
    return gone;
}

vf_status_t vf_unseal(vf_t *f)
{
    if (f->sealed && f->epoch <= f->seal_epoch) return VF_E_SEALED;
    f->sealed = false;
    return VF_OK;
}
