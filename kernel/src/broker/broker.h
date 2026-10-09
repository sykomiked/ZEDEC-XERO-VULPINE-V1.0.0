/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* broker.h — Mister Shanghai's Information Brokerage House.
 *
 * A P2P marketplace where any information product — a program file, a
 * glyph/sigil card, a document, a dataset — is LISTED by its content CID with a
 * seller-signed listing + a capability contract, sold peer-to-peer, and delivered FRAUD-PROOF: a
 * buyer can never be handed bytes other than the advertised CID, because the CID
 * is the SHA-256 of the bytes and delivery re-hashes what came back (that gate
 * lives in src/ipfs — reused verbatim, not re-invented here).
 *
 * "Mister Shanghai deals in information the way a fence deals in goods — no
 *  questions, but every item is what it claims (the CID doesn't lie). You decide
 *  whose flag you trust."  Trust is not the House's to grant: a listing's author
 *  key is a REPUTATION ROOT the buyer chooses to trust, and the seller's
 *  signature over the CID is what binds identity to inventory.
 *
 * WHAT IS HONEST HERE (no hollow capabilities):
 *   - Signature verification is an ops boundary (a supplied verify hook — bind
 *     the kernel's ed25519_verify). No hook bound => a listing fails closed. We
 *     verify the SIGNATURE over the CID; we never certify the real-world facts
 *     behind an author.
 *   - Delivery inherits ipfs's transport ops boundary: no adapter => NO_TRANSPORT,
 *     bytes are never fabricated, and mismatched bytes are rejected.
 *   - SETTLEMENT is an ops boundary (a supplied settlement hook that in a real
 *     deployment routes to src/vino_stores). No backend confirms => NO fill is
 *     recorded. A sale is never simulated.
 *   - The capability handshake authorises a fetch with a CONSTANT-TIME keyed
 *     transcript MAC (HMAC-SHA256 over HELLO||CHALLENGE), reusing src/tls HMAC.
 *     Freshness (a verifiable monotonic beacon + a persisted seen-nonce set +
 *     phase-window TTL) is an ops boundary — see self_issues.
 *
 * Freestanding: integer only, fixed-size arrays, no libc, no allocation, no
 * floating point. tribute_split is app-layer whole-unit integer economics, NOT a
 * security primitive.
 */
#ifndef ZXV_BROKER_H
#define ZXV_BROKER_H

#include <stdint.h>
#include <stdbool.h>
#include "ipfs.h"     /* ipfs_node_t + ipfs_get_verify — the fraud-proof delivery */
#include "hkdf.h"     /* hmac_sha256, hkdf_extract/expand, ct_equal — reused      */

#define BROKER_CID_LEN       32u
#define BROKER_KEY_LEN       32u
#define BROKER_SIG_LEN       64u
#define BROKER_MAX_LISTINGS  64u
#define BROKER_MAX_TRUSTED   16u

#define AIPIC_SCOPE_LEN      64u
#define AIPI_NONCE_LEN       24u
#define AIPI_TAG_LEN         32u
/* transcript = product_cid(32) || nonce(24) || phase(4, little-endian) */
#define AIPI_TRANSCRIPT_MAX  (BROKER_CID_LEN + AIPI_NONCE_LEN + 4u)

/* Verdicts. BROKER_OK == 0; every int32_t API returns an index/0 on success or
 * the NEGATED verdict on failure (so -BROKER_ERR_* is always < 0). */
typedef enum {
    BROKER_OK              = 0,
    BROKER_ERR_NULL        = 1,   /* NULL argument                              */
    BROKER_ERR_BAD_CID     = 2,   /* CID not exactly 32 bytes (MD5/SHA-1 refused)*/
    BROKER_ERR_FULL        = 3,   /* catalog full                               */
    BROKER_ERR_NOT_FOUND   = 4,   /* no such listing                            */
    BROKER_ERR_UNTRUSTED   = 5,   /* author is not on the buyer's allow-list    */
    BROKER_ERR_BAD_SIG     = 6,   /* seller signature over the CID did not verify*/
    BROKER_ERR_NO_VERIFY   = 7,   /* no verify hook bound — fail closed         */
    BROKER_ERR_SCOPE       = 8,   /* the contract does not authorise this call  */
    BROKER_ERR_HS_STATE    = 9,   /* handshake steps out of order               */
    BROKER_ERR_HS_MAC      = 10,  /* keyed transcript MAC failed (forged proof) */
    BROKER_ERR_NO_TRANSPORT= 11,  /* ipfs transport absent — fail closed        */
    BROKER_ERR_CID_MISMATCH= 12,  /* delivered bytes != advertised CID          */
    BROKER_ERR_NOT_FOUND_NET=13,  /* transport could not retrieve the content   */
    BROKER_ERR_NO_SETTLEMENT=14   /* settlement backend absent/declined; NO fill*/
} broker_result_t;

/* ===== The capability contract (aipic) =====
 * A per-listing term sheet. `function` is a capability-function id; `scope` is a
 * GLOB-PREFIX authorisation string — "*" (anything), "prefix:*" (anything under
 * that colon-prefix), or an exact match. NO regex, on purpose: a matcher you can
 * read in one glance cannot hide an authorisation bug.
 *
 * HONEST SCOPE OF THE SELLER SIGNATURE: the seller's Ed25519 signature at listing
 * time is over the 32-byte CONTENT CID (the identity/reputation root of what is
 * being sold) — NOT over these terms. The terms are the seller's posted policy,
 * stored with the listing and enforced by broker_deliver's capability gate; they
 * are not themselves cryptographically bound to the seller signature. Binding
 * terms into the signature (sign over cid||terms) is a forward extension. */
typedef struct {
    uint8_t  function;                 /* capability function id                 */
    char     scope[AIPIC_SCOPE_LEN];   /* "*", "prefix:*", or an exact scope     */
    uint32_t max_price;                /* informational app-layer term (units)   */
    bool     in_use;
} aipic_contract_t;

/* True iff this contract authorises `function` on `requested_scope` under the
 * glob-prefix rules above. */
bool aipic_matches(const aipic_contract_t *c, uint8_t function,
                   const char *requested_scope);

/* ===== A listing ===== */
typedef struct {
    bool             in_use;
    uint8_t          cid[BROKER_CID_LEN];
    uint8_t          author[BROKER_KEY_LEN];   /* seller identity / reputation root */
    uint8_t          sig[BROKER_SIG_LEN];      /* seller sig over the CID           */
    aipic_contract_t terms;
} broker_listing_t;

/* Signature verify hook — the reputation-root ops boundary. Bind the kernel's
 * ed25519_verify. Same shape the update system trusts. */
typedef bool (*broker_verify_fn)(const uint8_t *msg, uint32_t len,
                                 const uint8_t sig[64], const uint8_t pubkey[32]);

/* Settlement ops boundary. In a real deployment `settle` routes to
 * src/vino_stores (vino_ledger_act). Returns 0 IFF a settlement backend
 * CONFIRMS the transfer; anything else means NO fill. With no hook bound, a
 * sale cannot complete — never simulated. */
typedef struct {
    int (*settle)(const uint8_t buyer[BROKER_KEY_LEN],
                  const uint8_t seller[BROKER_KEY_LEN],
                  uint64_t amount, void *ctx);
    void *ctx;
} broker_settlement_t;

typedef struct {
    broker_listing_t   listing[BROKER_MAX_LISTINGS];
    uint32_t           n;
    uint8_t            trusted[BROKER_MAX_TRUSTED][BROKER_KEY_LEN];
    uint32_t           n_trusted;
    broker_verify_fn   verify;
    broker_settlement_t settlement;
} broker_t;

void broker_init(broker_t *b);

/* The buyer decides whose flag to trust. An author not on this allow-list is
 * refused however valid its signature. */
bool broker_trust_author(broker_t *b, const uint8_t pubkey[BROKER_KEY_LEN]);
bool broker_is_trusted(const broker_t *b, const uint8_t pubkey[BROKER_KEY_LEN]);

void broker_set_verifier(broker_t *b, broker_verify_fn fn);
void broker_set_settlement(broker_t *b, const broker_settlement_t *s);

/* List a product by its 32-byte content CID under a signed capability contract.
 * Fails closed: author must be trusted AND the seller's signature over the CID
 * must verify under a bound hook. Returns the listing index (>=0), or a negated
 * broker_result_t (<0). */
int32_t broker_list(broker_t *b, const uint8_t product_cid[BROKER_CID_LEN],
                    const uint8_t author[BROKER_KEY_LEN],
                    const uint8_t sig[BROKER_SIG_LEN],
                    const aipic_contract_t *terms);

/* Length-aware lister — the workhorse broker_list defers to with cid_len=32.
 * A 16-byte (MD5) or 20-byte (SHA-1) CID, or any length != 32, is HARD-ERRORED
 * with -BROKER_ERR_BAD_CID: the House does not stock weak-hash inventory. */
int32_t broker_list_bytes(broker_t *b, const uint8_t *cid, uint32_t cid_len,
                          const uint8_t author[BROKER_KEY_LEN],
                          const uint8_t sig[BROKER_SIG_LEN],
                          const aipic_contract_t *terms);

const broker_listing_t *broker_get(const broker_t *b, int32_t idx);

/* forward declaration so the capability-gated broker_deliver can name the type;
 * the full aipi_handshake struct is defined in the handshake section below. */
typedef struct aipi_handshake aipi_handshake_t;

/* Deliver a listing over a bound ipfs node — gated by a CAPABILITY PROOF.
 * `hs` must be a completed, ALLOWED handshake (aipi_hs_allow granted the fetch
 * ticket) whose committed product_cid equals this listing's CID, and the
 * listing's contract must authorise `function`/`scope` (aipic_matches). Without
 * a valid capability proof the fetch fails closed (BROKER_ERR_HS_MAC); a proof
 * for a different product or an unauthorised scope fails closed
 * (BROKER_ERR_SCOPE). Only then does it route through ipfs_get_verify, so bytes
 * that do not hash to the advertised CID are rejected (BROKER_ERR_CID_MISMATCH)
 * and an unbound transport fails closed (BROKER_ERR_NO_TRANSPORT). The buyer can
 * never be handed non-advertised bytes, and never without a proven capability. */
broker_result_t broker_deliver(broker_t *b, int32_t listing_idx,
                               const aipi_handshake_t *hs,
                               uint8_t function, const char *scope,
                               ipfs_node_t *node,
                               uint8_t *buf, uint32_t cap, uint32_t *out_len);

/* Attempt settlement for a listing. NO fill is recorded unless the bound
 * settlement backend CONFIRMS (returns 0). No backend => BROKER_ERR_NO_SETTLEMENT. */
broker_result_t broker_settle(broker_t *b, int32_t listing_idx,
                              const uint8_t buyer[BROKER_KEY_LEN], uint64_t amount);

/* ===== HKDF key derivation (reuses src/tls HKDF — never reimplemented) =====
 * out_key = HKDF-Expand(HKDF-Extract(salt, ikm), info, 32). Returns 0, or a
 * negated broker_result_t on a NULL out. */
int32_t aipi_derive_key(const uint8_t *salt, uint32_t salt_len,
                        const uint8_t *ikm, uint32_t ikm_len,
                        const uint8_t *info, uint32_t info_len,
                        uint8_t out_key[AIPI_TAG_LEN]);

/* ===== The hardened capability handshake =====
 * HELLO -> CHALLENGE -> PROVE -> ALLOW. The grant is gated on a CONSTANT-TIME
 * comparison of a keyed HMAC over the whole transcript; a garbage or all-zero
 * proof is rejected. This is the ONLY discipline implemented — no known-broken
 * reference verify is ported. */
typedef enum {
    AIPI_HS_INIT      = 0,   /* keyed, no messages yet   */
    AIPI_HS_HELLO     = 1,   /* product_cid committed    */
    AIPI_HS_CHALLENGE = 2,   /* nonce + beacon phase in  */
    AIPI_HS_PROVE     = 3    /* proof tag presented      */
} aipi_hs_stage_t;

typedef struct aipi_handshake {
    uint8_t         key[AIPI_TAG_LEN];               /* session MAC key           */
    bool            have_key;
    uint8_t         transcript[AIPI_TRANSCRIPT_MAX];
    uint32_t        transcript_len;
    uint32_t        phase;                           /* from a verifiable beacon  */
    aipi_hs_stage_t stage;
    uint8_t         presented_tag[AIPI_TAG_LEN];     /* the proof the prover sent */
    uint8_t         product_cid[BROKER_CID_LEN];     /* the product this proof is for */
    bool            allowed;                         /* fetch ticket issued       */
} aipi_handshake_t;

/* Seed the handshake with the session MAC key (derive it via aipi_derive_key). */
void aipi_hs_init(aipi_handshake_t *hs, const uint8_t session_key[AIPI_TAG_LEN]);

/* HELLO: commit the product_cid to the transcript. */
int32_t aipi_hs_begin(aipi_handshake_t *hs, const uint8_t product_cid[BROKER_CID_LEN]);

/* CHALLENGE: commit a nonce and a beacon `phase` to the transcript. The nonce is
 * SUPPLIED (a verifiable beacon / a bound RNG — an ops boundary); this function
 * does NOT invent randomness. `phase` comes from a verifiable monotonic beacon,
 * NOT an unspecified clock. */
int32_t aipi_hs_challenge(aipi_handshake_t *hs, uint8_t nonce24[AIPI_NONCE_LEN],
                          uint32_t phase);

/* PROVE: tag = HMAC(key, transcript). Written to `tag` and recorded as the
 * presented proof. */
int32_t aipi_hs_prove(aipi_handshake_t *hs, uint8_t tag[AIPI_TAG_LEN]);

/* ALLOW: recompute the keyed transcript MAC and compare it to the presented
 * proof in CONSTANT TIME before issuing the fetch ticket. An all-zero or garbage
 * proof is rejected (guards the classic reference-verify bug). */
broker_result_t aipi_hs_allow(aipi_handshake_t *hs);

/* ===== App-layer tribute economics (NON-security) =====
 * Split `amount` by the five integer `weights` (the standard is 11/11/11/66/1,
 * summing to 100) into out[0..5]. EXACT integer allocation via largest-remainder
 * apportionment: out sums to `amount` with no unit lost or invented. */
void tribute_split(uint64_t amount, const uint8_t weights[5], uint64_t out[5]);

#endif /* ZXV_BROKER_H */
