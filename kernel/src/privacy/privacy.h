/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* privacy.h — the privacy layer: exact differential privacy, a privacy
 * budget, four-valued access control and selective disclosure.
 *
 * DESIGNED TO SUPPORT GDPR- and HIPAA-style controls (purpose limitation,
 * consent, retention, minimum disclosure). It is not a compliance claim:
 * compliance belongs to a deployment, its processes and its lawyers.
 *
 *   P1 EXACT NOISE. priv_dlap samples the discrete Laplace distribution
 *      P(Y = y) proportional to exp(-|y| * s / t) with integer arithmetic
 *      only (Canonne, Kamath and Steinke 2020, "The Discrete Gaussian for
 *      Differential Privacy", Algorithms 1 and 2): Bernoulli(exp(-g)) for a
 *      rational g, and rejection, from uniform random words. No floating
 *      point, so none of the rounding attacks on floating-point Laplace
 *      noise. For epsilon = en/ed and sensitivity d: s = en, t = ed * d.
 *   P2 BUDGET. Sequential composition: each release spends its epsilon
 *      (an exact fraction); a release that would take the total past the
 *      budget is refused before any noise is drawn. Spent budget never
 *      comes back; a new budget is the data owner's decision.
 *   P3 RELEASE. priv_release_count spends first, then adds noise; a
 *      failure after spending keeps the spend (fail closed).
 *   P4 ACCESS. A request (role, data category, purpose, subject) is checked
 *      against rules that each say allow or deny. The answer is a 64-state
 *      tag (lpres_hex.h): allow only -> TRUE, deny only -> FALSE, allow and
 *      deny both -> BOTH (escalate to a person), no rule -> NEITHER (wait;
 *      never allow). Then two hard checks turn TRUE into FALSE:
 *        - special-category data (health, biometric, genetic, ... the
 *          caller's mask) needs an explicit consent from the subject for
 *          this purpose, not withdrawn and not expired;
 *        - a record older than its category's retention period is FALSE
 *          (it is due for erasure, not for reading).
 *   P5 SELECTIVE DISCLOSURE. An issuer commits to each claim as
 *        SHA-256("zxv/sd/v1" || salt[16] || u8 len || name || u8 len || value)
 *      and signs the list of digests. A holder shows only the claims they
 *      choose (salt, name, value); priv_sd_check recomputes each digest and
 *      checks it is in the list, after the caller's verify hook checks the
 *      issuer's signature over priv_sd_list_msg. "Over 18" is an issuer's
 *      boolean claim (age_over_18), the way ISO 18013-5 mobile licences and
 *      SD-JWT credentials do it, so the birth date is never shown.
 *
 * HONEST LIMITS. No zero-knowledge proofs here: lattice ZK proofs (or any
 * ZK proof) need a vetted, audited library, and this layer will wrap one
 * rather than invent one. The noise is exact given uniform random words
 * from a CSPRNG the caller supplies; the sampler's running time depends on
 * the noise drawn, so a local timing observer learns something about it.
 * Differential privacy protects aggregates, not a single record shown in
 * full. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_PRIVACY_H
#define ZXV_PRIVACY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres_hex.h"

typedef enum {
    PRIV_OK = 0,
    PRIV_E_ARG = -1,
    PRIV_E_BUDGET = -2,   /* P2 */
    PRIV_E_RNG = -3,      /* P1: the random source looks broken */
    PRIV_E_OVERFLOW = -4,
    PRIV_E_SIG = -5,      /* P5 */
    PRIV_E_CLAIM = -6     /* P5: a disclosed claim is not in the list */
} priv_status_t;

/* A uniform 64-bit word from a CSPRNG. */
typedef uint64_t (*priv_rng_fn)(void *ctx);
typedef struct {
    priv_rng_fn next;
    void *ctx;
} priv_rng_t;

/* ---- P1 ---- */
/* Bernoulli(num/den), 0 <= num <= den, den > 0 */
priv_status_t priv_bern(priv_rng_t *r, uint64_t num, uint64_t den, bool *out);
/* Bernoulli(exp(-num/den)) */
priv_status_t priv_bern_exp(priv_rng_t *r, uint64_t num, uint64_t den, bool *out);
/* discrete Laplace with P(y) ~ exp(-|y| s/t), s, t > 0 */
priv_status_t priv_dlap(priv_rng_t *r, uint64_t s, uint64_t t, int64_t *out);

/* ---- P2 ---- */
typedef struct {
    uint64_t total_num, total_den; /* the budget, epsilon as a fraction */
    uint64_t spent_num, spent_den; /* reduced */
    uint32_t releases;
} priv_budget_t;
priv_status_t priv_budget_init(priv_budget_t *b, uint64_t num, uint64_t den);
/* spend en/ed if it fits; PRIV_E_BUDGET (nothing spent) otherwise */
priv_status_t priv_budget_spend(priv_budget_t *b, uint64_t en, uint64_t ed);

/* ---- P3 ---- */
priv_status_t priv_release_count(priv_budget_t *b, priv_rng_t *r, int64_t true_value,
                                 uint64_t en, uint64_t ed, uint64_t sensitivity, int64_t *out);

/* ---- P4 ---- */
#define PRIV_ANY 0xFFFFu
typedef enum { PRIV_DENY = 0, PRIV_ALLOW = 1 } priv_verdict_t;
typedef struct {
    uint16_t role, category, purpose; /* PRIV_ANY matches any */
    uint8_t verdict;                  /* priv_verdict_t */
} priv_rule_t;
typedef struct {
    uint32_t subject;
    uint16_t purpose;
    uint64_t granted, expires; /* expires 0 = no expiry */
    bool explicit_consent;
    bool withdrawn;
} priv_consent_t;
#define PRIV_MAX_CATEGORIES 32u
typedef struct {
    const priv_rule_t *rules;
    uint32_t n_rules;
    const priv_consent_t *consents;
    uint32_t n_consents;
    uint32_t special_mask;                   /* bit c: category c is special */
    uint64_t retention[PRIV_MAX_CATEGORIES]; /* 0 = keep */
} priv_policy_t;
typedef struct {
    uint16_t role, category, purpose;
    uint32_t subject;
    uint64_t now;
    uint64_t record_time;
} priv_request_t;
lpx_t priv_access(const priv_policy_t *p, const priv_request_t *q);

/* ---- P5 ---- */
#define PRIV_SD_SALT 16u
#define PRIV_SD_MAX 255u
typedef struct {
    uint8_t salt[PRIV_SD_SALT];
    const char *name;
    const char *value;
} priv_claim_t;
priv_status_t priv_sd_digest(const priv_claim_t *c, uint8_t out[32]);
/* the message the issuer signs: "zxv/sd-list/v1" || u32 n || digests */
priv_status_t priv_sd_list_msg(const uint8_t (*digests)[32], uint32_t n, uint8_t out[32]);
typedef bool (*priv_verify_fn)(void *ctx, const uint8_t msg[32], const uint8_t *sig, uint32_t sig_len);
priv_status_t priv_sd_check(const uint8_t (*digests)[32], uint32_t n, const uint8_t *sig,
                            uint32_t sig_len, priv_verify_fn verify, void *vctx,
                            const priv_claim_t *shown, uint32_t n_shown);

#endif /* ZXV_PRIVACY_H */
