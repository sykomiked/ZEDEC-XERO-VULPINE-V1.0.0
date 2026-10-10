/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ebcdic.h — EBCDIC <-> UTF-8 for the code pages a mainframe batch feed uses:
 * CCSID 037 (US/Canada), 500 (International) and 1047 (Latin-1/Open Systems,
 * z/OS C runtime). Full 256-entry round-trip tables.
 *
 * ebcdic_to_utf8 maps each EBCDIC byte to its Unicode code point (from the
 * IBM CDRA tables) and emits UTF-8. utf8_to_ebcdic reverses it, substituting
 * a configurable byte (default EBCDIC 0x6F, '?') for code points the page
 * cannot represent. Both are bounded and report output length or overflow.
 *
 * HONEST LIMITS. Single-byte EBCDIC only — no DBCS/mixed (shift-out/shift-in)
 * code pages, no EBCDIC Arabic/Cyrillic multi-byte sets. The reverse map is
 * exact for the printable repertoire; unmappable Unicode is substituted, not
 * rejected, so a round trip through utf8_to_ebcdic is lossy only for
 * characters outside the chosen page.
 */
#ifndef ZXV_LEGACY_EBCDIC_H
#define ZXV_LEGACY_EBCDIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EBCDIC_CP037 = 0,
    EBCDIC_CP500,
    EBCDIC_CP1047,
} ebcdic_cp;

/* Convert `n` EBCDIC bytes to UTF-8 in out[cap]. Returns bytes written, or -1
 * on overflow. */
int ebcdic_to_utf8(ebcdic_cp cp, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

/* Convert UTF-8 (in_bytes) to EBCDIC in out[cap]. Code points absent from the
 * page become `sub` (pass 0x6F for '?'). Returns EBCDIC byte count, or -1 on
 * overflow or malformed UTF-8. */
int utf8_to_ebcdic(ebcdic_cp cp, const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t cap,
                   uint8_t sub);

/* Direct single-byte helpers (no UTF-8 framing): EBCDIC byte -> Unicode code
 * point, and code point -> EBCDIC byte (returns false if unmappable). */
uint16_t ebcdic_byte_to_ucp(ebcdic_cp cp, uint8_t b);
bool ucp_to_ebcdic_byte(ebcdic_cp cp, uint16_t ucp, uint8_t *out);

#endif /* ZXV_LEGACY_EBCDIC_H */
