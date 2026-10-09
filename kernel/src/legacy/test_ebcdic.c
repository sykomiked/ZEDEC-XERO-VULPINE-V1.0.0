/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ebcdic.c — EBCDIC <-> UTF-8 round trips for CCSID 037/500/1047.
 * Host test (stdio). */
#include <stdio.h>
#include <string.h>
#include "ebcdic.h"

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

static void test_known_bytes(void)
{
    /* EBCDIC 037: 'A'=0xC1, 'Z'=0xE9, '0'=0xF0, '9'=0xF9, space=0x40, '$'=0x5B. */
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0xC1) == 'A');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0xE9) == 'Z');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0xF0) == '0');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0xF9) == '9');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0x40) == ' ');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0x5B) == '$');
    /* cp500: '[' is 0x4A, ']' is 0x5A (differs from 037 where 0x4A is cent) */
    OK(ebcdic_byte_to_ucp(EBCDIC_CP500, 0x4A) == '[');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP500, 0x5A) == ']');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP037, 0x4A) == 0x00A2); /* cent sign in 037 */
    /* cp1047: '[' is 0xAD, ']' is 0xBD */
    OK(ebcdic_byte_to_ucp(EBCDIC_CP1047, 0xAD) == '[');
    OK(ebcdic_byte_to_ucp(EBCDIC_CP1047, 0xBD) == ']');
}

static void test_roundtrip_all(ebcdic_cp cp, const char *name)
{
    /* every EBCDIC byte -> UTF-8 -> back must be identity (bytes that map to a
     * code point another byte also maps to could differ; verify via code
     * point identity instead) */
    for (uint32_t b = 0; b < 256; b++) {
        uint16_t ucp = ebcdic_byte_to_ucp(cp, b);
        if (ucp == 0xFFFD) continue; /* undefined slot */
        uint8_t back;
        bool ok = ucp_to_ebcdic_byte(cp, ucp, &back);
        /* code point must map back to a byte with the same code point */
        CHECK(ok && ebcdic_byte_to_ucp(cp, back) == ucp, name);
    }
}

static void test_utf8_roundtrip(void)
{
    const char *txt = "PAY 500 KES TO 254700000000"; /* a mobile-money memo */
    uint32_t n = (uint32_t) strlen(txt);
    for (int c = 0; c < 3; c++) {
        ebcdic_cp cp = (ebcdic_cp) c;
        uint8_t eb[64];
        int en = utf8_to_ebcdic(cp, (const uint8_t *) txt, n, eb, sizeof eb, 0x6F);
        OK(en == (int) n);
        uint8_t u8[128];
        int un = ebcdic_to_utf8(cp, eb, (uint32_t) en, u8, sizeof u8);
        OK(un == (int) n && memcmp(u8, txt, n) == 0);
    }
}

static void test_latin1(void)
{
    /* U+00E9 (e-acute) exists in all three pages; round-trip through UTF-8 */
    const uint8_t eacute_utf8[] = {0xC3, 0xA9}; /* é */
    for (int c = 0; c < 3; c++) {
        ebcdic_cp cp = (ebcdic_cp) c;
        uint8_t eb[4];
        int en = utf8_to_ebcdic(cp, eacute_utf8, 2, eb, sizeof eb, 0x6F);
        OK(en == 1);
        uint8_t u8[8];
        int un = ebcdic_to_utf8(cp, eb, 1, u8, sizeof u8);
        OK(un == 2 && memcmp(u8, eacute_utf8, 2) == 0);
    }
}

static void test_bounds(void)
{
    const char *txt = "HELLO";
    uint8_t eb[2];
    OK(utf8_to_ebcdic(EBCDIC_CP037, (const uint8_t *) txt, 5, eb, sizeof eb, 0x6F) == -1);
    uint8_t src[] = {0xC8, 0xC5}; /* 'HE' in EBCDIC */
    uint8_t u8[1];
    OK(ebcdic_to_utf8(EBCDIC_CP037, src, 2, u8, sizeof u8) == -1);
    /* an unmappable 4-byte UTF-8 scalar is substituted, not an error */
    const uint8_t emoji[] = {0xF0, 0x9F, 0x98, 0x80}; /* U+1F600 */
    uint8_t one[4];
    int en = utf8_to_ebcdic(EBCDIC_CP037, emoji, 4, one, sizeof one, 0x6F);
    OK(en == 1 && one[0] == 0x6F);
    /* malformed UTF-8 rejected */
    const uint8_t bad[] = {0xC3, 0x28};
    OK(utf8_to_ebcdic(EBCDIC_CP037, bad, 2, one, sizeof one, 0x6F) == -1);
}

int main(void)
{
    test_known_bytes();
    test_roundtrip_all(EBCDIC_CP037, "roundtrip 037");
    test_roundtrip_all(EBCDIC_CP500, "roundtrip 500");
    test_roundtrip_all(EBCDIC_CP1047, "roundtrip 1047");
    test_utf8_roundtrip();
    test_latin1();
    test_bounds();
    printf("EBCDIC: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
