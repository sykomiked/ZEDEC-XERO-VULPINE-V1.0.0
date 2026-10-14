/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpk.c — see cpk.h (K1-K4). Freestanding: calls only pqm_mldsa87_* and
 * sha256_*. */
#include "cpk.h"
#include "../robin_debanks/sha256.h"

#define KEYID_DOMAIN     "zxv/cpk-keyid/v1"
#define KEYID_DOMAIN_LEN 16u

void cpk_key_id(const uint8_t pk[CPK_PK_LEN], uint8_t out[CP_KEYID_LEN])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) KEYID_DOMAIN, KEYID_DOMAIN_LEN);
    sha256_update(&c, pk, CPK_PK_LEN);
    sha256_final(&c, out);
}

void cpk_init(cpk_ring_t *r)
{
    r->n = 0;
}

bool cpk_add(cpk_ring_t *r, const uint8_t pk[CPK_PK_LEN], uint32_t kinds, const char country[2])
{
    if (!r || !pk || !country || r->n >= CPK_MAX || kinds == 0 || !cp_cc_ok(country, true))
        return false;
    uint8_t id[CP_KEYID_LEN];
    cpk_key_id(pk, id);
    for (uint32_t i = 0; i < r->n; i++)
        if (cp_eq(r->key[i].key_id, id, CP_KEYID_LEN)) return false;
    cpk_key_t *k = &r->key[r->n++];
    cp_copy(k->key_id, id, CP_KEYID_LEN);
    cp_copy(k->pk, pk, CPK_PK_LEN);
    k->kinds = kinds;
    k->country[0] = country[0];
    k->country[1] = country[1];
    return true;
}

bool cpk_verify(void *ctx, const cp_hdr_t *hdr, const uint8_t *msg, size_t msg_len,
                const uint8_t *sig, size_t sig_len)
{
    const cpk_ring_t *r = (const cpk_ring_t *) ctx;
    if (!r || !hdr || !msg || !sig || sig_len != CPK_SIG_LEN || hdr->kind >= 32) return false;
    for (uint32_t i = 0; i < r->n; i++) {
        const cpk_key_t *k = &r->key[i];
        if (!cp_eq(k->key_id, hdr->key_id, CP_KEYID_LEN)) continue;
        if (!(k->kinds & (1u << hdr->kind))) return false;
        bool any = k->country[0] == '*' && k->country[1] == '*';
        if (!any && (k->country[0] != hdr->country[0] || k->country[1] != hdr->country[1]))
            return false;
        return pqm_mldsa87_verify(k->pk, msg, msg_len, (const uint8_t *) CPK_CTX, CPK_CTX_LEN, sig);
    }
    return false;
}

bool cpk_verify_pk(const uint8_t *pk, size_t pk_len, const uint8_t *msg, size_t msg_len,
                   const uint8_t *sig, size_t sig_len)
{
    if (!pk || pk_len != CPK_PK_LEN || !msg || !sig || sig_len != CPK_SIG_LEN) return false;
    return pqm_mldsa87_verify(pk, msg, msg_len, (const uint8_t *) CPK_CTX, CPK_CTX_LEN, sig);
}

void cpk_sign(const uint8_t sk[CPK_SK_LEN], const uint8_t *msg, size_t msg_len, const uint8_t *rnd,
              uint8_t sig[CPK_SIG_LEN])
{
    pqm_mldsa87_sign(sk, msg, msg_len, (const uint8_t *) CPK_CTX, CPK_CTX_LEN, rnd, sig);
}

void cpk_keygen(const uint8_t seed[32], uint8_t pk[CPK_PK_LEN], uint8_t sk[CPK_SK_LEN])
{
    pqm_mldsa87_keygen(seed, pk, sk);
}
