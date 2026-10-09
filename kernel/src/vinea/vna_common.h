/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_common.h — shared types and exact helpers for Vinea, the ZXV
 * peer-to-peer overlay (Hotline-style host spaces, Kademlia routing):
 * serverless and post-quantum. Design: docs/VINEA.md.
 *
 * Everything in src/vinea is freestanding integer C11: no libc, no
 * allocation (all state and buffers are the caller's), no floating point, no
 * 64-bit division, no clock calls (time is always passed in, in milliseconds),
 * no randomness source (randomness is passed in, or drawn from a vna_drbg_t
 * the caller seeded with real entropy).
 */
#ifndef VNA_COMMON_H
#define VNA_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define VNA_VERSION  2u
#define VNA_ID_LEN   32u /* NodeID = SHA3-256(ML-DSA-65 public key); never truncated */
#define VNA_HASH_LEN 32u
#define VNA_ADDR_MAX 32u /* opaque transport address, defined by the host layer */

typedef struct {
    uint8_t b[VNA_ID_LEN];
} vna_id_t;

typedef enum {
    VNA_OK = 0,
    VNA_ERR_ARG = -1,         /* NULL or out-of-range argument */
    VNA_ERR_PARSE = -2,       /* malformed bytes (bounds, lengths, enum ranges) */
    VNA_ERR_SIG = -3,         /* ML-DSA-65 signature does not verify */
    VNA_ERR_BINDING = -4,     /* NodeID != SHA3-256(public key) */
    VNA_ERR_POW = -5,         /* NodeID proof-of-work below the required difficulty */
    VNA_ERR_STALE = -6,       /* timestamp outside the acceptance window */
    VNA_ERR_REPLAY = -7,      /* sequence number already seen or too old */
    VNA_ERR_DST = -8,         /* message not addressed to this node */
    VNA_ERR_DUP = -9,         /* identical message bytes already processed */
    VNA_ERR_SPACE = -10,      /* caller buffer or table full */
    VNA_ERR_DENIED = -11,     /* refused by the sharing agreement */
    VNA_ERR_UNEXPECTED = -12, /* unsolicited response or wrong responder */
    VNA_ERR_FUNDS = -13,      /* balance does not cover the amount */
    VNA_ERR_CAP = -14,        /* over a cap or rate limit */
    VNA_ERR_AUTH = -15,       /* owner capability check failed */
    VNA_ERR_MERKLE = -16,     /* chunk does not prove into the file root */
    VNA_ERR_STATE = -17,      /* call not valid in the current state */
    VNA_ERR_EXPIRED = -18,    /* record or agreement past its expiry */
    VNA_ERR_HK = -19,         /* Hackronomicon command not canonical / unknown */
    VNA_ERR_FORK = -20,       /* receipt chain fork or gap */
    VNA_ERR_CRYPTO = -21,     /* key agreement failure (low-order point, bad tag) */
    VNA_ERR_INALIENABLE = -22 /* Crown capital form may not be traded */
} vna_status_t;

/* ---- little-endian codec (exact, any host byte order) ---- */
static inline void vna_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static inline void vna_put32(uint8_t *p, uint32_t v)
{
    for (uint32_t i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static inline void vna_put64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static inline uint16_t vna_get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | ((uint16_t) p[1] << 8));
}
static inline uint32_t vna_get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) v |= (uint32_t) p[i] << (8 * i);
    return v;
}
static inline uint64_t vna_get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * i);
    return v;
}

/* ---- byte helpers (no libc) ---- */
void vna_copy(void *dst, const void *src, uint32_t n);
void vna_zero(void *dst, uint32_t n);
/* Wipe secrets; the volatile store keeps the compiler from eliding it. */
void vna_wipe(void *dst, uint32_t n);
bool vna_eq(const void *a, const void *b, uint32_t n);    /* public data */
bool vna_ct_eq(const void *a, const void *b, uint32_t n); /* secrets/tags: constant time */
bool vna_id_eq(const vna_id_t *a, const vna_id_t *b);
bool vna_id_is_zero(const vna_id_t *a);

/* ---- XOR metric ---- */
/* -1 if a is closer to target than b, +1 if farther, 0 if equal. */
int vna_id_closer(const vna_id_t *target, const vna_id_t *a, const vna_id_t *b);
/* k-bucket index of `other` relative to `self`: 255 - (number of leading
 * equal bits); -1 if they are equal. Bucket 255 holds the farther half. */
int vna_bucket_index(const vna_id_t *self, const vna_id_t *other);
/* Number of leading zero bits of a 32-byte value. */
uint32_t vna_leading_zero_bits(const uint8_t h[32]);

/* ---- hashing (SHA3-256 from src/mlkem/keccak.h) ---- */
void vna_sha3(const uint8_t *data, uint32_t len, uint8_t out[32]);
/* SHA3-256(a || b) for two 32-byte values. */
void vna_h2(const uint8_t a[32], const uint8_t b[32], uint8_t out[32]);
/* Domain-separated hash: SHA3(SHA3(tag) || SHA3(data)). */
void vna_htag(const char *tag, const uint8_t *data, uint32_t len, uint8_t out[32]);

/* ---- deterministic random bit generator ----
 * SHAKE256 in counter mode with a forward-secure key ratchet. It is only as
 * good as its seed: the host must seed it from a real entropy source. */
typedef struct {
    uint8_t key[32];
    uint64_t ctr;
    bool seeded;
} vna_drbg_t;
void vna_drbg_seed(vna_drbg_t *d, const uint8_t *seed, uint32_t len);
void vna_drbg_gen(vna_drbg_t *d, uint8_t *out, uint32_t len);
uint64_t vna_drbg_u64(vna_drbg_t *d);

/* ---- exact arithmetic helpers (no 64-bit division) ---- */
uint64_t vna_sat_add(uint64_t a, uint64_t b);
uint64_t vna_min64(uint64_t a, uint64_t b);
/* Absolute difference. */
uint64_t vna_absdiff(uint64_t a, uint64_t b);

#endif /* VNA_COMMON_H */
