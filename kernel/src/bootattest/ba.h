/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ba.h — boot-state attestation (roadmap R1.4), with no cloud in the loop.
 *
 * WHAT IT IS
 * ----------
 * The bare-metal image measures each boot stage as it runs: the SHA-256
 * of the stage's bytes is written to an EVENT LOG (stage name + digest) and
 * extended into a running CHAIN value, the way a TPM extends a PCR. At the
 * end the device signs a QUOTE (event log + chain + the verifier's nonce)
 * with its node key, ML-DSA-87. A peer that wants to know what the device
 * booted checks the quote against a GOLDEN MANIFEST: the list of expected
 * stage digests, signed by the release key and published as a
 * content-addressed record (for example in the Carracho DHT or on IPFS).
 *
 * RULES
 *   B1 Chain.  chain_0 = 32 zero bytes;
 *              chain_{i+1} = SHA-256(chain_i || u8 name_len || name || digest_i).
 *      The stage name is inside the extend, so the same digest under a
 *      different name, or the same stages in a different order, gives a
 *      different chain.
 *   B2 Stage names are 1..BA_NAME_MAX bytes of [a-z0-9._-]; at most
 *      BA_MAX_EVENTS events per boot. A log that is full refuses further
 *      extends (BA_E_FULL) and the device should treat that as a failed boot
 *      measurement rather than silently stop measuring.
 *   B3 Quote. Signed with ML-DSA-87, FIPS 204 context string
 *      "ZXV-BA1-QUOTE". It carries the nonce, the id of the golden manifest
 *      the device claims to run, the SHA3-256 hash of the device public key,
 *      the final chain and the full event log. Signing goes through a
 *      callback (ba_sign_fn) so a hardware-sealed key can be plugged in
 *      later; ba_soft_signer is the software fallback.
 *   B4 Manifest. Signed with ML-DSA-87 by the release key, context
 *      "ZXV-BA1-MANIFEST". Its id is a CIDv1 (codec raw 0x55, multihash
 *      sha2-256) over the complete record bytes (body + signature), so an
 *      IPFS node storing the same bytes as a raw block gives the same CID.
 *      Sign manifests deterministically (rnd = NULL) so re-signing the same
 *      body reproduces the same bytes and therefore the same id.
 *   B5 Verification (ba_verify_quote) runs these checks in this order and
 *      stops at the first failure, each with its own error:
 *        quote format          BA_E_FORMAT
 *        manifest format       BA_E_MANIFEST_FORMAT
 *        manifest signer       BA_E_MANIFEST_KEY   (release key hash differs)
 *        manifest signature    BA_E_MANIFEST_SIG
 *        image version         BA_E_ROLLBACK       (below verifier minimum)
 *        manifest id           BA_E_MANIFEST_ID    (quote names another one)
 *        device key            BA_E_WRONG_KEY      (quote's key hash differs)
 *        quote signature       BA_E_SIG            (tampered quote or log)
 *        nonce reuse           BA_E_NONCE_REPLAY   (already accepted once)
 *        nonce unknown         BA_E_NONCE_UNKNOWN  (never issued here)
 *        log replay            BA_E_CHAIN          (log does not give chain)
 *        each event, in order: BA_E_STAGE_UNKNOWN  (name not in manifest)
 *                              BA_E_STAGE_ORDER    (known, wrong position)
 *                              BA_E_STAGE_DIGEST   (right place, wrong hash)
 *        log too short         BA_E_STAGE_MISSING
 *      A quote that passes consumes its nonce, so the same quote presented
 *      again fails with BA_E_NONCE_REPLAY.
 *   B6 Integer-only, no allocation, no libc: every buffer is the caller's.
 *
 * WIRE FORMATS (all integers little-endian)
 *   event     u8 name_len | name | digest[32]
 *   quote     "ZBQ1" | u8 version=1 | u8 n_events | u16 reserved=0 |
 *             nonce[32] | manifest_id[36] | device_key_hash[32] |
 *             chain[32] | events... | ML-DSA-87 signature[4627]
 *   manifest  "ZBM1" | u8 version=1 | u8 n_stages | u16 reserved=0 |
 *             u32 image_version | u32 reserved=0 | u64 issued_at |
 *             release_key_hash[32] | stages (as events)... |
 *             ML-DSA-87 signature[4627]
 *   manifest id (binary, 36 bytes)  01 55 12 20 || SHA-256(record)
 *   manifest id (text)              "b" + RFC 4648 base32, lower case,
 *                                   no padding (the IPFS "bafkrei..." form)
 *   key hash  SHA3-256(ML-DSA-87 public key)
 *
 * HONEST LIMITS
 *   - This is the format and the verifier. The measurement hook in the
 *     arm64/x86_64 UEFI boot path is NOT wired in yet (see
 *     docs/retail/ROOT_OF_TRUST.md, "next step"); the tests drive it from
 *     the host. Not tested on real hardware.
 *   - Without a hardware root (TPM, secure element, boot ROM that measures
 *     the first stage), a compromised first stage can lie about everything
 *     after it. Attestation is only as strong as the earliest stage that
 *     measures and the place the signing key lives. ba_soft_signer keeps
 *     the key in ordinary memory and is flagged as not hardware-backed.
 *   - The verifier's nonce memory is a fixed ring (BA_NONCE_SLOTS); a
 *     nonce pushed out of it is no longer recognised and fails as unknown.
 *   - issued_at in the manifest is informational; the device has no
 *     trusted clock and nothing here reads one.
 */
#ifndef ZXV_BOOTATTEST_BA_H
#define ZXV_BOOTATTEST_BA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BA_DIGEST_LEN   32u
#define BA_NAME_MAX     31u
#define BA_MAX_EVENTS   16u
#define BA_NONCE_LEN    32u
#define BA_KEYHASH_LEN  32u
#define BA_ID_LEN       36u                 /* CIDv1 raw sha2-256, binary */
#define BA_ID_TEXT_LEN  59u                 /* "b" + 58 base32 chars      */
#define BA_PK_LEN       2592u               /* ML-DSA-87 public key       */
#define BA_SK_LEN       4896u               /* ML-DSA-87 secret key       */
#define BA_SIG_LEN      4627u               /* ML-DSA-87 signature        */
#define BA_NONCE_SLOTS  16u

#define BA_QUOTE_HDR    (8u + BA_NONCE_LEN + BA_ID_LEN + BA_KEYHASH_LEN + BA_DIGEST_LEN)
#define BA_EVENT_MAX    (1u + BA_NAME_MAX + BA_DIGEST_LEN)
#define BA_QUOTE_MAX    (BA_QUOTE_HDR + BA_MAX_EVENTS * BA_EVENT_MAX + BA_SIG_LEN)
#define BA_MANIFEST_HDR (8u + 4u + 4u + 8u + BA_KEYHASH_LEN)
#define BA_MANIFEST_MAX (BA_MANIFEST_HDR + BA_MAX_EVENTS * BA_EVENT_MAX + BA_SIG_LEN)

typedef enum {
    BA_OK = 0,
    BA_E_ARG,
    BA_E_NAME,
    BA_E_FULL,
    BA_E_SPACE,
    BA_E_SIGNER,
    BA_E_FORMAT,
    BA_E_MANIFEST_FORMAT,
    BA_E_MANIFEST_KEY,
    BA_E_MANIFEST_SIG,
    BA_E_ROLLBACK,
    BA_E_MANIFEST_ID,
    BA_E_WRONG_KEY,
    BA_E_SIG,
    BA_E_NONCE_REPLAY,
    BA_E_NONCE_UNKNOWN,
    BA_E_CHAIN,
    BA_E_STAGE_UNKNOWN,
    BA_E_STAGE_ORDER,
    BA_E_STAGE_DIGEST,
    BA_E_STAGE_MISSING,
    BA_E_COUNT_
} ba_err_t;

/* Short stable name for an error ("ok", "chain", ...). Never NULL. */
const char *ba_strerror(ba_err_t e);

/* ---- signing and hashing helpers ------------------------------------- */

/* Signer callback: sign msg under the FIPS 204 context string ctx with the
 * device (or release) key and write a BA_SIG_LEN signature. Return true on
 * success. A hardware key store implements this; ba_soft_signer is the
 * software fallback. */
typedef bool (*ba_sign_fn)(void *user, const uint8_t *msg, size_t msg_len,
                           const uint8_t *ctx, size_t ctx_len, uint8_t sig[BA_SIG_LEN]);

typedef struct {
    const uint8_t *sk;          /* BA_SK_LEN bytes, held in ordinary memory */
    const uint8_t *rnd;         /* 32 fresh bytes (hedged) or NULL (deterministic) */
    bool hardware_backed;       /* always false for this signer */
} ba_soft_key_t;

/* user must point at a ba_soft_key_t. */
bool ba_soft_signer(void *user, const uint8_t *msg, size_t msg_len,
                    const uint8_t *ctx, size_t ctx_len, uint8_t sig[BA_SIG_LEN]);

/* Software key generation from a 32-byte seed (FIPS 204 KeyGen_internal). */
void ba_soft_keygen(const uint8_t seed[32], uint8_t pk[BA_PK_LEN], uint8_t sk[BA_SK_LEN]);

/* SHA3-256 of a public key. */
void ba_key_hash(const uint8_t pk[BA_PK_LEN], uint8_t out[BA_KEYHASH_LEN]);

/* CIDv1 (raw, sha2-256) of a record, binary form. */
void ba_cid(const uint8_t *rec, size_t len, uint8_t id[BA_ID_LEN]);

/* Lower-case RFC 4648 base32 without padding. Writes ceil(len*8/5) chars and
 * a NUL; returns the char count, or 0 if cap is too small. */
size_t ba_base32(const uint8_t *in, size_t len, char *out, size_t cap);

/* Text form of a binary id: "b" + base32. out needs BA_ID_TEXT_LEN + 1. */
void ba_id_text(const uint8_t id[BA_ID_LEN], char out[BA_ID_TEXT_LEN + 1]);

/* ---- measuring (device side) ----------------------------------------- */

typedef struct {
    uint8_t name_len;
    char    name[BA_NAME_MAX + 1];      /* NUL-terminated copy */
    uint8_t digest[BA_DIGEST_LEN];
} ba_event_t;

typedef struct {
    uint8_t    n;
    ba_event_t ev[BA_MAX_EVENTS];
    uint8_t    chain[BA_DIGEST_LEN];
} ba_log_t;

void ba_log_init(ba_log_t *log);
/* Extend with a precomputed digest (rule B1). */
ba_err_t ba_extend(ba_log_t *log, const char *name, const uint8_t digest[BA_DIGEST_LEN]);
/* Hash a stage's bytes with SHA-256, then extend. */
ba_err_t ba_measure(ba_log_t *log, const char *name, const uint8_t *data, size_t len);
/* Recompute the chain from an event list (what a verifier does). */
void ba_replay(const ba_event_t *ev, unsigned n, uint8_t chain[BA_DIGEST_LEN]);

/* Build and sign a quote. Writes *out_len bytes (at most BA_QUOTE_MAX). */
ba_err_t ba_quote(const ba_log_t *log, const uint8_t nonce[BA_NONCE_LEN],
                  const uint8_t manifest_id[BA_ID_LEN],
                  const uint8_t device_key_hash[BA_KEYHASH_LEN],
                  ba_sign_fn sign, void *sign_user,
                  uint8_t *out, size_t cap, size_t *out_len);

/* ---- golden manifest (release side) ---------------------------------- */

typedef struct {
    uint32_t   image_version;
    uint64_t   issued_at;
    uint8_t    release_key_hash[BA_KEYHASH_LEN];
    uint8_t    n;
    ba_event_t stage[BA_MAX_EVENTS];
} ba_manifest_t;

/* Encode and sign. The stage names follow rule B2. */
ba_err_t ba_manifest_build(const ba_manifest_t *m, ba_sign_fn sign, void *sign_user,
                           uint8_t *out, size_t cap, size_t *out_len);
/* Parse only (no signature check): BA_OK or BA_E_MANIFEST_FORMAT. */
ba_err_t ba_manifest_parse(const uint8_t *rec, size_t len, ba_manifest_t *m);
/* Parse, check the signer's hash and the signature. */
ba_err_t ba_manifest_verify(const uint8_t *rec, size_t len, const uint8_t release_pk[BA_PK_LEN],
                            ba_manifest_t *m);

/* ---- verifying (peer side) ------------------------------------------- */

typedef struct {
    uint8_t  issued[BA_NONCE_SLOTS][BA_NONCE_LEN];
    uint8_t  issued_live[BA_NONCE_SLOTS];
    uint8_t  used[BA_NONCE_SLOTS][BA_NONCE_LEN];
    uint8_t  used_live[BA_NONCE_SLOTS];
    unsigned issued_next, used_next;
    uint32_t min_image_version;
} ba_verifier_t;

void ba_verifier_init(ba_verifier_t *v, uint32_t min_image_version);
/* Remember a nonce the verifier just sent (32 fresh random bytes from the
 * caller). The oldest outstanding one is forgotten when the ring is full. */
void ba_verifier_issue(ba_verifier_t *v, const uint8_t nonce[BA_NONCE_LEN]);

typedef struct {
    ba_manifest_t manifest;
    uint8_t       n_events;
    ba_event_t    ev[BA_MAX_EVENTS];
    uint8_t       chain[BA_DIGEST_LEN];
    unsigned      bad_index;   /* event index for the BA_E_STAGE_* errors */
} ba_result_t;

ba_err_t ba_verify_quote(ba_verifier_t *v, const uint8_t *quote, size_t quote_len,
                         const uint8_t device_pk[BA_PK_LEN],
                         const uint8_t *manifest, size_t manifest_len,
                         const uint8_t release_pk[BA_PK_LEN], ba_result_t *res);

#endif /* ZXV_BOOTATTEST_BA_H */
