/* x509.h — X.509 v3 certificate parsing (RFC 5280), strict DER
 *
 * Parses a certificate into spans that point into the caller's buffer: the
 * signed TBS bytes, issuer and subject names, validity, the public key, and
 * the extensions a TLS server chain needs (basic constraints, key usage,
 * extended key usage, subject alternative names, name constraints).
 *
 * Strictness is the safety property here, so the parser refuses rather than
 * guesses:
 *  - DER only: definite, minimal lengths; single-byte tags; nothing trailing.
 *  - The inner and outer signature algorithms must be byte-identical.
 *  - An unknown extension marked critical fails the parse (RFC 5280 4.2).
 *  - A repeated extension fails the parse.
 *  - SHA-1 and MD5 signatures are not representable, so they cannot verify.
 *
 * Supported keys: RSA (rsaEncryption, 2048-4096 bits), EC P-256 and P-384.
 * Supported signatures: sha{256,384,512}WithRSAEncryption, RSASSA-PSS with
 * SHA-2 (MGF1 same hash, salt = digest length), ecdsa-with-SHA{256,384,512}.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_X509_H
#define ZXV_TLS_X509_H

#include <stdint.h>
#include <stdbool.h>
#include "digest.h"
#include "ecdsa.h"

typedef enum {
    X509_OK = 0,
    X509_ERR_PARSE,       /* not DER, or not a certificate */
    X509_ERR_UNSUPPORTED, /* well-formed, but uses something not implemented */
    X509_ERR_CRITICAL     /* an unknown extension is marked critical */
} x509_err_t;

typedef enum { X509_KEY_NONE = 0, X509_KEY_RSA, X509_KEY_EC } x509_key_type_t;

typedef struct {
    x509_key_type_t type;
    const uint8_t *n, *e; /* RSA, big-endian */
    uint32_t n_len, e_len;
    ecdsa_curve_t curve;  /* EC */
    const uint8_t *point; /* 0x04 || X || Y */
    uint32_t point_len;
} x509_pubkey_t;

typedef enum {
    X509_SIG_NONE = 0,
    X509_SIG_RSA_PKCS1,
    X509_SIG_RSA_PSS,
    X509_SIG_ECDSA
} x509_sig_kind_t;

typedef struct {
    x509_sig_kind_t kind;
    tls_hash_t hash;
} x509_sigalg_t;

/* key usage bits, numbered as in RFC 5280 (bit 0 = digitalSignature) */
#define X509_KU_DIGITAL_SIGNATURE (1u << 0)
#define X509_KU_KEY_CERT_SIGN     (1u << 5)

typedef struct {
    const uint8_t *p;
    uint32_t len;
} x509_span_t;

typedef struct {
    x509_span_t raw;     /* the whole certificate */
    x509_span_t tbs;     /* what the issuer signed */
    x509_span_t issuer;  /* full DER Name, compared byte-for-byte */
    x509_span_t subject; /* full DER Name */
    x509_span_t sig;     /* signature value (BIT STRING contents) */
    x509_sigalg_t sigalg;
    uint32_t version;              /* 1 or 3 */
    int64_t not_before, not_after; /* seconds since 1970-01-01T00:00:00Z */
    x509_pubkey_t key;

    bool has_bc, is_ca;
    int32_t path_len; /* -1 when absent */
    bool has_ku;
    uint32_t ku;
    bool has_eku, eku_server_auth, eku_any;
    x509_span_t san; /* contents of the GeneralNames SEQUENCE; len 0 if none */
    bool has_nc;
    x509_span_t nc_permitted, nc_excluded; /* GeneralSubtrees contents */
} x509_cert_t;

x509_err_t x509_parse(x509_cert_t *c, const uint8_t *der, uint32_t len);

/* Verify `sig` over `msg` with `key` under `alg`. For ECDSA the signature is
 * DER; for RSA it is the raw block. Weak or unsupported keys fail. */
bool x509_verify_sig(const x509_pubkey_t *key, x509_sigalg_t alg, const uint8_t *msg,
                     uint32_t msg_len, const uint8_t *sig, uint32_t sig_len);

/* Whether `issuer`'s key verifies `child`'s signature. */
bool x509_signed_by(const x509_cert_t *child, const x509_cert_t *issuer);

/* Whether the certificate's subjectAltName covers `host` (RFC 6125): DNS
 * names compared case-insensitively, a wildcard only as a whole left-most
 * label covering exactly one label and never directly under a top-level
 * label ("*.com" matches nothing); IPv4 literals only against iPAddress
 * entries. The subject common name is never consulted. */
bool x509_matches_host(const x509_cert_t *c, const char *host, uint32_t host_len);

/* Whether every DNS and IP name in `leaf`'s subjectAltName is allowed by
 * `ca`'s name constraints. Constraint forms other than dNSName and
 * iPAddress make this return false (fail closed). */
bool x509_name_constraints_ok(const x509_cert_t *ca, const x509_cert_t *leaf);

/* Seconds since the epoch for a UTC calendar time, or -1 if out of range. */
int64_t x509_unix_time(uint32_t y, uint32_t mo, uint32_t d, uint32_t h, uint32_t mi, uint32_t s);

#endif /* ZXV_TLS_X509_H */
