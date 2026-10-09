/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ehop_internal.h — helpers shared by the ehop translation units.
 *
 * The ML-DSA-65 prototypes are repeated from kernel/src/pqsec/pq_security.h
 * so that this module does not pull in that header's LPRES/surplus/event
 * dependencies. test_ehop.c includes both headers in one translation unit,
 * so a drift between the two declarations is a compile error there.
 */
#ifndef ZXV_EHOP_INTERNAL_H
#define ZXV_EHOP_INTERNAL_H

#include "ehop.h"

/* ===== ML-DSA-65 (FIPS 204), implemented in kernel/src/pqsec/pq_mldsa65.c */
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[EHOP_MLDSA_PK_BYTES],
                       uint8_t sk[EHOP_MLDSA_SK_BYTES]);
void pq_mldsa65_sign(const uint8_t sk[EHOP_MLDSA_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[EHOP_MLDSA_SIG_BYTES]);
bool pq_mldsa65_verify(const uint8_t pk[EHOP_MLDSA_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[EHOP_MLDSA_SIG_BYTES]);

/* ===== byte helpers (no libc) ===== */
void ehop_wipe(void *p, uint32_t n);
void ehop_cpy(uint8_t *dst, const uint8_t *src, uint32_t n);
/* 1 when equal; time depends only on n. */
int ehop_ct_eq(const uint8_t *a, const uint8_t *b, uint32_t n);
void ehop_le16(uint8_t *p, uint32_t v);
void ehop_le32(uint8_t *p, uint32_t v);
void ehop_le64(uint8_t *p, uint64_t v);
uint32_t ehop_rd16(const uint8_t *p);
uint32_t ehop_rd32(const uint8_t *p);
uint64_t ehop_rd64(const uint8_t *p);
/* Copy a domain-separation string (no NUL); returns its length. */
uint32_t ehop_put_str(uint8_t *dst, const char *s);
/* Serialise cfg into 12 bytes. */
#define EHOP_CFG_BYTES 12u
void ehop_cfg_bytes(const ehop_cfg_t *cfg, uint8_t out[EHOP_CFG_BYTES]);

#endif /* ZXV_EHOP_INTERNAL_H */
