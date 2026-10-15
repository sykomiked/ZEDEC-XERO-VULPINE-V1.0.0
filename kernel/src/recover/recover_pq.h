/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recover_pq.h — ML-DSA-87 (FIPS 204) for RecoveryProofs: an rcv_sign_fn
 * and an rcv_verify_fn, both with the domain context RCV_SIG_CONTEXT. Kept
 * apart from recover.c so the recovery policy stays crypto-free, as hwr_pq
 * is kept apart from hwr. */
#ifndef RECOVER_PQ_H
#define RECOVER_PQ_H

#include "recover.h"

#define RCV_SIG_CONTEXT "zxv-recover-v1"
#define RCV_MLDSA87_PK  2592u
#define RCV_MLDSA87_SK  4896u
#define RCV_MLDSA87_SIG 4627u

/* The signer's context: the secret key and, for hedged signing, 32 fresh
 * bytes (rnd NULL: deterministic). */
typedef struct {
    const uint8_t *sk; /* RCV_MLDSA87_SK bytes */
    const uint8_t *rnd;
} rcv_mldsa87_key_t;

/* rcv_sign_fn: ctx is an rcv_mldsa87_key_t. 0 for a NULL key or cap below
 * RCV_MLDSA87_SIG. */
uint32_t rcv_mldsa87_sign(void *ctx, const uint8_t msg[PDX_HASH], uint8_t *sig, uint32_t cap);
/* rcv_verify_fn: ctx unused. False unless pk_len and sig_len are exact. */
bool rcv_mldsa87_verify(void *ctx, const uint8_t *pk, uint32_t pk_len, const uint8_t msg[PDX_HASH],
                        const uint8_t *sig, uint32_t sig_len);

#endif /* RECOVER_PQ_H */
