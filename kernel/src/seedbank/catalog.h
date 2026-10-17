/* catalog.h — the master catalog over the hosted Seed Banks: a digest
 * resolves to its bank and its exact slice. The catalog is the local
 * partition of the planetary seed index; the CISEX envelopes carry the
 * registrations across the mesh, and the 36n9:// bank route lands here.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0.
 */
#ifndef SEEDBANK_CATALOG_H
#define SEEDBANK_CATALOG_H

#include "seedbank.h"

#define SB_CAT_MAX 8u /* banks one node hosts */

typedef struct {
    const sb_bank_t *banks[SB_CAT_MAX]; /* the hosted banks, in registration order */
    uint32_t nbanks;
} sb_catalog_t;

/* Register the banks (the caller's array must outlive the catalog). */
void sb_catalog_init(sb_catalog_t *c, const sb_bank_t *banks, uint32_t nbanks);

/* Find a seed's slice across the hosted banks. Returns the bytes (bounded
 * by *out_len) and sets *bank_idx to the bank holding them, or NULL when
 * the digest is not hosted. */
const uint8_t *sb_catalog_find(const sb_catalog_t *c, const uint8_t digest[SB_DIGEST_LEN],
                               uint32_t *out_len, uint32_t *bank_idx);

/* The bank index holding a digest, or SB_CAT_MAX when absent. */
uint32_t sb_catalog_bank_of(const sb_catalog_t *c, const uint8_t digest[SB_DIGEST_LEN]);

#endif /* SEEDBANK_CATALOG_H */