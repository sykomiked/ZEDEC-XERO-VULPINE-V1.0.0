/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dial_router.h — dial strings to trunk assignments and canonical frames.
 *
 * A dial string names an endpoint. This module parses it, picks one of the
 * ten trunks of zt_trunk_bank.h from the subscriber number, derives a
 * deterministic virtual address from it, and packs all of that into the
 * canonical 21-octet frame of zt_harmonic_wire.h (zt_dial_frame_t is that
 * type, not a second layout).
 *
 *   D1  SYNTAX.  "101-" "-" CC NATIONAL, e.g. "101--14155550199": the escape
 *       "101-", a minus sign, the E.164 country code, the national number.
 *       Only ASCII digits follow the minus; no spaces, no '+'. The parser
 *       reads at most ZT_DIAL_MAX_CHARS bytes and never past the first NUL;
 *       a string with no NUL in that window is rejected.
 *   D2  COUNTRY CODE.  E.164 country codes are prefix-free, so the code is
 *       the unique 1-, 2- or 3-digit prefix found in the generated table
 *       zt_e164_codes.h (source cited in gen_e164_codes.py). CC + national
 *       number is at most 15 digits (E.164 limit).
 *   D3  NATIONAL SPLIT.  CC 1 (NANP): exactly 10 digits, NPA (3-digit area
 *       code, first digit 2-9) then a 7-digit subscriber number whose first
 *       digit is 2-9. Other codes: national numbers have no single structure
 *       (UK area codes are 2-5 digits, Germany 2-5, Italy keeps a leading 0),
 *       so the split is a FIXED APPROXIMATE RULE, not the real numbering
 *       plan: with L national digits (L >= 7), routing_prefix is the first
 *       p = max(L - 12, min(3, L - 7)) digits and the subscriber number is
 *       the remaining 7..12. The prefix is not the true area code in general.
 *   D4  TRUNK.  assigned_trunk = subscriber_id mod 10 (zt_udiv64, no 64-bit
 *       division), carrier_freq_hz = ZT_TRUNK_FREQS[trunk] (111 .. 999, 1111),
 *       is_fleet_line = trunk == 9. zt_dial_line_ready checks that trunk on a
 *       zt_trunk_bank demux: ON_HOOK (bare carrier) = free.
 *   D5  VIRTUAL COORDINATES.  zt_dial_derive_coordinates maps the number to
 *       a socket, a WGS84 point or a sky position. THESE ARE DETERMINISTIC
 *       VIRTUAL ADDRESSES computed by hashing digits. They are NOT where the
 *       subscriber is, NOT a geolocation, NOT an orbit, and the socket
 *       address MUST NEVER be used as a real IP endpoint: it is always in
 *       240.0.0.0/4 (reserved by RFC 1112, RFC 6890; not routable) so that it
 *       cannot collide with a real host, and its port is in 49152-65535.
 *       All three come from a 64-bit mix (SplitMix64 finaliser) of country
 *       code, prefix and subscriber, so short numbers do not all land at one
 *       pole (the spec's raw shifts did: a 7-digit subscriber >> 16 is < 153).
 *       Units (the spec's "_q16" names were not Q16.16):
 *         lat_e4deg   1e-4 degree, -900000 .. 900000
 *         lon_e4deg   1e-4 degree, -1800000 .. 1799999
 *         ra_mdeg     1e-3 degree, 0 .. 359999   (right ascension)
 *         dec_mdeg    1e-3 degree, -90000 .. 90000
 *       alt_m and range_km are always 0 and are not carried in the frame.
 *   D6  FRAME (canonical layout; S+ = W0 W2 W4 little-endian, S- = W1 W3
 *       big-endian on the wire):
 *         tag  = trunk (shell 0, no sync), so octet 0 is the trunk number
 *         W0   subscriber bits 0-31
 *         W1   bits 0-9 routing_prefix (0..999), 10-11 prefix digit count,
 *              12-15 subscriber digit count, 16-23 zero, 24-31 subscriber
 *              bits 32-39 (subscriber < 10^12 < 2^40)
 *         W2   SOCKET ip | WGS84 lat_e4deg | ORBITAL ra_mdeg | NONE 0
 *         W3   SOCKET port | WGS84 lon_e4deg | ORBITAL dec_mdeg | NONE 0
 *              (from the union member that matches coord->type)
 *         W4   bits 0-9 |country code|, 10-11 zt_resolve_type_t, 12-31 the
 *              top 20 bits of CRC-32 (IEEE 802.3, reflected 0xEDB88320) over
 *              octets 0..16 of the serialised frame followed by W4's low 12
 *              bits as two little-endian bytes.
 *       A 20-bit check misses a random corruption with probability 2^-20; it
 *       is an error check, not authentication.
 *   D7  RENDEZVOUS.  zt_dial_resolve.h hands a parsed number to the call
 *       engine's signed DHT lookup (kernel/src/call/call_ice.h).
 */
#ifndef ZT_DIAL_ROUTER_H
#define ZT_DIAL_ROUTER_H

#include "zt_harmonic_wire.h"
#include "zt_trunk_bank.h"

#define ZT_PREFIX_STR        "101-"
#define ZT_DIAL_MAX_CHARS    32u /* bytes examined, NUL included */
#define ZT_MAX_DIAL_DIGITS   15u /* E.164: country code + national number */
#define ZT_TRUNK_COUNT       ZT_NUM_TRUNK_LINES
#define ZT_DIAL_SUB_MIN      7u
#define ZT_DIAL_SUB_MAX      12u
#define ZT_DIAL_VIRT_NET     0xF0000000u /* 240.0.0.0/4 */
#define ZT_DIAL_VIRT_PORT_LO 49152u

/* The carrier table is the trunk bank's: ZT_TRUNK_FREQS (zt_trunk_bank.h). */
#define ZT_CARRIER_FREQS ZT_TRUNK_FREQS

typedef enum {
    ZT_RESOLVE_NONE = 0,
    ZT_RESOLVE_SOCKET = 1,
    ZT_RESOLVE_WGS84 = 2,
    ZT_RESOLVE_ORBITAL = 3
} zt_resolve_type_t;

typedef struct {
    int16_t country_code;      /* negative, as dialled: -1 for NANP, -44 UK */
    uint32_t routing_prefix;   /* D3: NANP area code, else approximate split */
    uint64_t subscriber_id;    /* 7 .. 12 digits */
    uint8_t assigned_trunk;    /* 0 .. 9 */
    uint16_t carrier_freq_hz;  /* 111 .. 999, 1111 */
    bool is_fleet_line;        /* trunk 9 */
    uint8_t prefix_digits;     /* 0 .. 3 (keeps leading zeros); added to the spec */
    uint8_t subscriber_digits; /* 7 .. 12 (keeps leading zeros); added to the spec */
} zt_phone_descriptor_t;

/* D5: virtual addresses only. Never a real location or a real IP. */
typedef struct {
    zt_resolve_type_t type;
    union {
        struct {
            uint32_t ip; /* virtual, always in 240.0.0.0/4: NEVER connect to it */
            uint16_t port;
        } socket;
        struct {
            int32_t lat_e4deg; /* 1e-4 degree (spec: lat_q16) */
            int32_t lon_e4deg; /* 1e-4 degree (spec: lon_q16) */
            int32_t alt_m;     /* always 0 */
        } geo;
        struct {
            uint32_t ra_mdeg;  /* 1e-3 degree (spec: ra_q16) */
            int32_t dec_mdeg;  /* 1e-3 degree (spec: dec_q16) */
            uint32_t range_km; /* always 0 */
        } orbital;
    } point;
} zt_projected_coord_t;

/* The canonical frame, not a second layout. */
typedef zt_ubh168_wire_frame_t zt_dial_frame_t;

/* D1-D4. Returns false (and leaves *out_desc zeroed) on any syntax error. */
bool zt_dial_parse_number(const char *dial_str, zt_phone_descriptor_t *out_desc);

/* D5. An unknown domain gives ZT_RESOLVE_NONE with a zero point. */
void zt_dial_derive_coordinates(const zt_phone_descriptor_t *desc, zt_resolve_type_t target_domain,
                                zt_projected_coord_t *out_coord);

/* D6. coord may be NULL (type NONE). */
void zt_dial_pack_frame(const zt_phone_descriptor_t *desc, const zt_projected_coord_t *coord,
                        zt_dial_frame_t *out_frame);

/* D6 inverse. False if the tag, the check, the country code, the digit
 * counts, the trunk or the coordinate words (re-derived) do not agree. */
bool zt_dial_unpack_frame(const zt_dial_frame_t *frame, zt_phone_descriptor_t *out_desc,
                          zt_projected_coord_t *out_coord);

/* Writes "101--CCNATIONAL" (NUL-terminated); returns its length, or 0 if
 * cap is too small or desc is invalid. */
size_t zt_dial_format(const zt_phone_descriptor_t *desc, char *out, size_t cap);

/* True if code (positive) is an assigned E.164 country code. */
bool zt_dial_is_country_code(uint32_t code);

/* D4: the descriptor's trunk is ON_HOOK (bare carrier present) on bank. */
bool zt_dial_line_ready(const zt_trunk_demux_bank_t *bank, const zt_phone_descriptor_t *desc);

/* CRC-32 (IEEE 802.3, reflected), shared with tests. crc starts at 0. */
uint32_t zt_dial_crc32(uint32_t crc, const uint8_t *p, size_t n);

#endif /* ZT_DIAL_ROUTER_H */
