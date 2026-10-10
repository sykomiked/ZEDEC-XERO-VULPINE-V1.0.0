/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tcap.h — TCAP (ITU Q.773) Begin/Continue/End over BER, and enough MAP
 * (3GPP TS 29.002) to carry a USSD menu and an SMS MO/MT forward.
 *
 * TCAP message tags (Q.773): Begin 0x62, End 0x64, Continue 0x65, Abort 0x67.
 * Transaction IDs are OCTET STRING: Originating 0x48, Destination 0x49. The
 * component portion (0x6C) carries one or more components; we model the
 * Invoke (0xA1) with its invokeID (INTEGER), operationCode (local INTEGER)
 * and argument.
 *
 * MAP operations modelled (TS 29.002): processUnstructuredSS-Request (59) and
 * unstructuredSS-Request (60) carrying USSD-Arg { dataCodingScheme, ussd-String }
 * — this is the USSD mobile-money menu — and mo-forwardSM (46) / mt-forwardSM
 * (44) carrying the SM-RP-UI (the SMS TPDU) as an OCTET STRING.
 *
 * All BER here is the definite-length low-tag-number subset these structures
 * use. Parsing is length-checked against the buffer end at every step and
 * never reads past it; building reports overflow rather than overrunning.
 *
 * HONEST LIMITS. This is the component/argument carriage, not the full MAP
 * dialogue: no TC-User state machine, no dialogue portion (AARQ/AARE), no
 * SM-RP-DA/OA address modelling beyond carrying the SM-RP-UI, and no operation
 * timers. It is what is needed to frame a USSD request and an SMS forward, no
 * more. Nothing here connects to a real HLR/MSC.
 */
#ifndef ZXV_LEGACY_TCAP_H
#define ZXV_LEGACY_TCAP_H

#include <stdbool.h>
#include <stdint.h>

#define TCAP_TAG_BEGIN    0x62
#define TCAP_TAG_END      0x64
#define TCAP_TAG_CONTINUE 0x65
#define TCAP_TAG_ABORT    0x67
#define TCAP_TAG_OTID     0x48
#define TCAP_TAG_DTID     0x49
#define TCAP_TAG_COMP     0x6C
#define TCAP_TAG_INVOKE   0xA1

#define MAP_OP_MT_FORWARD_SM   44
#define MAP_OP_MO_FORWARD_SM   46
#define MAP_OP_PROCESS_USS_REQ 59
#define MAP_OP_USS_REQ         60

typedef enum {
    TCAP_BEGIN,
    TCAP_CONTINUE,
    TCAP_END,
    TCAP_ABORT,
} tcap_type;

#define TCAP_MAX_COMP 256

typedef struct {
    tcap_type type;
    bool has_otid;
    uint32_t otid;
    bool has_dtid;
    uint32_t dtid;
    /* component portion contents (inside the 0x6C wrapper), copied out */
    uint8_t comp[TCAP_MAX_COMP];
    uint32_t comp_len;
} tcap_msg;

bool tcap_parse(const uint8_t *buf, uint32_t len, tcap_msg *m);
uint32_t tcap_build(const tcap_msg *m, uint8_t *out, uint32_t cap);

/* ===== MAP Invoke within a component portion ===== */
typedef struct {
    uint8_t invoke_id;
    uint32_t opcode; /* MAP local operation code */
    uint8_t arg[256];
    uint32_t arg_len;
} map_invoke;

/* Build a component portion holding a single Invoke. */
uint32_t map_build_invoke(const map_invoke *iv, uint8_t *out, uint32_t cap);
/* Parse the first Invoke from a component portion. */
bool map_parse_invoke(const uint8_t *comp, uint32_t len, map_invoke *iv);

/* ===== USSD-Arg (TS 29.002) ===== */
/* Build SEQUENCE { dcs OCTET STRING(1), ussd-String OCTET STRING }. */
uint32_t map_build_ussd_arg(uint8_t dcs, const uint8_t *str, uint32_t nstr, uint8_t *out,
                            uint32_t cap);
bool map_parse_ussd_arg(const uint8_t *arg, uint32_t len, uint8_t *dcs, uint8_t *str, uint32_t cap,
                        uint32_t *nstr);

/* ===== forwardSM arg (SM-RP-UI carriage) ===== */
uint32_t map_build_forwardsm_arg(const uint8_t *tpdu, uint32_t n, uint8_t *out, uint32_t cap);
bool map_parse_forwardsm_arg(const uint8_t *arg, uint32_t len, uint8_t *tpdu, uint32_t cap,
                             uint32_t *n);

#endif /* ZXV_LEGACY_TCAP_H */
