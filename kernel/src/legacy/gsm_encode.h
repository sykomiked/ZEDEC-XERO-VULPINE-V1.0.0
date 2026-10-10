/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gsm_encode.h — GSM 03.38 7-bit default-alphabet packing and UCS-2, as used
 * by USSD and SMS over MAP (3GPP TS 23.038).
 *
 * gsm7_pack / gsm7_unpack implement the 7-bit septet packing of TS 23.038
 * section 6.1.2.1.1: septets packed LSB-first into octets. We map the
 * printable ASCII subset of the default alphabet to/from septet values; the
 * USSD menu characters a bank needs ('0'-'9','*','#','A'-'Z',' ', CR/LF, and
 * common punctuation) round-trip exactly. ucs2_* handle the 16-bit big-endian
 * alternative alphabet for text outside the GSM default set.
 *
 * Bounded: every function takes an output capacity and returns the length
 * produced or -1 if it would overflow; it never writes past the buffer.
 *
 * HONEST LIMITS. The 7-bit table covers the default alphabet's ASCII-mapped
 * positions plus the common Latin extensions; it is not the full national
 * language shift-table machinery, and it does not do the GSM extension-table
 * ESC sequences. Characters with no default-alphabet position are substituted
 * with '?' on pack. For arbitrary Unicode use UCS-2.
 */
#ifndef ZXV_LEGACY_GSM_ENCODE_H
#define ZXV_LEGACY_GSM_ENCODE_H

#include <stdint.h>

/* Pack n ASCII input bytes into GSM 7-bit septets. Returns packed octet count
 * or -1 on overflow. */
int gsm7_pack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

/* Unpack `septets` septets from packed octets. Returns character count or -1
 * on overflow. */
int gsm7_unpack(const uint8_t *in, uint32_t in_octets, uint32_t septets, uint8_t *out,
                uint32_t cap);

/* Number of packed octets needed for `septets` septets. */
uint32_t gsm7_packed_octets(uint32_t septets);

/* UCS-2 big-endian: encode ASCII/Latin-1 bytes to 16-bit code units. Returns
 * byte count written (2*n) or -1 on overflow. */
int ucs2_encode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

/* UCS-2 big-endian decode of code units in the ASCII range to bytes
 * (code points > 0xFF become '?'). Returns char count or -1. */
int ucs2_decode(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t cap);

#endif /* ZXV_LEGACY_GSM_ENCODE_H */
