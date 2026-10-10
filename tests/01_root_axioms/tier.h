/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* tier.h — the shared harness for the tiered tests (tests/01_root_axioms,
 * tests/02_integration, tests/03_metamorphic). Hosted only: these programs
 * run on the build machine, never in the kernel image.
 *
 * WHAT IT GIVES A TEST
 *   CHECK(cond, fmt, ...)            an ordinary assertion; a failure fails the run.
 *   CHECK_KNOWN(id, cond, fmt, ...)  an assertion that a KNOWN, documented bug
 *                                    breaks. `id` must be listed in the file's
 *                                    KNOWN_FAILURES table (tier_known_t) and in
 *                                    tests/FINDINGS.md. A failure is reported as
 *                                    [XFAIL] and does not fail the run; a pass is
 *                                    reported as [XPASS] (the bug is fixed: delete
 *                                    the entry). TIER_STRICT=1 in the environment
 *                                    makes an XPASS fail the run, so CI can insist
 *                                    the table is kept current.
 *   Every KNOWN_FAILURES entry must be exercised at least once; an entry no
 *   check names is reported as [STALE] and fails the run, so the table cannot
 *   quietly hide a bug the tests stopped looking at.
 *   An XFAIL is never silent: each one is printed with its id and reason, and
 *   the summary line counts them.
 *
 * BOUNDARY GENERATORS (table-driven, per parameter type)
 *   tier_bounds_u64(M, out) -> {0, 1, M-1, M, M+1} (deduplicated, saturated):
 *     the calculated boundaries of a domain whose largest legal value is M.
 *   tier_bounds_i64(M, out) -> {-M-1, -M, -1, 0, 1, M-1, M, M+1} for a signed
 *     domain [-M, M] (saturating at INT64_MIN / INT64_MAX).
 *   TIER_I64_EDGES / TIER_U64_EDGES / TIER_U32_EDGES: the machine-type edges.
 *   A test loops over these instead of hand-picking values, so every function
 *   in scope sees -1, 0, 1, MAX-1, MAX and MAX+1 of every parameter.
 *
 * UB-EXPOSING KNOWN FAILURES
 *   Some known bugs ARE undefined behaviour (signed overflow in rmag_core, an
 *   int shift by 31 in mm). Executing them under -fsanitize=undefined with
 *   -fno-sanitize-recover would abort the whole run, so TIER_UB_KNOWN(id, ...)
 *   records them as [XFAIL] without executing the UB when the program is built
 *   with a sanitizer, and executes them otherwise.
 */
#ifndef ZXV_TIER_H
#define ZXV_TIER_H

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE /* fork/waitpid under -std=c11 */
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
#    define TIER_SANITIZED 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#        define TIER_SANITIZED 1
#    endif
#endif
#ifndef TIER_SANITIZED
#    define TIER_SANITIZED 0
#endif

typedef struct {
    const char *id;  /* e.g. "F-RMAG-DIV0" (matches tests/FINDINGS.md) */
    const char *why; /* one line */
} tier_known_t;

#define TIER_MAX_KNOWN 64
typedef struct {
    const char *suite;
    const tier_known_t *known;
    unsigned n_known;
    unsigned used[TIER_MAX_KNOWN];
    unsigned long pass, fail, xfail, xpass;
    bool verbose;
    bool strict;
} tier_state_t;

static tier_state_t tier_g;

static inline void tier_begin(const char *suite, const tier_known_t *known, unsigned n_known)
{
    memset(&tier_g, 0, sizeof tier_g);
    tier_g.suite = suite;
    tier_g.known = known;
    tier_g.n_known = n_known > TIER_MAX_KNOWN ? TIER_MAX_KNOWN : n_known;
    const char *v = getenv("TIER_VERBOSE");
    const char *s = getenv("TIER_STRICT");
    tier_g.verbose = v && v[0] == '1';
    tier_g.strict = s && s[0] == '1';
}

static inline int tier_known_index(const char *id)
{
    for (unsigned i = 0; i < tier_g.n_known; i++)
        if (strcmp(tier_g.known[i].id, id) == 0) return (int) i;
    return -1;
}

static inline void tier_report(bool ok, const char *id, const char *file, int line,
                               const char *expr, const char *msg)
{
    if (!id) {
        if (ok) {
            tier_g.pass++;
            if (tier_g.verbose) printf("  [PASS] %s:%d %s\n", file, line, msg);
        } else {
            tier_g.fail++;
            printf("  [FAIL] %s:%d (%s) %s\n", file, line, expr, msg);
        }
        return;
    }
    int k = tier_known_index(id);
    if (k < 0) { /* an id that is not in the table is a test bug: fail loudly */
        tier_g.fail++;
        printf("  [FAIL] %s:%d known-failure id %s is not in KNOWN_FAILURES\n", file, line, id);
        return;
    }
    if (!tier_g.used[k]) tier_g.used[k] = 1; /* exercised */
    if (ok) {
        tier_g.xpass++;
        tier_g.used[k] = 2;
        printf("  [XPASS] %s %s:%d %s -- the bug appears fixed: remove it from KNOWN_FAILURES\n",
               id, file, line, msg);
    } else {
        tier_g.xfail++;
        if (tier_g.used[k] < 3) /* print each known failure once, then count */
            printf("  [XFAIL] %s (%s) %s:%d %s\n", id, tier_g.known[k].why, file, line, msg);
        if (tier_g.used[k] < 3) tier_g.used[k] = 3;
    }
}

#define TIER_MSG_MAX 256
#define TIER_FMT(buf, ...)                                                                         \
    char buf[TIER_MSG_MAX];                                                                        \
    snprintf(buf, sizeof buf, __VA_ARGS__)

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        bool tier_ok_ = (cond);                                                                    \
        TIER_FMT(tier_m_, __VA_ARGS__);                                                            \
        tier_report(tier_ok_, NULL, __FILE__, __LINE__, #cond, tier_m_);                           \
    } while (0)

#define CHECK_KNOWN(id, cond, ...)                                                                 \
    do {                                                                                           \
        bool tier_ok_ = (cond);                                                                    \
        TIER_FMT(tier_m_, __VA_ARGS__);                                                            \
        tier_report(tier_ok_, id, __FILE__, __LINE__, #cond, tier_m_);                             \
    } while (0)

/* Record a known failure whose demonstration is undefined behaviour. Under a
 * sanitizer the statement block is not run and the failure is recorded as
 * observed (XFAIL); otherwise the block runs and must set `ok`. */
#define TIER_UB_KNOWN(id, ok, block)                                                               \
    do {                                                                                           \
        bool ok = false;                                                                           \
        if (!TIER_SANITIZED) {                                                                     \
            block;                                                                                 \
        }                                                                                          \
        tier_report(ok, id, __FILE__, __LINE__, #ok,                                               \
                    TIER_SANITIZED ? "(UB path: not executed under a sanitizer)"                   \
                                   : "(UB path: executed, result observed)");                      \
    } while (0)

/* A known failure that this build cannot demonstrate (e.g. it is only visible
 * under a sanitizer): marks the entry exercised, prints [SKIP], counts nothing. */
#define TIER_SKIP_KNOWN(id, why)                                                                   \
    do {                                                                                           \
        int k_ = tier_known_index(id);                                                             \
        if (k_ < 0) {                                                                              \
            tier_report(false, id, __FILE__, __LINE__, "skip", why);                               \
        } else {                                                                                   \
            if (!tier_g.used[k_]) tier_g.used[k_] = 1;                                             \
            printf("  [SKIP] %s %s:%d %s\n", id, __FILE__, __LINE__, why);                         \
        }                                                                                          \
    } while (0)

static inline int tier_end(void)
{
    unsigned stale = 0;
    for (unsigned i = 0; i < tier_g.n_known; i++)
        if (!tier_g.used[i]) {
            stale++;
            printf("  [STALE] %s: listed in KNOWN_FAILURES but no check exercises it\n",
                   tier_g.known[i].id);
        }
    bool bad = tier_g.fail > 0 || stale > 0 || (tier_g.strict && tier_g.xpass > 0);
    printf("%s %s: %lu pass, %lu fail, %lu xfail (known, see tests/FINDINGS.md), %lu xpass, "
           "%u stale\n",
           bad ? "[FAIL]" : "[PASS]", tier_g.suite, tier_g.pass, tier_g.fail, tier_g.xfail,
           tier_g.xpass, stale);
    return bad ? 1 : 0;
}

/* Run fn(arg) in a child process with stdout/stderr discarded. Returns true
 * when the child died abnormally (signal, sanitizer abort or non-zero exit):
 * the way to demonstrate a NULL-dereference or a fatal sanitizer report
 * without taking the whole test run down with it. */
static inline bool tier_crashes(void (*fn)(void *), void *arg)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) {
            dup2(dn, 1);
            dup2(dn, 2);
        }
        fn(arg);
        _exit(0);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return false;
    return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

/* ===== boundary generators ===== */

/* {0, 1, M-1, M, M+1}: the calculated boundaries of an unsigned domain whose
 * largest legal value is M. Saturates at UINT64_MAX and drops duplicates.
 * Returns the count (<= 5). */
static inline unsigned tier_bounds_u64(uint64_t M, uint64_t out[5])
{
    uint64_t c[5] = {0, 1, M ? M - 1 : 0, M, M == UINT64_MAX ? M : M + 1};
    unsigned n = 0;
    for (unsigned i = 0; i < 5; i++) {
        bool dup = false;
        for (unsigned j = 0; j < n; j++) dup |= out[j] == c[i];
        if (!dup) out[n++] = c[i];
    }
    return n;
}

/* {-M-1, -M, -1, 0, 1, M-1, M, M+1} for a signed domain [-M, M], M >= 1. */
static inline unsigned tier_bounds_i64(int64_t M, int64_t out[8])
{
    int64_t c[8] = {M == INT64_MAX ? INT64_MIN : -M - 1, -M, -1, 0, 1, M - 1, M,
                    M == INT64_MAX ? M : M + 1};
    unsigned n = 0;
    for (unsigned i = 0; i < 8; i++) {
        bool dup = false;
        for (unsigned j = 0; j < n; j++) dup |= out[j] == c[i];
        if (!dup) out[n++] = c[i];
    }
    return n;
}

static const int64_t TIER_I64_EDGES[] = {
    INT64_MIN, INT64_MIN + 1,      -(INT64_C(1) << 32), -2,       -1, 0, 1,
    2,         (INT64_C(1) << 32), INT64_MAX - 1,       INT64_MAX};
static const uint64_t TIER_U64_EDGES[] = {0,
                                          1,
                                          2,
                                          UINT32_MAX,
                                          (uint64_t) UINT32_MAX + 1,
                                          (uint64_t) INT64_MAX,
                                          (uint64_t) INT64_MAX + 1,
                                          UINT64_MAX - 1,
                                          UINT64_MAX};
static const uint32_t TIER_U32_EDGES[] = {
    0, 1, 2, INT32_MAX, (uint32_t) INT32_MAX + 1, UINT32_MAX - 1, UINT32_MAX};
#define TIER_N(a) (sizeof(a) / sizeof((a)[0]))

/* A deterministic generator for the sampled parts (xorshift64*). */
static inline uint64_t tier_rand(uint64_t *s)
{
    uint64_t x = *s ? *s : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 0x2545F4914F6CDD1Dull;
}

#endif /* ZXV_TIER_H */
