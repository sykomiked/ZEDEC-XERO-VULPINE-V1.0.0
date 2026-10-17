/* seedbank_blob.c — the compiled-in Seed Bank: the inaugural bank's
 * bytes (icons + fonts) as a kernel blob, registered into the master
 * catalog at boot so the door's /bank resolves the seeds with no fetch.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0. */
#include "seedbank/catalog.h"

#include <stddef.h>

#if defined(ZXV_HAVE_SEEDBANK_BLOB)
#include "zxv_seedbank_blob.h"

/* The blob's bank + its slot in the host's store. The host calls
 * sb_seedbank_blob_register once at boot. */
extern sb_bank_t G_sbanks[SB_CAT_MAX];
extern uint32_t G_sbank_n;
extern sb_catalog_t G_catalog;

int sb_seedbank_blob_register(void)
{
    if (G_sbank_n >= SB_CAT_MAX) return -1;
    if (sb_open(&G_sbanks[G_sbank_n], zxv_seedbank_blob, zxv_seedbank_blob_len) != 0)
        return -1;
    G_sbank_n++;
    sb_catalog_init(&G_catalog, G_sbanks, G_sbank_n);
    return (int) sb_count(&G_sbanks[G_sbank_n - 1]);
}

uint32_t sb_seedbank_blob_bytes(void)
{
    return zxv_seedbank_blob_len;
}
#endif /* ZXV_HAVE_SEEDBANK_BLOB */