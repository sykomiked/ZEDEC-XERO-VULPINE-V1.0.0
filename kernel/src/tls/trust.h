/* trust.h — the root trust store and server certificate chain validation
 *
 * This is the piece that turns an encrypted TLS channel into an
 * AUTHENTICATED one: given the chain a server presents, the hostname the
 * caller meant to reach, and the current time, it decides whether a root the
 * device trusts vouches for that server.
 *
 * Fail closed, everywhere:
 *  - an empty trust store trusts nothing;
 *  - an unknown time (now <= 0) verifies nothing, because expiry cannot be
 *    checked without it;
 *  - anything not understood (critical extension, signature or key type,
 *    name-constraint form) is a failure, not a skip.
 *
 * What is checked (RFC 5280 6.1, CA/Browser Forum profile):
 *  - every signature on the path, up to a trust anchor;
 *  - validity periods of every certificate including the anchor;
 *  - the leaf's subjectAltName against the hostname (no common-name
 *    fallback), keyUsage digitalSignature and extendedKeyUsage serverAuth
 *    when those extensions are present;
 *  - every issuer is a CA (basicConstraints cA), may sign certificates
 *    (keyUsage keyCertSign when present), allows serverAuth (EKU when
 *    present), and respects its pathLenConstraint;
 *  - dNSName and iPAddress name constraints of every CA on the path;
 *  - key strength: RSA 2048-4096 bits, EC P-256 or P-384 only.
 *
 * Not checked: revocation (no OCSP or CRL fetching; certificate lifetimes
 * of 90 days or less are the mitigation), certificate policies, and
 * Certificate Transparency.
 *
 * Path building: from the leaf, an issuer is looked for among the trust
 * anchors first, then among the certificates the server sent, so a server
 * that also sends a cross-signed copy of a trusted root still validates.
 *
 * Freestanding: integer only, no libc, no allocation. Anchors are stored by
 * reference; their DER must outlive the store. Validation uses about 16 KB
 * of stack.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_TRUST_H
#define ZXV_TLS_TRUST_H

#include <stdint.h>
#include <stdbool.h>
#include "x509.h"

#define TLS_TRUST_MAX 192u
#define TLS_CHAIN_MAX 8u /* certificates in a path, leaf and anchor included */

typedef struct {
    const uint8_t *der;
    uint32_t len;
    const uint8_t *subject; /* points into der */
    uint32_t subject_len;
} tls_anchor_t;

typedef struct {
    tls_anchor_t a[TLS_TRUST_MAX];
    uint32_t n;
} tls_trust_t;

typedef enum {
    TLS_CERT_OK = 0,
    TLS_CERT_NO_CERT,     /* the server sent no certificate */
    TLS_CERT_NO_TRUST,    /* the trust store is empty */
    TLS_CERT_NO_TIME,     /* the current time is unknown */
    TLS_CERT_PARSE,       /* a certificate is not valid DER X.509 */
    TLS_CERT_UNSUPPORTED, /* an algorithm, key or critical extension we lack */
    TLS_CERT_EXPIRED,
    TLS_CERT_NOT_YET_VALID,
    TLS_CERT_NAME_MISMATCH,  /* the leaf does not name the host */
    TLS_CERT_UNKNOWN_ISSUER, /* no path to a trusted root */
    TLS_CERT_BAD_SIGNATURE,
    TLS_CERT_NOT_CA,          /* an issuer is not a CA */
    TLS_CERT_PATH_LEN,        /* pathLenConstraint exceeded */
    TLS_CERT_KEY_USAGE,       /* keyUsage forbids this use */
    TLS_CERT_EKU,             /* extendedKeyUsage lacks serverAuth */
    TLS_CERT_NAME_CONSTRAINT, /* a CA's name constraints exclude the leaf */
    TLS_CERT_WEAK_KEY,        /* key too small or not on a supported curve */
    TLS_CERT_CHAIN_TOO_LONG
} tls_cert_status_t;

const char *tls_cert_status_str(tls_cert_status_t s);

/* An empty store: it trusts nothing until anchors are added. */
void tls_trust_init(tls_trust_t *t);
/* Add one root certificate (DER). It must parse and carry a supported key.
 * The bytes are referenced, not copied. Duplicates are ignored. */
bool tls_trust_add_der(tls_trust_t *t, const uint8_t *der, uint32_t len);
/* Add the roots compiled into the kernel (trust_roots.h). Returns how many
 * were added. */
uint32_t tls_trust_add_builtin(tls_trust_t *t);
/* How many roots are compiled in. */
uint32_t tls_trust_builtin_count(void);

/* Validate a chain as sent in a TLS Certificate message: certs[0] is the
 * leaf, the rest are candidate intermediates in any order. On success, and
 * only then, *leaf_out (if not null) holds the parsed leaf, whose spans
 * point into certs[0]. */
tls_cert_status_t tls_verify_chain(const tls_trust_t *t, const uint8_t *const certs[],
                                   const uint32_t lens[], uint32_t count, const char *host,
                                   uint32_t host_len, int64_t now, x509_cert_t *leaf_out);

#endif /* ZXV_TLS_TRUST_H */
