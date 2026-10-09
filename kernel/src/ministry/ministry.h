/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ministry.h — the MINISTRY PILLAR (the Illumaheart Foundation).
 *
 * WHAT THIS IS
 * -----------
 * The Ministry is the half of the house that TOUCHES MONEY. It measures
 * Ministry-form capital, runs the treasury as honest double-entry bookkeeping
 * (by COMPOSING the existing triple_ledger — it does not reinvent accounting),
 * and computes tribute and gratuity.
 *
 * THE ANTI-CAPTURE WALL (non-negotiable)
 * -------------------------------------
 * The Ministry NEVER issues credentials. ministry.c does not #include crown.h,
 * holds no signing key, and references no ISC / attestation-signing machinery.
 * A treasury that measures your money and a Crown that flatly refuses to put a
 * price on your grandmother's lullaby are DIFFERENT ORGANS, and this header is
 * the membrane between them. The treasury is, by construction, physically
 * incapable of shredding your papers — it cannot revoke what it cannot issue.
 *
 * Every settlement is passed through The One Policy: no usury, ever.
 */
#ifndef ZXV_MINISTRY_H
#define ZXV_MINISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zcapital.h"
#include "triple_ledger.h"

/* ===== Result of a Ministry operation ===== */
typedef enum {
    MIN_OK = 0,          /* measured / settled                                 */
    MIN_NOT_MEASURABLE,  /* a Crown (or Co-Juris) form: recognised, never priced */
    MIN_UNDERCOVERED,    /* coverage < 1.8x or equity < 0 — settlement refused  */
    MIN_VOID_MAXIM       /* usury / non-term per The One Policy — refused       */
} ministry_result_t;

/* ===== The Ministry ===== */
typedef struct {
    triple_ledger_t *ledger;                 /* WRAPPED, not owned — the treasury books */
    zcap_vec_t measured;                     /* running tally of measured Ministry capital */

    /* Exchange rates are an OPS-BOUNDARY input: they are supplied/config, NEVER
     * discovered on-device. Unset priceable pairs default to 1:1 (SR_ONE). */
    surplus_real_t ratio[ZCAP_FORM_COUNT][ZCAP_FORM_COUNT];
    bool ratio_set[ZCAP_FORM_COUNT][ZCAP_FORM_COUNT];
} ministry_t;

/* Wrap an existing, already-initialised triple ledger. Does not own it. */
void ministry_init(ministry_t *m, triple_ledger_t *ledger);

/* MEASURE Ministry capital. Only the four Ministry forms
 * (Financial/Manufactured/Intellectual/Human) are quantified and tallied ->
 * MIN_OK. Any Crown form (Social/Natural/Cultural/Spiritual) or the Co-Juris
 * System form is recognised but NOT priced -> MIN_NOT_MEASURABLE. */
ministry_result_t ministry_measure(ministry_t *m, zcap_form_t form,
                                    surplus_real_t amount);

/* TRIBUTE: the mandatory eleven — 11% of `amount`. Pure function. */
surplus_real_t ministry_tribute(surplus_real_t amount);

/* GRATUITY: the voluntary eleven — 11% of `amount`, returned as a SEPARATE
 * amount the caller applies as its own entry. NEVER deducted from principal.
 * Pure function. */
surplus_real_t ministry_gratuity(surplus_real_t amount);

/* Set the ops-boundary exchange rate for a permitted `from`->`to` pair. */
void ministry_set_exchange_ratio(ministry_t *m, zcap_form_t from,
                                 zcap_form_t to, surplus_real_t ratio);

/* The technical exchange RATIO for a permitted `from`->`to` pair.
 * Returns SR_ZERO if either form is a non-priceable Crown form (the Ministry
 * refuses to price the inalienable). Otherwise returns the set rate, or the
 * documented default of 1:1 (SR_ONE) when no rate has been supplied. */
surplus_real_t ministry_exchange_ratio(const ministry_t *m, zcap_form_t from,
                                       zcap_form_t to);

/* SETTLE `amount` from account `from_acct` to `to_acct` through the triple
 * ledger's double-entry machinery.
 *   - interest > 0  -> composes The One Policy, which voids usury -> MIN_VOID_MAXIM
 *   - coverage < 1.8x or equity < 0 (fail-closed, checked BEFORE posting) ->
 *     MIN_UNDERCOVERED (the ledger is left untouched; no half-settlements)
 * Otherwise posts a balanced double entry and returns MIN_OK. */
ministry_result_t ministry_settle(ministry_t *m, uint32_t from_acct,
                                  uint32_t to_acct, surplus_real_t amount,
                                  surplus_real_t interest);

#endif /* ZXV_MINISTRY_H */
