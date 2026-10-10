/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gsm_encode.c — see gsm_encode.h. */
#include "gsm_encode.h"

/* Map an ASCII input byte to a GSM default-alphabet septet (0..127).
 * The USSD/SMS character set a bank uses (digits, letters, space, CR/LF and
 * common punctuation) occupies the same code points in the GSM default
 * alphabet as in ASCII, so those map identically. Anything outside becomes
 * '?' (septet 0x3F). */
static uint8_t ascii_to_septet(uint8_t c)
{
    if (c == '\n' || c == '\r') return c;
    if (c >= 0x20 && c <= 0x3F) return c; /* space, digits, punctuation */
    if (c >= 0x41 && c <= 0x5A) return c; /* A-Z */
    if (c >= 0x61 && c <= 0x7A) return c; /* a-z */
    return 0x3F;                          /* '?' */
}

static uint8_t septet_to_ascii(uint8_t s)
{
    s &= 0x7F;
    if (s == '\n' || s == '\r') return s;
    if (s >= 0x20 && s <= 0x3F) return s;
    if (s >= 0x41 && s <= 0x5A) return s;
    if (s >= 0x61 && s <= 0x7A) return s;
    return '?';
}

uint32_t gsm7_packed_octets(uint32_t septets)
{
    return (septets * 7 + 7) / 8;
}

int gsm7_pack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t need = gsm7_packed_octets(n);
    if (need > cap) return -1;
    for (uint32_t i = 0; i < need; i++) out[i] = 0;

    uint32_t bitpos = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t v = ascii_to_septet(in[i]) & 0x7F;
        uint32_t oct = bitpos >> 3;
        uint32_t off = bitpos & 7;
        out[oct] |= (uint8_t) (v << off);
        if (off > 1) /* spills into next octet (off in 2..7 => 7-(8-off) bits spill) */
            out[oct + 1] |= (uint8_t) (v >> (8 - off));
        bitpos += 7;
    }
    return (int) need;
}

int gsm7_unpack(const uint8_t *in, uint32_t in_octets, uint32_t septets, uint8_t *out, uint32_t cap)
{
    if (septets > cap) return -1;
    if (gsm7_packed_octets(septets) > in_octets) return -1;
    uint32_t bitpos = 0;
    for (uint32_t i = 0; i < septets; i++) {
        uint32_t oct = bitpos >> 3;
        uint32_t off = bitpos & 7;
        uint16_t v = (uint16_t) (in[oct] >> off);
        if (off > 1) v |= (uint16_t) ((uint16_t) in[oct + 1] << (8 - off));
        out[i] = septet_to_ascii((uint8_t) (v & 0x7F));
        bitpos += 7;
    }
    return (int) septets;
}

int ucs2_encode(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (n * 2 > cap) return -1;
    for (uint32_t i = 0; i < n; i++) {
        out[2 * i] = 0;
        out[2 * i + 1] = in[i];
    }
    return (int) (n * 2);
}

int ucs2_decode(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t cap)
{
    if (in_bytes & 1) return -1;
    uint32_t n = in_bytes / 2;
    if (n > cap) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t cp = (uint16_t) (((uint16_t) in[2 * i] << 8) | in[2 * i + 1]);
        out[i] = cp <= 0xFF ? (uint8_t) cp : (uint8_t) '?';
    }
    return (int) n;
}
