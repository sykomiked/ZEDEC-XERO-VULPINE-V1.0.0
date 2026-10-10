/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_abi.c — exercises the stable C ABI the language bindings call. */
#include <stdio.h>
#include <string.h>
#include "zx_legacy_api.h"

static int pass = 0, fail = 0;
#define OK(c)                                                                                      \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", #c, __LINE__);                                         \
        }                                                                                          \
    } while (0)

int main(void)
{
    OK(zx_legacy_abi_version() == ZX_ABI_VERSION);

    /* COMP-3 round trip */
    uint8_t p[3];
    OK(zx_comp3_encode(12345, p, 3) == ZX_OK);
    int64_t v;
    OK(zx_comp3_decode(p, 3, &v) == ZX_OK && v == 12345);

    /* EBCDIC round trip */
    uint8_t eb[16], u8[32];
    int32_t en = zx_utf8_to_ebcdic(ZX_CP037, (const uint8_t *) "PAY 500", 7, eb, sizeof eb, 0x6F);
    OK(en == 7);
    int32_t un = zx_ebcdic_to_utf8(ZX_CP037, eb, en, u8, sizeof u8);
    OK(un == 7 && memcmp(u8, "PAY 500", 7) == 0);

    /* HFP */
    OK(zx_hfp64_to_ieee64(0x4110000000000000ULL) == 0x3FF0000000000000ULL);
    OK(zx_ieee64_to_hfp64(0x3FF0000000000000ULL) == 0x4110000000000000ULL);

    /* USSD build/parse */
    uint8_t comp[128];
    int32_t cn = zx_ussd_build_request(1, (const uint8_t *) "*182*1*500#", 11, comp, sizeof comp);
    OK(cn > 0);
    uint8_t txt[64];
    int32_t tn;
    OK(zx_ussd_parse_request(comp, cn, txt, sizeof txt, &tn) == ZX_OK);
    OK(tn >= 11 && memcmp(txt, "*182*1*500#", 11) == 0);

    /* DTMF generate + detect */
    int16_t pcm[205];
    uint32_t pr = 0, pc = 0;
    OK(zx_dtmf_generate('5', pcm, 205, 12000, &pr, &pc) == ZX_OK);
    OK(zx_dtmf_detect(pcm) == '5');

    /* ISUP/SIP */
    OK(zx_q850_to_sip_status(17) == 486);
    OK(zx_sip_status_to_q850(486) == 17);

    printf("ABI: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
