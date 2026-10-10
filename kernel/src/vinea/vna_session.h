/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_session.h — mutually authenticated, post-quantum secure sessions.
 *
 * HANDSHAKE (Noise-IK-like: the initiator already knows the responder's
 * NodeID; SIGMA-style authentication with ML-DSA-65 only)
 *
 *   M1  I -> R : I_id, R_id, X25519 ephemeral eI, ML-KEM-768 ephemeral ek_I,
 *                nonce_I, features
 *   M2  R -> I : R_id, I_id, eR, ct = ML-KEM.Encaps(ek_I), nonce_R, R's pk,
 *                pow, sig_R = ML-DSA-65(R, "vinea/v2/hs-responder", th2)
 *   M3  I -> R : AEAD_{k_hs}( I's pk, pow,
 *                sig_I = ML-DSA-65(I, "vinea/v2/hs-initiator", th3) )
 *
 *   th1 = SHA3(SHA3("vinea/v2/hs1") || SHA3(M1))
 *   th2 = SHA3(th1 || SHA3(M2 without sig_R))
 *   th3 = SHA3(th2 || SHA3(sig_R))
 *   th4 = SHA3(th3 || SHA3(sig_I))           (= the session id)
 *   ss_x = X25519(eI, eR) (all-zero result refused), ss_k = ML-KEM-768 secret
 *   hybrid = SHAKE256("vinea/v2/hybrid" || ss_k || ss_x || th2, 32)
 *   prk = HKDF-Extract(salt = th2, hybrid)
 *   k_hs = HKDF-Expand(prk, "vinea/v2 hs3")            (M3 only)
 *   keys = HKDF-Expand(prk, "vinea/v2 traffic" || th4, 88)
 *        = i2r key | r2i key | i2r iv | r2i iv
 *
 * WHY IT HOLDS
 *   - Authentication is ML-DSA-65 only; each side checks the peer's NodeID =
 *     SHA3-256(pk) (and its PoW). There is no classical signature path.
 *   - Key secrecy needs ML-KEM-768 OR X25519 to hold: the secret is a SHAKE256
 *     of both. A quantum attacker who breaks X25519 learns ss_x but not ss_k
 *     and so gains nothing; a lattice break alone leaves X25519.
 *   - A man in the middle that swaps eI/ek_I/eR/ct changes th2, so sig_R or
 *     sig_I fails; one that swaps a public key fails the NodeID binding.
 *   - The initiator's identity is sent only inside M3, encrypted (identity
 *     hiding against passive observers; the responder's id is known to the
 *     initiator by construction).
 *
 * RECORDS: ChaCha20-Poly1305 (src/tls/aead.c) with per-direction keys and
 * nonces = iv XOR seq (tls13_record_nonce). Header (type, seq, length) is the
 * AAD. The receiver keeps a 64-entry sliding window, so records may arrive
 * out of order over datagrams but never twice; the window advances only after
 * the tag verified. A session refuses to send past 2^48 records (rekey by a
 * new handshake). File chunks travel only inside sessions.
 */
#ifndef VNA_SESSION_H
#define VNA_SESSION_H

#include "vna_id.h"
#include "vna_schema.h"
#include "../tls/x25519.h"

#define VNA_HS1_MAGIC     0x31484E56u /* "VNH1" */
#define VNA_HS2_MAGIC     0x32484E56u /* "VNH2" */
#define VNA_HS3_MAGIC     0x33484E56u /* "VNH3" */
#define VNA_HS3_INNER_MAX (8u + VNA_PK_LEN + VNA_SIG_LEN + 16u)
#define VNA_SESS_REC_HDR  13u
#define VNA_SESS_TAG      16u
#define VNA_SESS_MAX_PT   20000u

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint32_t features;
    vna_id_t init_id;
    vna_id_t resp_id;
    uint8_t x[X25519_LEN];
    uint8_t ek[MLKEM768_EK_BYTES];
    uint8_t nonce[32];
} vna_hs1_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint32_t features;
    vna_id_t resp_id;
    vna_id_t init_id;
    uint64_t pow;
    uint8_t x[X25519_LEN];
    uint8_t ct[MLKEM768_CT_BYTES];
    uint8_t nonce[32];
    uint8_t pk[VNA_PK_LEN];
    uint8_t sig[VNA_SIG_LEN];
} vna_hs2_t;

typedef struct {
    uint32_t magic;
    uint16_t ct_len;
    uint8_t ct[VNA_HS3_INNER_MAX];
    uint8_t tag[VNA_SESS_TAG];
} vna_hs3_t;

typedef struct {
    uint64_t pow;
    uint8_t pk[VNA_PK_LEN];
    uint8_t sig[VNA_SIG_LEN];
} vna_hs3_inner_t;

extern const vna_schema_t vna_hs1_schema, vna_hs2_schema, vna_hs3_schema;

typedef struct {
    vna_id_t peer;
    uint8_t session_id[32];
    uint8_t tx_key[32], rx_key[32];
    uint8_t tx_iv[12], rx_iv[12];
    uint64_t tx_seq;
    uint64_t rx_hi, rx_bitmap;
    uint32_t peer_features;
    bool established;
} vna_sess_t;

typedef enum {
    VNA_HS_IDLE = 0,
    VNA_HS_SENT_M1, /* initiator waiting for M2 */
    VNA_HS_SENT_M2, /* responder waiting for M3 */
    VNA_HS_DONE,
    VNA_HS_FAILED
} vna_hs_state_t;

typedef struct {
    uint8_t state;
    bool initiator;
    const vna_identity_t *self;
    uint32_t pow_bits;
    vna_id_t peer;
    uint8_t x_sk[X25519_LEN];
    uint8_t kem_dk[MLKEM768_DK_BYTES];
    uint8_t th[32]; /* th1 then th2 */
    uint8_t th3[32];
    uint8_t prk[32];
    uint32_t peer_features;
    vna_hs1_t m1;
    vna_hs2_t m2;
    vna_hs3_t m3;
    vna_hs3_inner_t inner;
} vna_hs_t;

/* Initiator: start a handshake with peer_id. rnd: 128 fresh bytes. Returns
 * the M1 length or -1. */
int32_t vna_hs_initiate(vna_hs_t *hs, const vna_identity_t *self, const vna_id_t *peer_id,
                        uint32_t pow_bits, uint32_t features, const uint8_t rnd[128], uint8_t *out,
                        uint32_t cap);

/* Responder: who claims to be calling (unauthenticated until M3; lets the
 * responder apply its agreement before doing any expensive work). */
vna_status_t vna_hs_peek_initiator(const uint8_t *m1, uint32_t len, vna_id_t *claimed);

/* Responder: answer M1. rnd: 128 fresh bytes. Returns the M2 length or a
 * negative vna_status_t. */
int32_t vna_hs_respond(vna_hs_t *hs, const vna_identity_t *self, uint32_t pow_bits,
                       uint32_t features, const uint8_t *m1, uint32_t m1_len,
                       const uint8_t rnd[128], uint8_t *out, uint32_t cap);

/* Initiator: process M2, produce M3 and the session. rnd: 32 fresh bytes. */
int32_t vna_hs_initiator_finish(vna_hs_t *hs, const uint8_t *m2, uint32_t m2_len,
                                const uint8_t rnd[32], uint8_t *out, uint32_t cap,
                                vna_sess_t *sess);

/* Responder: process M3 and produce the session. */
vna_status_t vna_hs_responder_finish(vna_hs_t *hs, const uint8_t *m3, uint32_t m3_len,
                                     vna_sess_t *sess);

/* Wipe every secret in the handshake state. */
void vna_hs_clear(vna_hs_t *hs);

/* Encrypt one record. Returns its length or a negative vna_status_t. */
int32_t vna_sess_seal(vna_sess_t *s, uint8_t type, const uint8_t *pt, uint32_t len, uint8_t *out,
                      uint32_t cap);

/* Decrypt one record into out (cap >= its plaintext). On VNA_OK, *type and
 * *pt_len are set. Tampered, replayed or out-of-window records fail. */
vna_status_t vna_sess_open(vna_sess_t *s, const uint8_t *rec, uint32_t len, uint8_t *out,
                           uint32_t cap, uint8_t *type, uint32_t *pt_len);

#endif /* VNA_SESSION_H */
