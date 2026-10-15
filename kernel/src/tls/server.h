/* server.h — a TLS 1.3 server handshake (RFC 8446) for a service on this
 * machine: the zxvd IRC gateway (docs/IRC_GATEWAY.md).
 *
 * SCOPE (the same narrow profile as the client, handshake.h)
 *   V1  One cipher suite, TLS_CHACHA20_POLY1305_SHA256; one group, x25519;
 *       TLS 1.3 only (supported_versions must offer 0x0304). A ClientHello
 *       without an x25519 key share is refused with handshake_failure: there
 *       is no HelloRetryRequest. No PSK, no resumption, no early data (a PSK
 *       offer is ignored and a full handshake runs), no client certificates.
 *   V2  The server authenticates with one P-256 certificate and its key,
 *       signing CertificateVerify ecdsa_secp256r1_sha256 (ecdsa.h S1); the
 *       client must offer that scheme. The certificate is usually
 *       self-signed (selfsigned.h) and pinned by the client.
 *   V3  Flight: ServerHello, a change_cipher_spec for middlebox
 *       compatibility (only when the client sent a legacy session id), then
 *       EncryptedExtensions, Certificate, CertificateVerify and Finished
 *       under the handshake keys. The server switches its sending side to
 *       application keys after its Finished; application data from the
 *       client is accepted only after the client's Finished has verified
 *       (constant-time compare).
 *   V4  Every refusal sends a fatal alert (in the clear before the
 *       handshake keys, protected after) and the state becomes FAILED; a
 *       failed server never accepts more input.
 *   V5  KeyUpdate from the client is honoured (and answered when it asks);
 *       close_notify sets peer_closed. NewSessionTicket is never sent.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_TLS_SERVER_H
#define ZXV_TLS_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "handshake.h" /* tls_error_t, the record and key schedule types */

#define TLS_SRV_CERT_MAX 2048u

typedef enum {
    TLS_SRV_WAIT_CH = 0,
    TLS_SRV_WAIT_FIN,
    TLS_SRV_ESTABLISHED,
    TLS_SRV_FAILED
} tls_srv_state_t;

typedef struct {
    tls_srv_state_t state;
    tls_error_t error;
    uint8_t alert_sent; /* the fatal alert we sent, 0 if none */
    uint8_t alert_got;  /* a fatal alert the client sent */

    uint8_t cert[TLS_SRV_CERT_MAX];
    uint32_t cert_len;
    uint8_t key[32]; /* the certificate's P-256 private key */
    uint8_t priv[X25519_LEN], pub[X25519_LEN], random[32];

    tls13_schedule_t sched;
    tls_keys_t rx, tx;
    uint8_t c_ap[HASH_LEN], s_ap[HASH_LEN]; /* current application secrets (V5) */
    uint8_t th_srv_fin[HASH_LEN];           /* transcript through server Finished */

    uint8_t transcript[TLS_TRANSCRIPT_MAX];
    uint32_t transcript_len;
    uint8_t hs_buf[TLS_HS_MSG_MAX];
    uint32_t hs_len;

    uint8_t app[TLS_MAX_PLAINTEXT];
    uint32_t app_len, app_off;
    bool peer_closed;
} tls_server_t;

/* `random32` and `x25519_priv32` must come from a real entropy source, fresh
 * per connection. cert/key: V2 (copied). Returns false (and the server is
 * FAILED) when the certificate is too long or the key is not a P-256 scalar. */
bool tls_server_init(tls_server_t *s, const uint8_t *cert_der, uint32_t cert_len,
                     const uint8_t key32[32], const uint8_t random32[32],
                     const uint8_t x25519_priv32[X25519_LEN]);

/* Feed received bytes; whole records are consumed. Anything that must be
 * sent back (the server flight, an alert, a KeyUpdate) is written to reply.
 * Returns the bytes of `in` consumed; the rest is an incomplete record to
 * feed again with more bytes. reply_cap of 4096 holds any reply. */
uint32_t tls_server_feed(tls_server_t *s, const uint8_t *in, uint32_t in_len, uint8_t *reply,
                         uint32_t reply_cap, uint32_t *reply_len);

/* Encrypt application data (at most TLS_MAX_PLAINTEXT per call) into one
 * record. 0 unless established. */
uint32_t tls_server_send(tls_server_t *s, const uint8_t *data, uint32_t len, uint8_t *out,
                         uint32_t cap);
/* Copy out decrypted application data. */
uint32_t tls_server_read(tls_server_t *s, uint8_t *out, uint32_t cap);
/* A close_notify alert record. */
uint32_t tls_server_close(tls_server_t *s, uint8_t *out, uint32_t cap);

static inline bool tls_server_established(const tls_server_t *s)
{
    return s && s->state == TLS_SRV_ESTABLISHED;
}
static inline bool tls_server_failed(const tls_server_t *s)
{
    return !s || s->state == TLS_SRV_FAILED;
}

#endif /* ZXV_TLS_SERVER_H */
