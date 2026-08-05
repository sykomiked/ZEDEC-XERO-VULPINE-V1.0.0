/* mlkem768.h — Top-level ML-KEM-768 (FIPS 203 Algorithms 19-21)
 *
 * Wraps K-PKE with the Fujisaki-Okamoto transform for IND-CCA2
 * security: Encaps binds the shared secret to the encapsulation key
 * via hashing (G = SHA3-512, H = SHA3-256); Decaps re-encrypts and
 * compares ciphertexts, falling back to a pseudorandom "implicit
 * rejection" secret (derived from a private seed z) on mismatch --
 * critical so a decapsulation failure is never distinguishable from
 * success by a network attacker (no decryption oracle).
 *
 * Sizes (independently matching the well-known published ML-KEM-768
 * parameter sizes): EK=1184B, DK=2400B, CT=1088B, shared secret=32B.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef MLKEM768_H
#define MLKEM768_H

#include "mlkem_kpe.h"
#include <stdint.h>

#define MLKEM768_EK_BYTES KPE_EK_BYTES                              /* 1184 */
#define MLKEM768_DK_BYTES (KPE_DK_BYTES + KPE_EK_BYTES + 32 + 32)   /* 2400 */
#define MLKEM768_CT_BYTES KPE_CT_BYTES                               /* 1088 */
#define MLKEM768_SS_BYTES 32

/* d, z: 32 bytes each of fresh randomness (never reused). */
void mlkem768_keygen(const uint8_t d[32], const uint8_t z[32],
                      uint8_t ek[MLKEM768_EK_BYTES],
                      uint8_t dk[MLKEM768_DK_BYTES]);

/* m: 32 bytes fresh randomness. Outputs ciphertext c and the 32-byte
 * shared secret ss (both parties end up with the same ss). */
void mlkem768_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t m[32],
                      uint8_t c[MLKEM768_CT_BYTES], uint8_t ss[MLKEM768_SS_BYTES]);

/* Recovers the shared secret from a ciphertext using the decapsulation
 * key. Always succeeds (never returns an error code) by design -- see
 * implicit rejection above; the caller cannot distinguish a tampered
 * ciphertext from a valid one via this function's return behavior. */
void mlkem768_decaps(const uint8_t dk[MLKEM768_DK_BYTES], const uint8_t c[MLKEM768_CT_BYTES],
                      uint8_t ss[MLKEM768_SS_BYTES]);

#endif /* MLKEM768_H */
