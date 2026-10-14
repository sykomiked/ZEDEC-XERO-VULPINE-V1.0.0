/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ax25.h — AX.25 UI frames (amateur packet radio), as used by the
 * HAM_SDR bearer for signed cleartext emergency messages (br_ham.h).
 *
 * FRAME (AX.25 v2.2, TAPR 2017, sections 3 and 4.3.3.6)
 *   [flag 0x7E] dest(7) src(7) [digi(7) x 0..2] control pid info fcs(2) [flag]
 *   address octet  = ASCII << 1 (upper-case letters and digits, space
 *                    padded to 6); the 7th octet is 0b CRRSSID E: C is the
 *                    command/response bit (dest C=1, src C=0 for a command)
 *                    or the has-been-repeated bit on a digipeater, RR are
 *                    reserved and set to 1, E (bit 0) is 1 only on the last
 *                    address of the field.
 *   control 0x03   UI frame, P/F = 0 (0x13 with P/F = 1 is accepted).
 *   pid 0xF0       no layer 3.
 *   info           0..256 octets (N1 default).
 *   fcs            CRC-16/X.25 (ISO/IEC 13239 / RFC 1662 FCS-16: reflected
 *                  polynomial 0x8408, init 0xFFFF, final XOR 0xFFFF), low
 *                  octet first. Check value of "123456789" is 0x906E; the
 *                  CRC of a good frame including its FCS is 0x0F47 (the
 *                  complement of RFC 1662's PPPGOODFCS16 0xF0B8).
 *
 * BIT STUFFING (optional, for hosts that drive a raw modem / SDR)
 *   br_hdlc_stuff() writes flag + frame + flag as a bit string, each octet
 *   least significant bit first, a 0 inserted after five 1s in the frame.
 *   br_hdlc_unstuff() reverses it: finds the first flag, removes stuffed
 *   zeros, stops at the next flag, refuses an abort (seven 1s) or a bit
 *   count that is not a whole number of octets. Hosts that use a KISS TNC
 *   skip both: the TNC does flags, stuffing and (often) the FCS.
 *
 * RULES
 *   X1  Call signs: 1..6 characters A-Z / 0-9 (lower case is upper-cased),
 *       optional "-SSID" 0..15. br_ax25_is_station() additionally requires
 *       at least one letter and one digit, the shape of an amateur call
 *       sign; the HAM bearer requires it for the source.
 *   X2  The decoder checks the FCS, every extension bit, the reserved bits'
 *       absence from call characters, control and PID, and refuses more
 *       than two digipeaters or an info field over 256 octets.
 *
 * HONEST LIMITS
 *   Encoder/decoder only: no AFSK/GFSK modem, no SDR driver, no KISS. Not
 *   tested on air. The spec's own Figure 3.4 prints 0x98 for 'N'; the
 *   correct value ('N' = 0x4E shifted left) is 0x9C, which is what this
 *   code produces and the test checks.
 */
#ifndef ZXV_BR_AX25_H
#define ZXV_BR_AX25_H

#include "bearer.h"

#define BR_AX25_INFO_MAX  256u
#define BR_AX25_MAX_DIGI  2u
#define BR_AX25_ADDR_LEN  7u
#define BR_AX25_CTRL_UI   0x03u
#define BR_AX25_PID_NONE  0xF0u
#define BR_AX25_FRAME_MAX (BR_AX25_ADDR_LEN * (2u + BR_AX25_MAX_DIGI) + 2u + BR_AX25_INFO_MAX + 2u)
#define BR_AX25_FLAG      0x7Eu

typedef struct {
    char call[7]; /* NUL-terminated, no padding */
    uint8_t ssid;
} br_ax25_addr_t;

uint16_t br_crc16_x25(const uint8_t *p, uint32_t n);

br_status_t br_ax25_addr_parse(const char *s, br_ax25_addr_t *a);
bool br_ax25_is_station(const br_ax25_addr_t *a);
/* "CALL" or "CALL-n"; returns length (<= 9) or <0. out needs 10 bytes. */
int32_t br_ax25_addr_format(const br_ax25_addr_t *a, char out[10]);
br_status_t br_ax25_addr_encode(const br_ax25_addr_t *a, bool cbit, bool last,
                                uint8_t out[BR_AX25_ADDR_LEN]);

typedef struct {
    br_ax25_addr_t dst, src;
    br_ax25_addr_t digi[BR_AX25_MAX_DIGI];
    uint8_t digi_h[BR_AX25_MAX_DIGI];
    uint32_t ndigi;
    uint8_t command; /* dest C = 1 and src C = 0 */
    uint8_t pf;
    uint8_t pid;
    const uint8_t *info;
    uint32_t info_len;
} br_ax25_ui_t;

/* Encode a UI command frame (no flags, FCS appended). Returns length. */
int32_t br_ax25_ui_encode(const br_ax25_addr_t *dst, const br_ax25_addr_t *src,
                          const br_ax25_addr_t *digis, uint32_t ndigi, uint8_t pid,
                          const uint8_t *info, uint32_t n, uint8_t *out, uint32_t cap);
/* Decode and check a frame (no flags, FCS included). ui->info points into f. */
br_status_t br_ax25_ui_decode(const uint8_t *f, uint32_t n, br_ax25_ui_t *ui);

/* Bit-level HDLC framing; bits are packed LSB first into bytes. */
int32_t br_hdlc_stuff(const uint8_t *frame, uint32_t n, uint8_t *bits, uint32_t cap_bytes,
                      uint32_t *nbits);
int32_t br_hdlc_unstuff(const uint8_t *bits, uint32_t nbits, uint8_t *out, uint32_t cap);

#endif /* ZXV_BR_AX25_H */
