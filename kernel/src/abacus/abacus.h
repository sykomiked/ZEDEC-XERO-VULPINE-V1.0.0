/* abacus.h — Smaug's Abacus: multilateral mutual-credit clearing
 *
 * WHAT IT ACTUALLY DOES
 * ---------------------
 * A trading circle accumulates bilateral obligations: A owes B, B owes C,
 * C owes A. Settled one pair at a time, that requires cash to move three
 * times. Multilateral clearing nets the whole graph at once: each member's
 * NET position is computed, offsetting cycles cancel, and only the residual
 * imbalances need to settle in cash.
 *
 * This is a real, well-defined operation used by commercial clearing houses
 * and mutual-credit systems (LETS, Sardex, WIR). The value is measurable and
 * we measure it: GROSS OBLIGATIONS ELIMINATED. A circle that owes 300 units
 * gross but nets to 0 settles with no cash at all.
 *
 * INVARIANTS THE IMPLEMENTATION MUST HOLD (all asserted in the tests)
 *   1. CONSERVATION — the sum of every member's net position is exactly zero
 *      before and after clearing. Clearing never creates or destroys value.
 *   2. NET-POSITION PRESERVATION — each member's net is IDENTICAL after
 *      clearing. Members are made no better or worse off; only the number of
 *      transfers changes.
 *   3. EXACTNESS — all amounts are rat_t. Not one minor unit is lost to
 *      rounding, which is the failure mode that makes float-based clearing
 *      unacceptable for money.
 *
 * WHAT WE DO NOT CLAIM
 *   This is netting, not consensus. It does not by itself prevent
 *   double-spending across mutually distrusting nodes, and it is not a
 *   cryptographic protocol — obligations must already be authenticated
 *   (the signed-package / Ed25519 path does that). Calling netting
 *   "quantum-resistant" would be a category error: there is no hardness
 *   assumption here to break.
 *
 * Freestanding, integer/rational only, bounded, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Smaug's Abacus slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ABACUS_H
#define ZXV_ABACUS_H

#include <stdint.h>
#include <stdbool.h>
#include "../rational/rational.h"

#define AB_MAX_MEMBERS      64
#define AB_MAX_OBLIGATIONS  512
#define AB_NAME_LEN         24

/* One directed obligation: `from` owes `to` the amount. */
typedef struct {
    uint32_t from;
    uint32_t to;
    rat_t    amount;
    bool     active;
    uint8_t  capital;      /* which of the nine capitals this is denominated in */
} ab_obligation_t;

typedef struct {
    char     name[AB_NAME_LEN];
    bool     used;
} ab_member_t;

typedef struct {
    ab_member_t     member[AB_MAX_MEMBERS];
    uint32_t        num_members;

    ab_obligation_t obl[AB_MAX_OBLIGATIONS];
    /* Set by smaug_clear. The return value alone cannot signal failure: it
     * is a savings figure, and that figure can overflow to invalid on a run
     * that cleared perfectly well. Check THIS to know whether the book was
     * rewritten. When true, the obligations are exactly as they were. */
    bool            clear_failed;
    uint32_t        num_obl;

    /* stats from the last clearing run */
    rat_t    gross_before;      /* sum of all obligation amounts */
    rat_t    gross_after;       /* sum after netting */
    uint32_t transfers_before;
    uint32_t transfers_after;
    uint32_t cleared_runs;
} abacus_t;

/* lifecycle */
void     smaug_init(abacus_t *a);
int32_t  smaug_add_member(abacus_t *a, const char *name);

/* Record that `from` owes `to`. Returns false if the graph is full,
 * the members are invalid, the amount is invalid/negative, or from==to. */
bool smaug_owe(abacus_t *a, uint32_t from, uint32_t to, rat_t amount,
            uint8_t capital);

/* A member's net position: (owed to them) - (what they owe).
 * Positive = creditor, negative = debtor. */
rat_t smaug_net(const abacus_t *a, uint32_t member);

/* Sum of every net position. MUST be exactly zero — the conservation
 * check. Exposed so callers can assert it, not just trust it. */
rat_t smaug_conservation(const abacus_t *a);

/* Total gross obligations currently outstanding. */
rat_t smaug_gross(const abacus_t *a);

/* Run multilateral clearing.
 *
 * Replaces the obligation set with a SMALLER one — at most members-1
 * transfers per capital — that preserves every member's net position
 * exactly. (True minimisation is the NP-hard zero-sum partition problem;
 * the greedy largest-debtor/largest-creditor matching used here gives the
 * members-1 bound, which is what matters operationally.) Returns the gross amount ELIMINATED
 * (gross_before - gross_after) — the headline number: obligations
 * discharged without any cash moving. */
rat_t smaug_clear(abacus_t *a);

/* Count of currently active obligations (transfers required to settle). */
uint32_t smaug_transfer_count(const abacus_t *a);

#endif /* ZXV_ABACUS_H */
