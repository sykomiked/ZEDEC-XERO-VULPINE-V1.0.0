/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_int.h -- helpers shared by the passport's source files. */
#ifndef ZXV_PP_INT_H
#define ZXV_PP_INT_H

#include "passport.h"

/* FIPS 204 ML-DSA-65 from kernel/src/pqsec/pq_mldsa65.c, declared here as
 * ident does (pq_security.h does not build freestanding). Must match
 * pq_security.h exactly. */
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[1952], uint8_t sk[4032]);
void pq_mldsa65_sign(const uint8_t sk[4032], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[3309]);
bool pq_mldsa65_verify(const uint8_t pk[1952], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len, const uint8_t sig[3309]);

void pp_mcpy(void *d, const void *s, uint32_t n);
void pp_mset(void *d, uint8_t v, uint32_t n);
int pp_mcmp(const void *a, const void *b, uint32_t n);
bool pp_ct_eq(const void *a, const void *b, uint32_t n);
void pp_wipe_mem(void *p, uint32_t n);
bool pp_is_zero(const void *p, uint32_t n);
uint32_t pp_strlen(const char *s, uint32_t max);

void pp_put32(uint8_t *p, uint32_t v);
void pp_put64(uint8_t *p, uint64_t v);
uint32_t pp_get32(const uint8_t *p);
uint64_t pp_get64(const uint8_t *p);

/* SHAKE256 / SHA3-256 of len(label) || label || a || b || c, as ident's
 * id_shake (a..c may be NULL/0; label is a constant under 64 bytes). The
 * whole input is at most PP_HASH_IN bytes; false (and nothing written) if
 * it is longer. */
#define PP_HASH_IN 512u
bool pp_shake(uint8_t *out, uint32_t out_len, const char *label, const void *a, uint32_t alen,
              const void *b, uint32_t blen, const void *c, uint32_t clen);
bool pp_sha3(uint8_t out[PP_HASH], const char *label, const void *a, uint32_t alen, const void *b,
             uint32_t blen, const void *c, uint32_t clen);

#define PP_CTX_SPEND "zxv.passport.spend.v1"
#define PP_CTX_GRANT "zxv.passport.grant.v1"

/* Entry encodings (big-endian), which are also the canonical sort keys. */
#define PP_EARN_BYTES   80u
#define PP_GRANT_BYTES  76u
#define PP_REVOKE_BYTES 24u
#define PP_DEV_BYTES    32u
void pp_spend_decode(const uint8_t *in, pp_spend_t *sp);
void pp_earn_enc(const pp_earn_t *e, uint8_t out[PP_EARN_BYTES]);
void pp_grant_rec_enc(const pp_grant_rec_t *g, uint8_t out[PP_GRANT_BYTES]);

/* Lookups inside a state. */
bool pp_state_has_spend(const pp_state_t *st, const pp_spend_t *sp);
bool pp_state_has_grant(const pp_state_t *st, const pp_grant_rec_t *g);
bool pp_state_has_device(const pp_state_t *st, const uint8_t cred[PP_ID]);
bool pp_state_is_revoked(const pp_state_t *st, const uint8_t key[PP_ID]);

/* Root -> status list -> certificate -> device key, for credential `cred`. */
pp_status_t pp_device_chain(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                            const id_dev_cert_t *cert, const uint8_t device_pk[ID_MLDSA65_PK],
                            const uint8_t cred[PP_ID]);

#endif /* ZXV_PP_INT_H */
