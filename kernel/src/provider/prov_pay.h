/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_pay.h — the provider layer's settle hook over the existing pay rails
 * (kernel/src/pay/pay_ledger.h: DEBIT / CREDIT / EQUITY, PAY_RAIL_*_CODE).
 *
 *   HOLD     user -> escrow            hold
 *   FINAL    escrow -> provider        net
 *            escrow -> fee buckets     fee (the published network fee), split
 *                                      exactly by pay_assure_split into the
 *                                      reserve floor, V-Bill dividend pool,
 *                                      infrastructure/node bounties and
 *                                      regenerative capital accounts
 *            escrow -> user            refund (unused hold + SLA credit)
 *            one atomic posting; hold == net + fee + refund
 *   RELEASE  escrow -> user            hold
 *
 * Every posting's UETR, end-to-end id and idempotency key are derived from
 * the receipt (or job) digest and the kind, so a replay is recognised by the
 * ledger (pay R1 / R2) and never moves money twice. The ledger itself bears
 * no interest (pay_usury_check); nothing here adds a charge of any kind.
 * The operator opens the ledger accounts and maps handles to them. */
#ifndef ZXV_PROV_PAY_H
#define ZXV_PROV_PAY_H

#include "prov.h"
#include "../pay/pay_ledger.h"

#define PROV_PAY_NOACCT 0xFFFFFFFFu

typedef struct {
    pay_ledger_t *L;
    uint32_t initiator; /* pay principal id for the provider layer */
    uint64_t tick;      /* supplied time */
    uint32_t escrow[PROV_MAX_ASSETS];
    uint32_t fee_acct[PROV_MAX_ASSETS][PAY_ASSURE_BUCKETS];      /* pay_assure_bucket_t */
    uint32_t user[PROV_MAX_ASSETS][PROV_MAX_USERS + 1u];         /* by handle */
    uint32_t provider[PROV_MAX_ASSETS][PROV_MAX_PROVIDERS + 1u]; /* by handle */
    pay_status_t last;                                           /* status of the last posting */
} prov_pay_t;

/* All maps set to PROV_PAY_NOACCT. */
void prov_pay_init(prov_pay_t *p, pay_ledger_t *L, uint32_t initiator);
/* prov_settle_fn: ctx is a prov_pay_t. 0 on success (or exact replay). */
int prov_pay_settle(void *ctx, const prov_settlement_t *s);

#endif /* ZXV_PROV_PAY_H */
