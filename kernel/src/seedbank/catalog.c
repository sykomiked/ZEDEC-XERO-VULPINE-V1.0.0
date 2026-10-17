/* catalog.c — the master catalog over hosted Seed Banks (catalog.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
 * SPDX-License-Identifier: Apache-2.0. */
#include "catalog.h"

#include <stddef.h>

void sb_catalog_init(sb_catalog_t *c, const sb_bank_t *banks, uint32_t nbanks)
{
    uint32_t i;
    if (!c) return;
    if (nbanks > SB_CAT_MAX) nbanks = SB_CAT_MAX;
    for (i = 0; i < nbanks; i++) c->banks[i] = &banks[i];
    c->nbanks = nbanks;
}

const uint8_t *sb_catalog_find(const sb_catalog_t *c, const uint8_t digest[SB_DIGEST_LEN],
                               uint32_t *out_len, uint32_t *bank_idx)
{
    uint32_t i;
    if (!c) return NULL;
    for (i = 0; i < c->nbanks; i++) {
        uint32_t n = 0;
        const uint8_t *p = sb_find(c->banks[i], digest, &n);
        if (p) {
            if (out_len) *out_len = n;
            if (bank_idx) *bank_idx = i;
            return p;
        }
    }
    return NULL;
}

uint32_t sb_catalog_bank_of(const sb_catalog_t *c, const uint8_t digest[SB_DIGEST_LEN])
{
    uint32_t i;
    if (!c) return SB_CAT_MAX;
    for (i = 0; i < c->nbanks; i++)
        if (sb_find(c->banks[i], digest, 0)) return i;
    return SB_CAT_MAX;
}