/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_pay.h — the market's settle hook on the ledger of record (pay_ledger).
 *
 * The market (market.h) never moves money itself: every capture, release,
 * refund, ruling and invoice payment is one mk_settle_t handed to the
 * `settle` hook, and the market changes its own state only when the hook
 * returns true. mk_pay_settle is that hook for a pay_ledger_t.
 *
 *   P1 ONE POSTING. Each mk_settle_t becomes exactly one pay_ledger_post:
 *      one line per account with a nonzero net (buyer, seller, escrow,
 *      commons, tax; accounts that coincide are merged). The ledger checks
 *      balance (L1), funds (L3) and freezes, and applies all or nothing (L4),
 *      so a refused posting leaves both the ledger and the market unchanged.
 *   P2 ESCROW. Capture, release, refund and ruling run through the
 *      currency's escrow account; the escrow line is whatever keeps the
 *      posting balanced (inflows minus outflows). An invoice settles
 *      directly and must net to zero without escrow, else it is refused.
 *   P3 PARTIES. account_of maps a market identity and currency to a ledger
 *      account; an unknown party (MK_PAY_NONE) refuses the settlement.
 *      Pool accounts (escrow, commons, tax) are bound per currency with
 *      mk_pay_bind; an unbound currency refuses.
 *   P4 IDENTIFIERS. The idempotency key and UETR of posting n are
 *      SHA-256(seed || tag || n), so a node's postings are reproducible from
 *      its seed and never collide with another seed's.
 *   P5 LIMITS. Each amount must be below 2^60 (no int64 overflow in the
 *      nets). A settlement that moves nothing posts nothing and succeeds.
 *
 * The assurance fee (pay_assure.h) is not charged here: market fees already
 * arrive as to_commons / to_tax lines. Freestanding C11, no allocation.
 */
#ifndef ZXV_MK_PAY_H
#define ZXV_MK_PAY_H

#include <stdbool.h>
#include <stdint.h>
#include "market.h"
#include "pay_ledger.h"

#define MK_PAY_NONE 0xFFFFFFFFu

typedef uint32_t (*mk_pay_account_fn)(void *ctx, const mk_id_t *id, uint8_t ccy);

typedef struct {
    bool bound;
    uint32_t escrow, commons, tax;
} mk_pay_pool_t;

typedef struct {
    pay_ledger_t *L;
    mk_pay_account_fn account_of;
    void *ctx;
    uint8_t seed[32];
    uint32_t owner;                     /* initiator and attestor of every posting */
    uint64_t tick;                      /* supplied time (phase tick) for the next posting */
    mk_pay_pool_t pool[MK_MAX_CCY + 1]; /* by currency handle */
    uint64_t seq;                       /* postings attempted */
    uint64_t posted, refused;
    pay_status_t last; /* status of the last attempted posting */
} mk_pay_t;

void mk_pay_init(mk_pay_t *p, pay_ledger_t *L, mk_pay_account_fn account_of, void *ctx,
                 const uint8_t seed[32], uint32_t owner);
/* Bind currency handle ccy's pool accounts. false on a bad handle. */
bool mk_pay_bind(mk_pay_t *p, uint8_t ccy, uint32_t escrow, uint32_t commons, uint32_t tax);
/* The market's settle hook (ctx = the mk_pay_t). */
bool mk_pay_settle(void *ctx, const mk_settle_t *s);

#endif /* ZXV_MK_PAY_H */
