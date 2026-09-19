/* assurance.h — Pay-It-Forward Assurance Protocol (PAPSS Specification)
 *
 * Proactive capital generation: contributions flow forward into prevention
 * that generates capital. Inverts insurance: regenerative, not extractive.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ASSURANCE_H
#define ZXV_ASSURANCE_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "capital_forms.h"
#include "triple_ledger.h"
#include "lpres.h"
#include "ipfs.h"
#include "onepolicy/onepolicy.h"
#include "interspace/interspace.h"
#include "rational/rational.h"

/* ===== Assurance Error Codes ===== */
typedef enum {
    ASSURANCE_OK                    = 0,
    ASSURANCE_ERR_CONTRIBUTION      = 1,
    ASSURANCE_ERR_EFFICACY_UNVERIFIED = 2,  /* LPRES NEITHER/GLUT */
    ASSURANCE_ERR_MAXIM_VIOLATION   = 3,
    ASSURANCE_ERR_INSUFFICIENT_GENERATION = 4, /* ratio < 1.0x */
    ASSURANCE_ERR_FORWARD_ROUTE_INVALID = 5,
    ASSURANCE_ERR_NO_GENERATION     = 6,
    ASSURANCE_ERR_ARITHMETIC        = 7,
    ASSURANCE_ERR_GENERATION_SHORTFALL = 8,
    ASSURANCE_ERR_GENERATION_UNATTTESTED = 9,
    ASSURANCE_DEFER                 = 10
} assurance_err_t;

/* ===== Assurance Contract =====
 * CONTRIBUTION (not premium) → PREVENTION → GENERATED CAPITAL → FORWARD
 */
typedef struct {
    /* CONTRIBUTION (not premium) */
    rat_t contribution;                  /* Exact rational — Financial rail (846) */
    capital_form_t target_form;          /* Which capital form to prevent (1-9) */
    uint8_t prevention_cid[32];          /* IPFS CID of prevention project (SHA-256) */
    lpres_attestation_t efficacy_proof;  /* LPRES: TRUE iff prevention verified effective */

    /* GENERATION TARGET (not claim) */
    capital_form_t generated_form;       /* Capital form PRODUCED by prevention */
    rat_t generation_ratio;              /* How much generated per contribution unit */
    uint64_t generation_tick;            /* Phase tick when generation verified */

    /* RECIPROCITY (not policy) */
    bool pay_it_forward;                 /* Generated capital flows to next contributor */
    interstitial_region_t forward_route; /* Lex Rhodia corridor for downstream recipient */

    /* Settlement tracking */
    uint8_t contribution_cid[32];        /* CID on Financial rail (846) */
    uint8_t generation_cid[32];          /* CID on generated form's rail */
} assurance_contract_t;

/* ===== Assurance Derivatives (Hedging Prevention Outcomes) ===== */
typedef enum {
    ASSUR_DERIV_PREVENTION_FORWARD = 0,  /* Fund prevention today, receive generated capital later */
    ASSUR_DERIV_EFFICACY_SWAP      = 1,  /* Swap unverified prevention for verified algorithm */
    ASSUR_DERIV_FORWARD_FLOW_OPTION = 2  /* Option to route generated capital to specific community */
} assurance_deriv_type_t;

typedef struct {
    assurance_deriv_type_t type;
    uint8_t underlying_assurance_cid[32]; /* CID of underlying assurance contract */
    rat_t notional;
    uint64_t expiry_tick;
    capital_form_t settlement_form;      /* Generated capital form */
} assurance_deriv_t;

/* ===== API ===== */

/* Admit assurance contract — kernel admission gate (5 gates) */
assurance_err_t assurance_admit(assurance_contract_t *a, triple_ledger_t *tl);

/* Verify capital generation — exact rational ratio + LPRES TRUE */
assurance_err_t assurance_verify_generation(assurance_contract_t *a, triple_ledger_t *tl);

/* Create assurance contract with validated prevention */
assurance_err_t assurance_create(assurance_contract_t *a, triple_ledger_t *tl,
                                 const rat_t *contribution, capital_form_t target_form,
                                 const uint8_t prevention_cid[32], const lpres_attestation_t *efficacy_proof,
                                 capital_form_t generated_form, const rat_t *generation_ratio,
                                 bool pay_it_forward, const interstitial_region_t *forward_route);

/* Admit assurance derivative — long prevention, not short risk */
assurance_err_t assurance_deriv_admit(assurance_deriv_t *d, triple_ledger_t *tl);

#endif /* ZXV_ASSURANCE_H */
