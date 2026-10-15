/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kycwrap.h — a privacy-preserving KYC wrapper (roadmap R3.6).
 *
 * WHAT IT IS
 * One API for "prove a predicate about me without showing who I am":
 * "over 18", "KYC'd by issuer X", "resident of country C". A verifier asks
 * with a fresh challenge; the holder answers with a proof; a pluggable
 * backend checks it. Two backends:
 *
 *   ZK BACKEND (kyc_zk_backend). The slot for a vetted, audited
 *      zero-knowledge library. The roadmap rule is that we do not write our
 *      own ZK cryptography, so today every call returns KYC_E_NOT_AVAILABLE.
 *      A library dropped in later implements kyc_backend_t and sets
 *      zero_knowledge = true.
 *
 *   MINIMAL-DISCLOSURE BACKEND (kyc_md_backend). Honestly NOT zero-
 *      knowledge. It only uses ML-DSA-87 signatures (through hooks; cpk.h
 *      supplies them) and SHA-256. It works like this:
 *
 * MINIMAL-DISCLOSURE RULES
 *   D1 TOKENS. After checking a person once (outside this code), the issuer
 *      batch-issues single-use tokens. Each token carries only: the
 *      predicate, the issuer's country and key id, a token id, an expiry
 *      day, and a fresh holder public key (ML-DSA-87) that the holder made
 *      for this token alone. The issuer signs it (context CPK_CTX):
 *        "zxv/kyc-md-token/v1" || SHA-256(token body).
 *      No name, birth date, document number or account appears anywhere.
 *   D2 TOKEN IDS. token_id = SHA-256("zxv/kyc-md-tokid/v1" || batch_seed ||
 *      index as u32 LE), where batch_seed is a secret 32-byte value the
 *      issuer draws per batch. Ids look random to everyone without the seed.
 *   D3 EXPIRY. expiry_day (days since 1970-01-01 UTC) must be a multiple of
 *      KYC_MD_EXPIRY_QUANTUM (7), so everyone issued in the same week shares
 *      one expiry and it does not single anybody out. Tokens past their
 *      expiry day are refused.
 *   D4 PRESENTATION. The verifier sends (predicate, verifier id, 32-byte
 *      random challenge). The holder sends the token plus a signature with
 *      the token's holder key over
 *        "zxv/kyc-md-present/v1" || SHA-256(token) || verifier id ||
 *        challenge || predicate (kind, age, country).
 *      The verifier checks, in order: exact length and format; the token's
 *      predicate equals the requested one exactly (a "over 18" token does
 *      not answer "over 16": answering would reveal more than asked); the
 *      issuer's signature (the issuer key must be trusted for
 *      CP_KIND_KYC_TOKEN and the issuer's country); expiry; revocation;
 *      not already spent here; the holder's signature. Only then is the
 *      token id added to the verifier's spent set.
 *   D5 SINGLE USE. A verifier keeps a spent-token set (caller storage). A
 *      token seen before is refused. When the set is full, entries whose
 *      token has expired are dropped; if it is still full the presentation
 *      is refused (fail closed), never accepted unrecorded. The holder's
 *      wallet (kyc_md_wallet_*) hands out each token once, so a holder does
 *      not present one token to two verifiers by mistake.
 *   D6 REVOCATION. The issuer publishes a signed revocation list (a
 *      cp_common.h manifest, kind CP_KIND_KYC_REVOKE, header key id = the
 *      issuer key): entries of (batch_seed, count). Revealing a batch's
 *      seed lets every verifier compute that batch's token ids (D2) and
 *      refuse them. With require_revlist (the default), a verifier refuses
 *      every token of an issuer for which it holds no revocation list valid
 *      at `now`: no list, no acceptance.
 *   D7 CHALLENGES. kyc_request_new records each challenge with its
 *      predicate; kyc_verify accepts a proof only for a recorded challenge
 *      issued at most KYC_CHALLENGE_TTL seconds ago, and consumes it whether
 *      the proof passes or not. A replayed presentation therefore fails.
 *
 * WHAT IS AND IS NOT HIDDEN (minimal-disclosure backend)
 *   Hidden from verifiers: identity, birth date, address, the document seen.
 *   Two verifiers comparing notes cannot link two presentations by their
 *   contents: different tokens share only fields common to everyone with the
 *   same issuer, predicate and expiry week. Shown to the verifier: issuer,
 *   predicate, expiry week. With a small issuer or a rare predicate that
 *   group can be small.
 *   NOT hidden from a colluding issuer: the issuer saw every holder key and
 *   token id at issuance next to the person's identity, so an issuer that
 *   shares data with a verifier can link any presentation to the person.
 *   This is the main difference from a real ZK scheme. Revoking a batch
 *   makes that batch's presentations linkable to each other. Network
 *   metadata (IP address, timing) is outside this code.
 *
 * OFF BY DEFAULT. kyc_verifier_init leaves the verifier disabled:
 * kyc_request_new and kyc_verify return KYC_E_DISABLED until
 * kyc_verifier_enable(v, true).
 *
 * HONEST LIMITS. No ZK library is included or invented here. The issuer's
 * identity check and its record keeping are outside this code and are where
 * most legal duties sit (data protection, AML/KYC rules per country). Tested
 * on the host only. Revocation checks cost up to KYC_MD_REV_MAX x
 * KYC_MD_BATCH_MAX SHA-256 calls per presentation. Freestanding C11.
 */
#ifndef ZXV_KYCWRAP_H
#define ZXV_KYCWRAP_H

#include "../capctl/cp_common.h"
#include "../pqsec/pq_matrix_algs.h"

#define KYC_PK_LEN  PQM_MLDSA87_PK_BYTES
#define KYC_SIG_LEN PQM_MLDSA87_SIG_BYTES
#define KYC_ID_LEN  32u

#define KYC_PRED_AGE_OVER 1u /* age = the threshold, 1..150 */
#define KYC_PRED_KYCD     2u /* "KYC'd by this issuer"; age 0, country zero */
#define KYC_PRED_RESIDENT 3u /* country = ISO 3166-1 alpha-2 */

typedef struct {
    uint8_t kind;
    uint8_t age;
    char country[2];
} kyc_pred_t;

typedef enum {
    KYC_OK = 0,
    KYC_E_ARG = -1,
    KYC_E_DISABLED = -2,
    KYC_E_NOT_AVAILABLE = -3, /* ZK backend: no vetted library yet */
    KYC_E_FULL = -4,
    KYC_E_CHALLENGE = -5, /* D7: unknown, expired or used challenge */
    KYC_E_DECODE = -6,
    KYC_E_PREDICATE = -7,
    KYC_E_ISSUER = -8, /* issuer signature or issuer not trusted */
    KYC_E_EXPIRED = -9,
    KYC_E_REVLIST = -10, /* D6: no current revocation list */
    KYC_E_REVOKED = -11,
    KYC_E_SPENT = -12,
    KYC_E_SPENT_FULL = -13,
    KYC_E_HOLDER = -14, /* holder signature */
    KYC_E_SIG = -15,    /* revocation list signature */
    KYC_E_WINDOW = -16,
    KYC_E_VERSION = -17
} kyc_status_t;

typedef struct {
    kyc_pred_t pred;
    uint8_t verifier_id[KYC_ID_LEN];
    uint8_t challenge[32];
    uint64_t issued_at; /* unix seconds */
} kyc_request_t;

typedef struct {
    kyc_pred_t pred;
    char issuer_country[2];
    uint8_t issuer_id[KYC_ID_LEN];
    uint8_t token_id[KYC_ID_LEN];
    uint32_t expiry_day;
} kyc_result_t;

/* ---- backend interface --------------------------------------------- */
typedef struct kyc_backend {
    const char *name;
    bool zero_knowledge; /* true only for a vetted, audited ZK library */
    /* Check `proof` against `req` at `now`; fill res on KYC_OK. */
    kyc_status_t (*verify)(void *ctx, const kyc_request_t *req, const uint8_t *proof, size_t len,
                           uint64_t now, kyc_result_t *res);
    void *ctx;
} kyc_backend_t;

/* The slot for a future vetted ZK library: every call is NOT_AVAILABLE. */
void kyc_zk_backend(kyc_backend_t *b);

/* ---- verifier front (D7) ------------------------------------------- */
#define KYC_MAX_PENDING   16u
#define KYC_CHALLENGE_TTL 300u

typedef struct {
    bool used;
    kyc_request_t req;
} kyc_pending_t;

typedef struct {
    bool enabled;
    uint8_t verifier_id[KYC_ID_LEN];
    const kyc_backend_t *backend;
    kyc_pending_t pending[KYC_MAX_PENDING];
} kyc_verifier_t;

void kyc_verifier_init(kyc_verifier_t *v, const uint8_t verifier_id[KYC_ID_LEN],
                       const kyc_backend_t *backend);
void kyc_verifier_enable(kyc_verifier_t *v, bool on);
/* rnd: 32 fresh random bytes from the caller's RNG. */
kyc_status_t kyc_request_new(kyc_verifier_t *v, const kyc_pred_t *pred, const uint8_t rnd[32],
                             uint64_t now, kyc_request_t *out);
kyc_status_t kyc_verify(kyc_verifier_t *v, const kyc_request_t *req, const uint8_t *proof,
                        size_t len, uint64_t now, kyc_result_t *res);

bool kyc_pred_ok(const kyc_pred_t *p);

/* ---- minimal-disclosure backend ------------------------------------ */
#define KYC_MD_EXPIRY_QUANTUM 7u
#define KYC_MD_BATCH_MAX      256u
#define KYC_MD_REV_MAX        64u
#define KYC_MD_MAX_ISSUERS    8u
#define KYC_MD_BODY_LEN       (4u + 1u + 1u + 1u + 2u + 2u + KYC_ID_LEN + KYC_ID_LEN + 4u + KYC_PK_LEN)
#define KYC_MD_TOKEN_LEN      (KYC_MD_BODY_LEN + KYC_SIG_LEN)
#define KYC_MD_PRESENT_LEN    (KYC_MD_TOKEN_LEN + KYC_SIG_LEN)
#define KYC_MD_REVLIST_MAX    (CP_HDR_LEN + 2u + KYC_MD_REV_MAX * 34u)

/* sign(ctx, msg, len, sig): sign with the caller's key (cpk_sign). */
typedef void (*kyc_sign_fn)(void *ctx, const uint8_t *msg, size_t len, uint8_t sig[KYC_SIG_LEN]);
/* pk_verify(pk, pk_len, msg, len, sig, sig_len) (cpk_verify_pk). */
typedef bool (*kyc_pk_verify_fn)(const uint8_t *pk, size_t pk_len, const uint8_t *msg,
                                 size_t msg_len, const uint8_t *sig, size_t sig_len);

typedef struct {
    uint8_t token_id[KYC_ID_LEN];
    uint32_t expiry_day;
} kyc_spent_t;

typedef struct {
    uint8_t seed[32];
    uint16_t count;
} kyc_rev_entry_t;

typedef struct {
    cp_hdr_t hdr; /* kind CP_KIND_KYC_REVOKE, country = issuer country, key_id = issuer */
    kyc_rev_entry_t e[KYC_MD_REV_MAX];
    uint16_t n;
} kyc_revlist_t;

typedef struct {
    cp_verify_fn issuer_verify; /* trusted issuers (cpk_verify) */
    void *ictx;
    kyc_pk_verify_fn holder_verify;
    bool require_revlist; /* default true */
    kyc_spent_t *spent;
    uint32_t spent_cap, n_spent;
    kyc_revlist_t rev[KYC_MD_MAX_ISSUERS];
    uint32_t n_rev;
} kyc_md_verifier_t;

void kyc_md_verifier_init(kyc_md_verifier_t *m, cp_verify_fn issuer_verify, void *ictx,
                          kyc_pk_verify_fn holder_verify, kyc_spent_t *spent, uint32_t cap);
void kyc_md_backend(kyc_backend_t *b, kyc_md_verifier_t *m);

/* D2. */
void kyc_md_token_id(const uint8_t batch_seed[32], uint32_t index, uint8_t out[KYC_ID_LEN]);

/* D1: the issuer side. */
typedef struct {
    uint8_t issuer_id[KYC_ID_LEN]; /* cpk_key_id of the issuer's public key */
    char issuer_country[2];
    kyc_sign_fn sign;
    void *sctx;
} kyc_md_issuer_t;

kyc_status_t kyc_md_issue(const kyc_md_issuer_t *is, const uint8_t batch_seed[32], uint32_t index,
                          const kyc_pred_t *pred, uint32_t expiry_day,
                          const uint8_t holder_pk[KYC_PK_LEN], uint8_t out[KYC_MD_TOKEN_LEN]);
/* n tokens, index 0..n-1, holder_pks n x KYC_PK_LEN, out n x KYC_MD_TOKEN_LEN. */
kyc_status_t kyc_md_issue_batch(const kyc_md_issuer_t *is, const uint8_t batch_seed[32],
                                const kyc_pred_t *pred, uint32_t expiry_day,
                                const uint8_t *holder_pks, uint32_t n, uint8_t *out);

/* D6: the issuer encodes and signs a list; verifiers install it. */
size_t kyc_md_revlist_encode(const kyc_revlist_t *r, uint8_t *out, size_t cap);
kyc_status_t kyc_md_revlist_decode(const uint8_t *m, size_t len, kyc_revlist_t *out);
kyc_status_t kyc_md_install_revlist(kyc_md_verifier_t *m, const uint8_t *list, size_t len,
                                    const uint8_t *sig, size_t sig_len, uint64_t now);

/* D4: the holder side. sign signs with this token's holder secret key. */
kyc_status_t kyc_md_present(const uint8_t token[KYC_MD_TOKEN_LEN], const kyc_request_t *req,
                            kyc_sign_fn sign, void *sctx, uint8_t out[KYC_MD_PRESENT_LEN]);

/* D5: a holder wallet over caller storage: each token is handed out once. */
typedef struct {
    const uint8_t *token; /* KYC_MD_TOKEN_LEN bytes */
    kyc_sign_fn sign;     /* signs with this token's holder key */
    void *sctx;
    bool used;
} kyc_md_slot_t;

/* Present with an unused, unexpired token whose predicate matches exactly;
 * marks it used before returning. KYC_E_FULL when none is left. */
kyc_status_t kyc_md_wallet_present(kyc_md_slot_t *slots, uint32_t n, const kyc_request_t *req,
                                   uint64_t now, uint8_t out[KYC_MD_PRESENT_LEN]);

const char *kyc_status_name(kyc_status_t s);

#endif /* ZXV_KYCWRAP_H */
