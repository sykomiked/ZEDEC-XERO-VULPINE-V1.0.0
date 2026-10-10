/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_isup_map.c — ISUP<->SIP mapping (RFC 3398). Host test (stdio). */
#include <stdio.h>
#include <string.h>
#include "isup_map.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)
#define OK(c) CHECK(c, #c)

int main(void)
{
    sip_side s;
    s = isup_to_sip(ISUP_IAM, 0);
    OK(s.kind == SIPSIDE_METHOD && strcmp(s.method, "INVITE") == 0);
    s = isup_to_sip(ISUP_ACM, 0);
    OK(s.kind == SIPSIDE_STATUS && s.status == 180);
    s = isup_to_sip(ISUP_CPG, 0);
    OK(s.kind == SIPSIDE_STATUS && s.status == 183);
    s = isup_to_sip(ISUP_ANM, 0);
    OK(s.kind == SIPSIDE_STATUS && s.status == 200);
    s = isup_to_sip(ISUP_CON, 0);
    OK(s.kind == SIPSIDE_STATUS && s.status == 200);
    s = isup_to_sip(ISUP_REL, 16); /* normal clearing -> BYE */
    OK(s.kind == SIPSIDE_METHOD && strcmp(s.method, "BYE") == 0);
    s = isup_to_sip(ISUP_REL, 17); /* user busy -> 486 */
    OK(s.kind == SIPSIDE_STATUS && s.status == 486);
    s = isup_to_sip(ISUP_RLC, 0);
    OK(s.kind == SIPSIDE_METHOD && strcmp(s.method, "ACK") == 0);

    /* Q.850 -> SIP, published points from RFC 3398 */
    OK(q850_to_sip_status(1) == 404);
    OK(q850_to_sip_status(17) == 486);
    OK(q850_to_sip_status(18) == 408);
    OK(q850_to_sip_status(19) == 480);
    OK(q850_to_sip_status(21) == 403);
    OK(q850_to_sip_status(22) == 410);
    OK(q850_to_sip_status(27) == 502);
    OK(q850_to_sip_status(28) == 484);
    OK(q850_to_sip_status(34) == 503);
    OK(q850_to_sip_status(38) == 503);
    OK(q850_to_sip_status(102) == 504);
    OK(q850_to_sip_status(200) == 500); /* unmapped -> default 500 */

    /* SIP -> Q.850 */
    OK(sip_status_to_q850(404) == 1);
    OK(sip_status_to_q850(486) == 17);
    OK(sip_status_to_q850(408) == 102);
    OK(sip_status_to_q850(410) == 22);
    OK(sip_status_to_q850(484) == 28);
    OK(sip_status_to_q850(503) == 41);
    OK(sip_status_to_q850(600) == 17);
    OK(sip_status_to_q850(499) == 31); /* 4xx family default */
    OK(sip_status_to_q850(599) == 41); /* 5xx family default */

    /* method -> ISUP */
    OK(sip_method_to_isup("INVITE") == ISUP_IAM);
    OK(sip_method_to_isup("BYE") == ISUP_REL);
    OK(sip_method_to_isup("ACK") == ISUP_RLC);
    OK(sip_method_to_isup("MESSAGE") == 0);

    printf("ISUP<->SIP: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
