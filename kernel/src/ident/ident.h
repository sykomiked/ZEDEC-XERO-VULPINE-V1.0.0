/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ident.h -- optional KYC credentials, post-quantum passkeys, and account
 * recovery from a biometric + a recovery name + guardians.
 *
 * Plain-language explanation: docs/IDENTITY_AND_RECOVERY.md.
 *
 * THREE PARTS
 * -----------
 * 1. OPTIONAL KYC (id_kyc.c). Off by default. An operator profile (a
 *    central bank, a bank, any institution) may require a KYC level; an
 *    individual decides whether to get verified at all. A licensed
 *    verifier checks documents in its own flow, outside this module, and
 *    returns a signed attestation shaped like a W3C Verifiable Credential:
 *    "KYC level N, jurisdiction X, valid until T", plus salted hash
 *    commitments to the personal claims (name, date of birth, "age over
 *    18", ...). The platform stores the signed attestation and the
 *    holder's openings, never the documents. A presentation opens only
 *    the claims the holder chooses (selective disclosure) and is signed by
 *    a per-credential holder key. Issuers publish signed revocation
 *    bitmaps. Verifiers plug in as callbacks (id_kyc_adapter_t).
 *    id_kyc_check_payment() is the hook payments and cards call to apply
 *    an operator's tiered limits.
 *
 * 2. PASSKEYS (id_passkey.c). WebAuthn / FIDO2-shaped credentials with
 *    ML-DSA-65 signatures (COSE alg -49, draft-ietf-cose-dilithium), raw
 *    FIPS 204 with an empty context so the signed bytes are exactly
 *    WebAuthn's authenticatorData || SHA-256(clientDataJSON). Each passkey
 *    is bound to one device of the user's devmesh (kernel/src/devmesh) by
 *    that device's 16-byte id; ident keeps no device roster of its own.
 *    The device's own biometric unlock (Face ID / Touch ID, Android
 *    BiometricPrompt) only sets the UV flag; no biometric leaves the
 *    device's secure hardware for a login. The user's ROOT identity key
 *    is a pq_matrix dual-signature key (MATRIX by default: ML-DSA-87 +
 *    SLH-DSA-SHAKE-256s). It certifies each passkey (id_dev_cert_t) and
 *    signs the list of passkeys that are currently valid
 *    (id_cred_status_t). A relying party that knows the root key accepts
 *    any passkey certified by it and listed as active, so after a
 *    recovery every account works again from the new device. Root-
 *    certified passkeys use the platform rp id ID_PLATFORM_RP ("zxv.id");
 *    each ZXV relying party binds an assertion to itself through its own
 *    challenge and origin in clientDataJSON.
 *
 * 3. RECOVERY (id_fuzzy.c, id_shamir.c, id_argon2.c, id_recover.c). The
 *    user picks a short recovery name and k-of-n guardians (3 of 5 by
 *    default: people or devices). At enrolment the host supplies a binary
 *    biometric feature vector (this module never sees an image). A
 *    code-offset fuzzy extractor over BCH(255, k, t) turns it into a key;
 *    only the helper data is kept, encrypted under a key derived from the
 *    recovery name by Argon2id. The root seed is encrypted under
 *    SHAKE256(name key || biometric key || G), where G is a random secret
 *    split k-of-n to the guardians with Shamir sharing over GF(2^8). The
 *    sealed vault is pinned to IPFS (raw CIDv1) and announced under a
 *    lookup key derived only from the recovery name. Recovery: name ->
 *    vault; k guardian approvals -> G; biometric re-scan -> biometric key;
 *    all three -> root seed -> new passkey on the new device -> a new
 *    status list that omits every old passkey (old devices revoked, and
 *    the host's revoke_device callback is told each old devmesh id).
 *
 * WHY THERE IS NO BIOMETRIC INDEX
 * -------------------------------
 * Nothing stored anywhere is a function of the biometric alone. The
 * lookup key is a function of the recovery name only. The helper data is
 * encrypted under the name key. The only check that tells whether a
 * biometric is right is the final AEAD, whose key also needs G, so even
 * someone holding the vault and the name cannot test biometrics offline
 * until k guardians have released their shares. No one can search the
 * network for "the account of this face".
 *
 * KDF CHOICE
 * ----------
 * Argon2id (RFC 9106), implemented here bounded: the caller supplies the
 * memory, so there is no allocation, and the parameters are capped. It is
 * the standardised memory-hard password hash with published test vectors
 * (checked by test_ident.c) and a reviewed time-memory trade-off
 * analysis; a home-made scrypt-like construction over SHAKE would have
 * neither. Argon2id needs BLAKE2b, implemented here (RFC 7693).
 *
 * HONEST LIMITS
 * -------------
 * - Fuzzy extractors leak entropy. The helper data reveals the biometric
 *   up to a BCH codeword, which can cost up to n-k bits per 255-bit
 *   block, and real feature vectors have far less entropy than their
 *   length (often tens of bits, not hundreds). The biometric is a factor,
 *   not a secret: faces and fingerprints can be photographed or lifted.
 *   The recovery name and the guardians carry most of the security.
 * - Biometric quality varies with sensor, lighting, age, injury. A
 *   genuine re-scan that differs in more than t bits of any block fails.
 *   The host's sensor and feature pipeline (liveness, alignment, stable
 *   bit selection) is out of scope; garbage in means no recovery.
 * - Recovery names are low entropy. Argon2id makes each guess cost
 *   memory and time but cannot stop a patient attacker from finding the
 *   vault of a guessable name; the vault is useless without k guardians.
 *   The lookup salt is fixed network-wide (it must be computable from the
 *   name alone), so one guess is tested against every vault at once.
 * - Guardians are the main barrier. If k of them are fooled or collude,
 *   the attacker can then try biometrics offline. Guardian devices
 *   enforce a waiting period, rate limits and alerts, but cannot enforce
 *   them against a guardian who runs modified software.
 * - Rate limiting on the recovering device is advisory (an attacker runs
 *   their own code); the enforceable limits are on guardian devices.
 * - BCH decoding and Argon2id's data-dependent passes use table lookups
 *   and memory accesses that depend on secret data: they are not constant
 *   time. Run them on the user's own device.
 * - KYC selective disclosure uses salted hash commitments (as SD-JWT and
 *   ISO mDL do). Presentations of the same credential are linkable by its
 *   signature and subject key; unlinkability needs one credential per
 *   verifier (batch issuance), which the API supports, or zero-knowledge
 *   proofs, which are not implemented. Predicates such as "over 18" exist
 *   only if the issuer put them in as claims.
 * - The passkey format is WebAuthn-shaped, but browsers and FIDO servers
 *   do not accept ML-DSA today; interop with them needs a classical
 *   passkey alongside, which is out of scope here.
 * - devmesh accepts revocations only from an active admin device. After
 *   every device is lost, the new device starts a new mesh; surviving old
 *   devices learn of the revocation from the root-signed status list via
 *   the host (revoke_device), not from devmesh itself.
 * - No randomness source and no clock: host.random() must be a CSPRNG,
 *   and every call that needs the time takes it. Single-threaded: the
 *   module keeps large signature scratch buffers in static storage.
 *
 * Freestanding integer C11: no libc, no float, no allocation, no 64-bit
 * division (zt_udiv64), fixed-size buffers.
 */
#ifndef ZXV_IDENT_H
#define ZXV_IDENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../pqsec/pq_matrix.h"
#include "../ipfs_node/ipfs_node.h"

/* ===== status codes ===== */
typedef enum {
    ID_OK = 0,
    ID_ERR_ARG = -1,
    ID_ERR_SIZE = -2,
    ID_ERR_FORMAT = -3,
    ID_ERR_AUTH = -4, /* signature or AEAD failure */
    ID_ERR_REVOKED = -5,
    ID_ERR_EXPIRED = -6,
    ID_ERR_UNTRUSTED = -7, /* issuer not trusted by the profile */
    ID_ERR_LEVEL = -8,     /* KYC level too low */
    ID_ERR_LIMIT = -9,     /* tier limit exceeded */
    ID_ERR_CRYPTO = -10,
    ID_ERR_NOMATCH = -11, /* biometric, name or guardian shares did not open the vault */
    ID_ERR_THRESHOLD = -12,
    ID_ERR_RATE = -13, /* rate limit */
    ID_ERR_STATE = -14,
    ID_ERR_FULL = -15,
    ID_ERR_NOTFOUND = -16,
    ID_ERR_CANCELLED = -17,
    ID_ERR_WAIT = -18,   /* guardian waiting period not over */
    ID_ERR_LOCKED = -19, /* too many failed attempts */
    ID_ERR_NAME = -20,   /* recovery name too short, too long or taken */
    ID_ERR_JURISDICTION = -21,
    ID_ERR_DISABLED = -22, /* the holder has not opted in to KYC */
    ID_ERR_REPLAY = -23,   /* passkey signature counter did not increase */
} id_status_t;

/* ===== sizes ===== */
#define ID_HASH      32u
#define ID_DEV_ID    16u /* = DM_ID_BYTES (devmesh device id) */
#define ID_CRED_ID   16u
#define ID_SALT      16u
#define ID_DID_CHARS 61u /* "did:zxv:" + 52 base32 + NUL */
#define ID_NAME_MAX  32u

/* ===== host interface ===== */
typedef enum {
    ID_ALERT_RECOVERY_REQUESTED = 1, /* guardian: someone asks to recover acct */
    ID_ALERT_RECOVERY_CANCELLED = 2, /* guardian: the owner's device cancelled */
    ID_ALERT_RECOVERY_RATE = 3,      /* guardian: requests over the rate limit */
    ID_ALERT_CANCEL_ABUSE = 4,       /* guardian: too many cancels (stolen device?) */
    ID_ALERT_SHARE_RELEASED = 5,     /* guardian: share released for a request */
    ID_ALERT_RECOVERY_FAILED = 6,    /* requester: a failed attempt */
    ID_ALERT_RECOVERY_LOCKED = 7,    /* requester: attempts exhausted */
    ID_ALERT_DEVICE_REVOKED = 8,     /* a passkey's device was revoked */
} id_alert_kind_t;

typedef struct {
    uint8_t kind;              /* id_alert_kind_t */
    uint8_t account[ID_HASH];  /* root DID hash of the account, if known */
    uint8_t vault_id[ID_HASH]; /* recovery vault, if relevant */
    uint8_t ref[ID_CRED_ID];   /* request id or device id */
    uint64_t when_ms;          /* event time, or when a share may be released */
    uint32_t count;            /* attempts / requests so far */
} id_alert_t;

typedef struct {
    void *ctx;
    /* CSPRNG. Required. */
    void (*random)(void *ctx, uint8_t *out, uint32_t n);
    /* Alerts for the person (guardian, owner). Optional. */
    void (*on_alert)(void *ctx, const id_alert_t *a);
    /* devmesh bridge: an old device lost its passkey; the host calls
     * dm_revoke() if this device is an admin of the mesh. Optional. */
    void (*revoke_device)(void *ctx, const uint8_t dev_id[ID_DEV_ID]);
    /* Pin `blob` (whose raw CIDv1 is `cid`) and announce it under
     * `lookup` (DHT / Carracho). Return 0 on success. Optional. */
    int (*publish)(void *ctx, const uint8_t lookup[ID_HASH], const ipfsn_cid_t *cid,
                   const uint8_t *blob, uint32_t len);
    /* Fetch the index-th blob announced under `lookup`. Return 0 and set
     * *len, or nonzero when there is no such blob. Optional. */
    int (*fetch)(void *ctx, const uint8_t lookup[ID_HASH], uint32_t index, uint8_t *buf,
                 uint32_t cap, uint32_t *len);
} id_host_t;

/* ===== identifiers ===== */

/* id = SHA3-256("ZXV-ident/did" || pqm_sig_pk_encode(pk)). */
id_status_t id_did_of(const pqm_sig_pk_t *pk, uint8_t did[ID_HASH]);
/* "did:zxv:" + lowercase RFC 4648 base32 of the 32-byte id. */
void id_did_string(const uint8_t did[ID_HASH], char out[ID_DID_CHARS]);

/* ===== root identity ===== */
typedef struct {
    pqm_level_t level;
    uint8_t seed[32];
    pqm_sig_pk_t pk;
    pqm_sig_sk_t sk;
    uint8_t did[ID_HASH];
} id_root_t;

/* level: PQM_LEVEL_MATRIX for production (default anchor), HIGH or
 * STANDARD allowed (tests use them for speed). */
id_status_t id_root_from_seed(id_root_t *r, pqm_level_t level, const uint8_t seed[32]);
void id_root_wipe(id_root_t *r);
/* Derive a sub-key from the root seed: SHAKE256(label || index || seed). */
void id_root_derive(const uint8_t seed[32], const char *label, uint32_t index, uint8_t out[32]);

/* ======================================================================
 * PASSKEYS
 * ====================================================================== */
#define ID_COSE_MLDSA65   (-49)
#define ID_MLDSA65_PK     1952u
#define ID_MLDSA65_SK     4032u
#define ID_MLDSA65_SIG    3309u
#define ID_AUTH_UP        0x01u /* user present */
#define ID_AUTH_UV        0x04u /* user verified (device biometric / PIN) */
#define ID_AUTH_BE        0x08u /* backup eligible: never set (device-bound) */
#define ID_AUTH_AT        0x40u /* attested credential data included */
#define ID_AUTHDATA_MIN   37u
#define ID_COSE_KEY_LEN   (10u + ID_MLDSA65_PK)
#define ID_AUTHDATA_MAX   (ID_AUTHDATA_MIN + 16u + 2u + ID_CRED_ID + ID_COSE_KEY_LEN)
#define ID_RP_MAX         64u
#define ID_CLIENTDATA_MAX 256u

typedef struct {
    uint8_t cred_id[ID_CRED_ID];
    uint8_t dev_id[ID_DEV_ID]; /* devmesh device this key lives on */
    uint8_t rp_hash[ID_HASH];  /* SHA-256(rp id) */
    uint8_t user[16];          /* user handle at the relying party */
    uint32_t sign_count;
    uint8_t pk[ID_MLDSA65_PK];
    uint8_t sk[ID_MLDSA65_SK];
} id_passkey_t;

/* What a relying party stores for a registered passkey. */
typedef struct {
    uint8_t cred_id[ID_CRED_ID];
    uint8_t rp_hash[ID_HASH];
    uint32_t sign_count;
    uint8_t pk[ID_MLDSA65_PK];
} id_rp_cred_t;

/* SHA-256 of the WebAuthn clientDataJSON
 * {"type":"<type>","challenge":"<b64url>","origin":"<origin>","crossOrigin":false} */
id_status_t id_client_data_hash(const char *type, const uint8_t *challenge, uint32_t chal_len,
                                const char *origin, uint8_t out[ID_HASH]);

/* Authenticator: make a new device-bound passkey for rp_id. */
id_status_t id_passkey_create(id_passkey_t *pk, const id_host_t *h, const char *rp_id,
                              const uint8_t dev_id[ID_DEV_ID], const uint8_t user[16]);
/* Registration: authenticatorData with attested credential data (the COSE
 * key {1:7, 3:-49, -1:pk}); attestation format "none". uv: the device's
 * own biometric/PIN check passed. */
id_status_t id_passkey_register(const id_passkey_t *pk, bool uv, uint8_t *auth, uint32_t cap,
                                uint32_t *len);
/* Assertion: 37-byte authenticatorData and an ML-DSA-65 signature over
 * auth || client_data_hash. Increments sign_count. */
id_status_t id_passkey_assert(id_passkey_t *pk, const id_host_t *h, bool uv,
                              const uint8_t client_data_hash[ID_HASH],
                              uint8_t auth[ID_AUTHDATA_MIN], uint8_t sig[ID_MLDSA65_SIG]);
void id_passkey_wipe(id_passkey_t *pk);

/* Relying party side. */
id_status_t id_rp_register(id_rp_cred_t *out, const char *rp_id, const uint8_t *auth, uint32_t len,
                           bool require_uv);
id_status_t id_rp_verify(id_rp_cred_t *cred, const char *rp_id, const uint8_t *auth, uint32_t len,
                         const uint8_t client_data_hash[ID_HASH], const uint8_t sig[ID_MLDSA65_SIG],
                         bool require_uv);

/* Root certificate for one passkey. */
typedef struct {
    uint8_t root_did[ID_HASH];
    uint8_t cred_id[ID_CRED_ID];
    uint8_t dev_id[ID_DEV_ID];
    uint8_t pk_hash[ID_HASH]; /* SHA3-256(passkey public key) */
    uint64_t issued_ms;
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} id_dev_cert_t;

id_status_t id_dev_cert_issue(const id_root_t *root, const id_host_t *h, const id_passkey_t *pk,
                              uint64_t now_ms, id_dev_cert_t *cert);
id_status_t id_dev_cert_verify(const pqm_sig_pk_t *root_pk, const id_dev_cert_t *cert,
                               const uint8_t passkey_pk[ID_MLDSA65_PK]);

/* The passkeys that are valid now. Every passkey not listed is revoked;
 * a higher epoch replaces a lower one. */
#define ID_MAX_CREDS 16u
typedef struct {
    uint8_t cred_id[ID_CRED_ID];
    uint8_t dev_id[ID_DEV_ID];
} id_cred_ref_t;

typedef struct {
    uint8_t root_did[ID_HASH];
    uint64_t epoch_ms;
    uint8_t n;
    id_cred_ref_t active[ID_MAX_CREDS];
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} id_cred_status_t;

void id_status_init(id_cred_status_t *st, const uint8_t root_did[ID_HASH], uint64_t epoch_ms);
id_status_t id_status_add(id_cred_status_t *st, const uint8_t cred_id[ID_CRED_ID],
                          const uint8_t dev_id[ID_DEV_ID]);
id_status_t id_status_sign(const id_root_t *root, const id_host_t *h, id_cred_status_t *st);
id_status_t id_status_verify(const pqm_sig_pk_t *root_pk, const id_cred_status_t *st);
bool id_status_is_active(const id_cred_status_t *st, const uint8_t cred_id[ID_CRED_ID]);
/* Install `next` over `*cur` if it verifies and its epoch is higher. Each
 * device active in *cur but absent from next is reported to
 * host.revoke_device (and on_alert). */
id_status_t id_status_accept(id_cred_status_t *cur, const id_cred_status_t *next,
                             const pqm_sig_pk_t *root_pk, const id_host_t *h);

/* Relying party login for an account bound to a root identity: the cert
 * chains to the root, the passkey is in the current status list, the
 * assertion verifies. */
id_status_t id_rp_login(const pqm_sig_pk_t *root_pk, const id_cred_status_t *st,
                        const id_dev_cert_t *cert, id_rp_cred_t *cred, const char *rp_id,
                        const uint8_t *auth, uint32_t len, const uint8_t client_data_hash[ID_HASH],
                        const uint8_t sig[ID_MLDSA65_SIG], bool require_uv);

/* ======================================================================
 * OPTIONAL KYC
 * ====================================================================== */
#define ID_KYC_MAX_LEVEL 4u /* 0 none, 1 basic, 2 standard, 3 enhanced, 4 institutional */
#define ID_MAX_CLAIMS    12u
#define ID_CLAIM_TAG     24u
#define ID_CLAIM_VAL     32u
#define ID_MAX_ISSUERS   6u
#define ID_MAX_JUR       16u
#define ID_MAX_TIERS     5u
#define ID_REV_BITS      4096u
#define ID_WALLET_SLOTS  4u
#define ID_AUD_MAX       64u

typedef struct {
    char tag[ID_CLAIM_TAG]; /* e.g. "given_name", "birth_date", "age_over_18" */
    uint8_t len;
    uint8_t value[ID_CLAIM_VAL];
    uint8_t salt[ID_SALT];
} id_claim_t;

/* The signed attestation. Personal data appears only as commitments
 * commit[i] = SHA3-256("ZXV-ident/claim" || salt || tag || len || value). */
typedef struct {
    uint8_t issuer[ID_HASH];  /* issuer DID id */
    uint8_t subject[ID_HASH]; /* DID id of the per-credential holder key */
    uint8_t kyc_level;
    char jurisdiction[3]; /* ISO 3166-1 alpha-2, NUL terminated */
    uint64_t issued_ms;
    uint64_t expires_ms;
    uint32_t status_index; /* bit in the issuer's revocation list */
    uint8_t nclaims;
    uint8_t commit[ID_MAX_CLAIMS][ID_HASH];
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} id_vc_t;

typedef struct {
    char name[ID_NAME_MAX];
    pqm_sig_pk_t pk;
    pqm_sig_sk_t sk;
    uint8_t did[ID_HASH];
} id_issuer_t;

typedef struct {
    uint8_t issuer[ID_HASH];
    uint64_t version;
    uint64_t issued_ms;
    uint8_t bits[ID_REV_BITS / 8u]; /* 1 = revoked */
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} id_revlist_t;

typedef struct {
    uint8_t did[ID_HASH];
    pqm_sig_pk_t pk;
    uint8_t max_level; /* highest level this issuer may attest */
} id_trusted_issuer_t;

typedef enum {
    ID_OP_INDIVIDUAL = 0,
    ID_OP_INSTITUTION = 1,
    ID_OP_BANK = 2,
    ID_OP_CENTRAL_BANK = 3,
} id_op_kind_t;

/* Limits for holders at `level` or above. 0 = unlimited. VFV minor units. */
typedef struct {
    uint8_t level;
    uint64_t max_single;
    uint64_t max_daily;
} id_tier_t;

typedef struct {
    char name[ID_NAME_MAX];
    uint8_t kind;           /* id_op_kind_t */
    uint8_t required_level; /* 0 = KYC off (the default) */
    uint8_t njur;           /* 0 = any jurisdiction */
    char jur[ID_MAX_JUR][3];
    uint8_t nissuers;
    id_trusted_issuer_t issuers[ID_MAX_ISSUERS];
    uint64_t max_revlist_age_ms; /* 0 = any age */
    uint8_t ntiers;
    id_tier_t tiers[ID_MAX_TIERS]; /* ascending level */
} id_kyc_profile_t;

/* What a successful presentation proves. */
typedef struct {
    bool verified;
    uint8_t level;
    char jurisdiction[3];
    uint64_t expires_ms;
    uint8_t issuer[ID_HASH];
    uint8_t ndisc;
    id_claim_t disc[ID_MAX_CLAIMS]; /* opened claims (salts included) */
} id_kyc_status_t;

typedef struct {
    uint8_t used;
    uint32_t key_index; /* holder key = derive(holder_master, "kyc-holder", key_index) */
    id_vc_t vc;
    id_claim_t claims[ID_MAX_CLAIMS];
} id_wallet_slot_t;

typedef struct {
    bool opted_in; /* false by default: the person has not chosen KYC */
    uint8_t holder_master[32];
    uint32_t next_key;
    id_wallet_slot_t slot[ID_WALLET_SLOTS];
} id_kyc_wallet_t;

/* What goes to a verifier adapter: no personal data. */
typedef struct {
    uint8_t subject[ID_HASH]; /* holder key DID id for the new credential */
    uint8_t level;
    char jurisdiction[3];
    uint8_t nonce[32];
} id_kyc_request_t;

/* A licensed verifier. verify() runs the verifier's own process (the
 * person deals with the verifier directly; documents never pass through
 * ident) and returns the signed credential and the claim openings. */
typedef struct {
    char name[ID_NAME_MAX];
    void *ctx;
    int (*verify)(void *ctx, const id_kyc_request_t *req, id_vc_t *vc, id_claim_t *claims,
                  uint8_t *nclaims);
} id_kyc_adapter_t;

typedef struct {
    id_vc_t vc;
    uint16_t disc_mask; /* bit i: claim i opened */
    id_claim_t disc[ID_MAX_CLAIMS];
    uint8_t nonce[32];
    uint8_t aud_hash[ID_HASH]; /* SHA3-256 of the verifier's audience string */
    uint32_t holder_pk_len;
    uint8_t holder_pk[PQM_SIG_PK_MAX_BYTES];
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} id_vp_t;

/* Issuer. */
id_status_t id_issuer_init(id_issuer_t *iss, pqm_level_t level, const uint8_t seed[32],
                           const char *name);
id_status_t id_claim_set(id_claim_t *c, const id_host_t *h, const char *tag, const uint8_t *value,
                         uint32_t len);
id_status_t id_vc_issue(const id_issuer_t *iss, const id_host_t *h, const id_kyc_request_t *req,
                        const id_claim_t *claims, uint8_t nclaims, uint64_t now_ms,
                        uint64_t expires_ms, uint32_t status_index, id_vc_t *vc);
id_status_t id_vc_verify_sig(const pqm_sig_pk_t *issuer_pk, const id_vc_t *vc);
/* W3C VC data-model JSON of the credential (proof value base64url). */
int32_t id_vc_to_json(const id_vc_t *vc, char *out, uint32_t cap);

void id_revlist_init(id_revlist_t *rl, const uint8_t issuer[ID_HASH], uint64_t version,
                     uint64_t now_ms);
id_status_t id_revlist_revoke(id_revlist_t *rl, uint32_t index);
id_status_t id_revlist_sign(const id_issuer_t *iss, const id_host_t *h, id_revlist_t *rl);
id_status_t id_revlist_verify(const pqm_sig_pk_t *issuer_pk, const id_revlist_t *rl);

/* Operator profile. Default: KYC off, no limits. */
void id_kyc_profile_default(id_kyc_profile_t *p, const char *name, id_op_kind_t kind);
id_status_t id_kyc_profile_require(id_kyc_profile_t *p, uint8_t level);
id_status_t id_kyc_profile_trust(id_kyc_profile_t *p, const pqm_sig_pk_t *issuer_pk,
                                 uint8_t max_level);
id_status_t id_kyc_profile_jurisdiction(id_kyc_profile_t *p, const char *iso2);
id_status_t id_kyc_profile_tier(id_kyc_profile_t *p, uint8_t level, uint64_t max_single,
                                uint64_t max_daily);

/* Holder wallet. Off until the person opts in. */
void id_kyc_wallet_init(id_kyc_wallet_t *w, const uint8_t root_seed[32]);
void id_kyc_opt_in(id_kyc_wallet_t *w, bool yes);
/* Ask adapter `a` for a level-`level` credential; check it against
 * `trust` and store it. *slot_out: wallet slot. */
id_status_t id_kyc_request(id_kyc_wallet_t *w, const id_kyc_adapter_t *a,
                           const id_kyc_profile_t *trust, const id_host_t *h, uint8_t level,
                           const char *jurisdiction, uint64_t now_ms, uint32_t *slot_out);
/* Make a presentation of wallet slot `slot` opening the claims in mask. */
id_status_t id_vp_create(const id_kyc_wallet_t *w, uint32_t slot, uint16_t mask,
                         const uint8_t nonce[32], const char *audience, const id_host_t *h,
                         id_vp_t *vp);
/* Verifier: check a presentation against a profile. rl may be NULL only
 * if the profile does not require KYC; a required level fails closed. */
id_status_t id_vp_verify(const id_kyc_profile_t *p, const id_vp_t *vp, const id_revlist_t *rl,
                         const uint8_t nonce[32], const char *audience, uint64_t now_ms,
                         id_kyc_status_t *out);
/* Find an opened claim by tag; NULL if not disclosed. */
const id_claim_t *id_kyc_claim(const id_kyc_status_t *s, const char *tag);

/* Tier hook for payments and cards. s may be NULL (no KYC). rail must be
 * 555, 777 or 888. On ID_ERR_LEVEL / ID_ERR_LIMIT, *need_level (if not
 * NULL) is the lowest level that would allow the payment, or 255 if none. */
id_status_t id_kyc_check_payment(const id_kyc_profile_t *p, const id_kyc_status_t *s,
                                 uint64_t now_ms, uint16_t rail, uint64_t amount,
                                 uint64_t spent_today, uint8_t *need_level);

/* ======================================================================
 * RECOVERY
 * ====================================================================== */

/* --- Argon2id (RFC 9106), bounded --- */
#define ID_ARGON2_BLOCK   1024u
#define ID_ARGON2_MAX_P   8u
#define ID_ARGON2_MAX_T   16u
#define ID_ARGON2_MAX_OUT 64u
typedef struct {
    uint32_t m_kib; /* memory in KiB (= 1 KiB blocks), >= 8*p */
    uint32_t t;     /* passes */
    uint32_t p;     /* lanes */
} id_kdf_params_t;

/* mem: caller memory of mem_blocks 1 KiB blocks (>= m_kib rounded down to
 * a multiple of 4p). secret/ad may be NULL/0. */
id_status_t id_argon2id(const id_kdf_params_t *kp, const uint8_t *pwd, uint32_t pwd_len,
                        const uint8_t *salt, uint32_t salt_len, const uint8_t *secret,
                        uint32_t secret_len, const uint8_t *ad, uint32_t ad_len, uint64_t *mem,
                        uint32_t mem_blocks, uint8_t *out, uint32_t out_len);
void id_blake2b(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_len);

/* Network defaults for the recovery-name KDF: 64 MiB, 3 passes, 1 lane. */
#define ID_LOOKUP_M_KIB 65536u
#define ID_LOOKUP_T     3u
#define ID_LOOKUP_P     1u
void id_kdf_default(id_kdf_params_t *kp);

/* --- BCH fuzzy extractor --- */
#define ID_BCH_N         255u
#define ID_FE_MAX_BLOCKS 8u
#define ID_FE_MAX_BITS   (ID_BCH_N * ID_FE_MAX_BLOCKS)
#define ID_FE_MAX_BYTES  ((ID_FE_MAX_BITS + 7u) / 8u)
#define ID_FE_HELPER_MAX (32u * ID_FE_MAX_BLOCKS) /* 32 bytes per block */
#define ID_FE_T_MIN      4u
#define ID_FE_T_MAX      30u
#define ID_FE_T_DEFAULT  25u /* BCH(255,91,25): corrects ~9.8% of bits per block */

/* Message length k of BCH(255, k, t), or 0 if t is out of range. */
uint32_t id_bch_k(uint32_t t);
/* Systematic encode: msg bits (k of them, packed LSB first) -> 255-bit
 * codeword (packed, 32 bytes). */
id_status_t id_bch_encode(uint32_t t, const uint8_t *msg, uint8_t cw[32]);
/* Decode in place; returns the number of corrected bits, or -1 on a
 * decoding failure. */
int32_t id_bch_decode(uint32_t t, uint8_t cw[32]);

/* Gen: helper (32 bytes per block) = w XOR random codewords; key = SHAKE256(dom ||
 * salt || w). features: nbits = blocks * 255, packed LSB first. */
id_status_t id_fe_gen(uint32_t t, const uint8_t *features, uint32_t nbits, const id_host_t *h,
                      const uint8_t salt[ID_SALT], uint8_t *helper, uint8_t key[32]);
/* Rep: returns ID_OK and the key, or ID_ERR_NOMATCH if any block fails. */
id_status_t id_fe_rep(uint32_t t, const uint8_t *features, uint32_t nbits, const uint8_t *helper,
                      const uint8_t salt[ID_SALT], uint8_t key[32]);

/* --- Shamir over GF(2^8) (polynomial 0x11B) --- */
#define ID_MAX_GUARDIANS 8u
#define ID_SHARE_LEN     32u
id_status_t id_shamir_split(const uint8_t secret[ID_SHARE_LEN], uint8_t k, uint8_t n,
                            const id_host_t *h, uint8_t shares[][ID_SHARE_LEN]);
/* xs[i] in 1..255, distinct; uses the first k pairs. */
id_status_t id_shamir_combine(const uint8_t *xs, const uint8_t ys[][ID_SHARE_LEN], uint8_t k,
                              uint8_t secret[ID_SHARE_LEN]);

/* --- vault, guardians, recovery --- */
#define ID_VAULT_VER        1u
#define ID_VAULT_MAX        2048u
#define ID_DEFAULT_K        3u
#define ID_DEFAULT_N        5u
#define ID_NAME_MIN         4u /* bytes after normalisation */
#define ID_GUARD_DELAY_MS   (24ull * 3600ull * 1000ull)
#define ID_GUARD_WINDOW_MS  (30ull * 24ull * 3600ull * 1000ull)
#define ID_GUARD_MAX_REQ    3u /* recovery requests per window per account */
#define ID_GUARD_MAX_CANCEL 2u
#define ID_GUARD_MAX_ACCTS  8u
#define ID_REC_MAX_FAILS    3u
#define ID_REC_MAX_CAND     4u
#define ID_REQ_ID           16u
#define ID_PLATFORM_RP      "zxv.id" /* rp id of root-certified passkeys */
#define ID_RECOVERY_ORIGIN  "zxv://recovery"

typedef struct {
    uint8_t id[ID_HASH];     /* how the owner reaches the guardian (DID or devmesh id) */
    char label[ID_NAME_MAX]; /* "Mum", "home server" */
} id_guardian_ref_t;

typedef struct {
    id_kdf_params_t kdf; /* recovery-name KDF (id_kdf_default in production) */
    uint8_t fe_t;        /* BCH correction capability */
    uint8_t k, n;        /* guardian threshold */
} id_vault_params_t;

/* Handed to guardian i at enrolment over an authenticated channel
 * (devmesh / EHOP session). */
typedef struct {
    uint8_t vault_id[ID_HASH]; /* sha2-256 digest of the vault = its CID digest */
    uint8_t account[ID_HASH];  /* root DID id */
    uint8_t index;             /* Shamir x coordinate, 1..n */
    uint8_t k, n;
    uint8_t share[ID_SHARE_LEN];
} id_guardian_share_t;

void id_vault_params_default(id_vault_params_t *vp);
/* Normalise a recovery name: trim, collapse runs of spaces, ASCII to
 * lower case (UTF-8 passes through: the host applies NFC first). Returns
 * the length or -1 if it does not fit / is shorter than ID_NAME_MIN. */
int32_t id_name_normalise(const char *name, char out[ID_NAME_MAX]);
/* Name key (64 bytes) and lookup key from the recovery name. */
id_status_t id_name_keys(const char *name, const id_kdf_params_t *kp, uint64_t *mem,
                         uint32_t mem_blocks, uint8_t nk[64], uint8_t lookup[ID_HASH]);

/* Enrol: build and publish the vault, and fill shares[0..n). If the host
 * can fetch, a name already in use is refused with ID_ERR_NAME. */
id_status_t id_recovery_enrol(const id_host_t *h, const id_root_t *root, const char *name,
                              const uint8_t *features, uint32_t nbits, const id_vault_params_t *vp,
                              const id_guardian_ref_t *guardians, uint64_t *mem,
                              uint32_t mem_blocks, uint8_t *vault, uint32_t vault_cap,
                              uint32_t *vault_len, ipfsn_cid_t *cid, id_guardian_share_t *shares);

/* Recovery request from the new device to each guardian. */
typedef struct {
    uint8_t vault_id[ID_HASH];
    uint8_t req_id[ID_REQ_ID];
    uint64_t requested_ms;
    uint32_t kem_pk_len;
    uint8_t kem_pk[PQM_KEM_PK_MAX_BYTES]; /* pqm KEM key the share is sealed to */
} id_rec_request_t;

/* A guardian's share, sealed to the request's KEM key. */
typedef struct {
    uint8_t vault_id[ID_HASH];
    uint8_t req_id[ID_REQ_ID];
    uint32_t ct_len;
    uint8_t ct[PQM_KEM_CT_MAX_BYTES];
    uint8_t enc[1u + ID_SHARE_LEN]; /* index || share, ChaCha20 */
    uint8_t tag[16];
} id_sealed_share_t;

/* The owner's device cancels a recovery it did not start. */
typedef struct {
    uint8_t vault_id[ID_HASH];
    uint8_t req_id[ID_REQ_ID];
    id_dev_cert_t cert;
    uint8_t passkey_pk[ID_MLDSA65_PK];
    uint8_t auth[ID_AUTHDATA_MIN];
    uint8_t sig[ID_MLDSA65_SIG];
} id_rec_cancel_t;

/* --- guardian side --- */
typedef struct {
    uint8_t used;
    id_guardian_share_t sh;
    pqm_sig_pk_t root_pk;
    uint64_t delay_ms;
    uint64_t window_start_ms;
    uint8_t requests;
    uint8_t cancels;
    uint8_t pending;
    uint8_t req_id[ID_REQ_ID];
    uint64_t req_ms;
    pqm_kem_pk_t kem_pk;
} id_guard_acct_t;

typedef struct {
    id_host_t host;
    id_guard_acct_t acct[ID_GUARD_MAX_ACCTS];
} id_guardian_t;

void id_guard_init(id_guardian_t *g, const id_host_t *h);
/* Keep a share. delay_ms: waiting period before release (0 = default). */
id_status_t id_guard_keep(id_guardian_t *g, const id_guardian_share_t *sh,
                          const pqm_sig_pk_t *root_pk, uint64_t delay_ms);
/* A request arrived: alert the guardian, start the waiting period. */
id_status_t id_guard_on_request(id_guardian_t *g, const id_rec_request_t *req, uint64_t now_ms);
/* The guardian (the person) approves after the waiting period. */
id_status_t id_guard_approve(id_guardian_t *g, const uint8_t vault_id[ID_HASH],
                             const uint8_t req_id[ID_REQ_ID], uint64_t now_ms,
                             id_sealed_share_t *out);
id_status_t id_guard_cancel(id_guardian_t *g, const id_rec_cancel_t *c, uint64_t now_ms);

/* Owner's device: sign a cancel for request req_id. */
id_status_t id_rec_cancel_make(id_passkey_t *pk, const id_host_t *h, const id_dev_cert_t *cert,
                               const uint8_t vault_id[ID_HASH], const uint8_t req_id[ID_REQ_ID],
                               id_rec_cancel_t *out);

/* --- requester (new device) side --- */
typedef struct {
    uint32_t len;
    uint8_t blob[ID_VAULT_MAX];
} id_vault_blob_t;

typedef struct {
    uint8_t state;
    id_host_t host;
    pqm_level_t kem_level;
    uint8_t nk[64];
    uint8_t lookup[ID_HASH];
    uint8_t ncand;
    id_vault_blob_t cand[ID_REC_MAX_CAND];
    uint8_t chosen;
    uint8_t vault_id[ID_HASH];
    /* parsed (from the chosen vault) */
    uint8_t fe_t, k, n, blocks;
    uint8_t account[ID_HASH];
    id_guardian_ref_t guardians[ID_MAX_GUARDIANS];
    uint8_t helper[ID_FE_HELPER_MAX];
    uint8_t fe_salt[ID_SALT];
    /* request */
    uint8_t req_id[ID_REQ_ID];
    uint64_t started_ms;
    pqm_kem_pk_t kem_pk;
    pqm_kem_sk_t kem_sk;
    /* shares */
    uint8_t nshares;
    uint8_t xs[ID_MAX_GUARDIANS];
    uint8_t ys[ID_MAX_GUARDIANS][ID_SHARE_LEN];
    uint8_t fails;
} id_recovery_t;

/* Begin: derive the name keys, fetch the vault(s) announced under the
 * lookup key, keep those the name opens. *ncand: how many (normally 1). */
id_status_t id_rec_begin(id_recovery_t *r, const id_host_t *h, const char *name,
                         const id_kdf_params_t *kp, uint64_t *mem, uint32_t mem_blocks,
                         uint8_t *ncand);
/* Like id_rec_begin, but with vault bytes the user already has. */
id_status_t id_rec_begin_with(id_recovery_t *r, const id_host_t *h, const char *name,
                              const id_kdf_params_t *kp, uint64_t *mem, uint32_t mem_blocks,
                              const uint8_t *vault, uint32_t len);
/* Guardians of candidate i (to let the person recognise their own). */
const id_guardian_ref_t *id_rec_guardians(id_recovery_t *r, uint32_t cand, uint8_t *n);
/* Choose candidate i and make the request for the guardians. */
id_status_t id_rec_request(id_recovery_t *r, uint32_t cand, pqm_level_t kem_level, uint64_t now_ms,
                           id_rec_request_t *req);
/* Add one sealed share (checked against the vault's commitment). */
id_status_t id_rec_add_share(id_recovery_t *r, const id_sealed_share_t *s);
/* With k shares: re-scan + name + shares -> root seed and level. After
 * ID_REC_MAX_FAILS failures the session is locked and its shares wiped. */
id_status_t id_rec_finish(id_recovery_t *r, const uint8_t *features, uint32_t nbits,
                          uint64_t now_ms, uint8_t seed[32], pqm_level_t *level);
void id_rec_wipe(id_recovery_t *r);

/* After recovery: certify the new device's passkey and sign a status list
 * that holds only it (every old passkey revoked). If old != NULL it is
 * replaced through id_status_accept, so host.revoke_device hears of each
 * old device. */
id_status_t id_reenrol(const id_root_t *root, const id_host_t *h, const id_passkey_t *new_pk,
                       uint64_t now_ms, id_dev_cert_t *cert, id_cred_status_t *next,
                       id_cred_status_t *old);

/* ===== wire encodings for requests / shares (big-endian) ===== */
int32_t id_rec_request_encode(const id_rec_request_t *q, uint8_t *out, uint32_t cap);
id_status_t id_rec_request_decode(id_rec_request_t *q, const uint8_t *in, uint32_t len);
int32_t id_sealed_share_encode(const id_sealed_share_t *s, uint8_t *out, uint32_t cap);
id_status_t id_sealed_share_decode(id_sealed_share_t *s, const uint8_t *in, uint32_t len);

#endif /* ZXV_IDENT_H */
