/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zab_test.h — the check macro, a deterministic PRNG and an independent
 * model ("oracle") of the documented swarm rules, shared by the tests and
 * the fuzzer.
 *
 * The oracle is written from the rules as documented (zxv_api.h, the
 * kernel's swarm_budget.h R1-R6 and swarm_emotion.h E1-E4), not from the
 * library's code, so a test that compares the two checks that ZAB-Logic
 * reports exactly what the library computes and that the library computes
 * what its documentation says. The kit itself never uses it.
 */
#ifndef ZAB_TEST_H
#define ZAB_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zab_logic.h"

static int g_checks, g_fail;

static inline const char *last_err(void)
{
    static char b[300];
    size_t n = 0;
    if (zab_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, " [last error: %s]\n", last_err());                                \
        }                                                                                      \
    } while (0)

#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s = %s, want %s", #expr, zxv_status_name(rc_),                  \
              zxv_status_name(want));                                                          \
    } while (0)

/* ------------------------------------------------------------ PRNG */

typedef struct {
    uint64_t s;
} prng_t;

static inline uint64_t prng_next(prng_t *p) /* splitmix64 */
{
    uint64_t z = (p->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static inline uint64_t prng_below(prng_t *p, uint64_t n)
{
    return n ? prng_next(p) % n : 0;
}

/* ------------------------------------------------------------ palette */

#define FACE_NEUTRAL 0x1F610u
#define FACE_CALM    0x1F60Cu
#define FACE_JOY     0x1F604u
#define FACE_CURIOUS 0x1F914u
#define FACE_LOVE    0x1F970u
#define FACE_WORRY   0x1F61Fu
#define FACE_FRUST   0x1F620u
#define FACE_SAD     0x1F622u

static const uint32_t PALETTE[8] = {FACE_NEUTRAL, FACE_CALM,  FACE_JOY,   FACE_CURIOUS,
                                    FACE_LOVE,    FACE_WORRY, FACE_FRUST, FACE_SAD};

/* ------------------------------------------------------------ oracle */

static inline uint64_t o_fib(uint32_t n)
{
    uint64_t a = 0, b = 1; /* F(0), F(1) */
    for (uint32_t i = 0; i < n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return a;
}

/* Documented: intensity 0..5 has charge 0,1,2,3,5,8; the neutral face 0. */
static inline uint64_t o_charge(uint32_t codepoint, uint32_t k)
{
    static const uint64_t C[6] = {0, 1, 2, 3, 5, 8};
    if (codepoint == FACE_NEUTRAL || k > 5) return 0;
    return C[k];
}

/* Largest remainder: floor shares, then one more unit to the largest
 * remainders, ties to the lower index; zero weights get nothing. Values in
 * the tests keep total * w below 2^64. */
static inline void o_split(uint64_t total, const uint64_t *w, uint32_t n, uint64_t *out)
{
    uint64_t sum = 0, given = 0, rem[ZAB_MAX_AGENTS];
    int used[ZAB_MAX_AGENTS];
    for (uint32_t i = 0; i < n; i++) sum += w[i];
    for (uint32_t i = 0; i < n; i++) {
        out[i] = sum ? total * w[i] / sum : 0;
        rem[i] = sum ? total * w[i] % sum : 0;
        used[i] = 0;
        given += out[i];
    }
    if (!sum) return;
    for (uint64_t left = total - given; left > 0; left--) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++)
            if (w[i] && !used[i] && (best == n || rem[i] > rem[best])) best = i;
        if (best == n) break;
        out[best]++;
        used[best] = 1;
    }
}

typedef struct {
    uint32_t level, active, cp, k;
} o_agent_t;

/* Expected allotments (real + emotional) and emotional parts, for agents in
 * the order they were added. */
static inline void o_allot(uint32_t L, uint64_t T, uint32_t mood_cp, uint32_t mood_k,
                           const o_agent_t *a, uint32_t n, uint64_t *allot, uint64_t *imag)
{
    uint64_t charge[ZAB_MAX_AGENTS], any = 0;
    for (uint32_t i = 0; i < n; i++) {
        charge[i] = a[i].active ? o_charge(a[i].cp, a[i].k) : 0;
        any += charge[i];
    }
    /* E1 + E4: the mood moves charge/21 of T to the emotional axis, unless
     * no active agent has a charge. */
    uint64_t pool = any ? T * o_charge(mood_cp, mood_k) / 21u : 0;
    uint64_t real = T - pool;
    /* R2 + R3: level d weighs F(L-d+1) when it has an active agent. */
    uint64_t lw[ZAB_MAX_LEVELS] = {0}, lb[ZAB_MAX_LEVELS];
    for (uint32_t i = 0; i < n; i++)
        if (a[i].active) lw[a[i].level] = o_fib(L - a[i].level + 1);
    o_split(real, lw, L, lb);
    /* R4: equal shares inside a level, in the order agents were added. */
    for (uint32_t i = 0; i < n; i++) allot[i] = 0;
    for (uint32_t d = 0; d < L; d++) {
        uint32_t idx[ZAB_MAX_AGENTS], m = 0;
        uint64_t ones[ZAB_MAX_AGENTS], share[ZAB_MAX_AGENTS];
        for (uint32_t i = 0; i < n; i++)
            if (a[i].level == d && a[i].active) {
                idx[m] = i;
                ones[m] = 1;
                m++;
            }
        o_split(lb[d], ones, m, share);
        for (uint32_t j = 0; j < m; j++) allot[idx[j]] = share[j];
    }
    /* E3: the pool by each active agent's own charge. */
    o_split(pool, charge, n, imag);
    for (uint32_t i = 0; i < n; i++) allot[i] += imag[i];
}

/* The documented gate (zab_logic.h), from the tag's fields alone. */
static inline int32_t o_gate(uint8_t tag, uint64_t tokens, uint64_t left)
{
    uint8_t base = (uint8_t) (tag >> 4), pol = (uint8_t) ((tag >> 2) & 3u), ph = tag & 3u;
    if (pol == ZXV_HEX_POL_FORBIDDEN) return ZAB_QUARANTINED;
    if (base == ZXV_HEX_BASE_BOTH) return ZAB_HELD;
    if (base == ZXV_HEX_BASE_FALSE) return ZAB_REFUSED;
    if (base == ZXV_HEX_BASE_NEITHER) return ZAB_DEFERRED;
    if (pol == ZXV_HEX_POL_ENTROPIC) return ZAB_DEFERRED; /* a weakening TRUE waits */
    if (ph != ZXV_HEX_PHASE_MANIFEST) return ZAB_DEFERRED;
    return tokens <= left ? ZAB_ADMITTED : ZAB_OVER_BUDGET;
}

static inline uint8_t mk_tag(uint8_t base, uint8_t pol, uint8_t phase)
{
    return (uint8_t) ((base << 4) | (pol << 2) | phase);
}

#define TAG_GO mk_tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST)

#endif /* ZAB_TEST_H */
