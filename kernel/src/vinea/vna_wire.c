/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_wire.c — signed messages and DHT records. See vna_wire.h. */
#include "vna_wire.h"
#include "vna_agree.h"

/* ---- schemas: the single declaration of every wire record ---- */
#define M vna_msg_t
static const vna_field_t msg_fields[] = {
    VNA_FCONST(M, magic, VNA_MSG_MAGIC),
    VNA_FU8(M, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU8(M, type, VNA_MSG_TYPE_MAX, VNA_AXIS_NONE),
    VNA_FU16(M, flags, 0, VNA_AXIS_NONE),
    VNA_FU32(M, features, 0, VNA_AXIS_NONE),
    VNA_FFIX(M, src, VNA_AXIS_PROVENANCE),
    VNA_FFIX(M, dst, VNA_AXIS_PROVENANCE),
    VNA_FU64(M, pow, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(M, seq, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(M, ts, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(M, rpc, 0, VNA_AXIS_NONE),
    VNA_FVAR(M, body, body_len, VNA_AXIS_NONE),
    VNA_FFIX(M, pk, VNA_AXIS_PROVENANCE),
    VNA_FSIG(M, sig),
};
#undef M
/* UBH class 1 = CAUSAL_EVENT */
const vna_schema_t vna_msg_schema = {"message", 0x0001, 1, msg_fields,
                                     sizeof msg_fields / sizeof msg_fields[0]};

static const vna_field_t key_fields[] = {VNA_FFIX(vna_b_key_t, key, VNA_AXIS_NONE)};
const vna_schema_t vna_b_key_schema = {"body_key", 0x0002, 1, key_fields, 1};

static const vna_field_t contact_fields[] = {
    VNA_FFIX(vna_contact_wire_t, id, VNA_AXIS_NONE),
    VNA_FVAR(vna_contact_wire_t, addr, addr_len, VNA_AXIS_NONE),
};
static const vna_schema_t contact_schema = {"contact", 0x0003, 1, contact_fields, 2};

static const vna_field_t nodes_fields[] = {
    VNA_FARR(vna_b_nodes_t, c, n, &contact_schema, vna_contact_wire_t, VNA_AXIS_NONE),
};
const vna_schema_t vna_b_nodes_schema = {"body_nodes", 0x0004, 1, nodes_fields, 1};

static const vna_field_t rec_body_fields[] = {
    VNA_FVAR(vna_b_rec_t, rec, len, VNA_AXIS_NONE),
};
const vna_schema_t vna_b_rec_schema = {"body_record", 0x0005, 1, rec_body_fields, 1};

static const vna_field_t ack_fields[] = {VNA_FU8(vna_b_ack_t, status, 0, VNA_AXIS_NONE)};
const vna_schema_t vna_b_ack_schema = {"body_ack", 0x0006, 1, ack_fields, 1};

static const vna_field_t hk_fields[] = {
    VNA_FU8(vna_b_hk_t, truth, 5, VNA_AXIS_PROVENANCE),
    VNA_FU32(vna_b_hk_t, ordinal, 0, VNA_AXIS_NONE),
    VNA_FHK(vna_b_hk_t, text, len, VNA_AXIS_NONE),
};
const vna_schema_t vna_b_hk_schema = {"body_hk", 0x0007, 1, hk_fields, 3};

#define R vna_rec_t
static const vna_field_t rec_fields[] = {
    VNA_FCONST(R, magic, VNA_REC_MAGIC),
    VNA_FU8(R, rtype, 2, VNA_AXIS_NONE),
    VNA_FFIX(R, key, VNA_AXIS_NONE),
    VNA_FFIX(R, publisher, VNA_AXIS_PROVENANCE),
    VNA_FU64(R, pow, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(R, created, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(R, expires, 0, VNA_AXIS_PROVENANCE),
    VNA_FVAR(R, payload, payload_len, VNA_AXIS_NONE),
    VNA_FFIX(R, pk, VNA_AXIS_PROVENANCE),
    VNA_FSIG(R, sig),
};
#undef R
/* UBH class 3 = CONTENT_OBJECT */
const vna_schema_t vna_rec_schema = {"dht_record", 0x0010, 3, rec_fields,
                                     sizeof rec_fields / sizeof rec_fields[0]};

static const vna_field_t prov_fields[] = {
    VNA_FU64(vna_provider_t, file_size, 0, VNA_AXIS_NONE),
    VNA_FU32(vna_provider_t, chunk_size, 0, VNA_AXIS_NONE),
    VNA_FU32(vna_provider_t, n_chunks, 0, VNA_AXIS_NONE),
    VNA_FU8(vna_provider_t, visibility, 1, VNA_AXIS_NONE),
    VNA_FVAR(vna_provider_t, addr, addr_len, VNA_AXIS_NONE),
};
const vna_schema_t vna_provider_schema = {"provider", 0x0011, 3, prov_fields, 5};

/* ---- dedupe ring ---- */
void vna_dedupe_init(vna_dedupe_t *d, uint8_t (*storage)[32], uint32_t cap)
{
    d->h = storage;
    d->cap = cap;
    d->head = d->n = 0;
}

bool vna_dedupe_seen(const vna_dedupe_t *d, const uint8_t h[32])
{
    if (!d) return false;
    for (uint32_t i = 0; i < d->n; i++)
        if (vna_eq(d->h[i], h, 32)) return true;
    return false;
}

void vna_dedupe_add(vna_dedupe_t *d, const uint8_t h[32])
{
    if (!d || d->cap == 0) return;
    vna_copy(d->h[d->head], h, 32);
    d->head = (d->head + 1u == d->cap) ? 0 : d->head + 1u;
    if (d->n < d->cap) d->n++;
}

/* ---- messages ---- */
int32_t vna_msg_seal(vna_msg_t *m, const vna_identity_t *idn, const uint8_t rnd[32], bool ubh,
                     uint8_t *out, uint32_t cap)
{
    if (!m || !idn || !rnd || !out) return -1;
    uint32_t hdr = ubh ? VNA_UBH_HDR : 0;
    if (cap < hdr) return -1;
    m->magic = VNA_MSG_MAGIC;
    m->version = VNA_VERSION;
    m->src = idn->id;
    m->pow = idn->pow_nonce;
    vna_copy(m->pk, idn->pk, VNA_PK_LEN);
    vna_zero(m->sig, VNA_SIG_LEN);
    int32_t n = vna_schema_pack(&vna_msg_schema, m, out + hdr, cap - hdr, true);
    if (n < (int32_t) VNA_SIG_LEN) return -1;
    uint32_t signed_len = (uint32_t) n - VNA_SIG_LEN; /* the SIG field is last (S7) */
    vna_sign(idn->sk, VNA_CTX_MSG, out + hdr, signed_len, rnd, m->sig);
    vna_copy(out + hdr + signed_len, m->sig, VNA_SIG_LEN);
    if (!ubh) return n;
    return vna_frame_wrap(&vna_msg_schema, out + hdr, (uint32_t) n, out, cap);
}

vna_status_t vna_msg_open(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                          vna_msg_t *m, bool *was_ubh)
{
    const uint8_t *rec;
    uint32_t rlen, sig_off = 0;
    uint8_t h[32];
    if (!v || !buf || !m) return VNA_ERR_ARG;
    /* 1 */
    if (vna_frame_accept(&vna_msg_schema, buf, len, &rec, &rlen, was_ubh) != VNA_OK ||
        vna_schema_unpack(&vna_msg_schema, rec, rlen, m, &sig_off) < 0 || m->type == 0 ||
        m->version != VNA_VERSION) {
        v->rej_parse++;
        return VNA_ERR_PARSE;
    }
    /* 2 */
    bool zero_dst = vna_id_is_zero(&m->dst);
    if ((zero_dst && m->type != VNA_MSG_PING) || (!zero_dst && !vna_id_eq(&m->dst, &v->self)) ||
        vna_id_eq(&m->src, &v->self)) {
        v->rej_dst++;
        return VNA_ERR_DST;
    }
    /* 3 */
    vna_sha3(rec, rlen, h);
    if (vna_dedupe_seen(v->dd, h)) {
        v->rej_dup++;
        return VNA_ERR_DUP;
    }
    /* 4 */
    if (v->rp) {
        vna_status_t st = vna_replay_check(v->rp, &m->src, m->seq, m->ts, now);
        if (st != VNA_OK) {
            if (st == VNA_ERR_STALE)
                v->rej_stale++;
            else
                v->rej_replay++;
            return st;
        }
    }
    /* 5 */
    vna_status_t bs = vna_keycache_check(v->kc, &m->src, m->pk, m->pow, v->pow_bits);
    if (bs != VNA_OK) {
        if (bs == VNA_ERR_POW)
            v->rej_pow++;
        else
            v->rej_binding++;
        return bs;
    }
    /* 6 */
    if (!vna_verify(m->pk, VNA_CTX_MSG, rec, sig_off, m->sig)) {
        v->rej_sig++;
        return VNA_ERR_SIG;
    }
    /* 7 */
    if (v->rp) vna_replay_commit(v->rp, &m->src, m->seq, m->ts);
    vna_dedupe_add(v->dd, h);
    return VNA_OK;
}

/* ---- records ---- */
int32_t vna_rec_seal(vna_rec_t *r, const vna_identity_t *idn, const uint8_t rnd[32], uint8_t *out,
                     uint32_t cap)
{
    if (!r || !idn || !rnd || !out) return -1;
    r->magic = VNA_REC_MAGIC;
    r->publisher = idn->id;
    r->pow = idn->pow_nonce;
    vna_copy(r->pk, idn->pk, VNA_PK_LEN);
    vna_zero(r->sig, VNA_SIG_LEN);
    int32_t n = vna_schema_pack(&vna_rec_schema, r, out, cap, true);
    if (n < (int32_t) VNA_SIG_LEN) return -1;
    uint32_t signed_len = (uint32_t) n - VNA_SIG_LEN;
    vna_sign(idn->sk, VNA_CTX_RECORD, out, signed_len, rnd, r->sig);
    vna_copy(out + signed_len, r->sig, VNA_SIG_LEN);
    return n;
}

vna_status_t vna_rec_verify(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                            uint64_t skew_ms, vna_rec_t *r)
{
    uint32_t sig_off = 0;
    if (!v || !buf || !r) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_rec_schema, buf, len, r, &sig_off) < 0 || r->rtype == 0)
        return VNA_ERR_PARSE;
    if (r->created > now + skew_ms) return VNA_ERR_STALE;
    if (r->expires <= now || r->expires <= r->created) return VNA_ERR_EXPIRED;
    vna_status_t bs = vna_keycache_check(v->kc, &r->publisher, r->pk, r->pow, v->pow_bits);
    if (bs != VNA_OK) return bs;
    if (!vna_verify(r->pk, VNA_CTX_RECORD, buf, sig_off, r->sig)) return VNA_ERR_SIG;
    if (r->rtype == VNA_REC_PROVIDER) {
        vna_provider_t p;
        if (vna_schema_unpack(&vna_provider_schema, r->payload, r->payload_len, &p, 0) < 0)
            return VNA_ERR_PARSE;
        if (p.visibility != VNA_VIS_PUBLIC) return VNA_ERR_DENIED; /* never stored */
        return VNA_OK;
    }
    if (r->rtype == VNA_REC_AGREEMENT) {
        static vna_agreement_t a; /* single-threaded scratch */
        vna_id_t k;
        vna_agree_key(&r->publisher, &k);
        if (!vna_id_eq(&k, &r->key)) return VNA_ERR_PARSE;
        if (vna_agree_decode(r->payload, r->payload_len, &a) != VNA_OK) return VNA_ERR_PARSE;
        if (!vna_id_eq(&a.owner, &r->publisher)) return VNA_ERR_BINDING;
        return VNA_OK;
    }
    return VNA_ERR_PARSE;
}

/* ---- node records ---- */
static const vna_field_t addr_el_fields[] = {VNA_FVAR(vna_addr_el_t, a, len, VNA_AXIS_NONE)};
static const vna_schema_t addr_el_schema = {"addr", 0x0013, 3, addr_el_fields, 1};

#define N vna_noderec_t
static const vna_field_t noderec_fields[] = {
    VNA_FCONST(N, magic, VNA_NODEREC_MAGIC),
    VNA_FU8(N, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FFIX(N, id, VNA_AXIS_PROVENANCE),
    VNA_FU64(N, pow, 0, VNA_AXIS_PROVENANCE),
    VNA_FU32(N, features, 0, VNA_AXIS_NONE),
    VNA_FU8(N, flags, VNA_NR_HIGHCAP | VNA_NR_GOODBYE, VNA_AXIS_NONE),
    VNA_FU64(N, seq, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(N, created, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(N, expires, 0, VNA_AXIS_PROVENANCE),
    VNA_FARR(N, addr, naddr, &addr_el_schema, vna_addr_el_t, VNA_AXIS_NONE),
    VNA_FFIX(N, pk, VNA_AXIS_PROVENANCE),
    VNA_FSIG(N, sig),
};
#undef N
/* UBH class 3 = CONTENT_OBJECT */
const vna_schema_t vna_noderec_schema = {"node_record", 0x0012, 3, noderec_fields,
                                         sizeof noderec_fields / sizeof noderec_fields[0]};

int32_t vna_noderec_seal(vna_noderec_t *r, const vna_identity_t *idn, const uint8_t rnd[32],
                         uint8_t *out, uint32_t cap)
{
    if (!r || !idn || !rnd || !out || r->naddr == 0 || r->naddr > VNA_NR_ADDRS) return -1;
    r->magic = VNA_NODEREC_MAGIC;
    r->version = VNA_VERSION;
    r->id = idn->id;
    r->pow = idn->pow_nonce;
    vna_copy(r->pk, idn->pk, VNA_PK_LEN);
    vna_zero(r->sig, VNA_SIG_LEN);
    int32_t n = vna_schema_pack(&vna_noderec_schema, r, out, cap, true);
    if (n < (int32_t) VNA_SIG_LEN) return -1;
    uint32_t signed_len = (uint32_t) n - VNA_SIG_LEN;
    vna_sign(idn->sk, VNA_CTX_NODEREC, out, signed_len, rnd, r->sig);
    vna_copy(out + signed_len, r->sig, VNA_SIG_LEN);
    return n;
}

vna_status_t vna_noderec_verify(vna_verifier_t *v, const uint8_t *buf, uint32_t len, uint64_t now,
                                uint64_t skew_ms, vna_noderec_t *r)
{
    uint32_t sig_off = 0;
    if (!v || !buf || !r) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_noderec_schema, buf, len, r, &sig_off) < 0 ||
        r->version != VNA_VERSION || r->naddr == 0)
        return VNA_ERR_PARSE;
    for (uint16_t i = 0; i < r->naddr; i++)
        if (r->addr[i].len == 0) return VNA_ERR_PARSE;
    if (r->created > now + skew_ms) return VNA_ERR_STALE;
    if (r->flags & VNA_NR_GOODBYE) {
        if (r->expires < r->created || r->created + skew_ms < now) return VNA_ERR_STALE;
    } else if (r->expires <= now || r->expires <= r->created) {
        return VNA_ERR_EXPIRED;
    }
    vna_status_t bs = vna_keycache_check(v->kc, &r->id, r->pk, r->pow, v->pow_bits);
    if (bs != VNA_OK) {
        if (bs == VNA_ERR_POW)
            v->rej_pow++;
        else
            v->rej_binding++;
        return bs;
    }
    if (!vna_verify(r->pk, VNA_CTX_NODEREC, buf, sig_off, r->sig)) {
        v->rej_sig++;
        return VNA_ERR_SIG;
    }
    return VNA_OK;
}

/* ---- spool items ---- */
#define SI vna_spool_item_t
static const vna_field_t spool_fields[] = {
    VNA_FCONST(SI, magic, VNA_SPOOL_MAGIC),
    VNA_FU8(SI, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FFIX(SI, src, VNA_AXIS_PROVENANCE),
    VNA_FFIX(SI, dst, VNA_AXIS_PROVENANCE),
    VNA_FU64(SI, sseq, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(SI, created, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(SI, expires, 0, VNA_AXIS_PROVENANCE),
    VNA_FU8(SI, truth, 5, VNA_AXIS_PROVENANCE),
    VNA_FU32(SI, ordinal, 0, VNA_AXIS_NONE),
    VNA_FHK(SI, text, len, VNA_AXIS_NONE),
    VNA_FSIG(SI, sig),
};
#undef SI
/* UBH class 1 = CAUSAL_EVENT */
const vna_schema_t vna_spool_item_schema = {"spool_item", 0x0008, 1, spool_fields,
                                            sizeof spool_fields / sizeof spool_fields[0]};

static const vna_field_t spool_ack_fields[] = {
    VNA_FU64(vna_b_spool_ack_t, sseq, 0, VNA_AXIS_NONE),
    VNA_FU8(vna_b_spool_ack_t, status, VNA_SPOOL_ACK_BUSY, VNA_AXIS_NONE),
};
const vna_schema_t vna_b_spool_ack_schema = {"body_spool_ack", 0x0009, 1, spool_ack_fields, 2};
