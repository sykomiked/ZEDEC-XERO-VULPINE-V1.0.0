/* handshake.h — a TLS 1.3 client handshake (RFC 8446)
 *
 * ============================ READ THIS FIRST ============================
 *
 * WHETHER THE SERVER IS AUTHENTICATED DEPENDS ON THE POLICY, AND THE POLICY
 * HAS NO DEFAULT.
 *
 *   TLS_VERIFY_REQUIRED + tls_client_set_trust()
 *       The server's certificate chain is validated against the trust store
 *       (trust.h: signatures to a trusted root, expiry, hostname, CA flags,
 *       key and extended key usage, path length, name constraints), and its
 *       CertificateVerify signature is checked with the leaf key
 *       (ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384,
 *       rsa_pss_rsae_sha256/384/512). Only after both, and the server's
 *       Finished, does tls_client_is_authenticated() become true.
 *
 *   TLS_VERIFY_REQUIRED without a trust store or without the time
 *       Fails at the Certificate message with TLS_ERR_NO_VERIFIER. A device
 *       that has not learned the time cannot check expiry, so it does not
 *       pretend to.
 *
 *   TLS_VERIFY_INSECURE_ACKNOWLEDGED
 *       Completes without looking at the certificate. The channel is
 *       encrypted against a passive eavesdropper and NOT against an active
 *       machine-in-the-middle; tls_client_is_authenticated() stays false
 *       forever. The long constant is deliberate.
 *
 * Revocation (OCSP, CRLs) is not checked; see trust.h.
 *
 * ========================================================================
 *
 * SCOPE
 * -----
 *  - Client only. One cipher suite: TLS_CHACHA20_POLY1305_SHA256.
 *  - One group: x25519. No HelloRetryRequest handling beyond detecting it and
 *    failing cleanly (a server that wants a different group is refused, not
 *    silently mishandled).
 *  - No session resumption, no PSK, no early data, no client certificates.
 *  - The transcript is buffered rather than streamed, because the SHA-256 in
 *    tree is one-shot. TLS_TRANSCRIPT_MAX bounds it; a server whose
 *    certificate chain exceeds that is refused rather than truncated.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TLS slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_HANDSHAKE_H
#define ZXV_TLS_HANDSHAKE_H

#include <stdint.h>
#include <stdbool.h>
#include "hkdf.h"
#include "aead.h"
#include "record.h"
#include "x25519.h"
#include "trust.h"

#define TLS_TRANSCRIPT_MAX   16384u
#define TLS_HS_MSG_MAX       16384u
#define TLS_MAX_HOSTNAME     256u

/* cipher suite / group / version codes we use */
#define TLS_CS_CHACHA20_POLY1305_SHA256 0x1303u
#define TLS_GROUP_X25519                0x001Du
#define TLS_VERSION_13                  0x0304u

/* handshake message types */
#define TLS_HS_CLIENT_HELLO         1u
#define TLS_HS_SERVER_HELLO         2u
#define TLS_HS_NEW_SESSION_TICKET   4u
#define TLS_HS_ENCRYPTED_EXTENSIONS 8u
#define TLS_HS_CERTIFICATE          11u
#define TLS_HS_CERTIFICATE_VERIFY   15u
#define TLS_HS_FINISHED             20u

/* Verification policy. There is no default: tls_client_init() takes it, so a
 * caller has to state which one they are choosing. */
typedef enum {
    /* Refuse to complete unless the server is verified: its certificate
     * chain against the trust store given to tls_client_set_trust(), and its
     * CertificateVerify signature. With no trust store this always fails. */
    TLS_VERIFY_REQUIRED = 1,
    /* Complete without authenticating the server. The connection is then
     * encrypted but MITM-able, and tls_client_is_authenticated() returns
     * false forever. The long constant is deliberate: it should be hard to
     * pass this by accident. */
    TLS_VERIFY_INSECURE_ACKNOWLEDGED = 0x5A5A11FEu
} tls_verify_policy_t;

typedef enum {
    TLS_ST_INIT = 0,
    TLS_ST_WAIT_SH,        /* ClientHello sent                     */
    TLS_ST_WAIT_EE,        /* handshake keys installed             */
    TLS_ST_WAIT_CERT,
    TLS_ST_WAIT_CV,
    TLS_ST_WAIT_FIN,
    TLS_ST_ESTABLISHED,
    TLS_ST_FAILED
} tls_hs_state_t;

/* Why a handshake failed, so a caller can report something better than
 * "it didn't work". */
typedef enum {
    TLS_ERR_NONE = 0,
    TLS_ERR_PROTOCOL,     /* malformed or unexpected message   */
    TLS_ERR_UNSUPPORTED,  /* server chose something we lack    */
    TLS_ERR_BAD_RECORD,   /* AEAD authentication failed        */
    TLS_ERR_BAD_FINISHED, /* the server's Finished did not verify */
    TLS_ERR_NO_VERIFIER,  /* TLS_VERIFY_REQUIRED with no trust store or time */
    TLS_ERR_TOO_BIG,      /* exceeded a fixed buffer           */
    TLS_ERR_ALERT,        /* the server sent a fatal alert     */
    TLS_ERR_HELLO_RETRY,  /* server wants a group we do not offer */
    TLS_ERR_BAD_CERT,     /* the chain did not validate: see cert_status */
    TLS_ERR_BAD_SIGNATURE /* CertificateVerify did not verify */
} tls_error_t;

/* The server's leaf key, copied out of the Certificate message so it
 * survives until CertificateVerify. */
typedef struct {
    x509_key_type_t type;
    ecdsa_curve_t curve;
    uint8_t n[512];
    uint32_t n_len;
    uint8_t e[4];
    uint32_t e_len;
    uint8_t point[97];
    uint32_t point_len;
} tls_peer_key_t;

typedef struct {
    tls_hs_state_t state;
    tls_error_t    error;
    uint8_t        alert_desc;      /* when error == TLS_ERR_ALERT */

    tls_verify_policy_t policy;
    bool authenticated; /* chain + CertificateVerify + Finished */

    const tls_trust_t *trust;      /* tls_client_set_trust() */
    int64_t now;                   /* seconds since the epoch, UTC */
    tls_cert_status_t cert_status; /* why the chain failed, when it did */
    bool cert_ok;                  /* the chain validated */
    bool cv_ok;                    /* CertificateVerify verified */
    tls_peer_key_t peer_key;

    uint8_t  hostname[TLS_MAX_HOSTNAME];
    uint32_t hostname_len;

    uint8_t  priv[X25519_LEN];
    uint8_t  pub[X25519_LEN];
    uint8_t  random[32];
    uint8_t  session_id[32];

    tls13_schedule_t sched;
    tls_keys_t rx, tx;

    /* the running handshake transcript */
    uint8_t  transcript[TLS_TRANSCRIPT_MAX];
    uint32_t transcript_len;
    /* the transcript hash snapshot through CertificateVerify, which is what
     * the server's Finished is computed over */
    uint8_t  th_precert_fin[HASH_LEN];
    bool     have_th_precert;

    /* partial handshake message reassembly: one message may span records and
     * one record may hold several messages */
    uint8_t  hs_buf[TLS_HS_MSG_MAX];
    uint32_t hs_len;

    /* application data decrypted but not yet read by the caller */
    uint8_t  app[TLS_MAX_PLAINTEXT];
    uint32_t app_len;
    uint32_t app_off;

    bool     peer_closed;
} tls_client_t;

/* `random32` and `priv32` must come from a real entropy source. A predictable
 * private scalar makes the whole exchange decryptable by anyone who can guess
 * it; this function cannot check that and does not pretend to. */
void tls_client_init(tls_client_t *c,
                     const char *hostname,
                     const uint8_t random32[32],
                     const uint8_t priv32[X25519_LEN],
                     tls_verify_policy_t policy);

/* Give a TLS_VERIFY_REQUIRED client its trust store and the current time
 * (seconds since 1970-01-01T00:00:00Z, from a clock the caller believes:
 * RTC plus SNTP, not a guess). Call before tls_client_hello(). The store is
 * referenced, not copied, and must outlive the handshake. */
void tls_client_set_trust(tls_client_t *c, const tls_trust_t *trust, int64_t now_unix);

/* Build the ClientHello, wrapped in a record, ready to write to the socket. */
uint32_t tls_client_hello(tls_client_t *c, uint8_t *out, uint32_t cap);

/* Feed received bytes. Consumes as many whole records as are present.
 * `reply`/`reply_len` receive anything that must be sent back (the client
 * Finished). Returns the number of input bytes consumed; check
 * tls_client_failed() afterwards. */
uint32_t tls_client_feed(tls_client_t *c, const uint8_t *in, uint32_t in_len,
                         uint8_t *reply, uint32_t reply_cap, uint32_t *reply_len);

/* Encrypt application data into a record. */
uint32_t tls_client_send(tls_client_t *c, const uint8_t *data, uint32_t len,
                         uint8_t *out, uint32_t cap);

/* Copy out decrypted application data. Returns bytes copied. */
uint32_t tls_client_read(tls_client_t *c, uint8_t *out, uint32_t cap);

static inline bool tls_client_established(const tls_client_t *c) {
    return c && c->state == TLS_ST_ESTABLISHED;
}
static inline bool tls_client_failed(const tls_client_t *c) {
    return !c || c->state == TLS_ST_FAILED;
}
/* Returns whether the SERVER'S IDENTITY was verified: true only for a
 * TLS_VERIFY_REQUIRED connection whose chain, CertificateVerify and
 * Finished all checked out. Callers that print a status must use this
 * rather than imply a secure connection. */
static inline bool tls_client_is_authenticated(const tls_client_t *c) {
    return c && c->authenticated;
}
const char *tls_error_str(tls_error_t e);

#endif /* ZXV_TLS_HANDSHAKE_H */
