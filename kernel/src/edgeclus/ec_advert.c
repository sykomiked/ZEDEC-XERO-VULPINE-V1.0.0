/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ec_advert.c — the signed node advert (edgeclus.h, rule A1). The
 * signature is ML-DSA-87 from pqsec through Vinea's vna_sign / vna_verify;
 * the identity binding and proof of work are Vinea's own checks. */
#include "edgeclus.h"

uint32_t ec_advert_body_len(uint32_t n_models)
{
    if (n_models == 0 || n_models > EC_MAX_MODELS) return 0;
    return EC_BODY_FIXED + n_models * EC_MODEL_WIRE;
}

static bool name_ok(const char *nm)
{
    uint32_t i = 0;
    while (i < EC_NAME_LEN && nm[i]) {
        uint8_t ch = (uint8_t) nm[i];
        if (ch < 0x21u || ch > 0x7Eu) return false;
        i++;
    }
    if (i == 0) return false;
    for (; i < EC_NAME_LEN; i++)
        if (nm[i]) return false; /* zero padding only */
    return true;
}

/* A1 field rules shared by seal and open. */
static bool fields_ok(const ec_advert_t *a)
{
    if ((a->flags & (uint8_t) ~EC_F_KNOWN) != 0) return false;
    if (a->n_models == 0 || a->n_models > EC_MAX_MODELS) return false;
    if (a->ttl_ms == 0 || a->ttl_ms > EC_MAX_TTL_MS) return false;
    for (uint32_t i = 0; i < a->n_models; i++) {
        const ec_model_t *m = &a->models[i];
        if (!name_ok(m->name)) return false;
        if (m->quant == EC_Q_ANY || m->quant > EC_Q_LAST) return false;
        if (m->prefill_mtps == 0 || m->decode_mtps == 0) return false;
        for (uint32_t j = 0; j < i; j++)
            if (a->models[j].quant == m->quant && vna_eq(a->models[j].name, m->name, EC_NAME_LEN))
                return false;
    }
    return true;
}

static void put_body(const ec_advert_t *a, uint8_t *p)
{
    vna_put32(p, EC_ADVERT_MAGIC);
    p[4] = (uint8_t) EC_ADVERT_VERSION;
    p[5] = a->flags;
    p[6] = a->n_models;
    p[7] = 0;
    vna_copy(p + 8, a->node.b, VNA_ID_LEN);
    vna_put64(p + 40, a->pow_nonce);
    vna_put64(p + 48, a->seq);
    vna_put64(p + 56, a->issued_ms);
    vna_put32(p + 64, a->ttl_ms);
    vna_put32(p + 68, a->free_mem_mb);
    vna_put32(p + 72, a->queue_depth);
    vna_put64(p + 76, a->price);
    uint8_t *q = p + EC_BODY_FIXED;
    for (uint32_t i = 0; i < a->n_models; i++, q += EC_MODEL_WIRE) {
        const ec_model_t *m = &a->models[i];
        vna_copy(q, m->name, EC_NAME_LEN);
        q[32] = m->quant;
        vna_put32(q + 33, m->mem_mb);
        vna_put32(q + 37, m->prefill_mtps);
        vna_put32(q + 41, m->decode_mtps);
    }
}

int32_t ec_advert_seal(ec_advert_t *a, const vna_identity_t *idn, const uint8_t rnd[32],
                       uint8_t *out, uint32_t cap)
{
    if (!a || !idn || !out) return -1;
    vna_copy(a->node.b, idn->id.b, VNA_ID_LEN);
    a->pow_nonce = idn->pow_nonce;
    if (!fields_ok(a)) return -1;
    uint32_t bl = ec_advert_body_len(a->n_models);
    uint32_t total = bl + VNA_PK_LEN + VNA_SIG_LEN;
    if (cap < total) return -1;
    put_body(a, out);
    vna_copy(out + bl, idn->pk, VNA_PK_LEN);
    vna_sign(idn->sk, EC_CTX_ADVERT, out, bl, rnd, out + bl + VNA_PK_LEN);
    return (int32_t) total;
}

ec_status_t ec_advert_open(const uint8_t *buf, uint32_t len, uint32_t pow_bits, ec_advert_t *out,
                           uint8_t body_hash[32])
{
    if (!buf || !out) return EC_ERR_ARG;
    if (len < EC_BODY_FIXED) return EC_ERR_PARSE;
    if (vna_get32(buf) != EC_ADVERT_MAGIC || buf[4] != EC_ADVERT_VERSION || buf[7] != 0)
        return EC_ERR_PARSE;
    uint32_t nm = buf[6];
    uint32_t bl = ec_advert_body_len(nm);
    if (bl == 0 || len != bl + VNA_PK_LEN + VNA_SIG_LEN) return EC_ERR_PARSE;

    ec_advert_t a;
    vna_zero(&a, (uint32_t) sizeof a);
    a.flags = buf[5];
    a.n_models = (uint8_t) nm;
    vna_copy(a.node.b, buf + 8, VNA_ID_LEN);
    a.pow_nonce = vna_get64(buf + 40);
    a.seq = vna_get64(buf + 48);
    a.issued_ms = vna_get64(buf + 56);
    a.ttl_ms = vna_get32(buf + 64);
    a.free_mem_mb = vna_get32(buf + 68);
    a.queue_depth = vna_get32(buf + 72);
    a.price = vna_get64(buf + 76);
    const uint8_t *q = buf + EC_BODY_FIXED;
    for (uint32_t i = 0; i < nm; i++, q += EC_MODEL_WIRE) {
        ec_model_t *m = &a.models[i];
        vna_copy(m->name, q, EC_NAME_LEN);
        m->quant = q[32];
        m->mem_mb = vna_get32(q + 33);
        m->prefill_mtps = vna_get32(q + 37);
        m->decode_mtps = vna_get32(q + 41);
    }
    if (!fields_ok(&a)) return EC_ERR_PARSE;

    const uint8_t *pk = buf + bl;
    vna_id_t id;
    vna_node_id(pk, &id);
    if (!vna_id_eq(&id, &a.node)) return EC_ERR_BINDING;
    if (!vna_pow_ok(&a.node, a.pow_nonce, pow_bits)) return EC_ERR_POW;
    if (!vna_verify(pk, EC_CTX_ADVERT, buf, bl, pk + VNA_PK_LEN)) return EC_ERR_SIG;

    vna_copy(out, &a, (uint32_t) sizeof a);
    if (body_hash) vna_sha3(buf, bl, body_hash);
    return EC_OK;
}
