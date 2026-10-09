/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cobol.c — COBOL numeric codecs, copybook parser, record codec, and
 * record readers. Packed/zoned vectors are the standard textbook encodings.
 * Host test (stdio). */
#include <stdio.h>
#include <string.h>
#include "cobol.h"

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

static void test_comp3(void)
{
    int64_t v;
    /* +12345 -> 12 34 5C */
    const uint8_t a[] = {0x12, 0x34, 0x5C};
    OK(comp3_decode(a, 3, &v) && v == 12345);
    /* -12345 -> 12 34 5D */
    const uint8_t b[] = {0x12, 0x34, 0x5D};
    OK(comp3_decode(b, 3, &v) && v == -12345);
    /* 0x0F sign nibble = unsigned positive */
    const uint8_t c[] = {0x00, 0x0F};
    OK(comp3_decode(c, 2, &v) && v == 0);

    uint8_t out[3];
    OK(comp3_encode(12345, out, 3) && memcmp(out, a, 3) == 0);
    OK(comp3_encode(-12345, out, 3) && memcmp(out, b, 3) == 0);
    /* round trip a big exact amount: 98765432 (8 digits -> 5 bytes + sign) */
    uint8_t big[5];
    OK(comp3_encode(98765432, big, 5));
    OK(comp3_decode(big, 5, &v) && v == 98765432);
    /* overflow: 12345 needs 3 digits-worth but 1-byte field (1 digit) fails */
    uint8_t tiny[1];
    OK(!comp3_encode(12345, tiny, 1));
}

static void test_zoned(void)
{
    int64_t v;
    /* EBCDIC zoned +123 signed -> F1 F2 C3 */
    const uint8_t a[] = {0xF1, 0xF2, 0xC3};
    OK(zoned_decode(a, 3, &v) && v == 123);
    /* -123 -> F1 F2 D3 */
    const uint8_t b[] = {0xF1, 0xF2, 0xD3};
    OK(zoned_decode(b, 3, &v) && v == -123);
    /* unsigned 123 -> F1 F2 F3 */
    const uint8_t c[] = {0xF1, 0xF2, 0xF3};
    OK(zoned_decode(c, 3, &v) && v == 123);

    uint8_t out[3];
    OK(zoned_encode(123, out, 3, true) && memcmp(out, a, 3) == 0);
    OK(zoned_encode(-123, out, 3, true) && memcmp(out, b, 3) == 0);
    OK(zoned_encode(123, out, 3, false) && memcmp(out, c, 3) == 0);
}

static void test_comp(void)
{
    int64_t v;
    /* big-endian 1000 in 2 bytes = 03 E8 */
    const uint8_t a[] = {0x03, 0xE8};
    OK(comp_decode(a, 2, true, &v) && v == 1000);
    /* -1 in 2 bytes signed = FF FF */
    const uint8_t b[] = {0xFF, 0xFF};
    OK(comp_decode(b, 2, true, &v) && v == -1);
    OK(comp_decode(b, 2, false, &v) && v == 65535);
    /* 4-byte 16909060 = 01 02 03 04 */
    const uint8_t c[] = {0x01, 0x02, 0x03, 0x04};
    OK(comp_decode(c, 4, true, &v) && v == 16909060);
    uint8_t out[4];
    OK(comp_encode(16909060, out, 4, true) && memcmp(out, c, 4) == 0);
    OK(comp_encode(-1, out, 2, true) && out[0] == 0xFF && out[1] == 0xFF);
}

static const char *COPYBOOK = "01  TXN-RECORD.\n"
                              "    05  TXN-ID          PIC 9(6).\n"
                              "    05  ACCT-NAME       PIC X(20).\n"
                              "    05  AMOUNT          PIC S9(9)V99 COMP-3.\n"
                              "    05  RAIL-CODE       PIC 9(4) COMP.\n"
                              "    05  HIST-AMT        PIC S9(7)V99 COMP-3 OCCURS 3 TIMES.\n"
                              "    05  ALT-ID          REDEFINES TXN-ID PIC X(6).\n";

static void test_copybook(void)
{
    cob_layout lo;
    OK(cobol_parse_copybook(COPYBOOK, (uint32_t) strlen(COPYBOOK), &lo));

    const cob_field *id = cobol_find(&lo, "TXN-ID");
    OK(id && id->type == COB_ZONED && id->offset == 0 && id->size == 6 && id->ndigits == 6);
    const cob_field *nm = cobol_find(&lo, "ACCT-NAME");
    OK(nm && nm->type == COB_ALPHA && nm->offset == 6 && nm->size == 20);
    const cob_field *amt = cobol_find(&lo, "AMOUNT");
    /* S9(9)V99 COMP-3: 11 digits -> 11/2+1 = 6 bytes, scale 2, signed */
    OK(amt && amt->type == COB_COMP3 && amt->offset == 26 && amt->size == 6 && amt->scale == 2 &&
       amt->is_signed);
    const cob_field *rail = cobol_find(&lo, "RAIL-CODE");
    /* 9(4) COMP -> 2 bytes at offset 32 */
    OK(rail && rail->type == COB_COMP && rail->offset == 32 && rail->size == 2);
    /* HIST-AMT OCCURS 3: S9(7)V99 -> 9 digits -> 5 bytes each, at 34,39,44 */
    const cob_field *h0 = cobol_find(&lo, "HIST-AMT");
    const cob_field *h1 = cobol_find_indexed(&lo, "HIST-AMT", 1);
    const cob_field *h2 = cobol_find_indexed(&lo, "HIST-AMT", 2);
    OK(h0 && h0->offset == 34 && h0->size == 5);
    OK(h1 && h1->offset == 39);
    OK(h2 && h2->offset == 44);
    /* ALT-ID REDEFINES TXN-ID: offset back to 0 */
    const cob_field *alt = cobol_find(&lo, "ALT-ID");
    OK(alt && alt->offset == 0 && alt->type == COB_ALPHA && alt->size == 6);
    /* record size: through HIST-AMT(2) end = 44+5 = 49 */
    OK(lo.record_size == 49);
}

static void test_record_roundtrip(void)
{
    cob_layout lo;
    OK(cobol_parse_copybook(COPYBOOK, (uint32_t) strlen(COPYBOOK), &lo));
    uint8_t rec[64];
    memset(rec, 0, sizeof rec);

    const cob_field *id = cobol_find(&lo, "TXN-ID");
    const cob_field *nm = cobol_find(&lo, "ACCT-NAME");
    const cob_field *amt = cobol_find(&lo, "AMOUNT");
    const cob_field *rail = cobol_find(&lo, "RAIL-CODE");
    const cob_field *h1 = cobol_find_indexed(&lo, "HIST-AMT", 1);

    OK(cobol_set_int(rec, lo.record_size, id, 42));
    OK(cobol_set_text(rec, lo.record_size, nm, (const uint8_t *) "VINO FLOATING VOUCHER", 21));
    /* amount 1234567.89 -> scaled integer 123456789 */
    OK(cobol_set_int(rec, lo.record_size, amt, 123456789));
    OK(cobol_set_int(rec, lo.record_size, rail, 846)); /* DEBIT rail */
    OK(cobol_set_int(rec, lo.record_size, h1, -9999));

    int64_t v;
    OK(cobol_get_int(rec, lo.record_size, id, &v) && v == 42);
    OK(cobol_get_int(rec, lo.record_size, amt, &v) && v == 123456789);
    OK(cobol_get_int(rec, lo.record_size, rail, &v) && v == 846);
    OK(cobol_get_int(rec, lo.record_size, h1, &v) && v == -9999);
    uint8_t txt[24];
    uint32_t tn;
    OK(cobol_get_text(rec, lo.record_size, nm, txt, sizeof txt, &tn) && tn == 20);
    OK(memcmp(txt, "VINO FLOATING VOUCHE", 20) == 0); /* truncated to width 20 */
}

static void test_readers(void)
{
    /* fixed-length: 3 records of 4 bytes */
    const uint8_t buf[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint32_t rl;
    const uint8_t *r0 = cobol_fixed_record(buf, sizeof buf, 4, 0, &rl);
    const uint8_t *r2 = cobol_fixed_record(buf, sizeof buf, 4, 2, &rl);
    OK(r0 && r0[0] == 1 && rl == 4);
    OK(r2 && r2[0] == 9);
    OK(cobol_fixed_record(buf, sizeof buf, 4, 3, &rl) == 0); /* past end */

    /* RDW: two records "AB" (len 6) and "CDE" (len 7) */
    const uint8_t vb[] = {0x00, 0x06, 0, 0, 'A', 'B', 0x00, 0x07, 0, 0, 'C', 'D', 'E'};
    cob_rdw_iter it;
    cobol_rdw_init(&it, vb, sizeof vb);
    uint32_t n;
    const uint8_t *p = cobol_rdw_next(&it, &n);
    OK(p && n == 2 && p[0] == 'A' && p[1] == 'B');
    p = cobol_rdw_next(&it, &n);
    OK(p && n == 3 && memcmp(p, "CDE", 3) == 0);
    OK(cobol_rdw_next(&it, &n) == 0);
    /* malformed RDW (length beyond buffer) */
    const uint8_t bad[] = {0x00, 0x40, 0, 0, 'X'};
    cobol_rdw_init(&it, bad, sizeof bad);
    OK(cobol_rdw_next(&it, &n) == 0);
}

int main(void)
{
    test_comp3();
    test_zoned();
    test_comp();
    test_copybook();
    test_record_roundtrip();
    test_readers();
    printf("COBOL: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
