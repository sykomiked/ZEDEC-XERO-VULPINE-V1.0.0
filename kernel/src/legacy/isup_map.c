/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isup_map.c — see isup_map.h. Static tables from RFC 3398. */
#include "isup_map.h"
#include "legacy_util.h"

/* Q.850 cause -> SIP status (RFC 3398 Table in 7.2.4.1, representative set). */
static const struct {
    uint8_t cause;
    uint16_t status;
} CAUSE_TO_SIP[] = {
    {1, 404},   /* unallocated number */
    {2, 404},   /* no route to specified transit network */
    {3, 404},   /* no route to destination */
    {16, 0},    /* normal clearing -> BYE/200, no status */
    {17, 486},  /* user busy */
    {18, 408},  /* no user responding */
    {19, 480},  /* no answer from user */
    {20, 480},  /* subscriber absent */
    {21, 403},  /* call rejected */
    {22, 410},  /* number changed */
    {23, 410},  /* redirection to new destination */
    {26, 404},  /* non-selected user clearing */
    {27, 502},  /* destination out of order */
    {28, 484},  /* invalid number format */
    {29, 501},  /* facility rejected */
    {31, 480},  /* normal, unspecified */
    {34, 503},  /* no circuit/channel available */
    {38, 503},  /* network out of order */
    {41, 503},  /* temporary failure */
    {42, 503},  /* switching equipment congestion */
    {47, 503},  /* resource unavailable, unspecified */
    {55, 403},  /* incoming calls barred within CUG */
    {57, 403},  /* bearer capability not authorized */
    {58, 503},  /* bearer capability not presently available */
    {65, 488},  /* bearer capability not implemented */
    {69, 501},  /* requested facility not implemented */
    {70, 488},  /* only restricted digital bearer available */
    {79, 501},  /* service or option not implemented, unspecified */
    {87, 403},  /* user not member of CUG */
    {88, 503},  /* incompatible destination */
    {102, 504}, /* recovery on timer expiry */
    {111, 500}, /* protocol error, unspecified */
    {127, 500}, /* interworking, unspecified */
};

/* SIP status -> Q.850 cause (RFC 3398 8.2.3, representative set). */
static const struct {
    uint16_t status;
    uint8_t cause;
} SIP_TO_CAUSE[] = {
    {400, 41},  {401, 21},  {402, 21},  {403, 21},  {404, 1},  {405, 63},  {406, 79}, {407, 21},
    {408, 102}, {410, 22},  {413, 127}, {414, 127}, {415, 79}, {420, 127}, {480, 18}, {481, 41},
    {483, 25},  {484, 28},  {485, 1},   {486, 17},  {488, 65}, {500, 41},  {501, 79}, {502, 38},
    {503, 41},  {504, 102}, {505, 127}, {580, 47},  {600, 17}, {603, 21},  {604, 1},  {606, 58},
};

uint16_t q850_to_sip_status(uint8_t cause)
{
    for (uint32_t i = 0; i < sizeof CAUSE_TO_SIP / sizeof CAUSE_TO_SIP[0]; i++)
        if (CAUSE_TO_SIP[i].cause == cause) return CAUSE_TO_SIP[i].status;
    return 500; /* RFC 3398 default for unmapped causes */
}

uint8_t sip_status_to_q850(uint16_t status)
{
    for (uint32_t i = 0; i < sizeof SIP_TO_CAUSE / sizeof SIP_TO_CAUSE[0]; i++)
        if (SIP_TO_CAUSE[i].status == status) return SIP_TO_CAUSE[i].cause;
    /* family defaults per RFC 3398 */
    if (status >= 400 && status < 500) return 31;
    if (status >= 500 && status < 600) return 41;
    if (status >= 600) return 31;
    return 16;
}

sip_side isup_to_sip(uint8_t isup_msg, uint8_t q850_cause)
{
    sip_side s = {SIPSIDE_NONE, 0, 0};
    switch (isup_msg) {
    case ISUP_IAM:
        s.kind = SIPSIDE_METHOD;
        s.method = "INVITE";
        break;
    case ISUP_ACM:
        s.kind = SIPSIDE_STATUS;
        s.status = 180; /* Ringing (RFC 3398: ACM -> 180/183) */
        break;
    case ISUP_CPG:
        s.kind = SIPSIDE_STATUS;
        s.status = 183; /* Session Progress */
        break;
    case ISUP_ANM:
    case ISUP_CON:
        s.kind = SIPSIDE_STATUS;
        s.status = 200; /* OK (answered) */
        break;
    case ISUP_REL:
        /* During an established call REL -> BYE; before answer it becomes the
         * SIP failure response for the cause. A gateway chooses by call state;
         * here, cause 16 (normal) maps to BYE, other causes to a status. */
        if (q850_cause == 16 || q850_cause == 0) {
            s.kind = SIPSIDE_METHOD;
            s.method = "BYE";
        } else {
            s.kind = SIPSIDE_STATUS;
            s.status = q850_to_sip_status(q850_cause);
        }
        break;
    case ISUP_RLC:
        s.kind = SIPSIDE_METHOD;
        s.method = "ACK"; /* acknowledges release completion */
        break;
    default:
        break;
    }
    return s;
}

uint8_t sip_method_to_isup(const char *method)
{
    uint32_t n = lg_strnlen(method, 16);
    if (lg_ascii_ieq(method, "INVITE", n) && n == 6) return ISUP_IAM;
    if (lg_ascii_ieq(method, "BYE", n) && n == 3) return ISUP_REL;
    if (lg_ascii_ieq(method, "ACK", n) && n == 3) return ISUP_RLC;
    return 0;
}
