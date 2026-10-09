/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_session.c — hybrid PQ handshake and AEAD records. See vna_session.h. */
#include "vna_session.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"
#include "../tls/hkdf.h"

#define H1 vna_hs1_t
static const vna_field_t hs1_fields[] = {
    VNA_FCONST(H1, magic, VNA_HS1_MAGIC),
    VNA_FU8(H1, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU32(H1, features, 0, VNA_AXIS_NONE),
    VNA_FFIX(H1, init_id, VNA_AXIS_PROVENANCE),
    VNA_FFIX(H1, resp_id, VNA_AXIS_PROVENANCE),
    VNA_FFIX(H1, x, VNA_AXIS_NONE),
    VNA_FFIX(H1, ek, VNA_AXIS_NONE),
    VNA_FFIX(H1, nonce, VNA_AXIS_NONE),
};
#undef H1
/* UBH class 6 = INTEGRITY */
const vna_schema_t vna_hs1_schema = {"hs1", 0x0020, 6, hs1_fields, 8};

#define H2 vna_hs2_t
static const vna_field_t hs2_fields[] = {
    VNA_FCONST(H2, magic, VNA_HS2_MAGIC),
    VNA_FU8(H2, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU32(H2, features, 0, VNA_AXIS_NONE),
    VNA_FFIX(H2, resp_id, VNA_AXIS_PROVENANCE),
    VNA_FFIX(H2, init_id, VNA_AXIS_PROVENANCE),
    VNA_FU64(H2, pow, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(H2, x, VNA_AXIS_NONE),
    VNA_FFIX(H2, ct, VNA_AXIS_NONE),
    VNA_FFIX(H2, nonce, VNA_AXIS_NONE),
    VNA_FFIX(H2, pk, VNA_AXIS_PROVENANCE),
    VNA_FSIG(H2, sig),
};
#undef H2
const vna_schema_t vna_hs2_schema = {"hs2", 0x0021, 6, hs2_fields, 11};

static const vna_field_t hs3_fields[] = {
    VNA_FCONST(vna_hs3_t, magic, VNA_HS3_MAGIC),
    VNA_FVAR(vna_hs3_t, ct, ct_len, VNA_AXIS_NONE),
    VNA_FFIX(vna_hs3_t, tag, VNA_AXIS_PROVENANCE),
};
const vna_schema_t vna_hs3_schema = {"hs3", 0x0022, 6, hs3_fields, 3};

static const vna_field_t inner_fields[] = {
    VNA_FU64(vna_hs3_inner_t, pow, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(vna_hs3_inner_t, pk, VNA_AXIS_PROVENANCE),
    VNA_FSIG(vna_hs3_inner_t, sig),
};
static const vna_schema_t inner_schema = {"hs3_inner", 0x0023, 6, inner_fields, 3};

typedef struct {
    uint8_t type;
    uint64_t seq;
    uint32_t len;
} rec_hdr_t;
static const vna_field_t rh_fields[] = {
    VNA_FU8(rec_hdr_t, type, 0, VNA_AXIS_NONE),
    VNA_FU64(rec_hdr_t, seq, 0, VNA_AXIS_PROVENANCE),
    VNA_FU32(rec_hdr_t, len, VNA_SESS_MAX_PT, VNA_AXIS_NONE),
};
static const vna_schema_t rh_schema = {"sess_rec_hdr", 0x0024, 1, rh_fields, 3};

static uint8_t g_buf[8192]; /* single-threaded scratch for transcript encodings */

void vna_hs_clear(vna_hs_t *hs)
{
    vna_wipe(hs, sizeof *hs);
}

static void th_of(const char *tag, const uint8_t *data, uint32_t len, uint8_t out[32])
{
    vna_htag(tag, data, len, out);
}

/* hybrid = SHAKE256("vinea/v2/hybrid" || ss_k || ss_x || th2); prk = Extract(th2, hybrid) */
static void derive_prk(const uint8_t ss_k[32], const uint8_t ss_x[32], const uint8_t th2[32],
                       uint8_t prk[32])
{
    static const char tag[] = "vinea/v2/hybrid";
    uint8_t in[sizeof tag - 1 + 96];
    uint8_t hyb[32];
    vna_copy(in, tag, sizeof tag - 1);
    vna_copy(in + sizeof tag - 1, ss_k, 32);
    vna_copy(in + sizeof tag - 1 + 32, ss_x, 32);
    vna_copy(in + sizeof tag - 1 + 64, th2, 32);
    shake256(in, sizeof in, hyb, 32);
    hkdf_extract(th2, 32, hyb, 32, prk);
    vna_wipe(in, sizeof in);
    vna_wipe(hyb, 32);
}

static void derive_hs_key(const uint8_t prk[32], uint8_t k[32])
{
    static const char info[] = "vinea/v2 hs3";
    (void) hkdf_expand(prk, (const uint8_t *) info, sizeof info - 1, k, 32);
}

static void derive_traffic(const uint8_t prk[32], const uint8_t th4[32], bool initiator,
                           vna_sess_t *s)
{
    static const char lab[] = "vinea/v2 traffic";
    uint8_t info[sizeof lab - 1 + 32];
    uint8_t okm[88];
    vna_copy(info, lab, sizeof lab - 1);
    vna_copy(info + sizeof lab - 1, th4, 32);
    (void) hkdf_expand(prk, info, sizeof info, okm, sizeof okm);
    const uint8_t *i2r_k = okm, *r2i_k = okm + 32, *i2r_iv = okm + 64, *r2i_iv = okm + 76;
    vna_copy(s->tx_key, initiator ? i2r_k : r2i_k, 32);
    vna_copy(s->rx_key, initiator ? r2i_k : i2r_k, 32);
    vna_copy(s->tx_iv, initiator ? i2r_iv : r2i_iv, 12);
    vna_copy(s->rx_iv, initiator ? r2i_iv : i2r_iv, 12);
    vna_copy(s->session_id, th4, 32);
    s->tx_seq = 0;
    s->rx_hi = 0;
    s->rx_bitmap = 0;
    s->established = true;
    vna_wipe(okm, sizeof okm);
}

int32_t vna_hs_initiate(vna_hs_t *hs, const vna_identity_t *self, const vna_id_t *peer_id,
                        uint32_t pow_bits, uint32_t features, const uint8_t rnd[128], uint8_t *out,
                        uint32_t cap)
{
    if (!hs || !self || !peer_id || !rnd || !out) return -1;
    vna_zero(hs, sizeof *hs);
    hs->initiator = true;
    hs->self = self;
    hs->pow_bits = pow_bits;
    hs->peer = *peer_id;
    vna_copy(hs->x_sk, rnd, 32);
    vna_hs1_t *m = &hs->m1;
    m->magic = VNA_HS1_MAGIC;
    m->version = VNA_VERSION;
    m->features = features;
    m->init_id = self->id;
    m->resp_id = *peer_id;
    x25519_public(m->x, hs->x_sk);
    mlkem768_keygen(rnd + 32, rnd + 64, m->ek, hs->kem_dk);
    vna_copy(m->nonce, rnd + 96, 32);
    int32_t n = vna_schema_pack(&vna_hs1_schema, m, out, cap, true);
    if (n < 0) return -1;
    th_of("vinea/v2/hs1", out, (uint32_t) n, hs->th);
    hs->state = VNA_HS_SENT_M1;
    return n;
}

vna_status_t vna_hs_peek_initiator(const uint8_t *m1, uint32_t len, vna_id_t *claimed)
{
    static vna_hs1_t t;
    if (vna_schema_unpack(&vna_hs1_schema, m1, len, &t, 0) < 0) return VNA_ERR_PARSE;
    *claimed = t.init_id;
    return VNA_OK;
}

int32_t vna_hs_respond(vna_hs_t *hs, const vna_identity_t *self, uint32_t pow_bits,
                       uint32_t features, const uint8_t *m1, uint32_t m1_len,
                       const uint8_t rnd[128], uint8_t *out, uint32_t cap)
{
    uint8_t ss_x[32], ss_k[32], h[32];
    if (!hs || !self || !m1 || !rnd || !out) return VNA_ERR_ARG;
    vna_zero(hs, sizeof *hs);
    hs->self = self;
    hs->pow_bits = pow_bits;
    if (vna_schema_unpack(&vna_hs1_schema, m1, m1_len, &hs->m1, 0) < 0) return VNA_ERR_PARSE;
    if (!vna_id_eq(&hs->m1.resp_id, &self->id) || vna_id_eq(&hs->m1.init_id, &self->id))
        return VNA_ERR_DST;
    hs->peer = hs->m1.init_id; /* claimed; proven in M3 */
    hs->peer_features = hs->m1.features;
    th_of("vinea/v2/hs1", m1, m1_len, hs->th);

    vna_copy(hs->x_sk, rnd, 32);
    vna_hs2_t *m = &hs->m2;
    m->magic = VNA_HS2_MAGIC;
    m->version = VNA_VERSION;
    m->features = features;
    m->resp_id = self->id;
    m->init_id = hs->m1.init_id;
    m->pow = self->pow_nonce;
    x25519_public(m->x, hs->x_sk);
    if (!x25519_shared(ss_x, hs->x_sk, hs->m1.x)) return VNA_ERR_CRYPTO; /* low-order point */
    mlkem768_encaps(hs->m1.ek, rnd + 32, m->ct, ss_k);
    vna_copy(m->nonce, rnd + 64, 32);
    vna_copy(m->pk, self->pk, VNA_PK_LEN);
    vna_zero(m->sig, VNA_SIG_LEN);
    int32_t n = vna_schema_pack(&vna_hs2_schema, m, out, cap, false);
    if (n < 0) return VNA_ERR_SPACE;
    vna_sha3(out, (uint32_t) n, h);
    vna_h2(hs->th, h, hs->th); /* th2 */
    vna_sign(self->sk, VNA_CTX_HS_RESP, hs->th, 32, rnd + 96, m->sig);
    derive_prk(ss_k, ss_x, hs->th, hs->prk);
    vna_wipe(ss_x, 32);
    vna_wipe(ss_k, 32);
    vna_wipe(hs->x_sk, 32);
    n = vna_schema_pack(&vna_hs2_schema, m, out, cap, true);
    if (n < 0) return VNA_ERR_SPACE;
    vna_sha3(m->sig, VNA_SIG_LEN, h);
    vna_h2(hs->th, h, hs->th3);
    hs->state = VNA_HS_SENT_M2;
    return n;
}

int32_t vna_hs_initiator_finish(vna_hs_t *hs, const uint8_t *m2, uint32_t m2_len,
                                const uint8_t rnd[32], uint8_t *out, uint32_t cap, vna_sess_t *sess)
{
    uint8_t ss_x[32], ss_k[32], h[32], k_hs[32], th4[32];
    uint8_t nonce[12] = {0};
    uint32_t sig_off = 0;
    if (!hs || hs->state != VNA_HS_SENT_M1 || !m2 || !rnd || !out || !sess) return VNA_ERR_STATE;
    hs->state = VNA_HS_FAILED; /* until proven otherwise */
    vna_hs2_t *m = &hs->m2;
    if (vna_schema_unpack(&vna_hs2_schema, m2, m2_len, m, &sig_off) < 0) return VNA_ERR_PARSE;
    if (!vna_id_eq(&m->resp_id, &hs->peer) || !vna_id_eq(&m->init_id, &hs->self->id))
        return VNA_ERR_DST;
    if (vna_keycache_check(0, &hs->peer, m->pk, m->pow, hs->pow_bits) != VNA_OK)
        return VNA_ERR_BINDING; /* a substituted key cannot hash to the expected NodeID */
    vna_sha3(m2, sig_off, h);
    vna_h2(hs->th, h, hs->th); /* th2 */
    if (!vna_verify(m->pk, VNA_CTX_HS_RESP, hs->th, 32, m->sig)) return VNA_ERR_SIG;
    if (!x25519_shared(ss_x, hs->x_sk, m->x)) return VNA_ERR_CRYPTO;
    mlkem768_decaps(hs->kem_dk, m->ct, ss_k);
    derive_prk(ss_k, ss_x, hs->th, hs->prk);
    vna_wipe(ss_x, 32);
    vna_wipe(ss_k, 32);
    vna_wipe(hs->x_sk, 32);
    vna_wipe(hs->kem_dk, sizeof hs->kem_dk);
    vna_sha3(m->sig, VNA_SIG_LEN, h);
    vna_h2(hs->th, h, hs->th3);

    vna_hs3_inner_t *in = &hs->inner;
    in->pow = hs->self->pow_nonce;
    vna_copy(in->pk, hs->self->pk, VNA_PK_LEN);
    vna_sign(hs->self->sk, VNA_CTX_HS_INIT, hs->th3, 32, rnd, in->sig);
    int32_t il = vna_schema_pack(&inner_schema, in, g_buf, sizeof g_buf, true);
    if (il < 0) return VNA_ERR_SPACE;
    derive_hs_key(hs->prk, k_hs);
    vna_hs3_t *m3 = &hs->m3;
    m3->magic = VNA_HS3_MAGIC;
    m3->ct_len = (uint16_t) il;
    aead_seal(k_hs, nonce, hs->th, 32, g_buf, m3->ct, (uint32_t) il, m3->tag);
    vna_wipe(k_hs, 32);
    int32_t n = vna_schema_pack(&vna_hs3_schema, m3, out, cap, true);
    if (n < 0) return VNA_ERR_SPACE;
    vna_sha3(in->sig, VNA_SIG_LEN, h);
    vna_h2(hs->th3, h, th4);
    vna_zero(sess, sizeof *sess);
    sess->peer = hs->peer;
    sess->peer_features = m->features;
    derive_traffic(hs->prk, th4, true, sess);
    vna_wipe(hs->prk, 32);
    hs->state = VNA_HS_DONE;
    return n;
}

vna_status_t vna_hs_responder_finish(vna_hs_t *hs, const uint8_t *m3, uint32_t m3_len,
                                     vna_sess_t *sess)
{
    uint8_t k_hs[32], h[32], th4[32];
    uint8_t nonce[12] = {0};
    if (!hs || hs->state != VNA_HS_SENT_M2 || !m3 || !sess) return VNA_ERR_STATE;
    hs->state = VNA_HS_FAILED;
    vna_hs3_t *m = &hs->m3;
    if (vna_schema_unpack(&vna_hs3_schema, m3, m3_len, m, 0) < 0) return VNA_ERR_PARSE;
    derive_hs_key(hs->prk, k_hs);
    bool ok = aead_open(k_hs, nonce, hs->th, 32, m->ct, g_buf, m->ct_len, m->tag);
    vna_wipe(k_hs, 32);
    if (!ok) return VNA_ERR_CRYPTO;
    vna_hs3_inner_t *in = &hs->inner;
    if (vna_schema_unpack(&inner_schema, g_buf, m->ct_len, in, 0) < 0) return VNA_ERR_PARSE;
    if (vna_keycache_check(0, &hs->peer, in->pk, in->pow, hs->pow_bits) != VNA_OK)
        return VNA_ERR_BINDING;
    if (!vna_verify(in->pk, VNA_CTX_HS_INIT, hs->th3, 32, in->sig)) return VNA_ERR_SIG;
    vna_sha3(in->sig, VNA_SIG_LEN, h);
    vna_h2(hs->th3, h, th4);
    vna_zero(sess, sizeof *sess);
    sess->peer = hs->peer;
    sess->peer_features = hs->peer_features;
    derive_traffic(hs->prk, th4, false, sess);
    vna_wipe(hs->prk, 32);
    hs->state = VNA_HS_DONE;
    return VNA_OK;
}

int32_t vna_sess_seal(vna_sess_t *s, uint8_t type, const uint8_t *pt, uint32_t len, uint8_t *out,
                      uint32_t cap)
{
    uint8_t nonce[12];
    if (!s || !s->established || (!pt && len) || !out) return VNA_ERR_STATE;
    if (len > VNA_SESS_MAX_PT || cap < VNA_SESS_REC_HDR + len + VNA_SESS_TAG) return VNA_ERR_SPACE;
    if (s->tx_seq >= ((uint64_t) 1 << 48)) return VNA_ERR_CAP; /* rekey required */
    rec_hdr_t h;
    h.type = type;
    h.seq = ++s->tx_seq; /* each (key, nonce) pair is used exactly once */
    h.len = len;
    if (vna_schema_pack(&rh_schema, &h, out, VNA_SESS_REC_HDR, true) != (int32_t) VNA_SESS_REC_HDR)
        return VNA_ERR_ARG;
    tls13_record_nonce(s->tx_iv, h.seq, nonce);
    aead_seal(s->tx_key, nonce, out, VNA_SESS_REC_HDR, pt, out + VNA_SESS_REC_HDR, len,
              out + VNA_SESS_REC_HDR + len);
    return (int32_t) (VNA_SESS_REC_HDR + len + VNA_SESS_TAG);
}

vna_status_t vna_sess_open(vna_sess_t *s, const uint8_t *rec, uint32_t len, uint8_t *out,
                           uint32_t cap, uint8_t *type, uint32_t *pt_len)
{
    uint8_t nonce[12];
    rec_hdr_t h;
    if (!s || !s->established || !rec || !out) return VNA_ERR_STATE;
    if (len < VNA_SESS_REC_HDR + VNA_SESS_TAG) return VNA_ERR_PARSE;
    if (vna_schema_unpack(&rh_schema, rec, VNA_SESS_REC_HDR, &h, 0) < 0) return VNA_ERR_PARSE;
    if (h.len != len - VNA_SESS_REC_HDR - VNA_SESS_TAG || h.len > cap) return VNA_ERR_PARSE;
    if (h.seq == 0) return VNA_ERR_REPLAY;
    if (h.seq <= s->rx_hi) { /* sliding window */
        uint64_t back = s->rx_hi - h.seq;
        if (back == 0 || back > 64 || ((s->rx_bitmap >> (back - 1)) & 1u)) return VNA_ERR_REPLAY;
    }
    tls13_record_nonce(s->rx_iv, h.seq, nonce);
    if (!aead_open(s->rx_key, nonce, rec, VNA_SESS_REC_HDR, rec + VNA_SESS_REC_HDR, out, h.len,
                   rec + VNA_SESS_REC_HDR + h.len))
        return VNA_ERR_CRYPTO;
    if (h.seq > s->rx_hi) { /* advance only after the tag verified */
        uint64_t shift = h.seq - s->rx_hi;
        if (shift < 64)
            s->rx_bitmap = (s->rx_bitmap << shift) | ((uint64_t) 1 << (shift - 1));
        else if (shift == 64)
            s->rx_bitmap = (uint64_t) 1 << 63;
        else
            s->rx_bitmap = 0;
        if (s->rx_hi == 0) s->rx_bitmap = 0; /* nothing below the first record */
        s->rx_hi = h.seq;
    } else {
        s->rx_bitmap |= (uint64_t) 1 << (s->rx_hi - h.seq - 1);
    }
    *type = h.type;
    *pt_len = h.len;
    return VNA_OK;
}
