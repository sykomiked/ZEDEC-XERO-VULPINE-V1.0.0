/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ss7.h — SS7-over-SIGTRAN data model: M3UA (RFC 4666) and SCCP UDT (ITU
 * Q.713) with ITU global title. This is the transport under TCAP/MAP that a
 * USSD mobile-money menu rides on.
 *
 * M3UA: the 8-byte common message header (version, class, type, length) and
 * the Protocol Data parameter (tag 0x0210) carrying OPC/DPC/SI/NI/MP/SLS and
 * the user payload (RFC 4666 sections 1.3.1 and 3.3.1). Parse and build.
 *
 * SCCP: Unitdata (UDT, message type 0x09, Q.713 section 4) with protocol
 * class, and called/calling party addresses. Addresses carry an address
 * indicator and, when present, an ITU global title (GT indicator 0100: with
 * translation type, numbering plan, encoding scheme and nature-of-address,
 * Q.713 section 3.4) with BCD address digits.
 *
 * Bounded: fixed-size address and digit buffers, length-checked parsing that
 * never reads past the input. No pointers escape into the caller's buffer for
 * mutable fields; digits are copied into the struct.
 *
 * HONEST LIMITS. Data model only. There is no SCTP association, no M3UA ASP
 * state machine (ASPUP/ASPAC), no MTP3 routing, no point-code management and
 * no connection to any signalling network. SS7 on a real network is
 * unauthenticated and must be firewalled and GT-screened by the operator;
 * none of that is provided here. See docs/LEGACY_BRIDGE.md.
 */
#ifndef ZXV_LEGACY_SS7_H
#define ZXV_LEGACY_SS7_H

#include <stdbool.h>
#include <stdint.h>

/* ===== M3UA ===== */
#define M3UA_VERSION          1
#define M3UA_CLASS_TRANSFER   1
#define M3UA_TYPE_DATA        1
#define M3UA_PARAM_PROTO_DATA 0x0210

#define SS7_MAX_PAYLOAD 512

typedef struct {
    uint32_t opc; /* originating point code */
    uint32_t dpc; /* destination point code */
    uint8_t si;   /* service indicator (3 = SCCP) */
    uint8_t ni;   /* network indicator */
    uint8_t mp;   /* message priority */
    uint8_t sls;  /* signalling link selection */
    uint8_t payload[SS7_MAX_PAYLOAD];
    uint32_t payload_len;
} m3ua_data;

/* Parse an M3UA DATA message. Returns true on success. */
bool m3ua_parse_data(const uint8_t *buf, uint32_t len, m3ua_data *d);

/* Build an M3UA DATA message into out[cap]. Returns byte count or 0. */
uint32_t m3ua_build_data(const m3ua_data *d, uint8_t *out, uint32_t cap);

/* ===== SCCP global title (ITU, GTI 0x04) ===== */
#define SS7_MAX_GT_DIGITS 32

typedef struct {
    bool present;
    uint8_t translation_type;
    uint8_t numbering_plan;            /* 4-bit */
    uint8_t encoding_scheme;           /* 4-bit: 1=BCD odd, 2=BCD even */
    uint8_t nature_of_address;         /* 7-bit */
    uint8_t digits[SS7_MAX_GT_DIGITS]; /* ASCII '0'..'9','*','#' */
    uint32_t ndigits;
} sccp_gt;

typedef struct {
    uint8_t indicator;  /* address indicator octet */
    bool route_on_ssn;  /* bit: route on SSN vs GT */
    uint8_t ssn;        /* subsystem number, 0 if absent */
    uint8_t pc_present; /* point code present flag */
    uint16_t pc;        /* signalling point code (ITU 14-bit) */
    sccp_gt gt;
} sccp_addr;

/* ===== SCCP UDT (Q.713 4.1) ===== */
#define SCCP_MSG_UDT 0x09

typedef struct {
    uint8_t protocol_class; /* usually 0 (class 0, connectionless) */
    sccp_addr called;
    sccp_addr calling;
    uint8_t data[SS7_MAX_PAYLOAD]; /* TCAP goes here */
    uint32_t data_len;
} sccp_udt;

bool sccp_parse_udt(const uint8_t *buf, uint32_t len, sccp_udt *u);
uint32_t sccp_build_udt(const sccp_udt *u, uint8_t *out, uint32_t cap);

/* Helpers: set a global title address from an ASCII E.164-style digit string. */
void sccp_set_gt(sccp_addr *a, uint8_t ssn, uint8_t tt, uint8_t np, uint8_t nai,
                 const char *digits);

#endif /* ZXV_LEGACY_SS7_H */
