/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* smic_verify.h — the post-quantum attestation wrapper for SMIC
 * provisioning payloads. An attestation datagram is bounded at
 * SMIC_ATTEST_BYTES: the 168-byte manifest envelope, the node's
 * ML-DSA-87 public key, and the signature over the envelope under the
 * domain "ZXV/SMIC/1". Verify = unpack the envelope, verify the
 * signature with the embedded key, check the manifest fields. */
#ifndef SMIC_VERIFY_H
#define SMIC_VERIFY_H

#include "smic_manifest.h"

/* Build an attestation datagram: pack `hw` into its envelope, append pk,
 * append ML-DSA-87 sig over the envelope. `out` needs SMIC_ATTEST_BYTES;
 * *out_len receives the byte count. */
int smic_attest(const smic_hw_t *hw, uint16_t type,
                const uint8_t pk[2592], const uint8_t sk[4896],
                uint8_t *out, uint32_t cap, uint32_t *out_len);

/* Verify an attestation datagram: exactly SMIC_ATTEST_BYTES, envelope
 * well-formed (checked inside unpack), ML-DSA-87 signature valid over
 * the envelope with the embedded pk. The decoded manifest lands in
 * `hw` (may be NULL when only validity is wanted). */
int smic_attest_verify(const uint8_t *in, uint32_t len, uint16_t *type, smic_hw_t *hw);

#endif /* SMIC_VERIFY_H */
