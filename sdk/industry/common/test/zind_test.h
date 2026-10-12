/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_test.h — the check macro, a deterministic PRNG and fixtures shared by
 * the tests and fuzzers of every industry kit. Header-only. */
#ifndef ZIND_TEST_H
#define ZIND_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zind_common.h"

static int g_checks, g_fail;

static inline const char *zt_last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zind_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, " [last error: %s]\n", zt_last_err());                             \
        }                                                                                      \
    } while (0)

/* CHECK that an expression returns the expected status. */
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zind_status_name(rc_), (int) (want), zind_status_name(want));                   \
    } while (0)

static inline int zt_report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

/* xorshift64*: deterministic across machines. */
static inline uint64_t zt_rand(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 2685821657736338717ULL;
}

static inline uint64_t zt_seed_from_env(uint64_t dflt) {
    const char *e = getenv("ZIND_FUZZ_SEED");
    uint64_t s = dflt;
    if (e && *e) s = 0x9E3779B97F4A7C15ULL ^ (uint64_t) strtoull(e, NULL, 10);
    return s ? s : 1;
}

/* A context with one root capability holding every 1.1 bit. */
typedef struct {
    zxv_ctx *ctx;
    zxv_cap *cap;
} zt_env;

static inline int zt_env_open(zt_env *e, const char *ccy) {
    memset(e, 0, sizeof *e);
    if (zxv_ctx_create(&e->ctx) != ZXV_OK) return -1;
    if (zxv_cap_create(e->ctx, 1, NULL, ZXV_CAP_ALL, &e->cap) != ZXV_OK) return -1;
    if (ccy && zxv_ccy_enable_cap(e->cap, ccy) != ZXV_OK) return -1;
    return 0;
}

static inline void zt_env_close(zt_env *e) {
    zxv_cap_destroy(e->cap);
    zxv_ctx_destroy(e->ctx);
    memset(e, 0, sizeof *e);
}

static inline void zt_seed(uint8_t seed[32], uint8_t v) {
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (v + i * 7);
}

/* A deterministic signer from one byte. */
static inline zind_signer *zt_signer(const zxv_cap *cap, uint8_t v) {
    uint8_t seed[32];
    zind_signer *s = NULL;
    zt_seed(seed, v);
    if (zind_signer_from_seed(cap, seed, &s) != ZXV_OK) return NULL;
    return s;
}

/* An issuer account (may carry a credit balance) for funding holders. */
static inline uint32_t zt_issuer(const zxv_cap *cap, const char *ccy) {
    uint32_t a = UINT32_MAX;
    if (zxv_ledger_open_account_cap(cap, "zt-issuer", ccy, ZXV_ACCT_ISSUER, &a) != ZXV_OK)
        return UINT32_MAX;
    return a;
}

/* Fund `holder` with `minor` units from `issuer`; 0 on success. */
static inline int zt_fund(const zxv_cap *cap, const char *ccy, uint32_t issuer, uint32_t holder,
                          int64_t minor, const char *ref) {
    return zxv_ledger_post_cap(cap, issuer, holder, minor, ccy, ref, NULL) == ZXV_OK ? 0 : -1;
}

static inline int64_t zt_equity(const zxv_cap *cap, uint32_t acct) {
    int64_t eq = -1;
    if (zxv_ledger_balance_cap(cap, acct, NULL, NULL, &eq) != ZXV_OK) return -999999;
    return eq;
}

#endif /* ZIND_TEST_H */
