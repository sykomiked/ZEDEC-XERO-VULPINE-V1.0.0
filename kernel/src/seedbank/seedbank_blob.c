/* seedbank_blob.c — the seed-bank blob's translation unit. The array is
 * GENERATED into build/<arch>/gen/zxv_seedbank_blob.h by
 * tools/seedbank/mk_blob.py (the ZXSB container: the icons, the fonts,
 * the seed content). This file includes it, parses it once through the
 * seedbank's own reader (sb_open), and exposes the two functions the
 * kernel calls (catalog.h): sb_seedbank_blob_register and
 * sb_seedbank_blob_bytes.
 *
 * Exactly one translation unit carries the array, so the blob is linked
 * once and every other module reads it through the parsed bank.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zxv_seedbank_blob.h"   /* the generated array */

#include <stddef.h>
#include <stdint.h>

#include "catalog.h"
#include "seedbank.h"

#ifdef ZXV_HAVE_SEEDBANK_BLOB
/* the parsed bank, held for the kernel's lifetime. sb_open validates the
 * container (the magic, the length, the counts), so a corrupt blob is
 * refused here rather than read later as if it were sound. */
static sb_bank_t g_blob_bank;
static int g_blob_registered;

int sb_seedbank_blob_register(void)
{
    if (g_blob_registered)
        return (int) sb_count(&g_blob_bank);   /* idempotent: parse once */
    if (sb_open(&g_blob_bank, zxv_seedbank_blob, (uint32_t) sizeof zxv_seedbank_blob) != 0)
        return -1;                              /* refused: never a false success */
    g_blob_registered = 1;
    return (int) sb_count(&g_blob_bank);
}

uint32_t sb_seedbank_blob_bytes(void)
{
    return (uint32_t) sizeof zxv_seedbank_blob;
}
#else
/* built without the blob: the functions stay present so the kernel links,
 * and the absence is explicit (0 seeds, 0 bytes) rather than a silent
 * read of an empty bank */
int sb_seedbank_blob_register(void)
{
    return 0;
}

uint32_t sb_seedbank_blob_bytes(void)
{
    return 0;
}
#endif
