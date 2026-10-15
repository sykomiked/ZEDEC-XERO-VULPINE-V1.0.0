/* selfsigned.h — a self-signed P-256 certificate for a server on this
 * machine (the zxvd IRC gateway, docs/IRC_GATEWAY.md).
 *
 *   C1  tls_selfsigned_p256 writes one DER X.509 v3 certificate: subject and
 *       issuer CN = `cn`, the P-256 key d*G, signed ecdsa-with-SHA256 by d
 *       itself (ecdsa_sign.c, RFC 6979 nonces).
 *   C2  Names: subjectAltName dNSName "localhost", iPAddress 127.0.0.1 and
 *       ::1, nothing else, so the certificate can never vouch for a host on
 *       the network. Extensions: basicConstraints cA FALSE (critical),
 *       keyUsage digitalSignature (critical), extendedKeyUsage serverAuth.
 *   C3  Validity from not_before to not_after (seconds since 1970, UTC):
 *       UTCTime through 2049, GeneralizedTime after, as RFC 5280 4.1.2.5
 *       requires. The serial is 16 bytes, its top bit cleared.
 *   C4  Nobody vouches for it: a client trusts it by pinning (its SHA-256
 *       fingerprint, or the certificate file as its only CA). trust.c
 *       accepts it when it is placed in the store itself (tls_verify_chain:
 *       "a leaf the owner placed in the store directly").
 *
 * Freestanding: integer only, no libc, no allocation, no division.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_TLS_SELFSIGNED_H
#define ZXV_TLS_SELFSIGNED_H

#include <stdint.h>
#include <stdbool.h>

#define TLS_SELFSIGNED_MAX 640u /* the certificate is about 450 bytes */

/* The certificate's length, or 0 (d out of range, a CN over 64 bytes or
 * empty, not_after <= not_before, a time before 1970 or after 9999, or
 * cap too small). */
uint32_t tls_selfsigned_p256(const uint8_t priv[32], const uint8_t serial[16], const char *cn,
                             int64_t not_before, int64_t not_after, uint8_t *out, uint32_t cap);

/* PEM text of a DER certificate ("-----BEGIN CERTIFICATE-----", 64-column
 * base64, a closing line, LF endings), NUL-terminated. Its length without
 * the NUL, or 0 if cap is too small. */
uint32_t tls_cert_pem(const uint8_t *der, uint32_t len, char *out, uint32_t cap);

#endif
