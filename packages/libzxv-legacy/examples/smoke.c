/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* smoke.c - links against libzxv-legacy.a and zxv_legacy.h only.
 *
 *   cc -Ibuild/libzxv-legacy/include smoke.c build/libzxv-legacy/libzxv-legacy.a
 *
 * A mainframe-style record: copybook parsed, a COMP-3 amount read exactly
 * into a rational and written back, EBCDIC text decoded, an IBM HFP float
 * converted to IEEE, and the lossy double path reported as lossy. */
#include <stdio.h>
#include <string.h>
#include "zxv_legacy.h"

static int fails, checks;
#define CHECK(c, what)                                                                             \
    do {                                                                                           \
        checks++;                                                                                  \
        printf("%s %s\n", (c) ? "ok  " : "FAIL", what);                                            \
        if (!(c)) fails++;                                                                         \
    } while (0)

static cob_layout lo;

int main(void)
{
    static const char copybook[] = "       01 CUST-REC.\n"
                                   "          05 CUST-NAME   PIC X(5).\n"
                                   "          05 BALANCE     PIC S9(7)V99 COMP-3.\n";
    CHECK(cobol_parse_copybook(copybook, (uint32_t) strlen(copybook), &lo), "copybook parses");
    const cob_field *name = cobol_find(&lo, "CUST-NAME");
    const cob_field *bal = cobol_find(&lo, "BALANCE");
    CHECK(name && bal && lo.record_size == 10 && bal->size == 5 && bal->scale == 2,
          "layout: X(5) + 5-byte COMP-3, scale 2");

    /* "HELLO" in EBCDIC 037, then -12345.67 packed: 01 23 45 67 D */
    uint8_t rec[10] = {0xC8, 0xC5, 0xD3, 0xD3, 0xD6, 0x00, 0x12, 0x34, 0x56, 0x7D};
    uint8_t text[16];
    int n = ebcdic_to_utf8(EBCDIC_CP037, rec, 5, text, sizeof text);
    CHECK(n == 5 && !memcmp(text, "HELLO", 5), "EBCDIC 037 -> UTF-8");

    int64_t cents = 0;
    CHECK(bal && cobol_get_int(rec, sizeof rec, bal, &cents) && cents == -1234567,
          "COMP-3 field read as scaled integer -1234567");

    rat_t v;
    lr_result_t r = lr_packed_to_rat(rec + 5, 5, 2, &v);
    char s[64];
    bool shown_exact = false;
    rat_to_fixed(v, 2, s, sizeof s, &shown_exact);
    CHECK(r.ok && r.exact && rat_eq(v, rat_make(-1234567, 100)) && !strcmp(s, "-12345.67") &&
              shown_exact,
          "COMP-3 -> exact rational -12345.67");

    /* add one cent exactly and pack it back (lr_rat_to_packed writes the
     * smallest odd digit count: 7 digits + sign = 4 bytes) */
    rat_t w = rat_add(v, rat_make(1, 100));
    uint8_t packed[8];
    uint32_t wr = 0;
    r = lr_rat_to_packed(w, 2, packed, sizeof packed, &wr);
    const uint8_t want[4] = {0x12, 0x34, 0x56, 0x6D};
    CHECK(r.ok && r.exact && wr == 4 && !memcmp(packed, want, 4), "rational -> COMP-3 -12345.66");

    CHECK(hfp32_to_ieee32(0x41100000u) == 0x3F800000u &&
              ieee32_to_hfp32(0x3F800000u) == 0x41100000u,
          "IBM HFP 1.0 <-> IEEE 1.0f");

    /* 0.1 as an IEEE double is not 1/10: the bridge must say so */
    r = lr_double_bits_to_rat(0x3FB999999999999Aull, &v);
    CHECK(!r.exact, "double 0.1 reported as not exact");

    printf("%d/%d checks\n", checks - fails, checks);
    return fails ? 1 : 0;
}
