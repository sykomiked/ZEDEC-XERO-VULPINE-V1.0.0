/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_internal.h -- helpers shared by the ident files. Not a public API.
 *
 * HONEST LIMITS: the hash helper concatenates its parts into a bounded
 * stack buffer (ID_HBUF bytes); longer inputs are refused, never cut.
 * The signature scratch is one static object: not re-entrant.
 */
#ifndef ZXV_ID_INTERNAL_H
#define ZXV_ID_INTERNAL_H

#include "ident.h"

#define ID_HBUF 1536u

void id_mcpy(void *dst, const void *src, size_t n);
void id_mset(void *dst, uint8_t v, size_t n);
bool id_meq(const void *a, const void *b, size_t n); /* constant time */
void id_wipe(void *p, size_t n);
uint32_t id_strlen(const char *s, uint32_t max);
bool id_streq(const char *a, const char *b, uint32_t max);

/* Byte writer / reader (big-endian). */
typedef struct {
    uint8_t *p;
    uint32_t cap, len;
    bool err;
} id_w_t;
typedef struct {
    const uint8_t *p;
    uint32_t len, pos;
    bool err;
} id_r_t;

void id_w_init(id_w_t *w, uint8_t *p, uint32_t cap);
void id_w_u8(id_w_t *w, uint8_t v);
void id_w_u16(id_w_t *w, uint16_t v);
void id_w_u32(id_w_t *w, uint32_t v);
void id_w_u64(id_w_t *w, uint64_t v);
void id_w_bytes(id_w_t *w, const void *b, uint32_t n);
void id_r_init(id_r_t *r, const uint8_t *p, uint32_t len);
uint8_t id_r_u8(id_r_t *r);
uint16_t id_r_u16(id_r_t *r);
uint32_t id_r_u32(id_r_t *r);
uint64_t id_r_u64(id_r_t *r);
void id_r_bytes(id_r_t *r, void *b, uint32_t n);

/* SHAKE256 / SHA3-256 over (label, a, b, c), any of a..c may be NULL. */
bool id_shake(uint8_t *out, uint32_t out_len, const char *label, const void *a, uint32_t alen,
              const void *b, uint32_t blen, const void *c, uint32_t clen);
bool id_sha3(uint8_t out[ID_HASH], const char *label, const void *a, uint32_t alen, const void *b,
             uint32_t blen, const void *c, uint32_t clen);

/* pq_matrix signing / verifying with encoded signatures and a static
 * pqm_sig_t scratch. ctx is a NUL-terminated domain string. */
id_status_t id_pqm_sign(const pqm_sig_sk_t *sk, const id_host_t *h, const char *ctx,
                        const uint8_t *msg, uint32_t len, uint8_t *sig, uint32_t *sig_len);
id_status_t id_pqm_verify(const pqm_sig_pk_t *pk, const char *ctx, const uint8_t *msg, uint32_t len,
                          const uint8_t *sig, uint32_t sig_len);

void id_alert(const id_host_t *h, uint8_t kind, const uint8_t *account, const uint8_t *vault,
              const uint8_t *ref, uint64_t when_ms, uint32_t count);

int32_t id_b64url(const uint8_t *in, uint32_t len, char *out, uint32_t cap);

#endif /* ZXV_ID_INTERNAL_H */
