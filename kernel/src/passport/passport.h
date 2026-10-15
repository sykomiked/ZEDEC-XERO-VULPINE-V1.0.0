/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* passport.h -- log in from any device with no server: a seed becomes the
 * whole identity, personal state travels as an encrypted capsule that any
 * swarm node can hold, and devices merge their state without a coordinator.
 *
 * Plain-language explanation: docs/PASSPORT.md.
 *
 * WHAT IS REUSED
 * --------------
 * The identity is ident's root identity (kernel/src/ident): a pq_matrix
 * dual-signature key, ML-DSA-87 + SLH-DSA-SHAKE-256s at MATRIX, derived
 * deterministically from a 32-byte seed by id_root_from_seed. Every key
 * below comes from the same seed through id_root_derive (SHAKE256 with a
 * label), so any device that has the seed rebuilds exactly the same
 * identity with no lookup. Devices are ident passkeys (ML-DSA-65, device
 * bound) certified by the root (id_dev_cert_t) and listed in the root-signed
 * status list (id_cred_status_t). Human passphrases are stretched with
 * ident's Argon2id (RFC 9106). Capsules are ChaCha20-Poly1305 (tls/aead),
 * named by raw CIDv1 (ipfs_node) and moved by ident's host publish/fetch
 * callbacks, which the host wires to IPFS, AIRC or Vinea.
 *
 * Both signature schemes are stateless, so two devices signing at the same
 * moment can never reuse a one-time leaf; nothing has to be coordinated.
 *
 * THE FOUR KEYS FROM ONE SEED
 * ---------------------------
 *   seed (32 bytes) = a typed seed (pp_seed_from_text) or
 *                     Argon2id(passphrase, salt = SHA3-256(label || handle))
 *   root identity   = id_root_from_seed(seed)          signs device certs
 *   vault key       = id_root_derive(seed, "passport-vault")    capsules
 *   rendezvous key  = id_root_derive(seed, "passport-rendezvous")
 *   topic(epoch)    = SHAKE256(label || rendezvous key || epoch)
 * The topic is keyed by a secret, not by the public key, so a node that
 * knows someone's public key still cannot tell which topic is theirs.
 * Every "label" is length-prefixed as in ident's id_shake: the hash input is
 * one length byte, the label (cut at 64 bytes), then the parts.
 *
 * PERSONAL STATE IS A SET OF RECEIPTS, NOT A TOTAL
 * ------------------------------------------------
 * The state holds five sets: earn receipts, spends, café grants, revocations
 * and devices. Merging two devices' states is set union (revocations keep
 * the lower last sequence), so the order of merges never matters, and the
 * balance is always recomputed from the sets by pp_state_replay. Taking the
 * larger of two totals would lose earnings made separately on two devices;
 * a union keeps both. The rules are the paraconsistent ledger's
 * (kernel/src/paradox/pdx_ledger.h, L1 and L4):
 *   - a spend slot is (signer key, sequence). Two different spends on one
 *     slot are an equivocation: both are HELD (lpres BOTH), the larger
 *     amount is kept back from the balance, and neither settles;
 *   - spends are funded in a fixed order (Lamport time, signer, sequence).
 *     A spend the earnings cannot cover is QUARANTINED (BOTH): this is what
 *     two offline devices spending the same balance turn into on merge;
 *   - a spend outside its authority (a revoked key past its last sequence, a
 *     café grant past its expiry, its cap, or without the spend right) is
 *     REFUSED (FALSE); a spend whose signer is not known yet is UNKNOWN
 *     (NEITHER) and its amount is kept back until it is;
 *   - two receipts with one issuer receipt hash but different contents are
 *     CONTESTED (BOTH) and neither counts.
 * Amounts are integer store-credit units (Vino minor units). Vino is issued
 * by the peers that write receipts, never minted here, and stays
 * non-transferable store credit; this module only counts it.
 *
 * THE CAPSULE
 * -----------
 *   "ZPC1" | version 1 | 0 0 0 | salt (32) | plaintext length (u32 BE)
 *   | ChaCha20-Poly1305(key, nonce 0, AAD = the 44 header bytes) | tag (16)
 *   key = SHAKE256("zxv.passport.capsule.v1" || vault key || salt, 32)
 * A fresh random salt gives every capsule its own key, so the fixed nonce
 * is never reused. Nodes that pin a capsule see only ciphertext. The
 * capsule is authenticated by the AEAD, not signed: only holders of the
 * seed have the vault key, and a signature would take about 1.5 s per sync
 * at MATRIX. Spends that leave the device carry their own signatures.
 *
 * CAFE MODE
 * ---------
 * The master seed is never typed into a machine the user does not own. The
 * café machine makes a fresh session key (ML-DSA-65 + a STANDARD pq_matrix
 * KEM key) and shows the request's fingerprint as a QR code and a six-digit
 * code. The user's phone fetches the request, checks the fingerprint, shows
 * what is asked, and its passkey signs a grant: rights, a spend cap and an
 * expiry of at most PP_CAFE_MAX_MS. The phone then seals a view of the state
 * and a report key to the session's KEM key. The café signs spends with the
 * session key; peers check the chain root -> device certificate -> status
 * list -> grant -> spend. The café reports its spends back sealed under the
 * report key, and the phone merges them, applying the cap and expiry again.
 * pp_cafe_end wipes the session.
 *
 * HONEST LIMITS
 * -------------
 * - A compromised café machine sees everything shown during the session
 *   and holds the session key until the grant expires: it can spend up to
 *   the cap. Peers that see a spend over the cap refuse it; spends split
 *   across peers who do not talk to each other are caught only when the
 *   phone merges (REFUSED), after the fact. Keep caps small.
 * - Zero residue is the host's job: the browser tab or process must wipe
 *   its memory. pp_cafe_end zeroes everything this module holds; it cannot
 *   reach copies the host made (swap, a screenshot, a keylogger).
 * - Revoking a grant early takes effect where the revocation reaches; until
 *   then only the expiry protects.
 * - A passphrase-derived seed is only as strong as the passphrase: the
 *   public key lets anyone test guesses offline, and Argon2id only makes each
 *   guess expensive. PP_PASS_MIN enforces length, not entropy. A typed
 *   random seed (pp_seed_to_text) has 256 bits.
 * - Seed text is base32 with a checksum, not a word list; a mnemonic word
 *   list is not included.
 * - The topic does not hide traffic patterns: whoever watches the network
 *   sees one topic being polled. Topics change every epoch.
 * - QUARANTINED spends were already handed to peers. The merge does not
 *   take them back; it tells both sides the voucher is contested.
 *
 * Freestanding integer C11: no libc, no allocation, no floating point;
 * every array is the caller's. Randomness and the clock come from the
 * caller. Single-threaded (ident keeps static signature scratch).
 */
#ifndef ZXV_PASSPORT_H
#define ZXV_PASSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ident/ident.h"
#include "../lpres/lpres.h"

/* ===== status codes ===== */
typedef enum {
    PP_OK = 0,
    PP_ERR_ARG = -1,
    PP_ERR_SIZE = -2,     /* output buffer too small                   */
    PP_ERR_FORMAT = -3,   /* malformed or non-canonical encoding       */
    PP_ERR_CHECKSUM = -4, /* seed text checksum wrong (a typo)         */
    PP_ERR_WEAK = -5,     /* passphrase shorter than PP_PASS_MIN       */
    PP_ERR_KDF = -6,      /* Argon2id refused (memory too small)       */
    PP_ERR_CRYPTO = -7,
    PP_ERR_AUTH = -8, /* AEAD or signature failure                 */
    PP_ERR_FULL = -9, /* a set is full: nothing was changed        */
    PP_ERR_OVERFLOW = -10,
    PP_ERR_FUNDS = -11,   /* the balance cannot cover a new spend      */
    PP_ERR_EXPIRED = -12, /* grant not valid at this time              */
    PP_ERR_RIGHTS = -13,  /* grant lacks the right                     */
    PP_ERR_CAP = -14,     /* over the grant's spend cap                */
    PP_ERR_REVOKED = -15,
    PP_ERR_MISMATCH = -16, /* fingerprint, account or key does not match */
    PP_ERR_NOTFOUND = -17,
    PP_ERR_STATE = -18 /* call out of order (no grant yet, ended)   */
} pp_status_t;

/* ===== sizes ===== */
#define PP_SEED        32u
#define PP_KEY         32u
#define PP_ID          16u /* key ids: device credential ids, session and grant ids */
#define PP_HASH        32u
#define PP_PASS_MIN    16u
#define PP_PASS_MAX    256u
#define PP_HANDLE_MAX  64u
#define PP_SEED_TEXT   66u /* 11 groups of 5 base32 characters, 10 dashes, NUL */
#define PP_SALT        32u
#define PP_TAG         16u
#define PP_CAPSULE_HDR 44u
#define PP_FETCH_MAX   64u /* capsules read under one topic per fetch */

/* Version-1 passphrase stretch: 64 MiB, 3 passes, 1 lane. Changing these
 * gives a different identity, so a login screen always passes pp_kdf_v1. */
#define PP_KDF_M_KIB 65536u
#define PP_KDF_T     3u
#define PP_KDF_P     1u
void pp_kdf_v1(id_kdf_params_t *kp);

/* ===== seed ===== */

/* 32 bytes + a 2-byte SHA3 checksum as Crockford base32 (no I, L, O, U),
 * grouped "XXXXX-XXXXX-...". Parsing ignores case, dashes and spaces and
 * reads I and L as 1, O as 0. */
pp_status_t pp_seed_to_text(const uint8_t seed[PP_SEED], char out[PP_SEED_TEXT]);
pp_status_t pp_seed_from_text(const char *text, uint32_t len, uint8_t seed[PP_SEED]);

/* Argon2id(passphrase, SHA3-256("zxv.passport.v1/salt" || handle)). The
 * handle is any name the person will remember (1..PP_HANDLE_MAX bytes); it
 * makes the salt unique per person without a server. mem: mem_blocks 1 KiB
 * blocks, at least kp->m_kib. */
pp_status_t pp_seed_from_passphrase(const id_kdf_params_t *kp, const uint8_t *pass,
                                    uint32_t pass_len, const uint8_t *handle, uint32_t handle_len,
                                    uint64_t *mem, uint32_t mem_blocks, uint8_t seed[PP_SEED]);

/* ===== passport ===== */
typedef struct {
    id_root_t root;             /* ident root identity: DID, dual-signature key */
    uint8_t vault_key[PP_KEY];  /* capsule key                                 */
    uint8_t rendezvous[PP_KEY]; /* secret behind the topics                    */
} pp_passport_t;

/* Rebuild the whole identity from a seed. level: PQM_LEVEL_MATRIX in
 * production (HIGH and STANDARD are allowed for tests, as in ident). */
pp_status_t pp_hydrate(pp_passport_t *p, pqm_level_t level, const uint8_t seed[PP_SEED]);
void pp_wipe(pp_passport_t *p);
/* Where this identity's capsules are announced during `epoch` (the host
 * picks the epoch length, a day by default in docs/PASSPORT.md). */
pp_status_t pp_topic(const pp_passport_t *p, uint64_t epoch, uint8_t topic[PP_HASH]);

/* ===== state ===== */
typedef struct {
    uint8_t receipt[PP_HASH]; /* hash of the issuer's signed receipt */
    uint8_t issuer[PP_HASH];  /* issuer DID id                        */
    uint64_t amount;
    uint64_t t_ms;
} pp_earn_t;

typedef struct {
    uint8_t signer[PP_ID]; /* device credential id or café session id     */
    uint64_t seq;          /* the signer's own counter, from 1            */
    uint64_t lamport;      /* 1 + the highest Lamport time the signer saw */
    uint64_t amount;
    uint64_t t_ms;          /* when it was signed, by the signer's clock   */
    uint8_t payee[PP_HASH]; /* payee DID id                                */
    uint8_t grant[PP_ID];   /* café grant id; zero for a device spend      */
} pp_spend_t;

#define PP_RIGHT_VIEW  0x1u /* the café may see the state view             */
#define PP_RIGHT_SPEND 0x2u /* the café may spend up to the cap            */
#define PP_RIGHT_QUERY 0x4u /* the café may run swarm queries as the user  */
#define PP_RIGHTS_ALL  0x7u

typedef struct {
    uint8_t grant[PP_ID];   /* grant id                         */
    uint8_t session[PP_ID]; /* café session key id              */
    uint8_t device[PP_ID];  /* credential id of the approving device */
    uint32_t rights;
    uint64_t cap;
    uint64_t issued_ms, expires_ms;
} pp_grant_rec_t;

typedef struct {
    uint8_t key[PP_ID]; /* a device credential id, session id or grant id */
    uint64_t last_seq;  /* spends after this sequence are refused          */
} pp_revoke_t;

/* Per-entry outcome of a replay. */
typedef enum {
    PP_ST_SETTLED = 1,     /* TRUE: paid from earnings                           */
    PP_ST_HELD = 2,        /* BOTH: equivocation on its slot, amount kept back   */
    PP_ST_QUARANTINED = 3, /* BOTH: earnings could not cover it                  */
    PP_ST_REFUSED = 4,     /* FALSE: outside its authority                       */
    PP_ST_UNKNOWN = 5,     /* NEITHER: signer not known yet, amount kept back    */
    PP_ST_COUNTED = 6,     /* TRUE: an earn receipt that counts                  */
    PP_ST_CONTESTED = 7    /* BOTH: an earn receipt with two different contents  */
} pp_entry_state_t;
lpres_state_t pp_entry_tag(uint8_t st);
const char *pp_entry_state_name(uint8_t st);
const char *pp_status_name(pp_status_t s);

typedef struct {
    uint64_t earned;      /* receipts that count                         */
    uint64_t settled;     /* spends paid                                 */
    uint64_t held;        /* kept back for equivocations                 */
    uint64_t unknown;     /* kept back for spends of unknown signers     */
    uint64_t quarantined; /* spends that could not be covered            */
    uint64_t refused;     /* spends outside their authority              */
    uint64_t available;   /* earned - settled - held - unknown, or 0     */
    uint64_t deficit;     /* how far held + unknown exceed what is left  */
} pp_balance_t;

/* Storage for one state, all the caller's. spend_order and grant_used are
 * replay scratch (one per spend / per grant). */
typedef struct {
    pp_earn_t *earn;
    uint8_t *earn_st;
    uint32_t earn_cap;
    pp_spend_t *spend;
    uint8_t *spend_st;
    uint32_t *spend_order;
    uint32_t spend_cap;
    pp_grant_rec_t *grant;
    uint64_t *grant_used;
    uint32_t grant_cap;
    pp_revoke_t *revoke;
    uint32_t revoke_cap;
    id_cred_ref_t *dev;
    uint32_t dev_cap;
} pp_store_t;

typedef struct {
    uint8_t root_did[PP_HASH];
    pp_store_t s;
    uint32_t n_earn, n_spend, n_grant, n_revoke, n_dev;
    pp_balance_t bal; /* as of the last replay */
} pp_state_t;

pp_status_t pp_state_init(pp_state_t *st, const uint8_t root_did[PP_HASH], const pp_store_t *s);

/* Each add is idempotent (an identical entry is a no-op) and keeps the sets
 * in canonical order. *added (may be NULL) is 1 if the entry was new. */
pp_status_t pp_state_add_earn(pp_state_t *st, const pp_earn_t *e, uint32_t *added);
pp_status_t pp_state_add_spend(pp_state_t *st, const pp_spend_t *sp, uint32_t *added);
pp_status_t pp_state_add_grant(pp_state_t *st, const pp_grant_rec_t *g, uint32_t *added);
pp_status_t pp_state_add_revoke(pp_state_t *st, const pp_revoke_t *r, uint32_t *added);
pp_status_t pp_state_add_device(pp_state_t *st, const id_cred_ref_t *d, uint32_t *added);

/* Recompute every entry's state and the balance from the sets. */
pp_status_t pp_state_replay(pp_state_t *st);

/* A new spend by a device of this account: next sequence for the signer,
 * next Lamport time; refused with PP_ERR_FUNDS unless the replayed balance
 * covers it, and with PP_ERR_REVOKED if the device is revoked. The spend
 * is added and returned in *out for signing. */
pp_status_t pp_state_spend(pp_state_t *st, const uint8_t signer[PP_ID], uint64_t amount,
                           const uint8_t payee[PP_HASH], uint64_t t_ms, pp_spend_t *out);

/* Canonical encoding of the whole state (the capsule plaintext). */
uint32_t pp_state_encoded_size(const pp_state_t *st);
pp_status_t pp_state_encode(const pp_state_t *st, uint8_t *out, uint32_t cap, uint32_t *len);
/* Validate an encoding fully, then union it into st (all or nothing). The
 * encoding must be for the same account. *added counts new entries. */
pp_status_t pp_state_merge_encoded(pp_state_t *st, const uint8_t *in, uint32_t len,
                                   uint32_t *added);
/* SHA3-256 of the canonical encoding: equal sets give equal digests. */
pp_status_t pp_state_digest(const pp_state_t *st, uint8_t *work, uint32_t work_cap,
                            uint8_t out[PP_HASH]);

/* Rebuild and sign ident's status list from the device set: every device
 * with no revocation entry is active. */
pp_status_t pp_status_rebuild(const pp_passport_t *p, const id_host_t *h, const pp_state_t *st,
                              uint64_t epoch_ms, id_cred_status_t *out);

/* ===== capsule ===== */
uint32_t pp_capsule_size(const pp_state_t *st);
pp_status_t pp_capsule_seal(const pp_passport_t *p, const pp_state_t *st,
                            const uint8_t salt[PP_SALT], uint8_t *out, uint32_t cap, uint32_t *len);
/* Open a capsule and merge it into st. work: at least the plaintext length.
 * PP_ERR_AUTH for a capsule under another key or changed in any bit. */
pp_status_t pp_capsule_open(const pp_passport_t *p, const uint8_t *in, uint32_t len, uint8_t *work,
                            uint32_t work_cap, pp_state_t *st, uint32_t *added);
pp_status_t pp_capsule_cid(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid);
/* Seal st with a fresh salt from h->random and announce it under the topic
 * of `epoch` through h->publish. */
pp_status_t pp_capsule_publish(const pp_passport_t *p, const id_host_t *h, const pp_state_t *st,
                               uint64_t epoch, uint8_t *buf, uint32_t cap);
/* Fetch every capsule under the topic of `epoch` (at most PP_FETCH_MAX),
 * merge the ones that open, and skip the rest (*skipped). */
pp_status_t pp_capsule_fetch(const pp_passport_t *p, const id_host_t *h, uint64_t epoch,
                             uint8_t *buf, uint32_t cap, uint8_t *work, uint32_t work_cap,
                             pp_state_t *st, uint32_t *merged, uint32_t *skipped);

/* ===== vouchers: a spend that leaves the device ===== */
#define PP_SPEND_BYTES 96u
pp_status_t pp_spend_encode(const pp_spend_t *sp, uint8_t out[PP_SPEND_BYTES]);
/* ML-DSA-65 over root_did || encoded spend, context "zxv.passport.spend.v1".
 * rnd: 32 fresh bytes (hedged) or NULL. */
pp_status_t pp_voucher_sign(const uint8_t sk[ID_MLDSA65_SK], const uint8_t root_did[PP_HASH],
                            const pp_spend_t *sp, const uint8_t *rnd, uint8_t sig[ID_MLDSA65_SIG]);
bool pp_voucher_verify(const uint8_t pk[ID_MLDSA65_PK], const uint8_t root_did[PP_HASH],
                       const pp_spend_t *sp, const uint8_t sig[ID_MLDSA65_SIG]);
/* What a peer checks before taking a device's voucher: the status list is
 * signed by the root and lists the device, the certificate binds the
 * device's key to the root, the voucher is signed by that key, the device
 * is the signer, and the spend is not a café spend. */
pp_status_t pp_verify_device_spend(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                                   const id_dev_cert_t *cert,
                                   const uint8_t device_pk[ID_MLDSA65_PK], const pp_spend_t *sp,
                                   const uint8_t sig[ID_MLDSA65_SIG]);

/* ===== café mode ===== */
#define PP_CAFE_MAX_MS 43200000u /* a grant lasts at most 12 hours */
#define PP_COMPARE_MOD 1000000u  /* six-digit comparison code      */

typedef struct {
    uint8_t grant[PP_ID];
    uint8_t root_did[PP_HASH];
    uint8_t device[PP_ID]; /* credential id of the approving passkey */
    uint8_t session[PP_ID];
    uint8_t session_pk_hash[PP_HASH]; /* SHA3-256 of the session ML-DSA-65 key */
    uint8_t request_fp[PP_HASH];      /* fingerprint of the request it answers */
    uint32_t rights;
    uint64_t cap;
    uint64_t issued_ms, expires_ms;
    uint64_t lamport;            /* the phone's highest Lamport time at issue */
    uint8_t sig[ID_MLDSA65_SIG]; /* by the approving passkey */
} pp_grant_t;

/* What the phone reads out of a request. kem_pk points into the request. */
typedef struct {
    uint32_t rights;
    uint64_t cap;
    uint64_t want_ms;
    uint8_t session[PP_ID];
    uint8_t session_pk_hash[PP_HASH];
    uint8_t fp[PP_HASH];
    const uint8_t *kem_pk;
    uint32_t kem_pk_len;
} pp_request_t;

typedef struct {
    bool live;
    uint8_t id[PP_ID];
    uint8_t sig_pk[ID_MLDSA65_PK];
    uint8_t sig_sk[ID_MLDSA65_SK];
    pqm_kem_sk_t kem_sk;
    uint8_t fp[PP_HASH];
    bool granted;
    pp_grant_t grant;
    uint8_t report_key[PP_KEY];
    uint64_t seq;     /* spends made                   */
    uint64_t spent;   /* sum of their amounts          */
    uint64_t lamport; /* highest Lamport time seen     */
    uint64_t report_seq;
} pp_cafe_t;

/* Request wire size; PP_REQUEST_MAX is enough for any. */
#define PP_REQUEST_MAX 3400u
/* Café: make a session from 32 fresh random bytes and write the request.
 * rights / cap / want_ms are what it asks for; the phone may give less. */
pp_status_t pp_cafe_begin(pp_cafe_t *c, const uint8_t rnd[PP_SEED], uint32_t rights, uint64_t cap,
                          uint64_t want_ms, uint8_t *req, uint32_t cap_bytes, uint32_t *req_len);
/* The six digits both screens show. */
uint32_t pp_compare_code(const uint8_t fp[PP_HASH]);

/* Phone: parse a request and check it against the fingerprint scanned from
 * the café screen (PP_ERR_MISMATCH if it differs). */
pp_status_t pp_request_parse(const uint8_t *req, uint32_t len, const uint8_t qr_fp[PP_HASH],
                             pp_request_t *out);
/* Phone: approve. rights must be a subset of what was asked, cap at most
 * what was asked, and the grant lasts min(want_ms, PP_CAFE_MAX_MS). The
 * passkey signs it, and the grant is recorded in st. */
pp_status_t pp_grant_issue(const pp_passport_t *p, const id_passkey_t *device,
                           const pp_request_t *req, uint32_t rights, uint64_t cap, uint64_t now_ms,
                           const uint8_t *rnd, pp_state_t *st, pp_grant_t *out);
bool pp_grant_verify(const pp_grant_t *g, const uint8_t device_pk[ID_MLDSA65_PK]);
/* The key the café uses to report its spends; the phone recomputes it. */
void pp_report_key(const pp_passport_t *p, const uint8_t grant[PP_ID], uint8_t out[PP_KEY]);

/* Phone: seal the grant's report key and a view of the state (or nothing
 * when the grant has no VIEW right: view_len must then be 0) to the
 * session's KEM key. coins: 32 fresh bytes. */
#define PP_ENVELOPE_HDR 32u
pp_status_t pp_envelope_seal(const pp_passport_t *p, const pp_grant_t *g, const pp_request_t *req,
                             const uint8_t *view, uint32_t view_len, const uint8_t coins[PP_SEED],
                             uint8_t *out, uint32_t cap, uint32_t *len);
/* Café: take the grant and open the envelope. */
pp_status_t pp_cafe_accept(pp_cafe_t *c, const pp_grant_t *g, const uint8_t *env, uint32_t len,
                           uint8_t *view, uint32_t view_cap, uint32_t *view_len);
/* Café: a spend under the grant, checked against the café's clock, the
 * right and the remaining cap, and signed by the session key. */
pp_status_t pp_cafe_spend(pp_cafe_t *c, uint64_t amount, const uint8_t payee[PP_HASH],
                          uint64_t now_ms, const uint8_t *rnd, pp_spend_t *out,
                          uint8_t sig[ID_MLDSA65_SIG]);
/* Café: seal spends for the phone. Each report has its own sequence. */
pp_status_t pp_cafe_report(pp_cafe_t *c, const pp_spend_t *spends, uint32_t n, uint8_t *out,
                           uint32_t cap, uint32_t *len);
/* Phone: open a report for grant g and merge its spends into st. work: at
 * least the report's plaintext length. */
#define PP_REPORT_HDR 36u
pp_status_t pp_report_open(const pp_passport_t *p, const pp_grant_t *g, const uint8_t *in,
                           uint32_t len, uint8_t *work, uint32_t work_cap, pp_state_t *st,
                           uint32_t *added);
/* Café: end the session; every key is wiped. */
void pp_cafe_end(pp_cafe_t *c);

/* What a peer checks before taking a café voucher: the device chain as in
 * pp_verify_device_spend, the grant signed by that device, the session key
 * matches the grant, the grant gives SPEND and is valid at now_ms, the
 * amount is within the cap and the voucher is signed by the session. */
pp_status_t pp_verify_cafe_spend(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                                 const id_dev_cert_t *cert, const uint8_t device_pk[ID_MLDSA65_PK],
                                 const pp_grant_t *g, const uint8_t session_pk[ID_MLDSA65_PK],
                                 const pp_spend_t *sp, const uint8_t sig[ID_MLDSA65_SIG],
                                 uint64_t now_ms);

#endif /* ZXV_PASSPORT_H */
