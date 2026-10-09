/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest_badge.h — badges that certify verified achievement.
 *
 * A badge here is a certificate, not a participation trophy:
 *
 *   B1  EARNED. qst_badge_issue refuses unless the holder's skill has
 *       reached the badge level (quest.h Q3) AND the points came from at
 *       least min(level, QST_VERIFIER_SET) distinct verifiers, so one
 *       friendly verifier cannot certify anyone past level 1. Opted-out
 *       profiles are not issued badges until they opt back in.
 *   B2  SIGNED. The badge is a fixed 124-byte record (version, skill,
 *       level, public flag, holder subject and key id, issuer key id,
 *       evidence root, distinct verifiers, season, day, serial). Its digest
 *       is SHA3-256("ZXV-QUEST-BADGE-v1" || record), signed by the issuer
 *       through a callback. quest_sign_mldsa.h binds it to ML-DSA-65.
 *   B3  PORTABLE. qst_badge_encode / qst_badge_decode carry the record
 *       between devices and peers; anyone with the issuer's public key can
 *       check it offline. The evidence root is a SHA3 chain over the
 *       evidence ids of the records behind the badge, so a peer holding
 *       those records can recompute it.
 *   B4  REVOCABLE IF FRAUDULENT. A revocation names the badge digest, the
 *       issuer key id, a reason and a day, and is signed by the same issuer.
 *       A revocation list accepts only revocations whose signature checks,
 *       and qst_badge_check reports REVOKED for a badge on it.
 *       qst_badge_still_backed tells an issuer when rejected evidence
 *       (qst_reject_evidence) has left a badge unsupported.
 *   B5  REPUTATION. qst_badge_to_reputation shows a valid badge through the
 *       existing reputation module (badge_award), as id
 *       QST_REP_BADGE_BASE + skill. Badges of minors are marked non-public
 *       and are not mirrored.
 *
 * HONEST LIMITS. reputation.h badge levels only ever rise, so a revoked
 * badge that was already mirrored stays in reputation's score until that
 * module gains a way to lower a level; quest_badge keeps the authoritative
 * signed record and revocation list. Revocation lists spread only as far
 * as peers exchange them: a peer that never sees a revocation still
 * accepts the badge. The issuer key id is SHA3-256 of the issuer public
 * key; resolving it to a key is the verify callback's job.
 * Freestanding C11: no libc, no allocation, no floating point.
 */
#ifndef ZXV_QUEST_BADGE_H
#define ZXV_QUEST_BADGE_H

#include "quest.h"
#include "reputation.h"

#define QST_BADGE_VERSION  1u
#define QST_BADGE_ENC_LEN  124u
#define QST_REVOKE_ENC_LEN 69u
#define QST_MAX_REVOKED    256u
#define QST_REP_BADGE_BASE 0x51540000u

typedef struct {
    uint8_t version;
    uint8_t skill;
    uint8_t level;
    uint8_t is_public;
    uint32_t holder_subject;
    uint8_t holder_key[32]; /* holder's key id (e.g. SHA3 of their public key) */
    uint8_t issuer_key[32]; /* SHA3-256 of the issuer's public key            */
    uint8_t evidence_root[32];
    uint32_t distinct_verifiers;
    uint32_t season;
    uint32_t issued_day;
    uint64_t serial;
} qst_badge_t;

/* Sign a 32-byte digest; write at most `cap` bytes and the length. */
typedef bool (*qst_sign_fn)(void *ctx, const uint8_t digest[32], uint8_t *sig, uint32_t cap,
                            uint32_t *len);
/* Check `sig` over `digest` by the key whose id is `issuer_key`. */
typedef bool (*qst_verify_fn)(void *ctx, const uint8_t issuer_key[32], const uint8_t digest[32],
                              const uint8_t *sig, uint32_t len);

typedef enum {
    QST_BADGE_VALID = 0,
    QST_BADGE_BAD_SIG = 1,
    QST_BADGE_REVOKED = 2,
    QST_BADGE_MALFORMED = 3
} qst_badge_verdict_t;

typedef enum {
    QST_REVOKE_FRAUD = 1,
    QST_REVOKE_ERROR = 2,
    QST_REVOKE_WITHDRAWN = 3
} qst_revoke_reason_t;

typedef struct {
    uint8_t badge_digest[32];
    uint8_t issuer_key[32];
    uint8_t reason; /* qst_revoke_reason_t */
    uint32_t day;
} qst_revocation_t;

typedef struct {
    qst_revocation_t r[QST_MAX_REVOKED];
    uint32_t n;
} qst_revlist_t;

/* B1 */
bool qst_badge_eligible(const qst_world_t *w, uint32_t subject, qst_skill_t skill, uint32_t level);
/* B3: the evidence chain for a subject's skill. */
void qst_evidence_root(const qst_world_t *w, uint32_t subject, qst_skill_t skill, uint8_t out[32]);

/* B1, B2: build and sign. Returns QST_ERR_NOT_ELIGIBLE when B1 fails. */
qst_status_t qst_badge_issue(const qst_world_t *w, uint32_t subject, qst_skill_t skill,
                             uint32_t level, const uint8_t holder_key[32],
                             const uint8_t issuer_key[32], uint32_t day, uint64_t serial,
                             qst_sign_fn sign, void *sign_ctx, qst_badge_t *out, uint8_t *sig,
                             uint32_t sig_cap, uint32_t *sig_len);

/* B2, B3 */
void qst_badge_encode(const qst_badge_t *b, uint8_t out[QST_BADGE_ENC_LEN]);
bool qst_badge_decode(const uint8_t *in, uint32_t len, qst_badge_t *out);
void qst_badge_digest(const qst_badge_t *b, uint8_t out[32]);

/* B4 */
void qst_revocation_digest(const qst_revocation_t *r, uint8_t out[32]);
void qst_revlist_init(qst_revlist_t *l);
/* Accept a signed revocation (idempotent). False on a bad signature or full list. */
bool qst_revlist_add(qst_revlist_t *l, const qst_revocation_t *r, const uint8_t *sig,
                     uint32_t sig_len, qst_verify_fn verify, void *verify_ctx);
qst_badge_verdict_t qst_badge_check(const qst_badge_t *b, const uint8_t *sig, uint32_t sig_len,
                                    qst_verify_fn verify, void *verify_ctx,
                                    const qst_revlist_t *revoked);
bool qst_badge_still_backed(const qst_world_t *w, const qst_badge_t *b);

/* B5. Returns the reputation level, or -1 when not valid, not public or full. */
int32_t qst_badge_to_reputation(rep_state_t *rep, const qst_badge_t *b, const uint8_t *sig,
                                uint32_t sig_len, qst_verify_fn verify, void *verify_ctx,
                                const qst_revlist_t *revoked);

#endif /* ZXV_QUEST_BADGE_H */
