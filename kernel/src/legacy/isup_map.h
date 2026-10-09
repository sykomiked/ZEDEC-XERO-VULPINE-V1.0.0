/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isup_map.h — ISUP (ITU Q.763/Q.764) <-> SIP (RFC 3261/3398) call-setup
 * mapping, as a static table plus translation functions.
 *
 * Maps the core ISUP call-control messages — IAM, ACM, CPG, ANM, CON, REL,
 * RLC (Q.764) — onto the SIP method/response that RFC 3398 ("ISUP to SIP
 * Mapping") assigns, and the Q.850 release cause value onto the SIP status
 * code, in both directions. The reverse (SIP -> ISUP) covers the SIP status
 * families RFC 3398 section 8.2.3 tabulates.
 *
 * Pure data and arithmetic: no state, no network.
 *
 * HONEST LIMITS. This is the message/cause correspondence, not a full
 * interworking function: no overlap-vs-en-bloc dialling, no COT/continuity,
 * no SDP<->bearer (codec) negotiation, no suspend/resume, and the cause/status
 * tables are the representative subset RFC 3398 lists, not every Q.850 value.
 * It tells a gateway which SIP message a given ISUP message becomes; it does
 * not itself terminate ISUP on any trunk.
 */
#ifndef ZXV_LEGACY_ISUP_MAP_H
#define ZXV_LEGACY_ISUP_MAP_H

#include <stdbool.h>
#include <stdint.h>

/* ISUP message type codes (ITU Q.763 table 4). */
#define ISUP_IAM 0x01 /* Initial Address */
#define ISUP_ACM 0x06 /* Address Complete */
#define ISUP_CPG 0x2C /* Call Progress */
#define ISUP_ANM 0x09 /* Answer */
#define ISUP_CON 0x07 /* Connect */
#define ISUP_REL 0x0C /* Release */
#define ISUP_RLC 0x10 /* Release Complete */

/* What a SIP side looks like after translation. */
typedef enum {
    SIPSIDE_NONE = 0,
    SIPSIDE_METHOD, /* a request method, e.g. INVITE/BYE/ACK */
    SIPSIDE_STATUS, /* a response status code */
} sipside_kind;

typedef struct {
    sipside_kind kind;
    const char *method; /* valid when kind==SIPSIDE_METHOD */
    uint16_t status;    /* valid when kind==SIPSIDE_STATUS */
} sip_side;

/* Map an ISUP message type to its SIP equivalent (RFC 3398 section 7).
 * For REL, pass the Q.850 cause (0 if unknown) to select the response. */
sip_side isup_to_sip(uint8_t isup_msg, uint8_t q850_cause);

/* Map a Q.850 release cause to a SIP status code (RFC 3398 7.2.4.1). */
uint16_t q850_to_sip_status(uint8_t cause);

/* Map a SIP status code to a Q.850 release cause (RFC 3398 8.2.3). */
uint8_t sip_status_to_q850(uint16_t status);

/* Map a SIP request method to the ISUP message a gateway emits.
 * Returns 0 if the method has no ISUP call-control equivalent. */
uint8_t sip_method_to_isup(const char *method);

#endif /* ZXV_LEGACY_ISUP_MAP_H */
