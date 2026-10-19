/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_genie.c — host unit test proving the Game Genie interception layer
 * (m5_genie.c): unconditional and compare-gated replacement, widths 1/2/4,
 * hit counts, symbolic code decoding, the enabled flag and clear. */
#include "m5_genie.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                           \
    } while (0)

int main(void)
{
    zxv_genie_patch_t p;
    int i;

    /* an unconditional byte patch at 0x2000 -> 0xAA */
    i = genie_apply_intercept("ZXV-LATT-144X", 0x2000, 0, 0xAA, 1);
    CHECK(i == 0);
    CHECK(genie_active() == 1);
    CHECK(genie_eval_read(0x2000, 0x00, 1) == 0xAA); /* fires */
    CHECK(genie_eval_read(0x2001, 0x00, 1) == 0x00); /* other address untouched */
    CHECK(genie_eval_read(0x2000, 0x7F, 1) == 0xAA); /* unconditional: any value */

    /* a compare-gated byte patch: only value 0x5A at 0x3000 becomes 0x11 */
    i = genie_apply_intercept("GATE-CHK-01", 0x3000, 0x5A, 0x11, 1);
    CHECK(i == 1);
    CHECK(genie_eval_read(0x3000, 0x5A, 1) == 0x11); /* matches the compare */
    CHECK(genie_eval_read(0x3000, 0x99, 1) == 0x99); /* no match: untouched */

    /* widths: a 16-bit patch and a 32-bit patch (m68k-style) */
    i = genie_apply_intercept("WIDE-16-BIT", 0x4000, 0, 0xBEEF, 2);
    CHECK(i == 2);
    CHECK(genie_eval_read(0x4000, 0x0000, 2) == 0xBEEF);
    CHECK(genie_eval_read(0x4002, 0x0000, 2) == 0x0000);
    i = genie_apply_intercept("WIDE-32-BIT", 0x5000, 0, 0xDEADBEEFu, 4);
    CHECK(i == 3);
    CHECK(genie_eval_read(0x5000, 0x0, 4) == 0xDEADBEEFu);

    /* hit counts accumulated on the first patch */
    CHECK(genie_get(0)->hits == 2);
    CHECK(genie_get(1)->hits == 1);

    /* the table is bounded */
    CHECK(genie_apply_intercept("FULL-TAB-LE", 0x6000, 0, 1, 1) == 4);
    for (int k = 5; k < 32; k++) CHECK(genie_apply_intercept("FILL-UP-XX", k * 0x100, 0, k, 1) == k);
    CHECK(genie_apply_intercept("OVER-FLOW", 0x7000, 0, 1, 1) == -1);
    CHECK(genie_apply_intercept("BAD-WIDTH", 0x7000, 0, 1, 3) == -1);

    /* symbolic decoding: deterministic, three words, width default 1 */
    CHECK(genie_decode_code("ZXV-LATT-144X", &p) == 0);
    CHECK(p.width_bytes == 1);
    {
        zxv_genie_patch_t q;
        CHECK(genie_decode_code("ZXV-LATT-144X", &q) == 0);
        CHECK(q.target_addr == p.target_addr && q.compare_val == p.compare_val &&
              q.replace_val == p.replace_val);
        CHECK(genie_decode_code("ZXV-LATT-999X", &q) == 0);
        CHECK(q.replace_val != p.replace_val); /* a different code differs */
    }
    CHECK(genie_decode_code("TOO-MANY-WORDS-X", &p) == -1);
    CHECK(genie_decode_code("LONGER-WORD-X", &p) == -1); /* a word > 4 chars */
    CHECK(genie_decode_code(0, &p) == -1);

    /* clear drops everything */
    genie_clear();
    CHECK(genie_active() == 0);
    CHECK(genie_count() == 0);
    CHECK(genie_eval_read(0x2000, 0x00, 1) == 0x00);

    printf("test_genie: %s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}