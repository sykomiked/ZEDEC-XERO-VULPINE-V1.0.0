/* x25519.h — Curve25519 scalar multiplication (RFC 7748)
 *
 * This is the key agreement TLS 1.3 actually uses in practice. It is also the
 * one piece of a TLS stack where a subtle arithmetic bug produces output that
 * looks perfectly random and is simply wrong — there is no error, no failed
 * check, just a shared secret the peer does not share. So it is verified
 * against RFC 7748's published vectors, including the iterated ones.
 *
 * WHAT THIS IMPLEMENTATION COMMITS TO
 * -----------------------------------
 *  - CONSTANT TIME with respect to the scalar. The Montgomery ladder does the
 *    same work for every bit and swaps with an arithmetic mask, never a
 *    branch. A branch on a secret bit is a timing oracle for the private key.
 *  - Field arithmetic in 10 limbs of 25.5 bits over int64, so a full
 *    multiply cannot overflow before reduction. No 128-bit type is required,
 *    which matters because this must build for 32-bit targets too.
 *  - The RFC 7748 clamping is applied inside x25519(), so a caller cannot
 *    forget it: clear the low 3 bits, clear bit 255, set bit 254.
 *
 * WHAT IT DOES NOT DO
 * -------------------
 *  - It does not reject the low-order points. RFC 7748 says a check for an
 *    all-zero output MAY be performed; TLS 1.3 (RFC 8446 section 7.4.2) says
 *    it MUST be. That check is exposed as x25519_shared() returning false, and
 *    the TLS layer is required to use that form rather than raw x25519().
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TLS slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_X25519_H
#define ZXV_X25519_H

#include <stdint.h>
#include <stdbool.h>

#define X25519_LEN 32u

/* Raw scalar multiplication: out = scalar * point. The scalar is clamped
 * internally per RFC 7748. Always succeeds. */
void x25519(uint8_t out[X25519_LEN],
            const uint8_t scalar[X25519_LEN],
            const uint8_t point[X25519_LEN]);

/* The public key for a private scalar: scalar * basepoint (u = 9). */
void x25519_public(uint8_t out[X25519_LEN], const uint8_t scalar[X25519_LEN]);

/* Key agreement WITH the all-zero check TLS 1.3 requires. Returns false if
 * the peer sent a low-order point, which would force a shared secret both
 * sides can predict. Use this, not x25519(), for anything security-bearing. */
bool x25519_shared(uint8_t out[X25519_LEN],
                   const uint8_t private_scalar[X25519_LEN],
                   const uint8_t peer_public[X25519_LEN]);

#endif /* ZXV_X25519_H */
