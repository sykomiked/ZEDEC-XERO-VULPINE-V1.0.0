/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* settle.h — the settlement spine: the swarm's money on the ledger of record.
 *
 * The swarm market (swarm_market.h) keeps each model's Financial capital in
 * its own table and conserves it (M8). This module makes pay_ledger the
 * ledger of record for that money: every change is posted to a pay_ledger_t,
 * hash-chained and checked against the ledger's invariants (L1-L5), and after
 * every cycle the two are reconciled. Two independent books that must agree
 * catch a conservation bug in either one.
 *
 * THE BOOKS
 * ---------
 *   S1  One unit asset "SWC" (swarm credit, 0 decimals, PAY_ASSET_UNIT): one
 *       unit is one unit of swarm Financial capital. It is never an ISO 4217
 *       currency and never leaves the node.
 *   S2  Accounts, all Financial form: one ISSUER account (the node), one POT
 *       account (the cycle's pot, swarm_market.h M5-M6) and one account per
 *       model. A model's endowment at join is an issue from the node.
 *   S3  settle_sync posts the difference between the market's table and the
 *       ledger as balanced two-line transfers, largest decrease to largest
 *       increase, so each posting balances on its own (L1).
 *
 * RECONCILIATION (fail closed)
 * ----------------------------
 *   R1  Before posting anything, settle_sync checks that the market's money is
 *       conserved: sum of holdings + pot == money_supply == what the node has
 *       issued. If not, nothing is posted and the spine HALTS.
 *   R2  After posting, every model's ledger balance must equal its market
 *       holding, and the pot account must equal the pot. If not, it HALTS.
 *   R3  A halted spine refuses every later call (SETTLE_ERR_HALTED) until
 *       settle_init. It never edits either book to make them agree: which
 *       book is wrong is a question for the operator, not a repair.
 *
 * Postings carry deterministic identifiers derived with SHAKE256 from the
 * node seed and the posting sequence, so a replay of the same cycles gives
 * the same chain hash on every machine.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_SETTLE_H
#define ZXV_SETTLE_H

#include <stdint.h>
#include <stdbool.h>
#include "../pay/pay_ledger.h"
#include "../swarm/swarm_market.h"

/* Total SWC a node may issue. pay_ledger refuses a posting line above 2^59
 * (8 lines * 2^59 = 2^62 keeps its sums exact), so keeping everything issued
 * below it means every endowment and every sync transfer fits one line. */
#define SETTLE_ISSUE_MAX ((uint64_t) 1 << 59)

typedef enum {
    SETTLE_OK = 0,
    SETTLE_ERR_ARG = -1,
    SETTLE_ERR_FULL = -2,
    SETTLE_ERR_UNKNOWN_MODEL = -3,
    SETTLE_ERR_CONSERVATION = -4, /* R1 */
    SETTLE_ERR_MISMATCH = -5,     /* R2 */
    SETTLE_ERR_LEDGER = -6,       /* pay_ledger refused a posting */
    SETTLE_ERR_HALTED = -7        /* R3 */
} settle_status_t;

typedef struct {
    pay_ledger_t L;
    uint8_t seed[32];
    uint16_t asset;  /* SWC */
    uint32_t issuer; /* node issuer account */
    uint32_t pot;    /* pot account */
    uint32_t model_id[SWARM_MAX_MODELS];
    uint32_t acct[SWARM_MAX_MODELS];
    uint32_t n;
    uint64_t issued; /* total SWC issued (== the issuer's CREDIT) */
    uint64_t seq;    /* postings made by the spine */
    uint64_t cycles; /* successful syncs */
    bool halted;
    settle_status_t why; /* the reason it halted */
} settle_spine_t;

/* Open the books. `seed` (32 bytes) makes the posting identifiers unique to
 * this node. */
settle_status_t settle_init(settle_spine_t *s, const uint8_t seed[32]);

/* S2: open a model's account and issue its endowment (the same amount the
 * caller gives swarm_market_join). Refused (SETTLE_ERR_ARG) if the total
 * issued would reach SETTLE_ISSUE_MAX. */
settle_status_t settle_join(settle_spine_t *s, uint32_t model_id, uint64_t endowment,
                            uint64_t tick);

/* S3 + R1 + R2: bring the ledger to the market's table and reconcile. */
settle_status_t settle_sync(settle_spine_t *s, const swarm_market_t *m, uint64_t tick);

/* R2 alone: true iff every model's balance and the pot agree with `m`. */
bool settle_reconciled(const settle_spine_t *s, const swarm_market_t *m);

/* A model's ledger balance (0 for an unknown model). */
uint64_t settle_balance(const settle_spine_t *s, uint32_t model_id);

#endif /* ZXV_SETTLE_H */
