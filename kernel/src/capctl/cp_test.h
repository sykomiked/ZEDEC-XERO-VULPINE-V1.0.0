/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cp_test.h — shared host-test helpers for capctl, taxhook and kycwrap.
 * Host only (stdio). One "[PASS] ..." line per check; cp_test_done prints
 * the summary line and returns the exit code. */
#ifndef ZXV_CP_TEST_H
#define ZXV_CP_TEST_H

#include <stdio.h>
#include <string.h>
#include "cp_common.h"
#include "cpk.h"

static int cp_t_pass, cp_t_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            cp_t_pass++;                                                                           \
            printf("[PASS] ");                                                                     \
        } else {                                                                                   \
            cp_t_fail++;                                                                           \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
        }                                                                                          \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static inline int cp_test_done(const char *name)
{
    if (cp_t_fail) printf("%s: %d of %d checks failed\n", name, cp_t_fail, cp_t_pass + cp_t_fail);
    printf("%d checks\n", cp_t_pass + cp_t_fail);
    return cp_t_fail ? 1 : 0;
}

/* A test signer: deterministic ML-DSA-87 key from a one-byte seed. */
typedef struct {
    uint8_t pk[CPK_PK_LEN];
    uint8_t sk[CPK_SK_LEN];
    uint8_t id[CP_KEYID_LEN];
} cp_t_key_t;

static inline void cp_t_keygen(cp_t_key_t *k, uint8_t seed_byte)
{
    uint8_t seed[32];
    memset(seed, seed_byte, sizeof seed);
    cpk_keygen(seed, k->pk, k->sk);
    cpk_key_id(k->pk, k->id);
}

/* Sign an encoded manifest (M2) with k. */
static inline void cp_t_sign(const cp_t_key_t *k, const uint8_t *m, size_t len,
                             uint8_t sig[CPK_SIG_LEN])
{
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(m, len, msg);
    cpk_sign(k->sk, msg, sizeof msg, NULL, sig);
}

#endif /* ZXV_CP_TEST_H */
