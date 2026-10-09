/* derivatives.h — Nine-Forms Derivative Contracts (PAPSS Specification)
 *
 * Derivative contracts that settle in actual capital form (not cash),
 * with kernel-enforced backing ratio >= 1.0x, LPRES attestation gates,
 * and interstitial jurisdiction (Lex Rhodia).
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_DERIVATIVES_H
#define ZXV_DERIVATIVES_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "capital_forms.h"
#include "triple_ledger.h"
#include "lpres.h"
#include "ipfs.h"
#include "rational/rational.h"
#include "interspace/interspace.h"
#include "onepolicy/onepolicy.h"
#include "m5_types.h"

/* ===== Derivative Error Codes ===== */
typedef enum {
    DERIV_OK = 0,
    DERIV_ERR_NO_BACKING = 1,
    DERIV_ERR_BACKING_UNATTTESTED = 2, /* LPRES NEITHER/GLUT */
    DERIV_ERR_ARITHMETIC_OVERFLOW = 3,
    DERIV_ERR_INSUFFICIENT_BACKING = 4, /* ratio < 1.0x */
    DERIV_ERR_INVALID_JURISDICTION = 5,
    DERIV_ERR_NO_TREATY = 6,
    DERIV_ERR_SOVEREIGNTY = 7,
    DERIV_ERR_VALUATION = 8,
    DERIV_ERR_PHASE_VETO = 9,
    DERIV_ERR_BACKING_A = 10,
    DERIV_ERR_BACKING_B = 11,
    DERIV_ERR_NO_SPREAD = 12,
    DERIV_DEFER = 13,
    DERIV_VETO = 14,
    DERIV_ABSTAIN = 15,
    DERIV_VETO_MAXIM = 16,
    DERIV_VETO_STATE = 17
} deriv_err_t;

/* ===== Interstitial Region (Lex Rhodia Corridor) =====
 * Uses interspace.h's interstitial_region_t with region_id as CID */
static inline bool interstitial_region_cid_resolves(const interstitial_region_t *r)
{
    if (!r) return false;
    ipfs_node_t node = {0};
    uint8_t dummy[256];
    uint32_t out_len = 0;
    return ipfs_get_verify(&node, r->region_id, dummy, 256, &out_len) == IPFS_OK;
}

/* ===== Derivative Contract =====
 * Posts atomically to three rails:
 * - Rail 555 (Financial): Exact economic terms
 * - Rail 777 (Provenance): Attestation & backing proof
 * - Rail 888 (Externality): Relational phase & jurisdiction
 */
typedef struct {
    /* RAIL 555 — FINANCIAL: Exact economic terms */
    rat_t notional;                 /* Exact rational — no FP rounding */
    surplus_real_t strike;          /* Q32.32 fixed-point */
    uint64_t expiry_tick;           /* Phase tick (not wall clock) */
    capital_form_t underlying_form; /* 5-9 (priceable only) */

    /* RAIL 777 — PROVENANCE: Attestation & backing proof */
    uint8_t backing_cid[32];            /* IPFS CID of backing asset proof (SHA-256) */
    lpres_attestation_t backing_proof;  /* LPRES: TRUE/NEITHER/FALSE/GLUT */
    uint64_t backing_verification_tick; /* When backing was verified */

    /* RAIL 888 — EXTERNAILITY: Relational phase & interstitial jurisdiction */
    surplus_real_t phase_curvature;     /* IPHASE φ — asymmetric routing cost */
    interstitial_region_t jurisdiction; /* Lex Rhodia governed corridor */
    bool treaty_backed;                 /* True iff treaty CID attached */

    /* Settlement tracking */
    uint8_t source_rail_cid[32]; /* CID on source rail */
    uint8_t target_rail_cid[32]; /* CID on target rail */
    uint8_t vouchers[3];         /* Voucher IDs for single-active-state */
} deriv_contract_t;

/* ===== Temporal Arbitrage ===== */
typedef struct {
    void *node_a;                 /* Low-phase node (earlier tick) */
    void *node_b;                 /* High-phase node (later tick) */
    deriv_contract_t *contract_a; /* Short on node_a */
    deriv_contract_t *contract_b; /* Long on node_b */
    surplus_real_t phase_spread;  /* φ_b - φ_a (IPHASE curvature) */
    uint64_t settlement_tick;     /* When phase converges */
} temporal_arb_t;

/* ===== API ===== */

/* Verify backing ratio >= 1.0x — kernel admission gate */
deriv_err_t deriv_verify_backing(const deriv_contract_t *d, const triple_ledger_t *tl);

/* Execute temporal arbitrage — no debt, no leverage */
deriv_err_t temporal_arb_execute(temporal_arb_t *arb);

/* Settle derivative — atomic three-rail transfer */
deriv_err_t deriv_settle(deriv_contract_t *d, triple_ledger_t *tl);

/* Create derivative contract with validated backing */
deriv_err_t deriv_create(deriv_contract_t *d, const triple_ledger_t *tl, capital_form_t form,
                         const rat_t *notional, const surplus_real_t *strike, uint64_t expiry_tick,
                         const uint8_t backing_cid[32], const lpres_attestation_t *backing_proof,
                         const interstitial_region_t *jurisdiction, bool treaty_backed);

#endif /* ZXV_DERIVATIVES_H */
