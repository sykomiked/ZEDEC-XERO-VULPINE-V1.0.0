/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_serial.h — frames on a serial line or a modem data connection.
 *
 * WIRE FORMAT
 *   0x00  COBS( payload || CRC-32(payload), little-endian )  0x00
 *   COBS is Consistent Overhead Byte Stuffing (Cheshire and Baker, IEEE/ACM
 *   ToN 1999): the encoded bytes never contain 0x00, so 0x00 marks frame
 *   boundaries and costs at most one byte in 254. CRC-32 is the IEEE 802.3
 *   / zlib polynomial (reflected 0xEDB88320, init and final XOR 0xFFFFFFFF;
 *   check value of "123456789" is 0xCBF43926).
 *
 * RULES
 *   S1  The encoder writes a leading 0x00 as well as the trailing one, so a
 *       receiver that joined mid-frame (or saw line noise) resynchronises at
 *       the start of the next frame.
 *   S2  The decoder never trusts a frame: an empty frame is ignored, a COBS
 *       code that runs past the frame, a CRC mismatch, or a frame longer than
 *       its buffer is dropped and counted (bad_cobs, bad_crc, too_long); the
 *       decoder then waits for the next 0x00. Garbage never stops it.
 *   S3  Payloads are 1 .. BR_SERIAL_MAX_PAYLOAD bytes. No encryption here:
 *       the CRC catches line noise, not attackers; devmesh frames carry
 *       their own AEAD or signatures.
 *
 * HONEST LIMITS
 *   Flow control (RTS/CTS) and line speed are set by the host
 *   (kernel/arch/hosted/bearer_host.c). Tested over a pseudo-terminal, not a
 *   real cable or modem.
 */
#ifndef ZXV_BR_SERIAL_H
#define ZXV_BR_SERIAL_H

#include "bearer.h"

#define BR_SERIAL_MAX_PAYLOAD (1u << 20) /* = br_caps(BR_SERIAL)->max_msg */
/* Worst case of br_serial_frame() for an n-byte payload. */
#define BR_SERIAL_FRAME_MAX(n) ((n) + 4u + ((n) + 4u) / 254u + 1u + 2u)

uint32_t br_crc32_update(uint32_t crc, const uint8_t *p, uint32_t n); /* start with 0 */
uint32_t br_crc32(const uint8_t *p, uint32_t n);

/* Encoded length bound: n + n/254 + 1. */
uint32_t br_cobs_max(uint32_t n);
/* COBS-encode (no delimiter). Returns bytes written or <0. */
int32_t br_cobs_encode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);
/* COBS-decode one frame without its delimiter. Returns bytes or <0
 * (BR_ERR_FORMAT on a zero byte or an overrun code). */
int32_t br_cobs_decode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

/* S1: write a whole frame. Returns bytes written or <0. */
int32_t br_serial_frame(const uint8_t *payload, uint32_t n, uint8_t *out, uint32_t cap);

typedef struct {
    uint8_t *raw; /* encoded bytes of the frame being received */
    uint32_t raw_cap, raw_n;
    uint8_t overflow;
    uint32_t frames, bad_crc, bad_cobs, too_long;
} br_serial_rx_t;

/* raw_cap must be at least BR_SERIAL_FRAME_MAX(largest payload). */
br_status_t br_serial_rx_init(br_serial_rx_t *rx, uint8_t *raw, uint32_t raw_cap);
/* Feed one byte. out is scratch for payload + CRC (cap >= payload + 4).
 * Returns 1 with a good payload in out[0..*len), 0 for nothing yet; bad
 * input is counted, never returned as an error (S2). */
int br_serial_rx_byte(br_serial_rx_t *rx, uint8_t b, uint8_t *out, uint32_t cap, uint32_t *len);

#endif /* ZXV_BR_SERIAL_H */
