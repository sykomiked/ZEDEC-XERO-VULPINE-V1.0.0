/* handshake.h — a TLS 1.3 client handshake (RFC 8446)
 *
 * ============================ READ THIS FIRST ============================
 *
 * THIS CLIENT DOES NOT AUTHENTICATE THE SERVER.
 *
 * It performs the full TLS 1.3 key exchange and derives real, correct traffic
 * keys, so the channel is encrypted against a PASSIVE eavesdropper. It does
 * NOT verify the server's certificate, because doing that needs X.509 DER
 * parsing, RSA-PSS and ECDSA-P256 signature verification, and a trust store
 * of root CAs — none of which ZXV has. (ZXV has Ed25519, which essentially no
 * public certificate uses.)
 *
 * An unauthenticated encrypted channel stops passive interception and does
 * NOT stop an active machine-in-the-middle. Anyone who can reroute the
 * connection can present their own key share, and this client will happily
 * agree a key with them.
 *
 * That fact is made STRUCTURAL rather than documentary:
 *   - tls_client_init() will not run without an explicit policy argument.
 *   - Passing TLS_VERIFY_REQUIRED makes the handshake FAIL at
 *     CertificateVerify, because there is no verifier. That is the default a
 *     caller gets for free, and it is the safe one.
 *   - Only TLS_VERIFY_INSECURE_ACKNOWLEDGED lets the handshake complete, and
 *     it permanently sets `authenticated = false` on the connection.
 *   - tls_client_is_authenticated() exists so callers and boot messages can
 *     report the truth instead of implying a guarantee.
 *
 * When certificate verification is implemented, the verifier hook is the
 * place it goes, and TLS_VERIFY_REQUIRED starts working. Until then, nothing
 * in this file claims a security property it does not have.
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
    /* Refuse to complete unless the server is verified. With no verifier
     * implemented this ALWAYS fails — which is the correct, safe behaviour. */
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
    TLS_ERR_PROTOCOL,          /* malformed or unexpected message   */
    TLS_ERR_UNSUPPORTED,       /* server chose something we lack    */
    TLS_ERR_BAD_RECORD,        /* AEAD authentication failed        */
    TLS_ERR_BAD_FINISHED,      /* the server's Finished did not verify */
    TLS_ERR_NO_VERIFIER,       /* TLS_VERIFY_REQUIRED with nothing to verify with */
    TLS_ERR_TOO_BIG,           /* exceeded a fixed buffer           */
    TLS_ERR_ALERT,             /* the server sent a fatal alert     */
    TLS_ERR_HELLO_RETRY        /* server wants a group we do not offer */
} tls_error_t;

typedef struct {
    tls_hs_state_t state;
    tls_error_t    error;
    uint8_t        alert_desc;      /* when error == TLS_ERR_ALERT */

    tls_verify_policy_t policy;
    bool           authenticated;   /* stays false: see the header comment */

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
/* Returns whether the SERVER'S IDENTITY was verified. With no certificate
 * verification implemented this is always false, and callers that print a
 * status must say so rather than implying a secure connection. */
static inline bool tls_client_is_authenticated(const tls_client_t *c) {
    return c && c->authenticated;
}
const char *tls_error_str(tls_error_t e);

#endif /* ZXV_TLS_HANDSHAKE_H */
