/* seedbank_blob.c — the compiled-in Seed Bank: the inaugural bank's
 * bytes (icons + fonts) as a kernel blob, registered into the master
 * catalog at boot so the door's /bank resolves the seeds with no fetch.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0. */
#include "catalog.h"
#include "seedbank.h"

#include <stddef.h>
#include <stdint.h>

#if defined(ZXV_HAVE_SEEDBANK_BLOB)
#include "zxv_seedbank_blob.h"

__attribute__((weak)) sb_bank_t G_sbanks[SB_CAT_MAX];
__attribute__((weak)) uint32_t G_sbank_n;
__attribute__((weak)) sb_catalog_t G_catalog;

int sb_seedbank_blob_register(void)
{
    if (G_sbank_n >= SB_CAT_MAX) return -1;
    if (sb_open(&G_sbanks[G_sbank_n], zxv_seedbank_blob, (uint32_t) sizeof zxv_seedbank_blob) != 0)
        return -1;
    G_sbank_n++;
    sb_catalog_init(&G_catalog, G_sbanks, G_sbank_n);
    return (int) sb_count(&G_sbanks[G_sbank_n - 1]);
}

uint32_t sb_seedbank_blob_bytes(void)
{
    return (uint32_t) sizeof zxv_seedbank_blob;
}
#else
int sb_seedbank_blob_register(void)
{
    return 0;
}

uint32_t sb_seedbank_blob_bytes(void)
{
    return 0;
}
#endif /* ZXV_HAVE_SEEDBANK_BLOB */
