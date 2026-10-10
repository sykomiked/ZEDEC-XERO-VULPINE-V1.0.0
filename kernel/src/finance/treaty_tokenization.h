/* treaty_tokenization.h — Treaty-Backed Asset Tokenization
 *
 * "PAPSS" in this file names the project's own capital-form specification.
 * Nothing here connects to, is certified by, or is endorsed by the
 * Pan-African Payment and Settlement System.
 *
 * Tokenize tangibles via treaty CID on Externality rail (888).
 * Conservation easements become treaty-backed Natural capital tokens.
 * Never fractionalized. Backing ratio >= 1.0x enforced by kernel.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TREATY_TOKENIZATION_H
#define ZXV_TREATY_TOKENIZATION_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "capital_forms.h"
#include "triple_ledger.h"
#include "lpres.h"
#include "ipfs.h"
#include "count_house.h"
#include "interspace/interspace.h"
#include "rational/rational.h"

/* ===== Treaty Asset Error Codes ===== */
typedef enum {
    TREATY_OK = 0,
    TREATY_ERR_NO_TREATY = 1,
    TREATY_ERR_SOVEREIGNTY = 2,
    TREATY_ERR_VALUATION = 3,
    TREATY_ERR_CID_MISMATCH = 4,
    TREATY_ERR_RAIL_FULL = 5
} treaty_err_t;

/* ===== Treaty-Backed Asset ===== */
typedef struct {
    uint8_t treaty_cid[32];                /* IPFS CID of signed treaty (SHA-256) */
    uint8_t asset_cid[32];                 /* Conservation easement / land / mineral CID */
    capital_form_t form;                   /* CAPITAL_NATURAL or CAPITAL_BUILT */
    rat_t quantified_value;                /* Exact rational — ecological/engineering appraisal */
    lpres_attestation_t sovereignty_proof; /* LPRES: TRUE iff treaty ratified */
    interstitial_region_t corridor;        /* Lex Rhodia corridor for transfer */

    /* Kernel tracking */
    uint8_t externality_cid[32]; /* CID on Externality rail (888) */
    uint64_t tokenization_tick;  /* Phase tick when tokenized */
} treaty_asset_t;

/* ===== API ===== */

/* Verify and tokenize treaty-backed asset on Externality rail (888) */
treaty_err_t treaty_asset_verify(const treaty_asset_t *ta, const triple_ledger_t *tl);

/* Create treaty-backed asset from conservation easement */
treaty_err_t treaty_asset_create(treaty_asset_t *ta, const triple_ledger_t *tl,
                                 const uint8_t treaty_cid[32], const uint8_t asset_cid[32],
                                 capital_form_t form, const rat_t *quantified_value,
                                 const lpres_attestation_t *sovereignty_proof,
                                 const interstitial_region_t *corridor);

/* Verify treaty CID resolves on IPFS */
static inline bool treaty_cid_resolves(const uint8_t cid[32])
{
    if (!cid) return false;
    ipfs_node_t node = {0};
    uint8_t dummy[256];
    uint32_t out_len = 0;
    return ipfs_get_verify(&node, cid, dummy, 256, &out_len) == IPFS_OK;
}

/* Verify sovereignty proof is LPRES STATE_TRUE */
static inline bool treaty_sovereignty_clear(const lpres_attestation_t *proof)
{
    if (!proof) return false;
    return proof->state == LPRES_STATE_TRUE;
}

#endif /* ZXV_TREATY_TOKENIZATION_H */
