/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_serial.c — CRC-32, COBS and the serial frame decoder (br_serial.h). */
#include "br_serial.h"

uint32_t br_crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t br_crc32(const uint8_t *p, uint32_t n)
{
    return br_crc32_update(0, p, n);
}

uint32_t br_cobs_max(uint32_t n)
{
    return n + n / 254u + 1u;
}

int32_t br_cobs_encode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if ((!in && n) || !out || cap < br_cobs_max(n) || n > 0x7FFFFF00u) return BR_ERR_ARG;
    uint32_t o = 1, codep = 0;
    uint8_t code = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = in[i];
        if (b) {
            out[o++] = b;
            code++;
        }
        if (!b || code == 0xFFu) {
            out[codep] = code;
            code = 1;
            codep = o;
            if (!b || i + 1u < n) o++;
        }
    }
    out[codep] = code;
    return (int32_t) o;
}

int32_t br_cobs_decode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if ((!in && n) || !out || n > 0x7FFFFF00u) return BR_ERR_ARG;
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint8_t code = in[i++];
        if (code == 0) return BR_ERR_FORMAT;
        for (uint32_t k = 1; k < code; k++) {
            if (i >= n) return BR_ERR_FORMAT;
            uint8_t b = in[i++];
            if (b == 0) return BR_ERR_FORMAT;
            if (o >= cap) return BR_ERR_SIZE;
            out[o++] = b;
        }
        if (code != 0xFFu && i < n) {
            if (o >= cap) return BR_ERR_SIZE;
            out[o++] = 0;
        }
    }
    return (int32_t) o;
}

int32_t br_serial_frame(const uint8_t *payload, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (!payload || !out || n == 0 || n > BR_SERIAL_MAX_PAYLOAD) return BR_ERR_ARG;
    if (cap < BR_SERIAL_FRAME_MAX(n)) return BR_ERR_SIZE;
    /* The same encoder as br_cobs_encode(), run over payload || crc
     * without copying the payload. */
    uint32_t crc = br_crc32(payload, n);
    uint8_t c4[4] = {(uint8_t) crc, (uint8_t) (crc >> 8), (uint8_t) (crc >> 16),
                     (uint8_t) (crc >> 24)};
    uint32_t total = n + 4u, o = 2, codep = 1;
    uint8_t code = 1;
    out[0] = 0;
    for (uint32_t i = 0; i < total; i++) {
        uint8_t b = i < n ? payload[i] : c4[i - n];
        if (b) {
            out[o++] = b;
            code++;
        }
        if (!b || code == 0xFFu) {
            out[codep] = code;
            code = 1;
            codep = o;
            if (!b || i + 1u < total) o++;
        }
    }
    out[codep] = code;
    out[o++] = 0;
    return (int32_t) o;
}

br_status_t br_serial_rx_init(br_serial_rx_t *rx, uint8_t *raw, uint32_t raw_cap)
{
    if (!rx || !raw || raw_cap < 8u) return BR_ERR_ARG;
    br_mset(rx, 0, sizeof *rx);
    rx->raw = raw;
    rx->raw_cap = raw_cap;
    return BR_OK;
}

int br_serial_rx_byte(br_serial_rx_t *rx, uint8_t b, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if (!rx || !rx->raw || !out || !len) return BR_ERR_ARG;
    if (b != 0) {
        if (rx->raw_n >= rx->raw_cap) {
            rx->overflow = 1;
            return 0;
        }
        rx->raw[rx->raw_n++] = b;
        return 0;
    }
    uint32_t n = rx->raw_n;
    uint8_t ovf = rx->overflow;
    rx->raw_n = 0;
    rx->overflow = 0;
    if (ovf) {
        rx->too_long++;
        return 0;
    }
    if (n == 0) return 0; /* back-to-back delimiters */
    int32_t d = br_cobs_decode(rx->raw, n, out, cap);
    if (d == BR_ERR_SIZE) {
        rx->too_long++;
        return 0;
    }
    if (d < 5) { /* bad COBS, or too short to hold a payload byte and a CRC */
        rx->bad_cobs++;
        return 0;
    }
    uint32_t pl = (uint32_t) d - 4u;
    uint32_t got = (uint32_t) out[pl] | ((uint32_t) out[pl + 1] << 8) |
                   ((uint32_t) out[pl + 2] << 16) | ((uint32_t) out[pl + 3] << 24);
    if (got != br_crc32(out, pl)) {
        rx->bad_crc++;
        return 0;
    }
    rx->frames++;
    *len = pl;
    return 1;
}
